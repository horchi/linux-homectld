/*
 *  gps.js
 *
 *  (c) 2020-2026 Jörg Wendel
 *
 * This code is distributed under the terms and conditions of the
 * GNU GENERAL PUBLIC LICENSE. See the file COPYING for details.
 *
 */

mapManager = {
   map: null,
   routeLine: null,
   isFirstPoint: true,
   isTrackingActive: true, // Steuert, ob die Karte dem Punkt folgt
   buttonControl: null,    // Referenz auf das Button-Element

   init() {
      this.map = L.map('map', {
         fadeAnimation: false,
         attributionControl: false     // deaktiviert das leaflet element unten rechts
      }).setView([50.1109, 8.6821], 2);

      if (navigator.onLine) {
         L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', {
            attribution: '© OpenStreetMap'
         }).addTo(this.map);
      }

      this.routeLine = L.polyline([], {
         color: 'red',
         weight: 5,
         opacity: 0.9
      }).addTo(this.map);

      this.map.on('movestart', (e) => {
         if (e.target._mainviewchange || (this.map._panAnim && this.map._panAnim.isPlaying)) return;
         if (this.isTrackingActive) {
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
         options: { position: 'topright' }, // Oben rechts platzieren
         onAdd: () => {
            const btn = L.DomUtil.create('button', 'live-tracking-control');
            this.buttonControl = btn;
            this.updateButtonUI();

            // Klick-Event für den Button
            L.DomEvent.on(btn, 'click', (e) => {
               L.DomEvent.stopPropagation(e); // Verhindert Klick-Event auf der Karte
               this.isTrackingActive = true;
               this.updateButtonUI();

               // Sofort zum letzten bekannten Punkt springen, falls vorhanden
               const points = this.routeLine.getLatLngs();
               if (points.length > 0) {
                  this.map.setView(points[points.length - 1], this.map.getZoom());
               }
            });
            return btn;
         }
      });
      this.map.addControl(new CustomControl());
   },

   updateButtonUI() {
      if (!this.buttonControl) return;
      if (this.isTrackingActive) {
         this.buttonControl.innerHTML = '● Live-Tracking aktiv';
         this.buttonControl.classList.add('live-tracking-active');
      } else {
         this.buttonControl.innerHTML = '🔄 Tracking pausiert (Klicken zum Folgen)';
         this.buttonControl.classList.remove('live-tracking-active');
      }
   }
};

// @param {Array} coordinate - [Breitengrad, Längengrad]

