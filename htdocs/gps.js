/*
 *  gps.js
 *
 *  (c) 2020-2026 Jörg Wendel
 *
 * This code is distributed under the terms and conditions of the
 * GNU GENERAL PUBLIC LICENSE. See the file COPYING for details.
 *
 *  Page 'Map': live position of the GPS sensor, recording of tours
 *  and display of recorded tours.
 *
 *  Views:  'live'  - the map follows the live position
 *          'tours' - list of the recorded tours (start/stop recording, show, rename, delete)
 *          'tour'  - a recorded tour on the map
 */

var gpsView = 'live';        // 'live' | 'tours' | 'tour'
var gpsTours = null;         // event 'gpstours': { active, tours, minDistance, pauseAfter }
var gpsShownTour = null;     // event 'gpstourpoints' of the displayed tour
var gpsLivePoints = [];      // live positions received in this session (L.LatLng)
var gpsLiveMinDistance = 10; // [m] a live point is added when moved at least this distance

mapManager = {
   map: null,
   routeLine: null,
   pointsLayer: null,          // historic points (circle markers)
   currentLocationMarker: null,
   isTrackingActive: true,     // the map follows the live position
   buttonControl: null,

   init() {
      this.map = L.map('map', {
         fadeAnimation: false,
         attributionControl: false     // deaktiviert das leaflet element unten rechts
      }).setView([50.1109, 8.6821], 2);

      if (navigator.onLine) {
         L.tileLayer('https://tile.openstreetmap.de/{z}/{x}/{y}.png', {
            attribution: '© OpenStreetMap'
         }).addTo(this.map);
      }

      this.routeLine = L.polyline([], {
         color: '#0066cc',
         weight: 5,
         opacity: 0.85
      }).addTo(this.map);

      this.pointsLayer = L.layerGroup().addTo(this.map);

      this.map.on('movestart', (e) => {
         if (e.target._mainviewchange || (this.map._panAnim && this.map._panAnim.isPlaying)) return;
         if (gpsView == 'live' && this.isTrackingActive) {
            this.isTrackingActive = false;
            this.updateButtonUI();
         }
      });

      this.createTrackingButton();

      // CSS Maße des DIVs sofort neu berechnen

      setTimeout(() => {
         this.map.invalidateSize();
      }, 100); // 100ms Verzögerung gibt dem Browser Zeit für das Layout
   },

   createTrackingButton() {
      const CustomControl = L.Control.extend({
         options: { position: 'topright' },
         onAdd: () => {
            const btn = L.DomUtil.create('button', 'live-tracking-control');
            this.buttonControl = btn;
            this.updateButtonUI();

            L.DomEvent.on(btn, 'click', (e) => {
               L.DomEvent.stopPropagation(e); // Verhindert Klick-Event auf der Karte

               if (gpsView != 'live') {
                  gpsShowLive();
                  return;
               }

               this.isTrackingActive = true;
               this.updateButtonUI();

               // Sofort zum letzten bekannten Punkt springen, falls vorhanden
               const points = this.routeLine.getLatLngs();
               if (points.length > 0)
                  this.map.setView(points[points.length - 1], this.map.getZoom());
            });
            return btn;
         }
      });
      this.map.addControl(new CustomControl());
   },

   updateButtonUI() {
      if (!this.buttonControl) return;
      if (gpsView != 'live') {
         this.buttonControl.innerHTML = '🗺 Tour (Klicken für Live)';
         this.buttonControl.classList.remove('live-tracking-active');
      }
      else if (this.isTrackingActive) {
         this.buttonControl.innerHTML = '● Live-Tracking aktiv';
         this.buttonControl.classList.add('live-tracking-active');
      } else {
         this.buttonControl.innerHTML = '🔄 Tracking pausiert (Klicken zum Folgen)';
         this.buttonControl.classList.remove('live-tracking-active');
      }
   },

   clear() {
      this.routeLine.setLatLngs([]);
      this.pointsLayer.clearLayers();
      if (this.currentLocationMarker) {
         this.map.removeLayer(this.currentLocationMarker);
         this.currentLocationMarker = null;
      }
   },

   addHistoryPoint(latLng) {
      L.circleMarker(latLng, {
         radius: 4,
         fillColor: '#33a2ff', // Soft historical light-blue
         fillOpacity: 0.7,
         color: '#0066cc',
         weight: 1,
         className: 'map-point'
      }).addTo(this.pointsLayer);
   },

   setCurrentLocation(latLng) {
      if (!this.currentLocationMarker) {
         this.currentLocationMarker = L.circleMarker(latLng, {
            radius: 7,
            fillColor: '#0052cc', // Solid deep blue center
            fillOpacity: 1,
            color: '#ffffff',     // Sharp white border
            weight: 3,
            className: 'map-point current-location'
         }).addTo(this.map);
      }
      else
         this.currentLocationMarker.setLatLng(latLng);
   },

   // live view: redraw all live points of this session

   showLive(points) {
      this.clear();
      this.routeLine.setStyle({ color: '#0066cc' });

      if (!points.length) {
         this.updateButtonUI();
         return;
      }

      for (let i = 0; i < points.length - 1; i++)
         this.addHistoryPoint(points[i]);

      this.routeLine.setLatLngs(points.length == 1 ? [points[0], points[0]] : points);
      this.setCurrentLocation(points[points.length - 1]);
      this.map.setView(points[points.length - 1], Math.max(this.map.getZoom(), 15), { animate: false });
      this.updateButtonUI();
   },

   addLivePoint(latLng, first) {
      if (first) {
         this.routeLine.setLatLngs([latLng, latLng]);
         this.map.setView(latLng, 15, { animate: false });
         this.setCurrentLocation(latLng);
         return;
      }

      // den alten Standort in die Historie überführen, den Live-Marker verschieben

      if (this.currentLocationMarker)
         this.addHistoryPoint(this.currentLocationMarker.getLatLng());

      this.routeLine.addLatLng(latLng);
      this.setCurrentLocation(latLng);

      if (this.isTrackingActive)
         this.map.panTo(latLng, { animate: false });
   },

   // a recorded tour: points [[lat, lng, time], ...]

   showTour(points) {
      this.clear();
      this.routeLine.setStyle({ color: '#d32f2f' });

      let latLngs = points.map(p => L.latLng(p[0], p[1]));

      if (!latLngs.length) {
         this.updateButtonUI();
         return;
      }

      this.routeLine.setLatLngs(latLngs.length == 1 ? [latLngs[0], latLngs[0]] : latLngs);

      L.circleMarker(latLngs[0], {
         radius: 7, fillColor: '#2e7d32', fillOpacity: 1, color: '#ffffff', weight: 3
      }).bindTooltip('Start ' + gpsFmtTime(points[0][2])).addTo(this.pointsLayer);

      if (latLngs.length > 1) {
         let last = points[points.length - 1];
         L.circleMarker(latLngs[latLngs.length - 1], {
            radius: 7, fillColor: '#d32f2f', fillOpacity: 1, color: '#ffffff', weight: 3
         }).bindTooltip('Ende ' + gpsFmtTime(last[2])).addTo(this.pointsLayer);
      }

      this.map.fitBounds(this.routeLine.getBounds(), { padding: [30, 30], animate: false });
      this.updateButtonUI();
   }
};

