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
 *          'tours' - the recorded tours: names in the control panel (left), details and the
 *                    event list of the selected tour on the right (show on map, edit, delete)
 *          'tour'  - a recorded tour on the map
 *
 *  Events of a tour (fuel stops, toll, pauses, night stops, notes) are entered while the tour
 *  is active (buttons in the tool bar), each with the current position. Together with the
 *  odometer at start / end (asked on start / stop, editable with the name) the costs and the
 *  fuel consumption are calculated by the daemon.
 */

var gpsView = localStorage.getItem(storagePrefix + 'gpsView') || 'live';   // 'live' | 'tours' | 'tour' - the tab survives a reload
var gpsTours = null;         // event 'gpstours': { active, tours, minDistance, pauseAfter }
var gpsShownTour = null;     // event 'gpstourpoints' of the displayed tour
var gpsLivePoints = [];      // live positions received in this session (L.LatLng)
var gpsLiveMinDistance = 10; // [m] a live point is added when moved at least this distance
var gpsSelectedTour = parseInt(localStorage.getItem(storagePrefix + 'gpsSelectedTour')) || null;   // tour shown in the 'tours' view (details / events)
var gpsFocusEvent = null;    // event id to center on the map once the tour points arrived (click in the event list)
var gpsTourEvents = {};      // event 'gpstourevents': events and totals by tour id

// event types: label, icon (mdi), which input fields the dialog offers

var gpsEventTypes = {
   'fuel':  { label: 'Tankstopp',  icon: 'mdi-gas-station',   color: '#d32f2f', fields: ['amount', 'full', 'price', 'odometer', 'note'] },
   'toll':  { label: 'Maut',       icon: 'mdi-boom-gate',     color: '#f57c00', fields: ['price', 'note'] },
   'pause': { label: 'Pause',      icon: 'mdi-coffee',        color: '#1976d2', fields: ['price', 'note'] },
   'night': { label: 'Nachtstopp', icon: 'mdi-weather-night', color: '#512da8', fields: ['price', 'note'] },
   'note':  { label: 'Hinweis',    icon: 'mdi-note-text',     color: '#455a64', fields: ['note'] }
};

