
# Important

- recover Output states also for MCPO !
  -> move ioStates from config table in a new separate table with type, address as KEY to
     support all kind of outputs

# General

- add write of victron registers to WEBIF

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
- test on arrival (within the return period): sensor on a valve, LYTPMS on the iPhone, capture on gate (sensor
  right at the BT stick, it receives weakly, all at -93..-96 dBm) and compare the values
- software: tpms.py like mopeka/mopeka.py (bleak, MQTT TPMS:0xNN - pressure / temperature / battery per wheel),
  service + switch in Make.user like MOPEKA
- reception in the motorhome: Odroid (BlueZ / bleak), if too weak an additional BT USB stick in the double floor
  above the rear axle via (active) USB extension; fallback ESP32 in the double floor powered by D+ / ignition
  (WiFi / MQTT); problem: aluminium walls, steel rims; spring test: reception of the third axle
- no 433 MHz SDR (gets very hot); lesson from the returned SYNJAUPO B0GYMYGJBC: solar display sets are 433 MHz
  (OOK, sync 9653, XOR scrambled), Bluetooth must be stated in the listing itself