//***************************************************************************
// Page
//***************************************************************************

// called on switching to the page 'gpslive' (main.js)

function initGpsPage()
{
   if (gpsView == 'tour' && !gpsShownTour)
      gpsView = 'live';

   gpsBuildToolbar();

   if (gpsView == 'tours')
      gpsShowTours();
   else if (gpsView == 'tour')
      gpsShowTour(gpsShownTour.id);
   else
      gpsShowLive();

   socket.send({ "event" : "gpstour", "object" : { "action" : "list" } });
}

function gpsHasControlRights()
{
   return (localStorage.getItem(storagePrefix + 'Rights') & 0x02) != 0;
}

function gpsBuildToolbar()
{
   $('#dashboardMenu').removeClass('hidden');
   $('#dashboardMenu').empty();

   $('#dashboardMenu')
      .append($('<button></button>')
              .attr('id', 'gpsBtnLive')
              .addClass('rounded-border buttonDashboard gpsViewButton')
              .html('Live')
              .click(function() { gpsShowLive(); }))
      .append($('<button></button>')
              .attr('id', 'gpsBtnTours')
              .addClass('rounded-border buttonDashboard gpsViewButton')
              .html('Touren')
              .click(function() { gpsShowTours(); }))
      .append($('<span></span>')
              .attr('id', 'gpsRecordState')
              .addClass('gpsRecordState'));

   gpsUpdateToolbar();
}

