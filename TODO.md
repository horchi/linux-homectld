
# Important

- recover Output states also for MCPO !
  -> move ioStates from config table in a new separate table with type, address as KEY to
     support all kind of outputs

# General

- add write of victron registers to WEBIF

## HTML widgets - stage 2: JS snippets

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

# Home (from the former NR-Plan.md)

## Shading (shade.c)

- Lux sensor (Aqara Light Sensor T1, GZCGQ11LM, via deCONZ) still missing, waiting for the shop
  (fallback: DIY ESP32-C6 Zigbee sensor); then pair it, set up the shading of Blumenfenster (HMB:0x02)
  and Küche (HMB:0x04) - windows face 224°, start with azimuth ~180-290° - and calibrate the thresholds;
  activate only with the sensor (without it the brightness is not checked)

## HomeMatic (hmmqtt)

- pairing / removing is implemented but not yet tested with a real device -> test with the next new actor

## Light rules / time control

- off timer: option to switch off also a light switched on before (today only what the rule switched on)
- off timer: keep running timers over a restart (store expiry and 'switched on by the rule', run expired
  ones right after the start)
- blinds: catch up a limit of the time range missed during a restart
- time zone: on travel use the time zone of the GPS position (today the one of the system)

## Lua rules for special cases (optional, not needed for the Node-RED replacement)

- [ ] Regel-Skripte (eigene Tabelle oder Dateien), Editor im WEBIF (CodeMirror ist vorhanden)
- [ ] `on(type, addr, fn)`: Auslöser (nutzt die Ereignisse aus Phase 3)
- [ ] `at("sunset-30", fn)` / `at("06:30", fn)`: Zeit-Ereignisse
- [ ] Sonnenzeiten: `sun.rise`, `sun.set`, `sun.dawn`, `sun.dusk`, `isNight([offStart, offEnd])`
- [ ] `set(type, addr, {state=, bri=, transition=, level=})`: Aktoren schalten (DZL, DO, GPIO, HMB, ...)
- [ ] `after(name, sec, fn)`: nachtriggerbarer Timer, `cancel(name)`
- [ ] Logging / Fehleranzeige pro Regel

# Motorhome tire pressure (TPMS) - test in spring 2027

- motorhome: 3 axles, 8.5 m, 6 wheels, target pressure up to 4.5 bar
- ordered 30.10.2026: Amazon B0GWH88YSV (WODHMIEY, BLE valve caps, app LYTPMS / ITPMS (K), battery replaceable),
  6 sensors needed; fallback with the same app: B0DPMT9VKS (0-6.8 bar, CR1632, sets of 2/3/4)
- format = type D of github.com/bkbilly/tpms_ble (decoded, tested with a real capture), passive, no connection:
  manufacturer data company id 172 (0x00AC); byte 0 battery (V = b0 * 0.01 + 1.22), byte 1 pressure
  (bar = (b1 + 256 * b6) * 0.03144), byte 2 temperature (°C = b2 - 55), byte 3 alarm; 15 byte packets carry the
  MAC in byte 9-14 (legacy variant: bar = (b7 - 2) / 103)
- test 31.10.2026 (arrived, bike tire): BLE confirmed - legacy ADV_NONCONN_IND on LE 1M, flags 06, 16-bit UUID
  0xfbb0, manufacturer data company 172 with 15 bytes, e.g. 99 13 4a 02 0a be 00 ea 28 11 11 11 01 8f 31;
  the advertising address is the ID printed on the cap + 11:11:11 (cap '318F01' -> 31:8F:01:11:11:11, bytes
  9-14 hold it reversed) -> the wheel mapping can be done by the printed ID; bytes 4-5 = battery in mV (0x0abe =
  2750, matches b0 * 0.01 + 1.22); temperature (b2 - 55) and pressure 0 match the app (LYTPMS)
- pressure scale confirmed: b1 * 0.03144 bar (packet 92 3d 4b 00 0a 9b 00 cb ... of cap 31953C = 1.92 bar / 20 °C,
  app 1.9 bar / 20 °C; 0.5 psi per step would give 2.1 bar)
- battery % of the app: 2.77 V -> 82 %, 2.72 V -> 75 %, 2.68 V -> 75 % (app seems to smooth); % = (V - 1.95) * 100
  hits it within 2-3 % -> publish the voltage, % only as rough estimate
- receiver for tests at home: ESP32 mini board (diymore, CH9102F, 1a86:55d4, /dev/ttyACM1 on gate) with a NimBLE
  passive scan sketch printing the company 172 adverts on the serial port; it gets the caps at -84 dBm
- the sensor sends at once on a pressure change (app updated 5-7 s after unscrewing / screwing on), rarely at rest
- the BT stick of gate (33fa:0001 USB2.0-BT, Barrot, 04:7F:0E:62:ED:11 - probably the old Mopeka stick, replaced
  for weak reception) is useless for it: RSSI of all devices -96 dBm, only 4 packets
  of the sensor in 5 min, none at all in 3 later captures (also LE only, stick right at the sensor) while the
  iPhone got every update
- software: tpms.py like mopeka/mopeka.py (bleak, MQTT TPMS:0xNN - pressure / temperature / battery per wheel),
  service + switch in Make.user like MOPEKA
- reception in the motorhome: Odroid (BlueZ / bleak), if too weak an additional BT USB stick in the double floor
  above the rear axle via (active) USB extension; fallback ESP32 in the double floor powered by D+ / ignition
  (WiFi / MQTT); problem: aluminium walls, steel rims; spring test: reception of the third axle
- no 433 MHz SDR (gets very hot); lesson from the returned SYNJAUPO B0GYMYGJBC: solar display sets are 433 MHz
  (OOK, sync 9653, XOR scrambled), Bluetooth must be stated in the listing itself
