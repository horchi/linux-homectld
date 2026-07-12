
# ESP32 Alpicool BLE to MQTT Bridge

Dieses Projekt implementiert eine stabile, performante Brücke auf Basis eines ESP32 zwischen einer Alpicool/Maentum-Kühlbox (via Bluetooth Low Energy)
und dem Haussteuerungssystem `homectld` (via MQTT). Durch die Portierung auf den ESP32 gehören die bekannten Stabilitätsprobleme
des Linux-Bluetooth-Stacks (BlueZ) der Vergangenheit an.

Die Files
  - alpicool.py
  - README-python.md
  - alpicool.service
  - alpicool2mqtt

Sind mit dem neuen Ansatz obsolete, es hat funktioniert nur war die BT Verbindung mit dem Linux-Bluetooth-Stacks exterm instabil.

## Features
* **Nativer BLE-Stack:** Nutzt `NimBLE-Arduino` für ressourcenschonende und dauerhaft stabile Bluetooth-Verbindungen.
* **homectld Integration:** Überträgt Zustände als JSON-Payloads und nimmt Steuerbefehle im passenden Adress-Schema entgegen.
* **Zentrales MQTT-Logging:** Status- und Fehlermeldungen werden strukturiert per JSON an das Log-Topic gesendet.
* **Visuelles Status-Feedback:** Die Onboard-LED zeigt jederzeit den aktuellen Verbindungsstatus an.

---

## Abhängigkeiten & Vorbereitung

Das Projekt wird auf Linux-Ebene über ein automatisiertes `Makefile` mittels der `arduino-cli` verwaltet.

### 1. Systemvoraussetzungen (einmalig)
Stelle sicher, dass `arduino-cli` auf deinem System installiert ist. Falls nicht, installiere es via:
```bash
curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | BINDIR=/usr/local/bin sh
```

### 2. Automatische Einrichtung der Toolchain
Das integrierte Makefile lädt alle benötigten Cores und externen Bibliotheken (`NimBLE-Arduino`, `PubSubClient`, `ArduinoJson`) automatisch herunter. Führe dazu einfach folgenden Befehl im Projektverzeichnis aus:

```bash
make install-deps
```

---

## Konfiguration

Vor dem compilieren müssen im übergeordneten Ordner in Make.user
die Einstellungen für WLAN, MQTT Broker IP und die MAC der Kühlbox eingestellt werden.

Beispiel:
```
WIFI_SSID = foo
WIFI_PWD = foobar
MQTT_HOST = 192.168.220.10
ALPI_MAC = FC:E4:97:72:E9:83
```

---

## Makefile Bedienung

* **Code kompilieren:**
  ```bash
  make
  # oder
  make compile
  ```
  *Kompiliert den Quellcode und legt das fertige Binärfile nach erfolgreichem Build unter `../bin/alpicool_bridge.bin` ab.*

* **Firmware flashen:**
  ```bash
  make upload
  ```
  *Überträgt die Firmware über den im Makefile definierten Port (Standard: `/dev/ttyUSB0`) auf den ESP32.*

* **Build-Verzeichnis bereinigen:**
  ```bash
  make clean
  ```

---

## LED Status-Blinkcodes

Die eingebaute blaue LED des ESP32 (`GPIO 2`) signalisiert den Zustand der Brücke ohne aktiven seriellen Monitor:

| LED-Verhalten | Bedeutung |
| :--- | :--- |
| **Langsames Blinken (1s)** | Der ESP32 versucht die WLAN-Verbindung aufzubauen. |
| **Medium Blinken (500ms)** | WLAN steht erfolgreich, aber die Verbindung zum MQTT-Broker wird gesucht. |
| **Blinken im ~10-Sekunden-Takt** | Netzwerk und MQTT laufen perfekt. Die BLE-Kühlbox ist **ausgeschaltet** oder außer Reichweite. Der ESP32 blinkt einige Sekunden schnell, friert dann für 4s (Hardware-Timeout) starr ein, während er versucht die Box zu erreichen, und startet den Zyklus nach kurzem Blinken nach etwa 10 Sekunden neu. |
| **Dauerhaft AN** | Perfekter Betriebszustand. WLAN, MQTT und BLE-Kühlbox sind erfolgreich verbunden. Daten werden im 10s-Takt zyklisch übertragen. |

---

## MQTT Payload-Strukturen

### Daten & Zustände (`homectld/alpicool/state`)
Die Datenpakete der Kühlbox werden zerlegt und im `homectld`-Format publiziert:
```json
{"device":"alpicool","address":4,"type":"CURRENT_TEMP","value":5}
```
* **Adresse 0:** POWER (0 = Aus, 1 = An)
* **Adresse 1:** MODE (0 = Eco, 1 = Max)
* **Adresse 2:** BATTERY_LEVEL (0 = Low, 1 = Medium, 2 = High)
* **Adresse 3:** TARGET_TEMP (Soll-Temperatur in °C)
* **Adresse 4:** CURRENT_TEMP (Ist-Temperatur der Zone in °C)
* **Adresse 5:** VOLTAGE (Aktuelle Betriebsspannung in Volt, z.B. `12.4`)
* **Adresse 6:** CURRENT (Strom der Box sofern separater INA-226 Sensor angeschlossen)
* **Adresse 7:** COOLING (Kompressor läuft, INA Sensor meldet mehr als 500mA)

### Diagnose des ESP32 Boot Vorgangs
Fehler bei der Initialisiwerung könnne nicht über MATT geloggt werden wenn die Verbindung nicht aufgebaut werden konnte.

Diagnose über die serielle Konsole:
- Anschluss über USB an den PC
- konsole öffnen mit `while true; do picocom -b 115200 /dev/ttyACM0 && break; sleep 0.1; done`
  -> beenden mit Strg-A, Strg-X

### System-Logs (`homectld/alpicool/log`)
Sämtliche Statusmeldungen der Brücke werden über ein eigenes Topic ausgegeben:
```json
{
  "device": "alpicool",
  "type": "LOG",
  "level": 1,
  "message": "Erfolgreich mit Kühlbox via BLE verbunden.",
  "timestamp": 12845
}
```
* **Level 0:** Debug / Verbose (z.B. gesendete BLE Hex-Befehle)
* **Level 1:** Info (z.B. erfolgreiche Verbindungsaufbauten)
* **Level 2:** Warning (z.B. Verbindungsabbrüche, fehlerhaftes Inbound-JSON)
* **Level 3:** Error (z.B. BLE-Charakteristiken oder Services nicht gefunden)