function gpsUpdateToolbar()
{
   $('.gpsViewButton').removeClass('buttonDashboardActive');

   if (gpsView == 'live')
      $('#gpsBtnLive').addClass('buttonDashboardActive');
   else
      $('#gpsBtnTours').addClass('buttonDashboardActive');

   let active = gpsTours ? gpsTours.active : null;

   if (active) {
      let state = active.paused ? 'pausiert' : 'Aufzeichnung';
      $('#gpsRecordState')
         .removeClass('gpsRecording gpsPaused')
         .addClass(active.paused ? 'gpsPaused' : 'gpsRecording')
         .html((active.paused ? '❚❚ ' : '● ') + state + ": '" + gpsEscape(active.name) + "' · " + gpsFmtDistance(active.distance) + ' · ' + active.points + ' Punkte');
   }
   else
      $('#gpsRecordState').removeClass('gpsRecording gpsPaused').html('');
}

// tool panel on the left of the tour list (like the setup pages)

function gpsBuildControlPanel()
{
   let active = gpsTours ? gpsTours.active : null;

   $("#controlContainer").empty();

   if (gpsHasControlRights()) {
      $("#controlContainer")
         .append($('<div></div>')
                 .append($('<button></button>')
                         .attr('id', 'gpsBtnRecord')
                         .addClass('rounded-border tool-button')
                         .html(active ? '■ Tour beenden' : '● Tour aufzeichnen')
                         .attr('title', active ? "Aufzeichnung von '" + active.name + "' beenden" : 'Aufzeichnung einer neuen Tour starten')
                         .click(function() { gpsToggleRecording(); })))
         .append($('<div></div>')
                 .addClass('button-group-spacing'));
   }

   if (active) {
      $("#controlContainer")
         .append($('<div></div>')
                 .addClass('labelB1 ' + (active.paused ? 'gpsPaused' : 'gpsRecording'))
                 .html((active.paused ? '❚❚ pausiert' : '● Aufzeichnung') + "<br/>'" + gpsEscape(active.name) + "'<br/>seit " + gpsFmtTime(active.start)
                       + '<br/>' + gpsFmtDistance(active.distance) + ' · ' + active.points + ' Punkte'
                       + (active.pausetime ? '<br/>Pause ' + gpsFmtDuration(active.pausetime) : '')))
         .append($('<div></div>')
                 .addClass('button-group-spacing'));
   }

   if (gpsTours) {
      $("#controlContainer")
         .append($('<div></div>')
                 .addClass('labelB1')
                 .html('Punkt ab ' + gpsTours.minDistance + ' m Bewegung<br/>Pause nach ' + gpsTours.pauseAfter + ' min Stillstand<br/>(Konfiguration -> GPS)'))
         .append($('<div></div>')
                 .addClass('button-group-spacing'));
   }

   $("#controlContainer")
      .append($('<button></button>')
              .addClass('rounded-border tool-button')
              .html('Hilfe')
              .attr('title', 'Beschreibung im README')
              .click(function() { showHelp('gps-tours'); }));
}

function gpsResizeContainer(id)
{
   let height = $(window).height() - getTotalHeightOf('menu') - getTotalHeightOf('dashboardMenu') - getTotalHeightOf('footer') - sab - 15;
   $('#' + id).height(height);
   window.onresize = function() {
      $('#' + id).height($(window).height() - getTotalHeightOf('menu') - getTotalHeightOf('dashboardMenu') - getTotalHeightOf('footer') - sab - 15);
      if (mapManager.map)
         mapManager.map.invalidateSize();
   };
}

// show the map container and make sure the leaflet map exists

function gpsShowMap()
{
   $('#container').addClass('hidden');
   $('#controlContainer').addClass('hidden');
   $('#controlToggle').addClass('hidden');
   $('#mapcontainer').removeClass('hidden');

   if (!$('#map').length)
      $('#mapcontainer').empty().append($('<div></div>').attr('id', 'map'));

   gpsResizeContainer('mapcontainer');

   if (!mapManager.map)
      mapManager.init();
   else
      setTimeout(() => { mapManager.map.invalidateSize(); }, 100);
}

function gpsShowLive()
{
   gpsView = 'live';
   gpsUpdateToolbar();
   gpsShowMap();
   $('#gpsTourInfo').addClass('hidden');
   mapManager.isTrackingActive = true;
   mapManager.showLive(gpsLivePoints);
}

function gpsShowTour(id)
{
   gpsView = 'tour';
   gpsUpdateToolbar();
   gpsShowMap();

   if (gpsShownTour && gpsShownTour.id == id)
      mapManager.showTour(gpsShownTour.points);
   else
      mapManager.clear();

   socket.send({ "event" : "gpstour", "object" : { "action" : "points", "id" : id } });
}

//***************************************************************************
// Live position - event 'gpslive' [lat, lng]
//***************************************************************************

