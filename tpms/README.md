
# ESP32 TPMS BLE to MQTT Bridge

Reifendruck der Ventilkappen (BLE, App LYTPMS / ITPMS, z.B. WODHMIEY) per ESP32 an `homectld` (MQTT).
Der ESP32 hört die Kappen nur passiv ab (keine Verbindung, kein Pairing), damit entfallen die
Empfangs- und Stabilitätsprobleme des Linux-Bluetooth-Stacks (BlueZ).

## Sensor-Format

Herstellerdaten mit Company-ID 172 (0x00AC), 15 Byte, legacy ADV_NONCONN_IND:

| Byte | Bedeutung |
| :--- | :--- |
| 0 | Batterie V = b0 * 0.01 + 1.22 |
| 1, 6 | Druck bar = (b1 + 256 * b6) * 0.03144 |
| 2 | Temperatur °C = b2 - 55 |
| 3 | Alarm |
| 4-5 | Batterie in mV |
| 9-14 | MAC (umgedreht) |

Die BLE-Adresse ist die **ID auf der Kappe** + `11:11:11` (Kappe `318F01` -> `31:8F:01:11:11:11`).
Die Batterie in % wird wie in der App geschätzt: % = (V - 1.95) * 100.

Sendeverhalten (gemessen 02.11.2026, Kappe 318C4B):

* in Ruhe alle ~201 s (3:20 min) eine kurze Aussendung
* bei einer Druckänderung sofort (einige Sekunden), danach ca. 1 min lang alle ~9 s (Alarm 1 / 2),
  dann wieder alle ~201 s
* pro Aussendung empfängt der ESP 1-3 Pakete (Scan-Fenster 50 %), fehlt eine Aussendung kommen die
  nächsten Daten erst ~3:20 min später

## Adressen in homectld

Je Kappe zwei Sensoren vom Typ `TPMS`, die Adresse ist die **ID auf der Kappe** und eine Ziffer:

| Adresse | Wert |
| :--- | :--- |
| `TPMS:0x<ID>1` | Druck [bar], mit der Batterie [%] (Spalte 'Batterie' im Sensor Setup, `{{p.vl.battery}}` im HTML Widget) |
| `TPMS:0x<ID>2` | Temperatur [°C] |

z.B. Kappe `318C4B`: `TPMS:0x318C4B1` Druck, `TPMS:0x318C4B2` Temperatur.

Die Sensoren werden von homectld inaktiv angelegt, im Sensor Setup aktivieren und benennen (z.B. 'Reifen VL').
Die Zuordnung zu den Rädern erfolgt im HTML Widget 'Reifendruck Womo' (Parameter `vl`, `vl_t`, `vr`, ... `h2r_t`);
wird ein Rad umgesetzt bleibt der Verlauf (Chart) bei seiner Kappe / seinem Reifen.

Veröffentlicht wird sofort bei geänderten Daten, kommt dasselbe Paket wieder wird es mit dem nächsten `interval`
nachgereicht, ohne Paket wird nichts gesendet - die letzte Aktualisierung in homectld ist also der letzte Empfang.
Jede Nachricht trägt `"maxage"`: kommt so lange nichts (Kappe stumm oder der ESP ausgefallen) setzt homectld die
Sensoren auf ungültig. Nach einem Start von homectld (requestinit) bzw. einer neuen MQTT Verbindung sendet der ESP
die letzten Werte aller Kappen.

Die Nachricht des Drucks enthält die Details der Kappe (Info Dialog, Reiter 'JSON'), z.B. um eine gelernte
fremde Kappe zu erkennen: `voltage`, `alarm`, `rssi`, `format`, `macmatch` (die BLE Adresse steht in den Daten,
typisch für diese Kappen) und `raw` (die Herstellerdaten). Gelernt wird jedes Gerät mit Company-ID 172 (das einzige
Format das der ESP kennt), das Lernen steht mit diesen Angaben auch im Log.

## Konfiguration

In `Make.user` (übergeordneter Ordner):
```
WIFI_SSID = foo
WIFI_PWD = foobar
MQTT_HOST = 192.168.220.10
TPMS_PORT = /dev/ttyACM0
TPMS_OTA_HOST = 192.168.220.35
TPMS_OTA_PWD =
```

Ein `$` im WLAN Passwort ist erlaubt, die Werte gehen unverändert in die `config.h`.

Der ESP meldet sich mit seinen Parametern an, sie stehen danach in der WEBIF Konfiguration
(Kategorie 'Sensors', `TPMS:<deviceid>`), eine Änderung wird beim Speichern sofort an den ESP gesendet:
```json
{"eloquence": 1, "interval": 60, "maxAge": 1800, "learn": true, "wheels": ["318F01", "31953C", "318C4B"]}
```

* `wheels` - die IDs der Kappen (wie aufgedruckt), nur diese werden gesendet
* `learn` - jede neu empfangene Kappe wird in `wheels` eingetragen (der ESP meldet sich neu an, homectld ergänzt
  die fehlenden Einträge in der gespeicherten Konfiguration). Sind alle Kappen drin auf `false` setzen, sonst sammelt
  er z.B. auf dem Campingplatz die Kappen der Nachbarn. Zum Tausch einer Kappe kurz wieder einschalten.
  Eine versehentlich gelernte Kappe aus `wheels` löschen (bei `learn` aus, sonst kommt sie wieder).
* `eloquence` - Log auf `homectld2mqtt/tpms/log` (Bits): 1 Info (Init, Lernen), 2 Detail (geänderte Daten
  einer Kappe), 4 Debug (jedes empfangene Paket einer Kappe, auch unverändert), z.B. 7 für alles.
  Die serielle Konsole zeigt immer alles.
* `interval` - Sekunden, die letzten Werte werden zyklisch gesendet
* `maxAge` - Sekunden ohne Daten, danach setzt homectld die Sensoren auf ungültig. Größer als das Intervall
  der Kappen in Ruhe (~201 s) wählen, ab ~420 s übersteht es eine verpasste Aussendung (z.B. 600 - 900 s)

homectld muss das Topic lesen: in der Konfiguration 'Zusätzliche sensor Topics' (`mqttSensorTopics`)
`homectld2mqtt/tpms` eintragen.

## Makefile

* `make` - Kompilieren (Toolchain einmalig mit `make install-deps`)
* `make upload` - per USB flashen (`TPMS_PORT`), der erste Upload muss per USB erfolgen (Partitionstabelle `min_spiffs`)
* `make upload-ota` - per WLAN flashen (`TPMS_OTA_HOST`), der ESP meldet sich als `tpms-bridge`
* `make monitor` - serielle Konsole

Der Scan läuft auch ohne WLAN / MQTT, die Werte stehen dann auf der seriellen Konsole:
```
Kappe 318F01: 1.92 bar, 20 °C, 2.68 V (73 %), Alarm 0, -84 dBm
```

## MQTT

* Daten: `homectld2mqtt/tpms` - `{"type":"TPMS","address":51954865,"value":1.92,"kind":"value","title":"Reifen 318C4B Druck","unit":"bar","battery":73}`
* Konfiguration: `homectld2mqtt/tpms/in` - `{"type":"TPMS","action":"init","config":{...}}`, `{"action":"requestinit"}`
* Log: `homectld2mqtt/tpms/log`

## LED (GPIO 2)

| LED | Bedeutung |
| :--- | :--- |
| Blinken 1 s | kein WLAN |
| Blinken 500 ms | WLAN, kein MQTT |
| An, kurz aus | verbunden, kurz aus bei jedem Paket einer Kappe |