mapManager = {
   map: null,
   routeLine: null,
   pointsLayer: null,          // historic points (circle markers)
   eventsLayer: null,          // the events of the shown tour (fuel, toll, ...)
   eventMarkers: {},           // their markers by event id
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
      this.eventsLayer = L.layerGroup().addTo(this.map);

      this.map.on('movestart', (e) => {
         if (e.target._mainviewchange || (this.map._panAnim && this.map._panAnim.isPlaying)) return;
         if (gpsView == 'live' && this.isTrackingActive) {
            this.isTrackingActive = false;
            this.updateButtonUI();
         }
      });

      this.createTrackingButton();
      gpsCenterControl(this.map, () => this.centerView());
      gpsMapButton(this.map, 'mdi-format-list-bulleted', 'zurück zur Tourenliste', () => gpsShowTours());

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

   // 'Zentrieren': the whole route (live or tour) into view, with a single point that one

   centerView() {
      let points = this.routeLine.getLatLngs();

      if (points.length >= 2)
         this.map.fitBounds(this.routeLine.getBounds(), { padding: [30, 30] });
      else if (this.currentLocationMarker)
         this.map.setView(this.currentLocationMarker.getLatLng(), Math.max(this.map.getZoom(), 15));
      else if (points.length)
         this.map.setView(points[0], Math.max(this.map.getZoom(), 15));
   },

   updateButtonUI() {
      if (!this.buttonControl) return;

      // the follow button belongs to the live view only (the 'Live' tab switches the view)

      this.buttonControl.style.display = gpsView == 'live' ? '' : 'none';

      if (gpsView != 'live')
         return;

      if (this.isTrackingActive) {
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
      this.eventsLayer.clearLayers();
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
   },

   // the events of a tour as markers, the popup shows the details (edit / delete with control rights)

   showEvents(events) {
      this.eventsLayer.clearLayers();
      this.eventMarkers = {};

      for (let ev of events || []) {
         if (!ev.lat && !ev.lon)
            continue;

         let type = gpsEventTypes[ev.type] || gpsEventTypes['note'];
         let icon = L.divIcon({ className: 'gpsEventIcon', html: '<span class="mdi ' + type.icon + '" style="color:' + type.color + '"></span>', iconSize: [26, 26], iconAnchor: [13, 13], popupAnchor: [0, -12] });
         let popup = '<div class="gpsEventPopup"><b>' + type.label + '</b> · ' + gpsFmtTime(ev.time) + '<br/>' + gpsEventDetails(ev, '<br/>');

         if (gpsHasControlRights())
            popup += '<div class="gpsEventPopupActions"><button class="rounded-border buttonOptions" type="button" onclick="gpsEventDialog(\'' + ev.type + '\', ' + ev.id + ')">Ändern</button>'
                   + '<button class="rounded-border buttonOptions" type="button" onclick="gpsEventDelete(' + ev.id + ')">Löschen</button></div>';

         popup += '</div>';

         this.eventMarkers[ev.id] = L.marker([ev.lat, ev.lon], { icon: icon, zIndexOffset: 500 })
            .bindTooltip(type.label + ' · ' + gpsEventDetails(ev, ' · '))
            .bindPopup(popup)
            .addTo(this.eventsLayer);
      }
   },

   // center on an event and open its popup (click in the event list)

   focusEvent(id) {
      let marker = this.eventMarkers[id];

      if (!marker)
         return false;

      this.map.setView(marker.getLatLng(), Math.max(this.map.getZoom(), 15), { animate: false });
      marker.openPopup();

      return true;
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
   $('#dashboardMenu').removeClass('hidden').addClass('gpsToolbar');   // opaque background (dashboard.js removes the class)
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
              .addClass('gpsRecordState'))
      .append($('<span></span>')
              .attr('id', 'gpsEventButtons')
              .addClass('gpsEventButtons'));

   gpsUpdateToolbar();
}

// the event buttons (tool bar) while a tour is active - control rights needed

function gpsUpdateEventButtons()
{
   let active = gpsTours ? gpsTours.active : null;
   let bar = $('#gpsEventButtons').empty();

   if (!active || !gpsHasControlRights())
      return;

   for (let key of ['fuel', 'toll', 'pause', 'night', 'note']) {
      let type = gpsEventTypes[key];
      bar.append($('<button></button>')
                 .addClass('rounded-border buttonDashboard gpsEventBtn mdi ' + type.icon)
                 .attr('title', type.label + ' eintragen (mit der aktuellen Position)')
                 .click(function() { gpsEventDialog(key); }));
   }
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

   gpsUpdateEventButtons();
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

   // the tours by name, the selected one is shown on the right

   if (gpsTours && gpsTours.tours.length) {
      let list = $('<div></div>').addClass('gpsTourList');

      for (let tour of gpsTours.tours) {
         let isActive = gpsTours.active && gpsTours.active.id == tour.id;

         list.append($('<button></button>')
                     .addClass('rounded-border tool-button gpsTourBtn' + (tour.id == gpsSelectedTour ? ' active' : ''))
                     .attr('title', tour.name)
                     .html((isActive ? '<span class="' + (gpsTours.active.paused ? 'gpsPaused' : 'gpsRecording') + '">● </span>' : '') + gpsEscape(tour.name)
                           + '<br/><span class="gpsTourBtnSub">' + gpsFmtDate(tour.start) + ' · ' + gpsFmtDistance(tour.distance) + '</span>')
                     .click(function() { gpsSelectTour(tour.id); }));
      }

      $("#controlContainer")
         .append($('<div></div>').addClass('labelB1').html('Touren'))
         .append(list)
         .append($('<div></div>').addClass('button-group-spacing'));
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
   localStorage.setItem(storagePrefix + 'gpsView', 'live');
   gpsUpdateToolbar();
   gpsShowMap();
   $('#gpsTourInfo').addClass('hidden');
   mapManager.isTrackingActive = true;
   mapManager.showLive(gpsLivePoints);
}

function gpsShowTour(id)
{
   gpsView = 'tour';
   localStorage.setItem(storagePrefix + 'gpsView', 'tours');   // after a reload back to the list (the points are gone)
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

   if (gpsView == 'tours') {
      if (gpsSelectedTour)
         socket.send({ "event" : "gpstour", "object" : { "action" : "events", "id" : gpsSelectedTour } });   // events may have changed
      gpsShowTours();
   }
   else if (gpsView == 'tour' && gpsShownTour)
      socket.send({ "event" : "gpstour", "object" : { "action" : "points", "id" : gpsShownTour.id } });
}

function gpsShowTours()
{
   gpsView = 'tours';
   localStorage.setItem(storagePrefix + 'gpsView', 'tours');
   gpsUpdateToolbar();

   $('#mapcontainer').addClass('hidden');
   showControlContainer();
   $('#container').removeClass('hidden');
   gpsResizeContainer('container');

   // default selection: the active tour, else the newest

   if (gpsTours && gpsTours.tours.length && !gpsTours.tours.find(t => t.id == gpsSelectedTour))
      gpsSelectTour(gpsTours.active ? gpsTours.active.id : gpsTours.tours[0].id, false);

   gpsBuildControlPanel();

   let root = document.getElementById("container");

   if (!gpsTours) {
      root.innerHTML = '<div class="rounded-border seperatorFold">Touren werden geladen ...</div>';
      return;
   }

   if (!gpsTours.tours.length) {
      let html = '<div class="rounded-border setupContainer gpsTourHint">Noch keine Tour aufgezeichnet.';
      if (gpsHasControlRights())
         html += " Mit '● Tour aufzeichnen' wird eine neue Tour gestartet.";
      root.innerHTML = html + '</div>';
      return;
   }

   root.innerHTML = gpsTourDetailsHtml(gpsTours.tours.find(t => t.id == gpsSelectedTour));
}

// select a tour (button in the control panel) and fetch its events

function gpsSelectTour(id, render = true)
{
   gpsSelectedTour = id;
   localStorage.setItem(storagePrefix + 'gpsSelectedTour', id);
   socket.send({ "event" : "gpstour", "object" : { "action" : "events", "id" : id } });

   if (render)
      gpsShowTours();
}

// event 'gpstourevents' - events and totals of one tour

function processGpsTourEvents(obj)
{
   gpsTourEvents[obj.id] = obj;

   if (currentPage == 'gpslive' && gpsView == 'tours' && gpsSelectedTour == obj.id)
      $('#gpsTourEvents').html(gpsTourEventsHtml(obj));
}

// details and the event list of a tour (right side of the 'tours' view)

function gpsTourDetailsHtml(tour)
{
   if (!tour)
      return '';

   let isActive = gpsTours.active && gpsTours.active.id == tour.id;
   let stop = tour.stop != null ? tour.stop : Math.floor(Date.now() / 1000);
   let control = gpsHasControlRights();
   let t = tour.totals || {};
   let html = '';

   html += '<div class="gpsTourDetails">';
   html += ' <div class="rounded-border seperatorFold gpsTourHead"><span>' + gpsEscape(tour.name)
         + (isActive ? (gpsTours.active.paused ? ' <span class="gpsPaused">❚❚ pausiert</span>' : ' <span class="gpsRecording">● aktiv</span>') : '') + '</span>';
   html += '  <span class="gpsTourActions">';
   html += '   <button class="rounded-border tool-button mdi mdi-map-marker-path" type="button" title="auf der Karte anzeigen" onclick="gpsShowTour(' + tour.id + ')"> Karte</button>';

   if (control) {
      html += '   <button class="rounded-border tool-button mdi mdi-lead-pencil" type="button" title="Name und km-Stände ändern" onclick="gpsRenameTour(' + tour.id + ')"></button>';
      if (!isActive)
         html += '   <button class="rounded-border tool-button mdi mdi-delete actDelete" type="button" title="Tour mit Punkten und Ereignissen löschen" onclick="gpsDeleteTour(' + tour.id + ')"></button>';
   }

   html += '  </span></div>';

   // key / value table, two pairs per row (one on phones)

   let rows = [['Start', gpsFmtTime(tour.start)],
               ['Ende', tour.stop != null ? gpsFmtTime(tour.stop) : '-'],
               ['Dauer', gpsFmtDuration(stop - tour.start)],
               ['Pause', gpsFmtDuration(tour.pausetime)],
               ['Distanz (GPS)', gpsFmtDistance(tour.distance)],
               ['Punkte', tour.points]];

   if (tour.odometer)
      rows.push(['km-Stand Start', tour.odometer.toLocaleString('de-DE') + ' km']);
   if (tour.odometerend)
      rows.push(['km-Stand Ende', tour.odometerend.toLocaleString('de-DE') + ' km']);
   if (t.odometerKm != null)
      rows.push(['Strecke (Tacho)', t.odometerKm.toLocaleString('de-DE') + ' km']);
   if (t.fuelStops)
      rows.push(['Getankt', t.fuelStops + 'x · ' + gpsFmtLiters(t.liters) + ' · ' + gpsFmtMoney(t.fuelCosts)]);
   if (t.tollCosts)
      rows.push(['Maut', gpsFmtMoney(t.tollCosts)]);
   if (t.otherCosts)
      rows.push(['Sonstiges', gpsFmtMoney(t.otherCosts)]);
   if (t.totalCosts)
      rows.push(['Kosten gesamt', '<b>' + gpsFmtMoney(t.totalCosts) + '</b>']);
   if (t.consumption != null)
      rows.push(['Ø Verbrauch (Tacho)', gpsFmtConsumption(t.consumption) + ' <span class="gpsTourSub">auf ' + t.consumptionKm + ' km</span>']);
   if (t.consumptionGps != null)
      rows.push(['Ø Verbrauch (GPS)', gpsFmtConsumption(t.consumptionGps) + ' <span class="gpsTourSub">auf ' + Math.round(t.consumptionGpsKm) + ' km</span>']);

   let narrow = window.innerWidth < 900;
   let half = narrow ? rows.length : Math.ceil(rows.length / 2);

   html += ' <div class="rounded-border gpsTourBody gpsTourBodyFlex"><table class="gpsDetailsTable">';

   for (let i = 0; i < half; i++) {
      let l = rows[i];
      let r = narrow ? null : rows[i + half];
      html += '<tr><td>' + l[0] + '</td><td class="gpsNum">' + l[1] + '</td>';
      if (!narrow)
         html += r ? '<td class="gpsDetailsCol2">' + r[0] + '</td><td class="gpsNum">' + r[1] + '</td>' : '<td></td><td></td>';
      html += '</tr>';
   }

   html += ' </table>';

   if (tour.comment)        // the comment to the right of the values (below them on narrow screens)
      html += ' <div class="gpsTourCommentText">' + gpsEscape(tour.comment).replace(/\n/g, '<br/>') + '</div>';

   html += ' </div>';

   html += ' <div class="rounded-border seperatorFold gpsTourHead">Ereignisse' + (t.events ? ' (' + t.events + ')' : '') + '</div>';
   html += ' <div class="rounded-border gpsTourBody" id="gpsTourEvents">' + gpsTourEventsHtml(gpsTourEvents[tour.id]) + '</div>';
   html += '</div>';

   return html;
}

// click on an event in the list: the tour on the map, centered on the event

function gpsShowEventOnMap(tourId, eventId)
{
   gpsFocusEvent = eventId;
   gpsShowTour(tourId);
}

function gpsTourEventsHtml(data)
{
   if (!data)
      return '<div class="gpsTourSub">Ereignisse werden geladen ...</div>';

   if (!data.events.length)
      return '<div class="gpsTourSub">Keine Ereignisse. Tankstopps, Maut, Pausen, Nachtstopps und Hinweise werden während der Aufzeichnung über die Buttons in der Leiste eingetragen, jeweils mit der aktuellen Position.</div>';

   let control = gpsHasControlRights();
   let html = '<table class="gpsEventTable">';

   html += '<thead><tr><td>Zeit</td><td>Ereignis</td><td class="gpsNum">Menge</td><td class="gpsNum">Preis</td><td class="gpsNum">km-Stand</td><td class="gpsNum">Verbrauch</td><td>Notiz</td><td></td></tr></thead><tbody>';

   for (let ev of data.events) {
      let type = gpsEventTypes[ev.type] || gpsEventTypes['note'];
      let hasPosition = ev.lat || ev.lon;

      html += '<tr class="gpsEventLine' + (hasPosition ? ' gpsEventClickable' : '') + '"' + (hasPosition ? ' title="auf der Karte zeigen" onclick="gpsShowEventOnMap(' + data.id + ', ' + ev.id + ')"' : '') + '>';
      html += '<td>' + gpsFmtTime(ev.time) + '</td>';
      html += '<td><span class="mdi ' + type.icon + '" style="color:' + type.color + '"></span> ' + type.label + '</td>';
      html += '<td class="gpsNum">' + (ev.amount ? gpsFmtLiters(ev.amount) + (ev.full === false ? '<br/><span class="gpsTourSub">nicht voll</span>' : '') : '') + '</td>';
      html += '<td class="gpsNum">' + (ev.price ? gpsFmtMoney(ev.price) + (ev.amount ? '<br/><span class="gpsTourSub">' + gpsFmtMoney(ev.price / ev.amount, 3) + '/l</span>' : '') : '') + '</td>';
      html += '<td class="gpsNum">' + (ev.odometer ? ev.odometer.toLocaleString('de-DE') + ' km' : '') + '</td>';
      html += '<td class="gpsNum">' + gpsFmtEventConsumption(ev, '<br/>') + '</td>';
      html += '<td class="gpsWrap">' + gpsEscape(ev.note) + '</td>';
      html += '<td class="gpsTourActions">';

      if (control)
         html += '<button class="rounded-border tool-button mdi mdi-lead-pencil" type="button" title="ändern" onclick="event.stopPropagation(); gpsEventDialog(\'' + ev.type + '\', ' + ev.id + ')"></button>'
               + '<button class="rounded-border tool-button mdi mdi-delete actDelete" type="button" title="löschen" onclick="event.stopPropagation(); gpsEventDelete(' + ev.id + ')"></button>';

      html += '</td></tr>';
   }

   html += '</tbody></table>';

   return html;
}

// event 'gpstourpoints'

function processGpsTourPoints(tour)
{
   gpsShownTour = tour;

   if (currentPage != 'gpslive' || gpsView != 'tour' || !mapManager.map)
      return;

   mapManager.showTour(tour.points);
   mapManager.showEvents(tour.events);

   if (gpsFocusEvent) {
      mapManager.focusEvent(gpsFocusEvent);
      gpsFocusEvent = null;
   }

   let info = "'" + gpsEscape(tour.name) + "' · " + gpsFmtTime(tour.start)
       + (tour.stop != null ? ' - ' + gpsFmtTime(tour.stop) : ' (aktiv)')
       + ' · ' + gpsFmtDistance(tour.distance) + ' · ' + tour.points.length + ' Punkte';

   if (tour.totals && tour.totals.events)
      info += '<div class="gpsTourInfoTotals">' + gpsFmtTotals(tour.totals, ' · ') + '</div>';

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
      // ask the daemon for the end proposal first (arrival at the final position) -> gpsStopDialog()
      socket.send({ "event" : "gpstour", "object" : { "action" : "stopinfo" } });
      return;
   }

   let now = new Date();
   let defaultName = 'Tour ' + now.toLocaleDateString('de-DE', { day: '2-digit', month: '2-digit', year: 'numeric' });

   gpsNameDialog('Tour aufzeichnen', defaultName, 'Starten', function(name, odometer) {
      socket.send({ "event" : "gpstour", "object" : { "action" : "start", "name" : name, "odometer" : odometer } });
   }, true);
}

// event 'gpstourstopinfo': stop the tour now or at the arrival at the final position

function gpsStopDialog(info)
{
   let hasArrival = info.arrival != null && info.now - info.arrival > 60;
   let stillFor = hasArrival ? gpsFmtDuration(info.now - info.arrival) : '';

   let form = '<div class="dialog-content gpsStopDialog">' +
       ' <div>Aufzeichnung der Tour <b>' + gpsEscape(info.name) + '</b> beenden.</div>' +
       ' <div class="labelB1">Ende der Tour</div>' +
       ' <div><label><input type="radio" name="gpsStopMode" value="now"' + (hasArrival ? '' : ' checked') + '> jetzt (' + gpsFmtTime(info.now) + ')</label></div>';

   if (hasArrival)
      form += ' <div><label><input type="radio" name="gpsStopMode" value="arrival" checked> Ankunft ' + gpsFmtTime(info.arrival) +
              ' <span class="actSyncHint">(seit ' + stillFor + ' innerhalb von ' + info.tolerance + ' m um die Endposition)</span></label></div>';
   else
      form += ' <div class="actSyncHint">Keine Ankunft erkannt, die Position hat sich zuletzt um mehr als ' + info.tolerance + ' m bewegt.</div>';

   form += ' <div class="labelB1">km-Stand am Ende (optional)</div>' +
           ' <div><input type="number" id="gpsStopOdometer" class="rounded-border input" min="0" step="1" style="width:100%;" placeholder="km"></div>';
   form += '</div>';

   $(form).dialog({
      modal: true,
      width: 'auto',
      title: 'Tour beenden',
      buttons: {
         'Abbrechen': function() { $(this).dialog('close'); },
         'Beenden': function() {
            let mode = $('input[name=gpsStopMode]:checked').val();
            let odometer = parseInt($('#gpsStopOdometer').val()) || 0;
            $(this).dialog('close');
            socket.send({ "event" : "gpstour", "object" : { "action" : "stop", "stop" : mode == 'arrival' ? info.arrival : 0, "odometer" : odometer } });
         }
      },
      close: function() { $(this).dialog('destroy').remove(); }
   });
}

function gpsRenameTour(id)
{
   let tour = gpsTours ? gpsTours.tours.find(t => t.id == id) : null;

   if (!tour)
      return;

   gpsNameDialog('Tour bearbeiten', tour.name, 'Speichern', function(name, odometer, odometerEnd, comment) {
      socket.send({ "event" : "gpstour", "object" : { "action" : "rename", "id" : id, "name" : name, "odometer" : odometer, "odometerend" : odometerEnd, "comment" : comment } });
   }, true, tour);
}

function gpsDeleteTour(id)
{
   let tour = gpsTours ? gpsTours.tours.find(t => t.id == id) : null;

   if (!tour)
      return;

   confirmDialog(function() {
      socket.send({ "event" : "gpstour", "object" : { "action" : "delete", "id" : id } });
   }, "Tour <b>" + gpsEscape(tour.name) + "</b> mit ihren Punkten und Ereignissen löschen?", 'Löschen');
}

// name and (withOdometer) the odometer at start, for a stopped tour (tour given) also at the end

function gpsNameDialog(title, value, okLabel, onOk, withOdometer = false, tour = null)
{
   let form = $('<div></div>').addClass('dialog-content')
       .append($('<div></div>').addClass('labelB1').html('Name'))
       .append($('<input></input>')
               .attr('id', 'gpsTourNameInput')
               .attr('type', 'text')
               .attr('maxlength', 100)
               .addClass('rounded-border input')
               .css('width', '100%')
               .val(value));

   let odometerInput = function(id, label, value) {
      form.append($('<div></div>').addClass('labelB1').html(label))
          .append($('<input></input>')
                  .attr('id', id)
                  .attr('type', 'number')
                  .attr('min', 0)
                  .attr('step', 1)
                  .attr('placeholder', 'km')
                  .addClass('rounded-border input')
                  .css('width', '100%')
                  .val(value || ''));
   };

   if (withOdometer) {
      odometerInput('gpsTourOdometerInput', 'km-Stand am Start (optional, für den Verbrauch)', tour ? tour.odometer : 0);

      if (tour && tour.stop != null)
         odometerInput('gpsTourOdometerEndInput', 'km-Stand am Ende', tour.odometerend);
   }

   // a comment for the whole tour (edit only, not on start)

   if (tour)
      form.append($('<div></div>').addClass('labelB1').html('Kommentar'))
          .append($('<textarea></textarea>')
                  .attr('id', 'gpsTourCommentInput')
                  .attr('rows', 4)
                  .attr('maxlength', 2000)
                  .addClass('rounded-border input gpsTourComment')
                  .val(tour.comment || ''));

   let buttons = {};

   buttons['Abbrechen'] = function() { $(this).dialog('close'); };
   buttons[okLabel] = function() {
      let name = $('#gpsTourNameInput').val().trim();
      let odometer = withOdometer ? parseInt($('#gpsTourOdometerInput').val()) || 0 : 0;
      let odometerEnd = $('#gpsTourOdometerEndInput').length ? parseInt($('#gpsTourOdometerEndInput').val()) || 0 : (tour ? tour.odometerend : 0);
      let comment = $('#gpsTourCommentInput').length ? $('#gpsTourCommentInput').val().trim() : '';
      $(this).dialog('close');
      onOk(name, odometer, odometerEnd, comment);
   };

   form.dialog({
      modal: true,
      resizable: false,
      closeOnEscape: true,
      hide: "fade",
      width: Math.min(400, window.innerWidth - 20),
      title: title,
      open: function() {
         $('#gpsTourNameInput').focus().select();
         $('#gpsTourNameInput').keypress(function(e) {
            if (e.which == 13)
               form.parent().find('.ui-dialog-buttonpane button:last').click();
         });
      },
      buttons: buttons,
      close: function() { $(this).dialog('destroy').remove(); }
   });
}

//***************************************************************************
// Tour events - dialog (new with the current position, or edit), delete
//***************************************************************************

function gpsEventDialog(typeKey, id = null)
{
   let type = gpsEventTypes[typeKey];

   if (!type)
      return;

   let ev = null;

   if (id) {
      let lists = Object.values(gpsTourEvents).map(d => d.events);

      if (gpsShownTour)
         lists.unshift(gpsShownTour.events || []);

      for (let list of lists)
         if (!ev)
            ev = list.find(e => e.id == id);
   }

   if (id && !ev)
      return;

   let labels = { 'amount': 'Menge [l]', 'price': typeKey == 'fuel' || typeKey == 'toll' ? 'Preis gesamt [€]' : 'Kosten [€] (optional)', 'odometer': 'km-Stand', 'note': 'Notiz' };
   let form = $('<div></div>').addClass('dialog-content gpsEventDialog')
       .append($('<div></div>').addClass('actDetailsHead').html('<span class="mdi ' + type.icon + '" style="color:' + type.color + '"></span> ' + type.label
                                                                + (ev ? ' · ' + gpsFmtTime(ev.time) : ' · jetzt, mit der aktuellen Position')));

   for (let field of type.fields) {
      if (field == 'full') {      // switch 'voll getankt' (checkbox.css styles input + label as a switch)
         form.append($('<div></div>').addClass('gpsEvFull')
                     .append($('<input></input>').attr('id', 'gpsEv_full').attr('type', 'checkbox').prop('checked', ev ? ev.full !== false : true))
                     .append($('<label></label>').attr('for', 'gpsEv_full').html('voll getankt')));
         continue;
      }

      form.append($('<div></div>').addClass('labelB1').html(labels[field]));

      if (field == 'note')
         form.append($('<input></input>').attr('id', 'gpsEv_note').attr('type', 'text').attr('maxlength', 200).addClass('rounded-border input').css('width', '100%').val(ev ? ev.note : ''));
      else
         form.append($('<input></input>').attr('id', 'gpsEv_' + field).attr('type', 'number').attr('min', 0).attr('step', field == 'odometer' ? 1 : 0.01)
                     .addClass('rounded-border input').css('width', '100%').val(ev && ev[field] ? ev[field] : ''));
   }

   if (typeKey == 'fuel')
      form.append($('<div></div>').addClass('actSyncHint').html('Verbrauch = Menge / km seit dem letzten Volltanken (oder dem Start). Eine Teilbetankung (Schalter „voll getankt“ aus) wird dem nächsten Volltanken zugerechnet und bekommt keinen eigenen Wert.'));

   let buttons = {};

   buttons['Abbrechen'] = function() { $(this).dialog('close'); };
   buttons['Speichern'] = function() {
      let obj = { "action" : "event", "type" : typeKey };

      if (ev)
         obj.id = ev.id;

      for (let field of type.fields) {
         if (field == 'full')
            obj.full = $('#gpsEv_full').is(':checked');
         else {
            let v = $('#gpsEv_' + field).val();
            obj[field] = field == 'note' ? v.trim() : (parseFloat(v) || 0);
         }
      }

      $(this).dialog('close');
      socket.send({ "event" : "gpstour", "object" : obj });
   };

   form.dialog({
      modal: true,
      width: Math.min(400, window.innerWidth - 20),
      title: ev ? 'Ereignis ändern' : type.label + ' eintragen',
      open: function() { $('#gpsEv_' + type.fields[0]).focus(); },
      buttons: buttons,
      close: function() { $(this).dialog('destroy').remove(); }
   });
}

function gpsEventDelete(id)
{
   if (mapManager.map)
      mapManager.map.closePopup();

   confirmDialog(function() {
      socket.send({ "event" : "gpstour", "object" : { "action" : "eventdelete", "id" : id } });
   }, 'Ereignis löschen?', 'Löschen');
}

// details of an event (popup, tooltip)

function gpsEventDetails(ev, sep)
{
   let parts = [];

   if (ev.amount)
      parts.push(gpsFmtLiters(ev.amount) + (ev.full === false ? ' (nicht voll)' : '') + (ev.price ? ' · ' + gpsFmtMoney(ev.price / ev.amount, 3) + '/l' : ''));
   if (ev.price)
      parts.push(gpsFmtMoney(ev.price));
   if (ev.odometer)
      parts.push(ev.odometer.toLocaleString('de-DE') + ' km');
   if (ev.consumption != null || ev.consumptionGps != null)
      parts.push(gpsFmtEventConsumption(ev, sep));
   if (ev.note)
      parts.push(gpsEscape(ev.note));

   return parts.join(sep);
}

// consumption of a fuel stop: by odometer and by GPS distance (whatever is known)

function gpsFmtEventConsumption(ev, sep)
{
   let parts = [];

   if (ev.consumption != null)
      parts.push(gpsFmtConsumption(ev.consumption) + ' <span class="gpsTourSub">Tacho ' + ev.km + ' km</span>');
   if (ev.consumptionGps != null)
      parts.push(gpsFmtConsumption(ev.consumptionGps) + ' <span class="gpsTourSub">GPS ' + Math.round(ev.kmGps) + ' km</span>');
   if (parts.length && ev.pendingLiters)
      parts.push('<span class="gpsTourSub">mit ' + gpsFmtLiters(ev.pendingLiters) + ' Teilbetankung</span>');

   return parts.join(sep);
}

// the totals of a tour (list, info box, control panel)

function gpsFmtTotals(t, sep)
{
   let parts = [];

   if (t.fuelStops)
      parts.push(t.fuelStops + 'x getankt · ' + gpsFmtLiters(t.liters) + ' · ' + gpsFmtMoney(t.fuelCosts));
   if (t.tollCosts)
      parts.push('Maut ' + gpsFmtMoney(t.tollCosts));
   if (t.otherCosts)
      parts.push('Sonstiges ' + gpsFmtMoney(t.otherCosts));
   if (t.totalCosts)
      parts.push('<b>gesamt ' + gpsFmtMoney(t.totalCosts) + '</b>');
   if (t.consumption != null)
      parts.push('Ø ' + gpsFmtConsumption(t.consumption) + ' (Tacho, ' + t.consumptionKm + ' km)');
   if (t.consumptionGps != null)
      parts.push('Ø ' + gpsFmtConsumption(t.consumptionGps) + ' (GPS, ' + Math.round(t.consumptionGpsKm) + ' km)');
   if (t.odometerKm != null)
      parts.push(t.odometerStart.toLocaleString('de-DE') + ' - ' + t.odometerEnd.toLocaleString('de-DE') + ' km (' + t.odometerKm + ' km)');
   else if (t.odometerStart)
      parts.push('Start bei ' + t.odometerStart.toLocaleString('de-DE') + ' km');

   return parts.join(sep);
}

function gpsFmtMoney(v, digits = 2)
{
   return (v || 0).toLocaleString('de-DE', { minimumFractionDigits: digits, maximumFractionDigits: digits }) + ' €';
}

function gpsFmtLiters(v)
{
   return (v || 0).toLocaleString('de-DE', { minimumFractionDigits: 1, maximumFractionDigits: 1 }) + ' l';
}

function gpsFmtConsumption(v)
{
   return (v || 0).toLocaleString('de-DE', { minimumFractionDigits: 1, maximumFractionDigits: 1 }) + ' l/100 km';
}

//***************************************************************************
// Map control 'Zentrieren' (Leaflet button) - live map, Garmin track and activity map
//***************************************************************************

function gpsCenterControl(map, onClick, title = 'Zentrieren')
{
   return gpsMapButton(map, 'mdi-crosshairs-gps', title, onClick);
}

// a button on the map (top right), Leaflet control

function gpsMapButton(map, iconClass, title, onClick)
{
   let Control = L.Control.extend({
      options: { position: 'topright' },
      onAdd: function() {
         let btn = L.DomUtil.create('button', 'live-tracking-control mapCenterButton mdi ' + iconClass);
         btn.type = 'button';
         btn.title = title;
         L.DomEvent.disableClickPropagation(btn);
         L.DomEvent.on(btn, 'click', function(e) {
            L.DomEvent.preventDefault(e);
            onClick();
         });
         return btn;
      }
   });

   let control = new Control();
   map.addControl(control);

   return control;
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

function gpsFmtDate(ts)
{
   if (!ts)
      return '-';

   return new Date(ts * 1000).toLocaleDateString('de-DE', { day: '2-digit', month: '2-digit', year: 'numeric' });
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