function processLiveGpsMessage(coordinate)
{
   if (!Array.isArray(coordinate) || coordinate.length !== 2) {
      console.warn("Invalid coordinate format", coordinate);
      return;
   }

   const latLng = L.latLng(parseFloat(coordinate[0]), parseFloat(coordinate[1]));

   if (isNaN(latLng.lat) || isNaN(latLng.lng))
      return;

   // nur bei Bewegung einen Punkt anhängen

   let first = !gpsLivePoints.length;

   if (!first && gpsLivePoints[gpsLivePoints.length - 1].distanceTo(latLng) < gpsLiveMinDistance)
      return;

   gpsLivePoints.push(latLng);

   if (currentPage != 'gpslive' || gpsView != 'live' || !mapManager.map)
      return;

   mapManager.addLivePoint(latLng, first);
}

//***************************************************************************
// Tours - event 'gpstours'
//***************************************************************************

function processGpsTours(tours)
{
   gpsTours = tours;

   if (currentPage != 'gpslive')
      return;

   gpsUpdateToolbar();

   if (gpsView == 'tours')
      gpsShowTours();
}

function gpsShowTours()
{
   gpsView = 'tours';
   gpsUpdateToolbar();

   $('#mapcontainer').addClass('hidden');
   showControlContainer();
   $('#container').removeClass('hidden');
   gpsResizeContainer('container');
   gpsBuildControlPanel();

   let root = document.getElementById("container");
   root.innerHTML = '';

   if (!gpsTours) {
      root.innerHTML = '<div class="rounded-border seperatorFold">Touren werden geladen ...</div>';
      return;
   }

   let html = '';

   if (!gpsTours.tours.length) {
      html += '<div class="rounded-border setupContainer gpsTourHint">Noch keine Tour aufgezeichnet.';
      if (gpsHasControlRights())
         html += " Mit '● Tour aufzeichnen' wird eine neue Tour gestartet.";
      html += '</div>';
   }
   else {
      html += '<table class="setupContainer tableMultiCol gpsTourTable">';
      html += ' <thead><tr>';
      html += '  <td class="tableMultiColCell">Name</td>';
      html += '  <td class="tableMultiColCell">Start</td>';
      html += '  <td class="tableMultiColCell">Ende</td>';
      html += '  <td class="tableMultiColCell">Dauer</td>';
      html += '  <td class="tableMultiColCell">Pause</td>';
      html += '  <td class="tableMultiColCell">Distanz</td>';
      html += '  <td class="tableMultiColCell">Punkte</td>';
      html += '  <td class="tableMultiColCell"></td>';
      html += ' </tr></thead><tbody>';

      for (let tour of gpsTours.tours) {
         let isActive = gpsTours.active && gpsTours.active.id == tour.id;
         let stop = tour.stop != null ? tour.stop : Math.floor(Date.now() / 1000);
         let state = '';

         if (isActive)
            state = gpsTours.active.paused ? ' <span class="gpsPaused">❚❚ pausiert</span>' : ' <span class="gpsRecording">● aktiv</span>';

         html += ' <tr data-id="' + tour.id + '">';
         html += '  <td class="tableMultiColCell gpsTourName">' + gpsEscape(tour.name) + state + '</td>';
         html += '  <td class="tableMultiColCell">' + gpsFmtTime(tour.start) + '</td>';
         html += '  <td class="tableMultiColCell">' + (tour.stop != null ? gpsFmtTime(tour.stop) : '-') + '</td>';
         html += '  <td class="tableMultiColCell">' + gpsFmtDuration(stop - tour.start) + '</td>';
         html += '  <td class="tableMultiColCell">' + gpsFmtDuration(tour.pausetime) + '</td>';
         html += '  <td class="tableMultiColCell">' + gpsFmtDistance(tour.distance) + '</td>';
         html += '  <td class="tableMultiColCell">' + tour.points + '</td>';
         html += '  <td class="tableMultiColCell gpsTourActions">';
         html += '   <button class="rounded-border buttonOptions" type="button" onclick="gpsShowTour(' + tour.id + ')">Anzeigen</button>';

         if (gpsHasControlRights()) {
            html += '   <button class="rounded-border buttonOptions" type="button" onclick="gpsRenameTour(' + tour.id + ')">Umbenennen</button>';
            if (!isActive)
               html += '   <button class="rounded-border buttonOptions" type="button" onclick="gpsDeleteTour(' + tour.id + ')">Löschen</button>';
         }

         html += '  </td>';
         html += ' </tr>';
      }

      html += ' </tbody></table>';
   }

   root.innerHTML = html;
}

// event 'gpstourpoints'

