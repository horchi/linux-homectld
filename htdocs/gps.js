/*
 *  gps.js
 *
 *  (c) 2020-2026 Jörg Wendel
 *
 * This code is distributed under the terms and conditions of the
 * GNU GENERAL PUBLIC LICENSE. See the file COPYING for details.
 *
 *  Page 'Map': live position of the GPS sensor, recording of tours, fuel stops
 *  and display of recorded tours.
 *
 *  Views:  'live'  - the map follows the live position
 *          'tours' - the recorded tours: names in the control panel (left), details and the
 *                    event list of the selected tour on the right (show on map, edit, delete)
 *          'tour'  - a recorded tour on the map
 *
 *  Events of a tour (fuel stops, toll, pauses, night stops, notes) are entered while the tour
 *  is active (buttons in the tool bar), each with the current position. The automatic pauses
 *  (standstill) are recorded by the daemon as 'halt' with begin and end. Together with the
 *  odometer at start / end (asked on start / stop, editable with the name) the costs and the
 *  fuel consumption are calculated by the daemon.
 */

var gpsView = localStorage.getItem(storagePrefix + 'gpsView') || 'live';   // 'live' | 'tours' | 'tour' - the tab survives a reload
var gpsTours = null;         // event 'gpstours': { active, tours, minDistance, pauseAfter }
var gpsShownTour = null;     // event 'gpstourpoints' of the displayed tour
var gpsFuelList = null;      // event 'gpsfuellist' - all fuel stops (tours and standalone) with the consumption chain
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
   'note':  { label: 'Hinweis',    icon: 'mdi-note-text',     color: '#455a64', fields: ['note'] },
   'halt':  { label: 'Stillstand', icon: 'mdi-pause-circle-outline', color: '#78909c', fields: ['note'] }   // by the daemon (automatic pause), only the note is editable
};

