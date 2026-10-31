
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

Die Kappe sendet bei einer Druckänderung sofort (einige Sekunden), jedes Paket ca. 10 mal, in Ruhe seltener.

## Adressen in homectld

Je Rad n (1-9) vier Sensoren vom Typ `TPMS`:

| Adresse | Wert |
| :--- | :--- |
| `TPMS:0x1n` | Druck [bar] |
| `TPMS:0x2n` | Temperatur [°C] |
| `TPMS:0x3n` | Batterie [%] |
| `TPMS:0x4n` | Batterie [V] |

Womo: 1 vorne links, 2 vorne rechts, 3 / 4 erste Hinterachse links / rechts, 5 / 6 zweite Hinterachse
links / rechts (passend zum HTML Widget 'Reifendruck Womo').

Veröffentlicht wird sofort bei geänderten Daten und alle `interval` Sekunden die letzten Werte.
Werte älter als `maxAge` Sekunden werden mit `"valid": false` gesendet.

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

Die Zuordnung der Kappen zu den Rädern erfolgt in homectld. Der ESP meldet sich mit seinen Parametern an,
sie stehen danach in der WEBIF Konfiguration (Kategorie 'Sensors', `TPMS:<deviceid>`):
```json
{"eloquence": 1, "interval": 60, "maxAge": 1800, "learn": true, "wheels": {"318F01": 1, "31953C": 2, "318C4B": 0}}
```

* `wheels` - ID auf der Kappe : Rad (1-9, siehe Adressen), 0 = nicht aktiv
* `learn` - jede neu empfangene Kappe wird mit 0 in `wheels` eingetragen (der ESP meldet sich neu an,
  homectld ergänzt die fehlenden Einträge in der gespeicherten Konfiguration, die eigenen Werte bleiben).
  Sind alle Kappen drin auf `false` setzen, sonst sammelt er z.B. auf dem Campingplatz die Kappen der Nachbarn.
  Zum Tausch einer Kappe kurz wieder einschalten.
* `interval` - Sekunden, die letzten Werte werden zyklisch gesendet
* `maxAge` - Sekunden, ältere Werte werden als ungültig gesendet

Also nur noch die Zahl hinter der ID ändern, speichern und homectld (oder den ESP) neu starten.

homectld muss das Topic lesen: in der Konfiguration 'Zusätzliche sensor Topics' (`mqttSensorTopics`)
`homectld2mqtt/tpms` eintragen.

## Makefile

* `make` - Kompilieren (Toolchain einmalig mit `make install-deps`)
* `make upload` - per USB flashen (`TPMS_PORT`), der erste Upload muss per USB erfolgen (Partitionstabelle `min_spiffs`)
* `make upload-ota` - per WLAN flashen (`TPMS_OTA_HOST`), der ESP meldet sich als `tpms-bridge`
* `make monitor` - serielle Konsole

Der Scan läuft auch ohne WLAN / MQTT, die Werte stehen dann auf der seriellen Konsole:
```
Rad 1 (318F01): 1.92 bar, 20 °C, 2.68 V (73 %), Alarm 0, -84 dBm
```

## MQTT

* Daten: `homectld2mqtt/tpms` - `{"type":"TPMS","address":17,"value":1.92,"kind":"value","title":"Rad 1 Druck","unit":"bar"}`
* Konfiguration: `homectld2mqtt/tpms/in` - `{"type":"TPMS","action":"init","config":{...}}`, `{"action":"requestinit"}`
* Log: `homectld2mqtt/tpms/log`

## LED (GPIO 2)

| LED | Bedeutung |
| :--- | :--- |
| Blinken 1 s | kein WLAN |
| Blinken 500 ms | WLAN, kein MQTT |
| An, kurz aus | verbunden, kurz aus bei jedem Paket einer Kappe |