function processGpsTourPoints(tour)
{
   gpsShownTour = tour;

   if (currentPage != 'gpslive' || gpsView != 'tour' || !mapManager.map)
      return;

   mapManager.showTour(tour.points);

   let info = "'" + gpsEscape(tour.name) + "' · " + gpsFmtTime(tour.start)
       + (tour.stop != null ? ' - ' + gpsFmtTime(tour.stop) : ' (aktiv)')
       + ' · ' + gpsFmtDistance(tour.distance) + ' · ' + tour.points.length + ' Punkte';

   if (!$('#gpsTourInfo').length)
      $('#mapcontainer').append($('<div></div>').attr('id', 'gpsTourInfo').addClass('rounded-border gpsTourInfo'));

   $('#gpsTourInfo').html(info).removeClass('hidden');
}

//***************************************************************************
// Actions
//***************************************************************************

function gpsToggleRecording()
{
   if (gpsTours && gpsTours.active) {
      if (confirm("Aufzeichnung der Tour '" + gpsTours.active.name + "' beenden?"))
         socket.send({ "event" : "gpstour", "object" : { "action" : "stop" } });
      return;
   }

   let now = new Date();
   let defaultName = 'Tour ' + now.toLocaleDateString('de-DE', { day: '2-digit', month: '2-digit', year: 'numeric' });

   gpsNameDialog('Tour aufzeichnen', defaultName, 'Starten', function(name) {
      socket.send({ "event" : "gpstour", "object" : { "action" : "start", "name" : name } });
   });
}

function gpsRenameTour(id)
{
   let tour = gpsTours ? gpsTours.tours.find(t => t.id == id) : null;

   if (!tour)
      return;

   gpsNameDialog('Tour umbenennen', tour.name, 'Speichern', function(name) {
      socket.send({ "event" : "gpstour", "object" : { "action" : "rename", "id" : id, "name" : name } });
   });
}

function gpsDeleteTour(id)
{
   let tour = gpsTours ? gpsTours.tours.find(t => t.id == id) : null;

   if (!tour)
      return;

   if (confirm("Tour '" + tour.name + "' und ihre aufgezeichneten Punkte löschen?"))
      socket.send({ "event" : "gpstour", "object" : { "action" : "delete", "id" : id } });
}

function gpsNameDialog(title, value, okLabel, onOk)
{
   let form = document.createElement("div");

   $(form).append($('<div></div>')
                  .addClass('inputTableConfig')
                  .append($('<div></div>')
                          .append($('<span></span>').html('Name'))
                          .append($('<span></span>')
                                  .append($('<input></input>')
                                          .attr('id', 'gpsTourNameInput')
                                          .attr('type', 'text')
                                          .attr('maxlength', 100)
                                          .addClass('rounded-border input')
                                          .css('width', '100%')
                                          .val(value)))));

   let buttons = {};

   buttons['Abbrechen'] = function() { $(this).dialog('close'); };
   buttons[okLabel] = function() {
      let name = $('#gpsTourNameInput').val().trim();
      $(this).dialog('close');
      onOk(name);
   };

   $(form).dialog({
      modal: true,
      resizable: false,
      closeOnEscape: true,
      hide: "fade",
      width: "400px",
      title: title,
      open: function() {
         $('#gpsTourNameInput').focus().select();
         $('#gpsTourNameInput').keypress(function(e) {
            if (e.which == 13)
               $(form).parent().find('.ui-dialog-buttonpane button:last').click();
         });
      },
      buttons: buttons,
      close: function() { $(this).dialog('destroy').remove(); }
   });
}

//***************************************************************************
// Formatting
//***************************************************************************

function gpsEscape(text)
{
   return $('<div></div>').text(text == null ? '' : text).html();
}

function gpsFmtTime(ts)
{
   if (!ts)
      return '-';

   return new Date(ts * 1000).toLocaleString('de-DE', { day: '2-digit', month: '2-digit', year: 'numeric', hour: '2-digit', minute: '2-digit' });
}

function gpsFmtDuration(seconds)
{
   seconds = Math.max(0, Math.round(seconds || 0));

   let h = Math.floor(seconds / 3600);
   let m = Math.floor((seconds % 3600) / 60);

   if (h >= 24) {
      let d = Math.floor(h / 24);
      return d + ' d ' + (h % 24) + ' h';
   }

   if (h)
      return h + ' h ' + m + ' min';

   return m + ' min';
}

function gpsFmtDistance(meters)
{
   meters = meters || 0;

   if (meters < 1000)
      return Math.round(meters) + ' m';

   return (meters / 1000).toLocaleString('de-DE', { minimumFractionDigits: 1, maximumFractionDigits: 1 }) + ' km';
}
