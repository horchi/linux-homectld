
# General

- move the sensor default settings (defaultWidgetProperties in daemon.c: widget type, scale, peak per type / unit,
  the colors in widgetDefaults2Json) out of the code into a JSON config on disk
  (e.g. /etc/homectld/widget-defaults.json + a user file) or a table - to be decided
- Anzeige der Widget defaults im Sensor Setup als Tab im Info Dialog
- add write of victron registers to WEBIF
- samsung TV einbinden
- Denox (x3000) einbinden

## HTML widgets - JS snippets

- new kind 'js' in the table 'htmlwidgets' (no schema change): CODE is an ES module, 'export default class
  extends HTMLElement', loaded by import() of a Blob URL, registered as 'hc-snippet-<id>-<version>' (a tag
  can't be defined twice, a new version after each store)
- the WEBIF sets element.sensors = { <key>: sensor } (only the used ones) on each update and element.params,
  element.api = { toggle(key), set(key, value), chart(key), title(key) }, later chart data (history)
- CSS of the snippet nested into '.htmlw-<id>' as for 'html'; editor with a JS mode (CodeMirror simple mode)
- errors of import / connectedCallback / updates caught, shown in the widget instead of its content
- imports of libraries only by absolute URL (/lib/... of htdocs, CDN not for offline kiosks), relative
  imports don't work from a Blob URL
- storing stays admin only, the code runs with the rights of each viewer (his websocket)
- use cases: sliders (dimming, blind position), animated bars / energy flow (Growatt), puffer graphic (P4)
  with color gradient, sparkline of the last 24 h, loops over a list of parameters

## HTML widgets - optional

- option 'isoliert' per snippet: render it in a Shadow DOM (isolation in both directions, the theme variables
  still inherit, the MDI CSS has to be loaded into the shadow root) - only if WEBIF styles really disturb

## Shading (shade.c)

- Lux sensor (Aqara Light Sensor T1, GZCGQ11LM, via deCONZ) still missing, waiting for the shop
  (fallback: DIY ESP32-C6 Zigbee sensor); then pair it, set up the shading of Blumenfenster (HMB:0x02)
  and Küche (HMB:0x04) - windows face 224°, start with azimuth ~180-290° - and calibrate the thresholds;
  activate only with the sensor (without it the brightness is not checked)

## Zigbee (zigbee.c, Setup -> Zigbee)

- pairing, renaming and removing tested 07.11.; 'API-Key holen' / 'Neu registrieren' not yet tested

## HomeMatic (hmmqtt)

- pairing / removing is implemented but not yet tested with a real device -> test with the next new actor

## Light rules / time control
- time zone: on travel use the time zone of the GPS position (today the one of the system)
- actor follows actor: e.g. DZL:0x3d (Steckdose Terrasse, Lichterkette) off when HMB:0x01 (Rollladen Terrasse)
  closes - also when closed by hand / earlier; today solved by the same time (22:30) in both time controls
  (09.11.); trigger: state change of an actor (light on/off, blind closed/open/position), action as the rules
  (an / aus / umschalten / folgen), targets lights, groups, outputs, blinds
- rules of switches / motion sensors: optional date range ('Gültig' DD.MM from - to, as the ranges of the time
  control already have - rangeDateValid()), e.g. a rule only at Christmas

## Lua rules for special cases (optional, not needed for the Node-RED replacement)

- [ ] Regel-Skripte (eigene Tabelle oder Dateien), Editor im WEBIF (CodeMirror ist vorhanden)
- [ ] `on(type, addr, fn)`: Auslöser (nutzt die Ereignisse aus Phase 3)
- [ ] `at("sunset-30", fn)` / `at("06:30", fn)`: Zeit-Ereignisse
- [ ] Sonnenzeiten: `sun.rise`, `sun.set`, `sun.dawn`, `sun.dusk`, `isNight([offStart, offEnd])`
- [ ] `set(type, addr, {state=, bri=, transition=, level=})`: Aktoren schalten (DZL, DO, GPIO, HMB, ...)
- [ ] `after(name, sec, fn)`: nachtriggerbarer Timer, `cancel(name)`
- [ ] Logging / Fehleranzeige pro Regel

# Motorhome tire pressure (TPMS) - test in March 2027

- open: the 6th cap ID; mount the ESP32 in the double floor above the rear axle, powered by D+ / ignition;
  spring test: reception of all axles (aluminium walls, steel rims), WLAN of the motorhome
- spring in the motorhome: Make.user there (WLAN, MQTT_HOST, TPMS_PORT / TPMS_OTA_HOST), build and flash the ESP32
  (make upload); homectld config 'Zusätzliche sensor Topics' + homectld2mqtt/tpms; 'learn' on until all 6 caps are
  in 'wheels', then off; activate / name the sensors; export the HTML widget 'Reifendruck Womo' at gate, import it
  there and set the IDs per wheel (vl, vl_t, ... h2r_t)
- wenn ESP nicht erreichbar "Parken" anzeigen