mapManager = {
   map: null,
   routeLine: null,
   pointsLayer: null,          // historic points (circle markers)
   eventsLayer: null,          // the events of the shown tour (fuel, toll, ...)
   eventMarkers: {},           // their markers by event id
   currentLocationMarker: null,
   isTrackingActive: true,     // live view: the map follows the live position
   isTourTrackingActive: localStorage.getItem(storagePrefix + 'gpsTourFollow') == '1',   // tour view of the active tour: follow the live position
   autoMove: false,            // a programmatic pan is running (not a user interaction)
   buttonControl: null,

   init() {
      this.map = L.map('map', {
         fadeAnimation: false,
         zoomControl: false,           // own zoom buttons in the button column on the right (gpsZoomButtons)
         attributionControl: false     // deaktiviert das leaflet element unten rechts
      }).setView([50.1109, 8.6821], 2);

      if (navigator.onLine) {
         L.tileLayer('https://tile.openstreetmap.de/{z}/{x}/{y}.png', {
            attribution: '© OpenStreetMap',
            maxZoom: 19                // the tile server serves 19, Leaflet's default is 18
         }).addTo(this.map);
      }

      this.routeLine = L.polyline([], {
         color: '#0066cc',
         weight: 5,
         opacity: 0.85
      }).addTo(this.map);

      this.pointsLayer = L.layerGroup().addTo(this.map);
      this.eventsLayer = L.layerGroup().addTo(this.map);

      // dragging the map stops following, zooming (buttons, wheel, pinch) does not: after a zoom the
      // position is centered again (a pinch zooms around the fingers and shifts the view)

      this.map.on('dragstart', () => {
         if (this.following())
            this.setFollow(false);
      });

      this.map.on('zoomend', () => {
         if (this.following() && !this.autoMove)
            this.followCurrent();
      });

      this.createTrackingButton();
      gpsCenterControl(this.map, () => this.centerView());
      gpsMapButton(this.map, 'mdi-format-list-bulleted', 'zurück zur Tourenliste', () => gpsShowTours());
      gpsZoomButtons(this.map);

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

               // live view: start following, tour view: toggle

               this.setFollow(gpsView == 'live' ? true : !this.isTourTrackingActive);

               // Sofort zum letzten bekannten Punkt springen, falls vorhanden

               if (this.following())
                  this.followLast();
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
         this.map.fitBounds(this.routeLine.getBounds(), { padding: [30, 30], maxZoom: 17 });
      else if (this.currentLocationMarker)
         this.map.setView(this.currentLocationMarker.getLatLng(), Math.max(this.map.getZoom(), 15));
      else if (points.length)
         this.map.setView(points[0], Math.max(this.map.getZoom(), 15));
   },

   // following the live position: live view, or the map of the tour being recorded

   isTourLive() {
      return gpsView == 'tour' && gpsShownTour && gpsTours && gpsTours.active && gpsTours.active.id == gpsShownTour.id;
   },

   following() {
      if (gpsView == 'live')
         return this.isTrackingActive;

      return this.isTourLive() && this.isTourTrackingActive;
   },

   setFollow(on) {
      if (gpsView == 'live')
         this.isTrackingActive = on;
      else {
         this.isTourTrackingActive = on;
         localStorage.setItem(storagePrefix + 'gpsTourFollow', on ? '1' : '0');
      }

      this.updateButtonUI();
   },

   // pan to a position without stopping the following by the own 'movestart'

   follow(latLng, zoom) {
      this.autoMove = true;

      if (zoom != null)
         this.map.setView(latLng, zoom, { animate: false });
      else
         this.map.panTo(latLng, { animate: false });

      this.autoMove = false;
   },

   followLast() {
      const points = this.routeLine.getLatLngs();

      if (this.currentLocationMarker)
         this.follow(this.currentLocationMarker.getLatLng(), Math.max(this.map.getZoom(), 15));
      else if (points.length > 0)
         this.follow(points[points.length - 1], Math.max(this.map.getZoom(), 15));
   },

   // center the position at the current zoom (after a zoom by the user)

   followCurrent() {
      const points = this.routeLine.getLatLngs();

      if (this.currentLocationMarker)
         this.follow(this.currentLocationMarker.getLatLng());
      else if (points.length > 0)
         this.follow(points[points.length - 1]);
   },

   updateButtonUI() {
      if (!this.buttonControl) return;

      // the follow button belongs to the live view and to the map of the tour being recorded

      let visible = gpsView == 'live' || this.isTourLive();

      this.buttonControl.style.display = visible ? '' : 'none';

      if (!visible)
         return;

      if (this.following()) {
         this.buttonControl.innerHTML = gpsView == 'live' ? '● Live-Tracking aktiv' : '● Live-Verfolgung aktiv';
         this.buttonControl.classList.add('live-tracking-active');
      } else {
         this.buttonControl.innerHTML = gpsView == 'live' ? '🔄 Tracking pausiert (Klicken zum Folgen)' : '🔄 Live-Verfolgung (Klicken zum Folgen)';
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
      this.follow(points[points.length - 1], Math.max(this.map.getZoom(), 15));
      this.updateButtonUI();
   },

   addLivePoint(latLng, first) {
      if (first) {
         this.routeLine.setLatLngs([latLng, latLng]);
         this.follow(latLng, 15);
         this.setCurrentLocation(latLng);
         return;
      }

      // den alten Standort in die Historie überführen, den Live-Marker verschieben

      if (this.currentLocationMarker)
         this.addHistoryPoint(this.currentLocationMarker.getLatLng());

      this.routeLine.addLatLng(latLng);
      this.setCurrentLocation(latLng);

      if (this.isTrackingActive)
         this.follow(latLng);
   },

   // a recorded tour: points [[lat, lng, time], ...]; 'active' - the tour is being recorded,
   // the last point is the current position (live marker instead of the 'Ende' marker)

   showTour(points, active = false) {
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

      if (active)
         this.setCurrentLocation(latLngs[latLngs.length - 1]);
      else if (latLngs.length > 1) {
         let last = points[points.length - 1];
         L.circleMarker(latLngs[latLngs.length - 1], {
            radius: 7, fillColor: '#d32f2f', fillOpacity: 1, color: '#ffffff', weight: 3
         }).bindTooltip('Ende ' + gpsFmtTime(last[2])).addTo(this.pointsLayer);
      }

      // maxZoom: a short tour (or one at standstill) would otherwise open at the maximum zoom

      this.autoMove = true;
      this.map.fitBounds(this.routeLine.getBounds(), { padding: [30, 30], animate: false, maxZoom: 17 });
      this.autoMove = false;

      if (active && this.isTourTrackingActive)
         this.followLast();

      this.updateButtonUI();
   },

   // tour being recorded: a stored point (event 'gpstourpoint') extends the route on the map,
   // optionally the map follows

   addTourLivePoint(latLng) {
      let points = this.routeLine.getLatLngs();

      if (points.length == 2 && points[0].equals(points[1]))
         this.routeLine.setLatLngs([points[0], latLng]);
      else
         this.routeLine.addLatLng(latLng);

      this.setCurrentLocation(latLng);

      if (this.isTourTrackingActive)
         this.follow(latLng);
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
   else if (gpsView == 'fuel')
      gpsShowFuel();
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
   $('#dashboardMenu').removeClass('hidden').addClass('gpsToolbar')    // opaque background (dashboard.js removes the classes)
      .toggleClass('gpsToolbarBig', bigDashSymbols());                 // higher bar like the big dashboard symbols (kiosk URL parameter, else config)
   $('#dashboardMenu').empty();

   $('#dashboardMenu')
      .append($('<button></button>')
              .attr('id', 'gpsBtnLive')
              .addClass('rounded-border buttonDashboard gpsViewButton mdi mdi-crosshairs-gps')
              .attr('title', 'Live-Position')
              .click(function() { gpsShowLive(); }))
      .append($('<button></button>')
              .attr('id', 'gpsBtnTours')
              .addClass('rounded-border buttonDashboard gpsViewButton mdi mdi-routes')
              .attr('title', 'Touren')
              .click(function() { gpsShowTours(); }))
      .append($('<button></button>')
              .attr('id', 'gpsBtnFuel')
              .addClass('rounded-border buttonDashboard gpsViewButton mdi mdi-gas-station')
              .attr('title', 'Tankstopps und Verbrauch')
              .click(function() { gpsShowFuel(); }))
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
   else if (gpsView == 'fuel')
      $('#gpsBtnFuel').addClass('buttonDashboardActive');
   else
      $('#gpsBtnTours').addClass('buttonDashboardActive');

   // the recording state colors the 'Touren' button (like the 'Map' tab)

   let active = gpsTours ? gpsTours.active : null;

   $('#gpsBtnTours').removeClass('tabRecording tabPaused');

   if (active)
      $('#gpsBtnTours').addClass(active.paused ? 'tabPaused' : 'tabRecording');

   gpsUpdateEventButtons();
}

// the recording state is shown by the color of the 'Map' tab (red: recording, orange: paused),
//  on every page; called with each 'gpstours' event and after the menu was rebuilt

function gpsUpdateTabState()
{
   let active = gpsTours ? gpsTours.active : null;
   let tab = $('#mainmenu_gpslive').removeClass('tabRecording tabPaused');

   if (active)
      tab.addClass(active.paused ? 'tabPaused' : 'tabRecording')
         .attr('title', (active.paused ? 'Tour pausiert' : 'Tour wird aufgezeichnet') + ": '" + active.name + "'");
   else
      tab.removeAttr('title');
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
                     .attr('data-id', tour.id)
                     .attr('title', tour.name)
                     .html((isActive ? '<span class="' + (gpsTours.active.paused ? 'gpsPaused' : 'gpsRecording') + '">● </span>' : '') + gpsEscape(tour.name)
                           + '<br/><span class="gpsTourBtnSub">' + gpsTourBtnSub(tour) + '</span>')
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
      mapManager.showTour(gpsShownTour.points, gpsShownTour.stop == null);
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

   if (currentPage != 'gpslive' || !mapManager.map)
      return;

   if (gpsView == 'live')
      mapManager.addLivePoint(latLng, first);
}

//***************************************************************************
// Tours - event 'gpstours'
//***************************************************************************

function processGpsTours(tours)
{
   gpsTours = tours;
   gpsUpdateTabState();

   if (currentPage != 'gpslive')
      return;

   gpsUpdateToolbar();

   if (gpsView == 'tours') {
      if (gpsSelectedTour)
         socket.send({ "event" : "gpstour", "object" : { "action" : "events", "id" : gpsSelectedTour } });   // events may have changed
      gpsShowTours();
   }
   else if (gpsView == 'fuel')
      socket.send({ "event" : "gpstour", "object" : { "action" : "fuellist" } });   // a fuel stop may have changed
   else if (gpsView == 'tour' && gpsShownTour) {
      let entry = gpsTours.tours.find(t => t.id == gpsShownTour.id);
      let isActive = gpsTours.active && gpsTours.active.id == gpsShownTour.id;

      if (gpsShownTour.stop == null && !isActive) {
         // the shown tour ended: the final state and the 'Ende' marker, the points once more
         socket.send({ "event" : "gpstour", "object" : { "action" : "points", "id" : gpsShownTour.id } });
      }
      else {
         // the points are already there (per 'gpstourpoint'), only name / events may have changed
         if (entry) {
            gpsShownTour.name = entry.name;
            gpsShownTour.distance = entry.distance;
            gpsShownTour.pausetime = entry.pausetime;
         }

         socket.send({ "event" : "gpstour", "object" : { "action" : "events", "id" : gpsShownTour.id } });
         $('#gpsTourInfo').html(gpsTourInfoHtml(gpsShownTour));
         mapManager.updateButtonUI();
      }
   }
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

   if (gpsShownTour && gpsShownTour.id == obj.id) {
      gpsShownTour.events = obj.events;
      gpsShownTour.totals = obj.totals;
   }

   if (currentPage != 'gpslive')
      return;

   if (gpsView == 'tours' && gpsSelectedTour == obj.id)
      $('#gpsTourEvents').html(gpsTourEventsHtml(obj));
   else if (gpsView == 'tour' && gpsShownTour && gpsShownTour.id == obj.id && mapManager.map) {
      mapManager.showEvents(obj.events);
      $('#gpsTourInfo').html(gpsTourInfoHtml(gpsShownTour));
   }
}

// event 'gpstourpoint' - a stored point of the tour being recorded with the totals

function processGpsTourPoint(obj)
{
   if (gpsTours && gpsTours.active && gpsTours.active.id == obj.id) {
      gpsTours.active.distance = obj.distance;
      gpsTours.active.points = obj.points;

      let entry = gpsTours.tours.find(t => t.id == obj.id);

      if (entry) {
         entry.distance = obj.distance;
         entry.points = obj.points;
      }
   }

   if (gpsShownTour && gpsShownTour.id == obj.id) {
      gpsShownTour.points.push(obj.point);
      gpsShownTour.distance = obj.distance;
   }

   if (currentPage != 'gpslive')
      return;

   gpsUpdateToolbar();

   if (gpsView == 'tours') {
      // targeted updates, no rebuild of the panel and the details
      $('.gpsTourBtn[data-id="' + obj.id + '"] .gpsTourBtnSub').html(gpsTourBtnSub(gpsTours.tours.find(t => t.id == obj.id)));

      if (gpsSelectedTour == obj.id) {
         $('#gpsTourState').html(gpsTourStateHtml());
         $('#gpsTourDistance').html(gpsFmtDistance(obj.distance));
         $('#gpsTourPoints').html(obj.points);
      }
   }
   else if (gpsView == 'tour' && gpsShownTour && gpsShownTour.id == obj.id && mapManager.map) {
      mapManager.addTourLivePoint(L.latLng(obj.point[0], obj.point[1]));
      $('#gpsTourInfo').html(gpsTourInfoHtml(gpsShownTour));
   }
}

//***************************************************************************
// Fuel stops - view 'fuel', event 'gpsfuellist'
//   all fuel stops of all tours plus the ones entered outside of a tour, with the
//   consumption from full tank to full tank by the odometer readings
//***************************************************************************

function gpsShowFuel()
{
   gpsView = 'fuel';
   localStorage.setItem(storagePrefix + 'gpsView', 'fuel');
   gpsUpdateToolbar();

   $('#mapcontainer').addClass('hidden');
   showControlContainer();
   $('#container').removeClass('hidden');
   gpsResizeContainer('container');

   gpsBuildFuelPanel();
   document.getElementById("container").innerHTML = gpsFuelListHtml(gpsFuelList);

   socket.send({ "event" : "gpstour", "object" : { "action" : "fuellist" } });
}

function gpsBuildFuelPanel()
{
   $("#controlContainer").empty();

   if (gpsHasControlRights()) {
      $("#controlContainer")
         .append($('<div></div>')
                 .append($('<button></button>')
                         .addClass('rounded-border tool-button mdi mdi-gas-station')
                         .html(' Tankstopp eintragen')
                         .attr('title', 'Tankstopp außerhalb einer Tour eintragen (mit Datum und km-Stand)')
                         .click(function() { gpsEventDialog('fuel', null, true); })))
         .append($('<div></div>').addClass('button-group-spacing'));
   }

   $("#controlContainer")
      .append($('<button></button>')
              .addClass('rounded-border tool-button')
              .html('Hilfe')
              .attr('title', 'Beschreibung im README')
              .click(function() { showHelp('gps-fuel'); }));
}

function processGpsFuelList(obj)
{
   gpsFuelList = obj;

   if (currentPage == 'gpslive' && gpsView == 'fuel')
      document.getElementById("container").innerHTML = gpsFuelListHtml(obj);
}

function gpsFuelListHtml(data)
{
   if (!data)
      return '<div class="rounded-border seperatorFold">Tankstopps werden geladen ...</div>';

   let t = data.totals || {};
   let control = gpsHasControlRights();
   let html = '<div class="gpsTourDetails">';

   html += ' <div class="rounded-border seperatorFold gpsTourHead"><span>Tankstopps' + (t.stops ? ' (' + t.stops + ')' : '') + '</span></div>';

   if (!data.stops.length) {
      html += ' <div class="rounded-border gpsTourBody"><div class="gpsTourSub">Noch keine Tankstopps. Sie werden während einer Tour über den Button in der Leiste eingetragen';
      if (control)
         html += " oder hier mit 'Tankstopp eintragen' auch außerhalb einer Tour";
      return html + '.</div></div></div>';
   }

   // the totals: average consumption over the counted fills, liters, costs, price per liter

   let parts = [];

   if (t.consumption != null)
      parts.push('<b>Ø ' + gpsFmtConsumption(t.consumption) + '</b> <span class="gpsTourSub">über ' + t.consumptionKm.toLocaleString('de-DE') + ' km, ' + t.counted + (t.counted == 1 ? ' Tankfüllung' : ' Tankfüllungen') + '</span>');
   parts.push(gpsFmtLiters(t.liters) + ' gesamt');
   if (t.costs)
      parts.push(gpsFmtMoney(t.costs) + ' gesamt');
   if (t.pricePerLiter != null && t.costs)
      parts.push('Ø ' + gpsFmtMoney(t.pricePerLiter, 3) + '/l');

   html += ' <div class="rounded-border gpsTourBody">' + parts.join(' · ') + '</div>';

   // the list, newest first

   html += ' <div class="rounded-border seperatorFold gpsTourHead">Liste</div>';
   html += ' <div class="rounded-border gpsTourBody"><table class="gpsEventTable">';
   html += '<thead><tr><td>Zeit</td><td>Tour</td><td class="gpsNum">km-Stand</td><td class="gpsNum">Menge</td><td class="gpsNum">Preis</td><td class="gpsNum">Verbrauch</td><td>Notiz</td><td></td></tr></thead><tbody>';

   for (let i = data.stops.length - 1; i >= 0; i--) {
      let ev = data.stops[i];
      let consumption = '';

      if (ev.consumption != null)
         consumption = gpsFmtConsumption(ev.consumption) + '<br/><span class="gpsTourSub">' + ev.km.toLocaleString('de-DE') + ' km' + (ev.pendingLiters ? ', mit ' + gpsFmtLiters(ev.pendingLiters) + ' Teilbetankung' : '') + '</span>';
      else if (ev.full === false)
         consumption = '<span class="gpsTourSub">Teilbetankung</span>';
      else if (!ev.odometer)
         consumption = '<span class="gpsTourSub">kein km-Stand</span>';

      html += '<tr>';
      html += '<td>' + gpsFmtTime(ev.time) + '</td>';
      html += '<td>' + (ev.tourid ? '<a class="gpsLink" title="Tour anzeigen" onclick="gpsSelectTour(' + ev.tourid + ')">' + gpsEscape(ev.tour || ('Tour ' + ev.tourid)) + '</a>' : '<span class="gpsTourSub">-</span>') + '</td>';
      html += '<td class="gpsNum">' + (ev.odometer ? ev.odometer.toLocaleString('de-DE') + ' km' : '') + '</td>';
      html += '<td class="gpsNum">' + (ev.amount ? gpsFmtLiters(ev.amount) + (ev.full === false ? '<br/><span class="gpsTourSub">nicht voll</span>' : '') : '') + '</td>';
      html += '<td class="gpsNum">' + (ev.price ? gpsFmtMoney(ev.price) + (ev.amount ? '<br/><span class="gpsTourSub">' + gpsFmtMoney(ev.price / ev.amount, 3) + '/l</span>' : '') : '') + '</td>';
      html += '<td class="gpsNum">' + consumption + '</td>';
      html += '<td class="gpsWrap">' + gpsEscape(ev.note) + '</td>';
      html += '<td class="gpsTourActions">';

      if (control)
         html += '<button class="rounded-border tool-button mdi mdi-lead-pencil" type="button" title="ändern" onclick="gpsEventDialog(\'fuel\', ' + ev.id + ')"></button>'
               + '<button class="rounded-border tool-button mdi mdi-delete actDelete" type="button" title="löschen" onclick="gpsEventDelete(' + ev.id + ')"></button>';

      html += '</td></tr>';
   }

   html += '</tbody></table></div></div>';

   return html;
}

function gpsTourBtnSub(tour)
{
   return tour ? gpsFmtDate(tour.start) + ' · ' + gpsFmtDistance(tour.distance) : '';
}

// the state of the active tour behind its name (head of the tour, updated per point)

function gpsTourStateHtml()
{
   let a = gpsTours ? gpsTours.active : null;

   if (!a)
      return '';

   return '<span class="' + (a.paused ? 'gpsPaused' : 'gpsRecording') + '">' + (a.paused ? '❚❚ pausiert' : '● aktiv')
      + ' · ' + gpsFmtDistance(a.distance) + ' · ' + a.points + ' Punkte</span>';
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
         + ' <span id="gpsTourState">' + (isActive ? gpsTourStateHtml() : '') + '</span></span>';
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
               ['Pause', gpsFmtDuration(tour.pausetime) + (t.halts ? ' <span class="gpsTourSub">' + t.halts + 'x</span>' : '')],
               ['Fahrzeit', gpsFmtDuration(stop - tour.start - (tour.pausetime || 0))],
               ['Strecke (GPS)', gpsFmtDistance(tour.distance), 'gpsTourDistance'],
               ['Punkte', tour.points, 'gpsTourPoints']];

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
   if (t.previousFullAt)
      rows.push(['Volltanken vor Tour', gpsFmtDate(t.previousFullAt) + ' <span class="gpsTourSub">bei ' + t.previousFullOdometer.toLocaleString('de-DE') + ' km</span>']);
   if (t.consumption != null)
      rows.push(['Ø Verbrauch (Tacho)', gpsFmtConsumption(t.consumption) + ' <span class="gpsTourSub">auf ' + t.consumptionKm + ' km' + (t.sinceStart ? ', ab Tour-Start' : '') + '</span>']);
   if (t.consumptionGps != null)
      rows.push(['Ø Verbrauch (GPS)', gpsFmtConsumption(t.consumptionGps) + ' <span class="gpsTourSub">auf ' + Math.round(t.consumptionGpsKm) + ' km</span>']);

   let narrow = window.innerWidth < 900;
   let half = narrow ? rows.length : Math.ceil(rows.length / 2);

   html += ' <div class="rounded-border gpsTourBody gpsTourBodyFlex"><table class="gpsDetailsTable">';

   for (let i = 0; i < half; i++) {
      let l = rows[i];
      let r = narrow ? null : rows[i + half];
      html += '<tr><td>' + l[0] + '</td><td class="gpsNum"' + (l[2] ? ' id="' + l[2] + '"' : '') + '>' + l[1] + '</td>';
      if (!narrow)
         html += r ? '<td class="gpsDetailsCol2">' + r[0] + '</td><td class="gpsNum"' + (r[2] ? ' id="' + r[2] + '"' : '') + '>' + r[1] + '</td>' : '<td></td><td></td>';
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
      html += '<td>' + gpsFmtTime(ev.time) + (ev.duration != null ? '<br/><span class="gpsTourSub">' + gpsFmtHaltEnd(ev) + '</span>' : '') + '</td>';
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

   mapManager.showTour(tour.points, tour.stop == null);
   mapManager.showEvents(tour.events);

   if (gpsFocusEvent) {
      mapManager.focusEvent(gpsFocusEvent);
      gpsFocusEvent = null;
   }

   if (!$('#gpsTourInfo').length)
      $('#mapcontainer').append($('<div></div>').attr('id', 'gpsTourInfo').addClass('rounded-border gpsTourInfo'));

   $('#gpsTourInfo').html(gpsTourInfoHtml(tour)).removeClass('hidden');
}

// the info box at the bottom of the tour map

function gpsTourInfoHtml(tour)
{
   let info = "'" + gpsEscape(tour.name) + "' · " + gpsFmtTime(tour.start)
       + (tour.stop != null ? ' - ' + gpsFmtTime(tour.stop) : ' (aktiv)')
       + ' · ' + gpsFmtDistance(tour.distance) + ' · ' + tour.points.length + ' Punkte';

   if (tour.totals && tour.totals.events)
      info += '<div class="gpsTourInfoTotals">' + gpsFmtTotals(tour.totals, ' · ') + '</div>';

   return info;
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

// standalone: a fuel stop outside of a tour (view 'fuel'), with date / time instead of 'now'

function gpsEventDialog(typeKey, id = null, standalone = false)
{
   let type = gpsEventTypes[typeKey];

   if (!type)
      return;

   let ev = null;

   if (id) {
      let lists = Object.values(gpsTourEvents).map(d => d.events);

      if (gpsShownTour)
         lists.unshift(gpsShownTour.events || []);
      if (gpsFuelList)
         lists.unshift(gpsFuelList.stops || []);

      for (let list of lists)
         if (!ev)
            ev = list.find(e => e.id == id);
   }

   if (id && !ev)
      return;

   if (ev && ev.tourid === 0)        // the fuel list carries the tour id, 0 -> entered outside of a tour
      standalone = true;

   let labels = { 'amount': 'Menge [l]', 'price': typeKey == 'fuel' || typeKey == 'toll' ? 'Preis gesamt [€]' : 'Kosten [€] (optional)', 'odometer': 'km-Stand', 'note': 'Notiz' };
   let form = $('<div></div>').addClass('dialog-content gpsEventDialog')
       .append($('<div></div>').addClass('actDetailsHead').html('<span class="mdi ' + type.icon + '" style="color:' + type.color + '"></span> ' + type.label
                                                                + (standalone ? ' · außerhalb einer Tour' : ev ? ' · ' + gpsFmtTime(ev.time) : ' · jetzt, mit der aktuellen Position')));

   if (standalone) {
      form.append($('<div></div>').addClass('labelB1').html('Datum / Zeit'));
      form.append($('<input></input>').attr('id', 'gpsEv_time').attr('type', 'datetime-local').addClass('rounded-border input').css('width', '100%')
                  .val(gpsToLocalInput(ev ? ev.time : Math.floor(Date.now() / 1000))));
   }

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

      if (standalone) {
         let time = Math.floor(new Date($('#gpsEv_time').val()).getTime() / 1000);

         if (!ev)
            obj.tourid = 0;
         if (time > 0)
            obj.time = time;
      }

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
      open: function() { $('#gpsEv_' + (standalone ? 'odometer' : type.fields[0])).focus(); },
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

   if (ev.duration != null)
      parts.push(gpsFmtHaltEnd(ev));
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

// end and duration of a standstill ('halt'), a running one counts from its begin; a pause / night
//  stop entered during a halt carries the halt's begin as 'halttime' (the halt is not listed itself)

function gpsFmtHaltEnd(ev)
{
   let begin = ev.halttime || ev.time;
   let prefix = ev.halttime ? 'Stillstand ' + gpsFmtClock(ev.halttime, ev.time) + ' ' : '';

   if (ev.endtime)
      return prefix + 'bis ' + gpsFmtClock(ev.endtime, ev.time) + ' · ' + gpsFmtDuration(ev.duration);

   return prefix + '<span class="gpsPaused">läuft</span> · ' + gpsFmtDuration(Math.floor(Date.now() / 1000) - begin);
}

// time of day, with the date only on another day than 'ref'

function gpsFmtClock(ts, ref)
{
   let d = new Date(ts * 1000);

   if (ref && d.toDateString() == new Date(ref * 1000).toDateString())
      return d.toLocaleTimeString('de-DE', { hour: '2-digit', minute: '2-digit' });

   return gpsFmtTime(ts);
}

// consumption of a fuel stop: by odometer and by GPS distance (whatever is known)

function gpsFmtEventConsumption(ev, sep)
{
   let parts = [];

   if (ev.consumption != null)
      parts.push(gpsFmtConsumption(ev.consumption) + ' <span class="gpsTourSub">Tacho ' + ev.km + ' km'
                 + (ev.since == 'start' ? ' ab Tour-Start' : ev.since ? ' seit Volltanken ' + gpsFmtDate(ev.since) : '') + '</span>');
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

// zoom in / out in the style of the other map buttons (the following of the position stays on)

function gpsZoomButtons(map)
{
   gpsMapButton(map, 'mdi-plus', 'Vergrößern', () => map.zoomIn());
   gpsMapButton(map, 'mdi-minus', 'Verkleinern', () => map.zoomOut());
}

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

// value of an <input type="datetime-local"> (local time, 'YYYY-MM-DDTHH:MM')

function gpsToLocalInput(ts)
{
   let d = new Date(ts * 1000);
   let p = n => String(n).padStart(2, '0');

   return d.getFullYear() + '-' + p(d.getMonth() + 1) + '-' + p(d.getDate()) + 'T' + p(d.getHours()) + ':' + p(d.getMinutes());
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