function processLiveGpsMessage(coordinate)
{
   if (currentPage == 'gpslive')
   {
      $('#mapcontainer').removeClass('hidden');

      if (!$('#map').length)
      {
         $('#mapcontainer').empty()
            .append($('<div></div>')
                    .attr('id', 'map'));
      }

      $('#mapcontainer').height($(window).height() - getTotalHeightOf('menu') - getTotalHeightOf('dashboardMenu') - getTotalHeightOf('footer') - sab - 15);
      window.onresize = function() {
         $('#mapcontainer').height($(window).height() - getTotalHeightOf('menu') - getTotalHeightOf('dashboardMenu') - getTotalHeightOf('footer') - sab - 15);
      };
   }

   if (!$('#map').length)
      return;

   console.log("processLiveGpsMessage", coordinate);

   if (!Array.isArray(coordinate) || coordinate.length !== 2) {
      console.warn("Invalid coordinate format", coordinate);
      return;
   }

   if (!mapManager.map)
      mapManager.init();

   const targetLatLng = L.latLng(coordinate);
   const currentPoints = mapManager.routeLine.getLatLngs();

   // 1. INITIALISIERUNG DER LINIE (BLAU FÄRBEN)
   // Zwingt die bestehende Linie im MapManager, sich ab jetzt blau zu zeichnen
   if (mapManager.routeLine) {
      mapManager.routeLine.setStyle({
         color: '#0066cc',   // Beautiful Electric Blue for the history path
         weight: 5,          // Thickness of the line
         opacity: 0.85
      });
   }

   if (currentPoints.length === 0) {
      // first point
      mapManager.routeLine.setLatLngs([targetLatLng, targetLatLng]);
      mapManager.map.setView(targetLatLng, 15, { animate: false });
      mapManager.isFirstPoint = false;

      // 2. ERSTELLEN DES AKTUELLEN STANDORT-MARKERS (HIGHLIGHT)
      // Wir nutzen einen nativen Leaflet-CircleMarker mit CSS-Klassen-Bindung
      mapManager.currentLocationMarker = L.circleMarker(targetLatLng, {
         radius: 7,
         fillColor: '#0052cc', // Solid deep blue center
         fillOpacity: 1,
         color: '#ffffff',     // Sharp white border
         weight: 3,
         className: 'map-point current-location' // Binds our custom pulsing CSS classes
      }).addTo(mapManager.map);
   }
   else {
      // Letzten bekannten Punkt aus der Linie holen
      const lastPoint = currentPoints[currentPoints.length - 1];

      // Abstand in Metern zwischen dem letzten Punkt und der neuen Koordinate berechnen
      const distance = lastPoint.distanceTo(targetLatLng);

      // bewegung > 10 Meter?
      console.log("distance", distance);

      if (distance > 10.0) {
         mapManager.routeLine.addLatLng(targetLatLng);

         // 3. DEN ALTEN STANDORT-MARKER IN DIE HISTORIE ÜBERFÜHREN
         // Wenn bereits ein aktiver Marker existiert, klonen wir ihn an der alten Position,
         // entfernen die Highlight-Klasse und färben ihn dauerhaft hellblau.
         if (mapManager.currentLocationMarker) {
            const oldLatLng = mapManager.currentLocationMarker.getLatLng();
            L.circleMarker(oldLatLng, {
               radius: 4,
               fillColor: '#33a2ff', // Soft historical light-blue
               fillOpacity: 0.7,
               color: '#0066cc',
               weight: 1,
               className: 'map-point' // Standard historic layout class
            }).addTo(mapManager.map);

            // 4. DEN HIGHLIGHT-MARKER AUF DIE NEUE LIVE-POSITION SCHIEBEN
            mapManager.currentLocationMarker.setLatLng(targetLatLng);
         }

         // Ausschnitt nur bei Bewegung nachführen
         if (mapManager.isTrackingActive)
            mapManager.map.panTo(targetLatLng, { animate: false });
      }
   }
}

function _processLiveGpsMessage(coordinate)
{
   if (currentPage == 'gpslive')
   {
      $('#mapcontainer').removeClass('hidden');

      if (!$('#map').length)
      {
         $('#mapcontainer').empty()
            .append($('<div></div>')
                    .attr('id', 'map'));
      }

      $('#mapcontainer').height($(window).height() - getTotalHeightOf('menu') - getTotalHeightOf('dashboardMenu') - getTotalHeightOf('footer') - sab - 15);
      window.onresize = function() {
         $('#mapcontainer').height($(window).height() - getTotalHeightOf('menu') - getTotalHeightOf('dashboardMenu') - getTotalHeightOf('footer') - sab - 15);
      };
   }

   if (!$('#map').length)
      return;

   console.log("processLiveGpsMessage", coordinate);

   if (!Array.isArray(coordinate) || coordinate.length !== 2) {
      console.warn("Invalid coordinate format", coordinate);
      return;
   }

   if (!mapManager.map)
      mapManager.init();

   const targetLatLng = L.latLng(coordinate);
   const currentPoints = mapManager.routeLine.getLatLngs();

   if (currentPoints.length === 0) {
      // first point

      mapManager.routeLine.setLatLngs([targetLatLng, targetLatLng]);
      mapManager.map.setView(targetLatLng, 15, { animate: false });
      mapManager.isFirstPoint = false;
   }
   else {
      // Letzten bekannten Punkt aus der Linie holen

      const lastPoint = currentPoints[currentPoints.length - 1];

      // Abstand in Metern zwischen dem letzten Punkt und der neuen Koordinate berechnen
      const distance = lastPoint.distanceTo(targetLatLng);

      // bewegung > 10 Meter?

      console.log("distance", distance);

      if (distance > 10.0) {
         mapManager.routeLine.addLatLng(targetLatLng);

         // Ausschnitt nur bei Bewegung nachführen

         if (mapManager.isTrackingActive)
            mapManager.map.panTo(targetLatLng, { animate: false });
      }
   }
}
