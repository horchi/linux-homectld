
function initLiveGps(coordinate) {
{
   $('#container').removeClass('hidden');

   $('#container').empty()
      .append($('<div></div>')
              .attr('id', 'map'));

   processLiveGpsMessage(coordinate);
}

const mapManager = {
    map: null,
    routeLine: null,
    isFirstPoint: true,
    isTrackingActive: true, // Steuert, ob die Karte dem Punkt folgt
    buttonControl: null,    // Referenz auf das Button-Element

    init() {
        this.map = L.map('map').setView([0, 0], 2);

        L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', {
            attribution: '© OpenStreetMap'
        }).addTo(this.map);

        this.routeLine = L.polyline([], {
            color: 'red',
            weight: 5,
            opacity: 0.8
        }).addTo(this.map);

        // --- INTERAKTIONS-ERKENNUNG ---
        // Sobald der Benutzer die Karte bewegt (Klicken + Ziehen oder Scrollen)
        this.map.on('movestart', (e) => {
            // Nur pausieren, wenn die Bewegung VOM BENUTZER kommt (nicht durch Code-Animation)
            if (e.target._mainviewchange || this.map._panAnim?.isPlaying) return;

            if (this.isTrackingActive) {
                this.isTrackingActive = false;
                this.updateButtonUI();
            }
        });

        // Custom Button zur Karte hinzufügen, um Tracking wieder einzuschalten
        this.createTrackingButton();
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

/**
 * Diese Funktion binden Sie unverändert in Ihren WebSocket-Dispatcher ein.
 * @param {Array} coordinate - [Breitengrad, Längengrad]
 */
function processLiveGpsMessage(coordinate) {
    if (!Array.isArray(coordinate) || coordinate.length !== 2) {
        console.warn("Ungültiges Koordinatenformat im Dispatcher:", coordinate);
        return;
    }

    if (!mapManager.map) {
        mapManager.init();
    }

    // Punkt wird IMMER im Hintergrund an die rote Linie angehängt
    mapManager.routeLine.addLatLng(coordinate);

    // Kamera-Nachführung
    if (mapManager.isFirstPoint) {
        mapManager.map.setView(coordinate, 15);
        mapManager.isFirstPoint = false;
    } else if (mapManager.isTrackingActive) {
        // Nur zentrieren, wenn der Benutzer die Karte nicht pausiert hat
        mapManager.map.setView(coordinate, mapManager.map.getZoom());
    }
}
