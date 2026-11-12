# Plan: Fröling-Heizung (S 3200) an homectld anbinden - `p4mqttd`

Stand: 2026-11-11 - **abgeschlossen**, siehe den Abschnitt "Abschluss" am Ende.

> **Lesehinweis:** Die Abschnitte 1 bis 6 sind der **ursprüngliche Entwurf** (vor der Umsetzung). Was davon anders
> gekommen ist, steht in den Abschnitten 7a bis 7k (Stand je Phase mit den Abweichungen) und im Abschnitt "Abschluss".
> Maßgeblich für das, was läuft, sind diese Stand-Abschnitte, `p4mqttd/README.md` und `doc/p4.md`.

## 1. Ziel und Rahmen

Aus `linux-p4d` wird ein schlanker Kommunikations-Daemon **`p4mqttd`** herausgelöst. Er hat **kein WEBIF,
keine Datenbank, keinen Webserver, keine Charts, Alerts, Mail oder Wetter**. Er spricht nur seriell mit der
S 3200 und per MQTT mit dem `homectld`. Alles andere (Anzeige, Historie, Charts, Alerts, Mail, Rechte, Dashboard)
übernimmt der bestehende `homectld`.

Vorgaben:

- `~/source/linux-p4d/` wird **nicht verändert**. Es bleibt das eigenständige p4d. Code aus p4d wird einmalig in den
  homectld-Baum **kopiert** und dort weiterentwickelt. Das ist derselbe Weg wie bei `lib/`, die in beiden Projekten
  schon als Fork vorliegt.
- Das Protokoll ist das übliche JSON-über-MQTT von homectld, also Sensordaten mit `type`/`address`/`title`/`value`
  und `action: "init"`. Für Service-Menü, Fehler, Parameter und Zeitprogramme kommt eine Erweiterung dazu (Abschnitt 4).
- Name: `p4mqttd` (mit "d" für Daemon, bewusst abweichend vom Schema `w1mqtt`, `votromqtt`, `victronmqtt`, `i2cmqtt`). Basis-Topic bleibt `p4`.

### Nicht-Ziele

- Keine Änderung an `linux-p4d`.
- Kein Schreiben von Digital-/Analogausgängen der Heizung (p4d liest sie nur).
- Kein Ersatz des Bedienfelds der Heizung.

## 2. Was p4d heute macht und wohin es wandert

| Funktion in p4d | Quelle | Neu |
|---|---|---|
| Serielles Protokoll S 3200 (Request/Response, Checksum, Escape, Retry) | `p4io.c/.h`, `service.h`, `lib/serial.*` | **p4mqttd/** (Kopie im Unterordner) |
| Zyklisches Lesen der Werte (VA), DI, DO, AO | `P4d::updateSensors` | **p4mqttd** liest, publiziert als Sensoren |
| Status und Betriebsart der Heizung | `P4d::updateState` (`getStatus`) | **p4mqttd** liest, publiziert; Auswertung in homectld |
| Sensor-Definitionen (ValueSpecs) | `initValueFacts` | **p4mqttd** liest sie, publiziert sie als Sensoren (erscheinen in homectld inaktiv) |
| Menü-Struktur und Parameter (Service-Menü) | `initMenu`, `updateParameter`, `performMenu*`, `parGet/parSet` | Struktur und Lesen/Schreiben in **p4mqttd**, Speicherung und Anzeige in **homectld** |
| Zeitprogramme | `updateTimeRangeData`, `performTimeParEdit/Store` | wie Menü |
| Fehlerliste | `updateErrors` (Tabelle `errors`) | Lesen in **p4mqttd**, Historie in **homectld** |
| Zeit-Sync der Heizung | `scheduleTimeSyncIn`, `syncTime` | bleibt in **p4mqttd** (nah an der Seriellen), Konfiguration kommt von homectld |
| Statusdauer (SD), Stoker-Stunden, Pellet-Verbrauch | SQL auf `samples`, Tabelle `pellets` | **homectld** (rechnet auf seinen Samples) |
| Status-Mail, Fehler-Mail | `sendStateMail`, `sendErrorMail` | **homectld** (vorhandene Mail-Infrastruktur) |
| WEBIF, Dashboard, Charts, Alerts, User, Wetter, LMC, deCONZ, HA/Node-RED-Publish | `daemon.c`, `htdocs/*` | entfällt, das hat homectld schon |
| MQTT-Request `s3200/request` (`parget`/`parset`) | `P4d::dispatchOther` | wird Teil des neuen Protokolls (Abschnitt 4) |

Schon in homectld vorhanden (aus der gemeinsamen Herkunft):

- Sensor-Kategorie `^P4.*` ("P4 Daemon", Icon `mdi-fire`) in `daemon.c`.
- Gültigkeits-Timeout 10 min für Typen `P4*` in `wsactions.c`.
- Kommentar-Beispiel `{"type":"P4VA", "address":1, "unit":"°C", "title":"Abgas"}`.
- Rudimente `s3200State` in `htdocs/main.js` (Event `s3200-state`).
- `shade.c` kennt `P4VA:0x04` als Außentemperatur.

Damit ist der Typ-Präfix **`P4`** gesetzt. Neue Typen: `P4VA` (Messwerte), `P4DI`, `P4DO`, `P4AO`, `P4UD` (Status, Betriebsart, Uhrzeit), `P4PA` (Kessel-Solltemperatur und weitere Parameter-Sensoren).

## 3. Architektur

```
 S 3200 --RS232/FTDI--  p4mqttd  --MQTT--  mosquitto  --MQTT--  homectld --WS--> WEBIF
                        (kein DB,                              (DB, Rechte, Charts,
                         kein WEB)                              Alerts, Mail, Menü-UI)
```

### 3.1 p4mqttd (neu, im homectld-Baum)

- **Alles liegt in einem eigenen Unterordner `p4mqttd/`** im homectld-Baum, wie bei `thetford/`, `tpms/` oder `alpicool/`:
  ```
  p4mqttd/
    Makefile          eigenes Makefile (baut gegen ../lib), vom Haupt-Makefile per `$(MAKE) -C p4mqttd` aufgerufen
    README.md         Beschreibung, Konfiguration, Disclaimer (aus dem README von p4d übernommen und angepasst)
    p4mqttd.c         main, MQTT, Scheduler, Kommando-Queue
    p4io.c, p4io.h    kopiert aus p4d (S 3200 Protokoll)
    p4cmd.c           kopiert aus p4d, Kommandozeilen-Tool `p4` (siehe 3.4)
    service.c, service.h  kopiert aus p4d (Datenstrukturen und Texte des Protokolls), Namen bleiben, siehe unten
    p4mqttd.service   systemd-Unit
    p4mqttd.default   Vorlage für /etc/default/p4mqttd
    scripts/          mosquitto_pub-Testskripte, p4d-migrate.sh
  ```
  `p4io.*` und `service.h` bleiben im Unterordner **unter ihrem Originalnamen**. Das erleichtert den Vergleich mit p4d, wenn dort
  später noch Protokoll-Fixes passieren. Die Kollision mit dem `service.h` im Wurzelverzeichnis von homectld gibt es nicht, weil
  `#include "service.h"` zuerst im Verzeichnis der einbindenden Datei sucht. Weil `p4io.h` `lib/serial.h` per `#include "lib/serial.h"`
  einbindet, bekommt das Makefile `-I..`. Das ändert an der Suchreihenfolge nichts. Sollte es in der Praxis trotzdem Ärger geben,
  wird die Datei nach `p4service.h` umbenannt.
- Abhängigkeiten nur aus `lib/`: `common`, `thread`, `serial`, `mqtt`/`mqtt_c`/`mqtt_pal`, `json`. Kein `db`, kein `libwebsockets`, kein MariaDB.
- Build: **`p4mqttd/` ist eigenständig baubar.** Auf einem Raspberry Pi wird das ganze Repository geklont, gebaut wird aber nur der Unterordner (`cd p4mqttd; make; make install`). Das `Makefile` dort bindet **weder `../Make.config` noch das Haupt-Makefile** ein und ruft es auch nicht auf. Die Dateien aus `../lib/` werden direkt verwendet (sie liegen im Git vollständig vor) und nach `p4mqttd/obj/` kompiliert, nichts von homectld und kein anderes Binary wird mitgebaut. Benötigt werden nur `build-essential pkg-config libssl-dev libjansson-dev zlib1g-dev`, also keine Datenbank, kein libwebsockets, kein Lua. Das Haupt-Makefile von homectld ruft bei `P4 = 1` lediglich `$(MAKE) -C p4mqttd` auf (`all`, `install`, `clean`, `SERVICES`). Die Unit liegt in `p4mqttd/p4mqttd.service`, nicht in `contrib/`.
  Wie bei `WOMO` ein Schalter in `Make.user` / `Make.user.template` (z. B. `P4 = 1`), damit Anlagen ohne Heizung nichts davon installieren.
- Aufbau intern:
  - **Ein Bus-Thread** hält die serielle Schnittstelle exklusiv. Eine **Prioritäts-Queue** nimmt die Aufträge entgegen: erst Benutzer-Kommandos (parset, menu), dann Status, dann zyklisches Polling.
  - **MQTT-Thread** (Reader und Writer wie in `votro.c`), Last-Will auf `p4/status`.
  - Der SysV-Semaphor `0x3da00001` von p4d (`Sem`, liegt schon in `lib/common.h` von homectld) bleibt **bindend** erhalten. Er ist die gemeinsame Sperre für p4mqttd, das Tool `p4` und den alten p4d (siehe 3.3).
- Konfiguration:
  - nur die **technischen** Werte in `/etc/default/p4mqttd`: `TTY`, `MQTT_URL`, `MQTT_USER`/`PASS`, `READONLY`, Log-Level.
  - **fachliche** Werte (Intervalle, aktive Sensoren, Zeit-Sync) kommen aus der homectld-Konfiguration per retained `p4/config` (Abschnitt 4.5). So gibt es genau eine Stelle zum Einstellen.

### 3.2 homectld (Erweiterungen)

- Neues Modul `p4service.c` im Wurzelverzeichnis (es ist Teil des homectld-Binaries, nicht des Unterordners) analog zu `hmservice.c` (HomeMatic): abonniert `p4/#` (nur wenn aktiviert), setzt Menü, Fehler, Parameter und Zeitprogramme um und hält die RPC-Zuordnung (`rid`).
- Neue Tabellen in `etc/configs/database.dat` (Hinweis aus der Memory: Infos in die Description-Spalte, nicht als `//`-Kommentar): `p4menu`, `p4errors`, optional `p4timeranges`, später `p4pellets`.
- Neue WS-Events in `wsactions.c`, jeweils mit Rechte-Prüfung: `evP4Menu`, `evP4ParEdit`, `evP4ParStore`, `evP4TimeParEdit`, `evP4TimeParStore`, `evP4Errors`, `evP4Command`.
- WEBIF: alle Dateien, die **nur für p4 gebraucht werden**, liegen in **`htdocs/p4/`** (übersichtlicher, klar zuzuordnen):
  ```
  htdocs/p4/
    page.js      der Rahmen der Seite mit dem Sub-Menü (Tabs), Registrierung der Tabs
    menu.js      Tab 'Service-Menü' (Port von p4d menu.js, 212 Zeilen)
    errors.js    Tab 'Fehler' (Port von errors.js, 60 Zeilen, Phase 5)
    pellets.js   Tab 'Pellets' (Port von pellets.js, Phase 6)
    p4.css       Styles nur für die S3200-Seite (soweit aus p4d nötig)
    img/         ggf. nur hier benötigte Bilder/Icons
  ```
  - Eingebunden wird in `index.html.in` mit `src="p4/menu.js"` usw. `genindex.sh` hängt die Content-Hash-Versionierung auch an Pfade mit Unterordner an (die Regex lässt `/` zu, wie bei `lib/map/leaflet.css`).
  - Die Hooks, die schon bestehende Dateien betreffen (Seitenumschaltung und Event-Dispatch in `main.js`, Menüeintrag, Konfig-Schalter `p4Enabled`/`p4PageTitle`), bleiben in `main.js`, damit keine zweite Verwaltung der Seitenumschaltung entsteht. Dort steht nur das Minimum, die Logik liegt in `p4/`.
  - Was homectld schon hat (Dialoge, Tabellen, Listen, `jquery`, Icons), wird wiederverwendet, nicht aus p4d kopiert. Von p4d kommt nur, was es in homectld nicht gibt.
  - `htdocs/Makefile`: `make install` und `gzip` arbeiten schon rekursiv (`cp -rL`, `find`). Nur `make check` (JS-Syntaxprüfung) schleift über `*.js` und wird um `p4/*.js` erweitert.
- **Eine Seite im Hauptmenü** (nicht in den Setup-Seiten), die Service-Menü und Fehlerliste enthält (Reiter oder zwei Bereiche):
  - Der Name ist Konfiguration: Config-Item `p4PageTitle`, Default **"S3200"**. Er kann z. B. in "Heizung" oder "Fröling" geändert werden.
  - Die Seite (und der Menüeintrag) erscheint **nur, wenn der p4-Dienst in der Konfiguration aktiviert ist** (`p4Enabled`). Die Information geht in der
    Konfig-Nachricht ans WEBIF (wie `homeMaticBridge` für die HomeMatic-Seite), das WEBIF blendet den Eintrag ein oder aus.
  - Auch der Server prüft: ohne `p4Enabled` werden die `evP4*`-Events abgelehnt.
  - Pellets (Phase 6) bekommen einen weiteren Reiter auf derselben Seite.
- Konfiguration: neue Gruppe "Heizung" in `config.c` (Aktivierung, Topic-Basis, Intervalle, Zeit-Sync, Mail-Optionen).

### 3.3 Parallelbetrieb von altem p4d und p4mqttd (Übergang)

**Ja, das geht**, weil p4d die serielle Schnittstelle nur über den SysV-Semaphor sperrt und nicht exklusiv öffnet. Gelesen habe ich:

- Der Semaphor hat den Key `0x3da00001`. `Sem::p()` wartet auf Wert 0 und zählt dann hoch (mit `SEM_UNDO`, die Sperre fällt also auch bei einem Absturz weg).
- `Serial::open` öffnet das Gerät ohne `flock`/`TIOCEXCL`. Mehrere Prozesse können das Gerät gleichzeitig offen haben. Das Wiederherstellen der alten Terminal-Einstellungen beim `close()` ist auskommentiert, ein `close()` des einen stört den anderen also nicht.
- p4d sperrt jeden Zyklus: `updateSensors` und `updateErrors` halten die Sperre über die ganze Schleife, `updateState`, `parGet/parSet` und die Menü-Zugriffe je Aufruf. `p4cmd` sperrt für die Dauer des Aufrufs, öffnet das Gerät selbst und leert vorher den Eingangspuffer. Das ist dasselbe Muster, das p4mqttd übernimmt.

Damit der Parallelbetrieb sicher läuft, muss p4mqttd Folgendes einhalten:

1. **Gleicher Key, gleiche Semantik**: `Sem sem(0x3da00001)`, `p()` vor jedem Request-/Response-Zyklus (nicht nur "irgendwann"), `v()` danach. Auch alle Schreib- und Menü-Zugriffe laufen unter der Sperre.
2. **Sperre kurz halten**: je Transaktion sperren und wieder freigeben, nicht über die ganze Polling-Schleife (p4d hält sie bei `updateSensors` lange, p4mqttd soll andere nicht aushungern). Zwischen zwei Transaktionen kurz Platz lassen, weil die Warteschlange der SysV-Semaphoren keine Fairness garantiert.
3. **Eingangspuffer unter der Sperre leeren**, bevor ein Request gesendet wird (wie `p4cmd`: `serial.look` bis leer). Ein verspätetes Byte des anderen Prozesses darf nicht als eigene Antwort gelesen werden. Beide Prozesse teilen sich einen Eingangspuffer des TTY.
4. **Terminal-Einstellungen unter der Sperre prüfen** und bei Abweichung neu setzen (`tcgetattr`/`tcsetattr`), falls ein Fremdprozess sie ändert.
5. **Gleicher Host und gleicher IPC-Namespace.** SysV-Semaphoren gelten nur innerhalb eines Hosts und eines IPC-Namespace. Läuft p4d oder p4mqttd in einem Container oder mit `PrivateIPC=yes` in der systemd-Unit, greift die Sperre nicht. Die Unit `p4mqttd.service` setzt kein `PrivateIPC`. Das alte `daemon.service` setzt es ebenfalls nicht.

Betriebsmodus für die Parallelphase (Option `PARALLEL=1` in `/etc/default/p4mqttd`):

- **Nur Lesen**: `READONLY` wird erzwungen, p4mqttd schreibt keine Parameter und Zeitprogramme. Geschrieben wird nur über den alten p4d.
- **Kein Zeit-Sync** durch p4mqttd (macht p4d, sonst korrigieren beide).
- **Eigener Takt**: p4mqttd pollt nicht öfter als p4d (Intervall mindestens gleich), damit sich die Buslast nicht verdoppelt. Der Bus ist langsam (57600 Baud), ein Sensor-Durchlauf dauert Sekunden.
- **Keine doppelten Mails**: Status- und Fehler-Mails von homectld (`p4Mail` in der Konfiguration) bleiben aus, solange p4d sie noch verschickt.
- **Keine Topic-Kollisionen**: p4d nutzt `p4d2mqtt/…`, p4mqttd nutzt `p4/…`. Die Datenbanken sind getrennt (p4d: Datenbank `p4`, homectld: eigene). Der parallele Betrieb schreibt also nichts doppelt in dieselbe Tabelle.
- Der Parallelbetrieb ist dafür gedacht, die Werte von p4mqttd/homectld **gegen den alten p4d zu vergleichen** (Werte, Menü, Fehlerliste, Antwortzeiten), bevor umgestellt wird. Der Dauerbetrieb zu zweit ist nicht das Ziel.

Zur Umstellung: `PARALLEL` aus, `READONLY` nach Bedarf, Mail-Option und Zeit-Sync in homectld aktivieren, p4d stoppen und deaktivieren (`systemctl disable --now p4d`).

### 3.4 Kommandozeilen-Tool `p4` (wird übernommen)

- `p4cmd.c` kommt in `p4mqttd/` und baut wie bisher das Binary **`p4`**, installiert nach `$(BINDEST)`. Es braucht dieselben Quellen wie p4mqttd (`p4io.c`, `lib/serial.c`, `lib/common.c`), die `service.c` von p4d (in p4d gehört `service.o` zu `CMDOBJS`, sie liefert u. a. die Status- und Fehlertexte) liegt ohnehin im Ordner.
- Befehle und Optionen bleiben gleich (`state`, `tsync`, `errors`, `values`, `menu`, `getv`, `getp`, `setp`, `times`, `getdo`, `getao`, `user`, `list`, Optionen `-a -v -o -l -d`), damit Skripte und Gewohnheiten weiter funktionieren.
- Es spricht weiter **direkt seriell** und nutzt den Semaphor. Es läuft deshalb auch dann, wenn p4mqttd und homectld nicht laufen (Diagnose, Fehlersuche, Serien-Mitschnitt für den Simulator in Phase 0). Neben einem laufenden p4mqttd (oder altem p4d) funktioniert es wegen der gemeinsamen Sperre ohne den Daemon zu stoppen.
- `setp` und `tsync` sind eine bewusste Handlung des Benutzers und werden vom Schalter `READONLY` des Daemons nicht geblockt (das Tool ist ein eigener Prozess). Die Doku im README sagt das deutlich.
- Später optional: Modus `p4 --mqtt …`, der Aufträge über `p4/cmd` schickt und auf `p4/reply` wartet. Dann ginge Schreiben auch über die Rechte von homectld. Nicht Teil des ersten Wurfs.
- Der Name `p4` kollidiert nicht mit dem Verzeichnis `p4mqttd/` oder `htdocs/p4/`. Bei einer Parallelinstallation mit dem alten p4d überschreibt `make install` dessen `p4` im gleichen Pfad. Das ist gewollt, die Version in homectld ist die weiterentwickelte.

### 3.5 README und Disclaimer in `p4mqttd/`

`p4mqttd/README.md` wird aus dem README von p4d **angepasst übernommen**, einschließlich des vollständigen Disclaimers. Wegen der Schreibzugriffe auf eine Heizungssteuerung ist der Disclaimer hier keine Formalie und kommt **bereits in Phase 0**, nicht erst mit der Doku am Ende.

Übernommen (inhaltlich unverändert aus `~/source/linux-p4d/README.md`):

- **Autor und Homepage** (Jörg Wendel, GitHub-Projekt).
- **License**: GPL, Verweis auf `LICENSE`.
- **Disclaimer** englisch ("USE AT YOUR OWN RISK. No warranty. …", "THE SOFTWARE IS PROVIDED "AS IS" …") und der **deutsche Absatz**: Software für den Eigengebrauch, Studie zur Kommunikation mit der S 3200, kein Anspruch auf Funktion, Risiko bei den Heizungsparametern (Fehlfunktion, Störung, Brand), Vorgaben und AGB des Herstellers bleiben maßgebend, keine Beziehung zu Fröling, Fehlfunktion bei künftigen Firmware-Ständen möglich.
- **Prerequisites**: USB-Seriell-Wandler mit FTDI-Chip an COM1 der Fröling-Platine, Linux. Die udev-Regel `contrib/99-usbftdi.rules` gibt es in homectld schon (von dort wird sie referenziert, nicht kopiert; vorher prüfen, ob sie dem p4d-Stand entspricht).

Angepasst bzw. neu:

- **Entfällt**: der Donation-Abschnitt (steht schon im übergeordneten README von homectld), kein MariaDB, keine Webserver-Installation, kein libwebsockets. Stattdessen Voraussetzung mosquitto und homectld.
- **Was der Daemon ist**: reiner Kommunikations-Daemon, WEBIF und Daten gehören homectld. Abgrenzung zum eigenständigen p4d (bleibt unverändert, Verweis auf dessen Projekt).
- **Installation** (`make P4=1`, Service, `/etc/default/p4mqttd`), **Konfiguration** (`TTY`, `MQTT_URL`, `READONLY`, `PARALLEL`, `DATA_TOPIC`), **Topics und Protokoll** (Kurzfassung aus Abschnitt 4), **Tool `p4`**, **Parallelbetrieb** (Abschnitt 3.3) und **Fehlersuche** (Port, Rechte, Semaphor, `p4 state`).
- Ein deutlicher Hinweis im Abschnitt zu `parset`/`setp`/`timeset`: Schreibzugriffe nur mit Verständnis der Parameter, `READONLY` als Schutz.

Weitere Stellen:

- Der **Disclaimer im Haupt-README von homectld** (`# Disclaimer`) bekommt einen Zusatz, der auf den Heizungs-Disclaimer in `p4mqttd/README.md` verweist.
- Das Haupt-README bekommt den Eintrag in der Funktionsliste und eine Zeile in der Dokumentationstabelle, jeweils mit Verweis auf `p4mqttd/README.md`.
- Das Konfig-Item `p4Enabled` in homectld bekommt eine Beschreibung mit Hinweis auf den Disclaimer.
- `p4mqttd -v` und der Log-Eintrag beim Start nennen Version und Lizenz. Beim ersten Start mit `READONLY=0` schreibt der Daemon einmal einen Hinweis ins Log.

## 4. Protokoll

### 4.1 Prinzipien

- **Bestehendes bleibt unverändert.** Messwerte und `init` laufen über das vorhandene Format in `HomeCtl::dispatchOther`.
- **Neues in eigenem Topic-Baum `p4/…`** (Basis konfigurierbar, wie `homeMaticTopic`), nach dem Vorbild von `hmmqtt`: `status`/`state`/`errors` retained, `event/…` flüchtig, Aufträge an den Dienst auf `cmd`, Antworten auf `reply`.
- **Request/Response mit Korrelations-ID** (`rid`), weil die serielle Antwort bis zu einigen Sekunden dauern kann. Der Aufrufer wartet nicht synchron, homectld meldet das Ergebnis per `replyResult` ans WEBIF.
- Alle Payloads sind JSON mit UTF-8 (Umlaute der Menütexte).

### 4.2 Messwerte (vorhandenes Format, Topic `p4/sensor`)

Das Nachrichtenformat ist das vorhandene von `HomeCtl::dispatchOther`. Das Topic ist **`p4/sensor`** und wird von homectld automatisch mit abonniert, solange `p4Enabled` gesetzt ist (kein Eintrag in `mqttSensorTopics` nötig).

**Übergangsphase:** Während des Aufbaus, solange ohne die neue Funktion in homectld getestet wird, kann p4mqttd die Messwerte stattdessen auf `homectld2mqtt/p4` senden (Option `DATA_TOPIC` in `/etc/default/p4mqttd`). Das Topic läuft dann über die vorhandene `mqttSensorTopics`-Konfiguration, ganz ohne Codeänderung in homectld. Nach der finalen Umstellung wird dieser Eintrag aus der Konfiguration entfernt und `DATA_TOPIC` bleibt auf dem Default `p4/sensor`.

```json
{ "type": "P4VA", "address": 1, "title": "Abgas", "unit": "°C", "value": 77.0 }
{ "type": "P4DO", "address": 12, "title": "Saugzug", "kind": "status", "state": 1, "text": "A" }
{ "type": "P4AO", "address": 3,  "title": "Saugzug Leistung", "unit": "%", "value": 62 }
{ "type": "P4UD", "address": 1,  "title": "Status", "kind": "text", "value": 5, "text": "Heizen" }
```

- Skalierung (`factor`, signed/unsigned `SUBTYPE`) macht **p4mqttd**. homectld bekommt fertige Werte, `factor` bleibt 1.
- `valid:false`, wenn der Wert nicht lesbar war (homectld zeigt dann "ungültig", statt einen alten Wert zu halten).
- `maxage` setzen (z. B. 3 × Intervall), damit der Timeout nicht auf die festen 10 min in `wsactions.c` angewiesen ist.
- Beim Start und nach `initsensors` einmal **alle** ValueSpecs lesen und publizieren, damit sie in homectld im Sensor-Setup als inaktive Sensoren auftauchen (wie bei den anderen Diensten). Danach nur die aktiven.
- Der Dienst meldet sich mit `{"action":"init","type":"P4UD","topic":"p4/cmd"}` an. Das ist der vorhandene Mechanismus, mit dem homectld sich das Kommando-Topic merkt.

### 4.3 Dienst-Zustand (neu, retained)

`p4/status`:
```json
{ "online": true, "tty": "/dev/ttyUSB0", "comm": "ok", "lastOk": 1788000000,
  "firmware": "V50.04 ...", "version": "p4mqttd 0.1.0", "readonly": false }
```
`comm` ist `ok`, `lost` (Reopen läuft) oder `closed`. Last-Will setzt `online:false`. homectld zeigt das als Dienst-Status und invalidiert die `P4*`-Sensoren bei `lost`.

`p4/state` (zusätzlich zu `P4UD`, für das Heizungs-Widget und die Mail-Logik):
```json
{ "state": 5, "stateinfo": "Heizen", "mode": 1, "modeinfo": "Automatik",
  "time": 1788000000, "error": false }
```

### 4.4 Aufträge und Antworten (neu)

Aufträge auf `p4/cmd`, Antworten auf `p4/reply`, beide mit `rid`.

| `cmd` | Parameter | Antwort |
|---|---|---|
| `menu` | - | Struktur des Service-Menüs: `{"items":[{"parent","id","child","type","address","title","unit"}...]}`, aus dem Cache. Beim Start einmal retained als `p4/menu`. |
| `initmenu` | - | liest die Menü-Struktur neu von der Heizung, danach `menu` |
| `menuvalues` | `parent` | aktuelle Werte der Einträge eines Menü-Knotens inkl. `digits`, `min`, `max`, `def`, `factor`, `unit` (Lesen "on demand" nur für die gerade geöffnete Seite) |
| `parget` | `address` | Wert und Metadaten eines Parameters (ersetzt `s3200/request` + `parget`) |
| `parset` | `address`, `value` | schreibt den Parameter. Antwort: `status` (`ok`, `nochange`, `range`, `format`, `comm`, `readonly`) und der gelesene neue Wert |
| `timeget` | `address` | die vier Zeitbereiche eines Zeitprogramms |
| `timeset` | `address`, `ranges:[{from,to}x4]` | schreibt ein Zeitprogramm |
| `errors` | - | aktuelle Fehlerliste der Heizung (siehe 4.6) |
| `initsensors` | - | liest die Sensor-Definitionen (Wertliste und Ein-/Ausgänge des Menüs) neu von der Steuerung, schreibt den Cache `sensors.json` neu und beginnt mit dem Polling der neuen Liste. Entspricht `kill -USR1` aus Phase 1. Die Antwort enthält die Anzahl je Typ (`{"status":"ok","values":87,"inputs":12,"outputs":9,"outputsAnalog":6}`) bzw. den Fehler. Neue Sensoren erscheinen in homectld als inaktive Sensoren. |
| `inittimes` | - | alle Zeitprogramme neu lesen |
| `timesync` | - | Uhr der Heizung jetzt auf Systemzeit setzen |

Beispiel:
```json
-> p4/cmd    { "cmd": "parset", "rid": "7f3a", "address": 480, "value": "72" }
<- p4/reply  { "rid": "7f3a", "cmd": "parset", "status": "ok", "address": 480,
               "value": "72.0", "unit": "°C" }
```
Fehlerfall: `{ "rid":"7f3a", "status":"range", "message":"Value out of range", "min":65, "max":85 }`.
Die Texte entsprechen den Meldungen aus `performParStore` in p4d.

Die Prüfung auf Bereich und Format (`ConfigParameter::setValue`, `wrnOutOfRange`, `wrnNonUpdate`) bleibt **in p4mqttd**, denn dort liegt das Wissen über Digits, Faktor und Typ.

### 4.5 Konfiguration (neu, retained, von homectld an p4mqttd)

`p4/config`, geschrieben von homectld bei Start und bei Änderung:
```json
{ "pollInterval": 60, "stateInterval": 10,
  "tsync": true, "maxTimeLeak": 10,
  "active": { "P4VA": [1,2,3,4], "P4DI": [5], "P4DO": [12], "P4AO": [3] } }
```
`active` ergibt sich aus den aktiven Sensoren in homectld. p4mqttd pollt nur diese. Bis die Konfiguration da ist, verwendet p4mqttd eine kleine Standardmenge (Status plus Kessel-/Abgastemperatur) bzw. publiziert nur Status.

### 4.6 Fehler (neu)

- `p4/errors` (retained): komplette aktuelle Liste der Heizung
  `{"errors":[{"number","state","time1","time2","time4","info","text"}]}`. `state` ist `1` = gekommen, `2` = gegangen, `4` = quittiert, genau wie in `Fs::ErrorInfo` / `errState2Text`.
- `p4/event/error` (flüchtig) bei neuem oder geändertem Eintrag, damit homectld sofort eine Mail auslösen kann, ohne die Liste zu vergleichen.
- **homectld** führt die Historie in `p4errors` und fasst dabei die drei Zeilen (kommt / geht / quittiert) zu einem Datensatz zusammen. Das ist der Merge-Code aus `P4d::updateErrors`, der unverändert übernommen wird. Schlüssel: `NUMBER` + `TIME1`.
- Abfrage nach einer Weile: p4mqttd liest die Fehlerliste im Status-Takt, aber nur wenn der Status auf "Störung" wechselt oder alle 10 min (das vollständige Lesen ist langsam).

### 4.7 Quittieren / Reset

In p4d gibt es das nicht, da es keinen Request im Protokoll gibt. Falls ein Reset-Befehl später entdeckt wird (`mstReset`), kommt er als `cmd:"reset"` hinzu. Vorerst nicht Teil des Plans.

## 5. Sicherheit

Schreibzugriffe auf eine Heizungssteuerung sind kritisch (siehe Disclaimer in p4d).

- Rechte in homectld: `parset`, `timeset`, `initmenu` nur mit `urSettings` (wie `evHomeMatic`), Anzeige des Menüs mit `urView`. Prüfung in `wsactions.c` über die Rechte-Tabelle.
- `READONLY=1` in `/etc/default/p4mqttd` lässt p4mqttd Schreibaufträge generell mit `readonly` ablehnen, auch wenn jemand direkt MQTT publiziert.
- p4mqttd erlaubt `parset` nur für die Menü-Typen, die p4d heute schon schreibt (`mstPar`, `mstParDig`, `mstParSet*`, `mstParZeit`), und nur für Adressen, die im Menü-Cache vorkommen.
- Jeder Schreibzugriff wird mit altem und neuem Wert und mit dem Benutzer (den homectld im Auftrag als `user` mitgibt) in homectld protokolliert.
- Der MQTT-Broker ist ein Vertrauensanker. Wer auf `p4/cmd` schreiben kann, kann die Heizung ändern. Der Broker bleibt wie bisher lokal, optional ACL/User.

## 6. Daten-Migration aus bestehendem p4d

Die Historie wird übernommen, aber **entkoppelt von der Umstellung**: Das Skript wird erst **nach der finalen Migration** manuell aufgerufen, nicht beim Umstieg. So kann die Umstellung zuerst mit leerer Historie laufen und der Import kommt später, ohne dass p4mqttd oder homectld davon abhängen.

- Skript `p4mqttd/scripts/p4d-migrate.sh` (neu, liest die p4-Datenbank nur lesend):
  - `valuefacts`: `TYPE` `VA/DI/DO/AO/UD/PA` auf `P4VA/P4DI/P4DO/P4AO/P4UD/P4PA`, Adresse bleibt.
  - `samples`, `peaks`: gleiche Typ-Umbenennung.
  - `errors` → `p4errors`, `menu` → `p4menu`, `pellets` → `p4pellets`.
  - Das Skript ist **wiederholbar** (idempotent): schon vorhandene Zeilen (Schlüssel Typ/Adresse/Zeit bzw. `NUMBER`+`TIME1`) werden übersprungen, die Daten laufen in die schon produktiv befüllten Tabellen hinein. Optionen: `--dry-run`, `--from <Datum>`, `--only samples|errors|pellets|menu`.
  - Die in der Zwischenzeit von homectld geschriebenen Samples der `P4*`-Typen werden nicht überschrieben.
- Vor dem Einlesen **Schema von `samples` in homectld gegen p4d vergleichen** (beide stammen aus derselben Linie, ist aber noch nicht geprüft).
- Dashboard, Gruppen und Widget-Zuordnungen müssen neu gesetzt werden (andere Typ-Namen). Das lässt sich mit dem Sensor-Setup in homectld erledigen.
- Umstieg: erst im Parallelbetrieb vergleichen (Abschnitt 3.3), dann p4d stoppen und deaktivieren, `PARALLEL` ausschalten, `p4Enabled` in homectld setzen.
  Der Import der Historie folgt später (siehe oben).

## 7. Phasen

| Phase | Inhalt | Ergebnis |
|---|---|---|
| **0 Vorbereitung** | Unterordner `p4mqttd/` anlegen, **`README.md` mit Disclaimer (Abschnitt 3.5)**, `p4io.c/h`, `service.c/h` und `p4cmd.c` (Tool `p4`) dorthin kopieren; eigenes Makefile, Service-Datei, Delegation im Haupt-Makefile, `Make.user`-Schalter; Build ohne DB | `p4mqttd` baut, liest Status von der Heizung |
| **1 Messwerte** | Semaphor-Sperre je Transaktion, Parallelmodus (`PARALLEL=1`), Polling, Skalierung, Sensor-Discovery, `init`, LWT/`p4/status`, Reconnect der Seriellen | Werte erscheinen in homectld wie `W1`/`VIC`, Charts und Alerts gehen sofort |
| **2 Konfiguration** | `p4/config` (aktive Sensoren, Intervalle), Config-Gruppe "Heizung" in homectld, Zeit-Sync | Polling nur aktiver Sensoren, Zeit-Sync läuft |
| **3 Menü lesen** | `initmenu`/`menu`/`menuvalues`, `p4service.c`, Tabelle `p4menu`, `htdocs/p4/menu.js` | Service-Menü im WEBIF ansehbar |
| **4 Parameter schreiben und Kommandos** | `parget`/`parset`, `timeget`/`timeset`, Rechte, `READONLY`, Protokollierung; Kommando-Topic `p4/cmd` mit `rid`/`p4/reply` und das `init` mit dem Topic (siehe 7b); **`initsensors` und `initmenu` per MQTT-Kommando und als Schaltflächen auf der S3200-Seite im WEBIF** (Recht `urSettings`, Rückmeldung mit Anzahl bzw. Fehler; `SIGUSR1` bleibt als Weg ohne MQTT) | Parameter im WEBIF änderbar, Neu-Einlesen der Sensoren ohne Shell |
| **5 Fehler und Mail** | `p4/errors`, `p4/event/error`, Tabelle `p4errors`, `htdocs/p4/errors.js`, Status-/Fehler-Mail über die Mail-Funktion von homectld | Fehlermenü und Benachrichtigung |
| **6 Statistik** (beschlossen, wird gebraucht) | Statusdauer (`SD`), Stoker-Stunden, Pellet-Tabelle und Pellet-Reiter (`htdocs/p4/pellets.js`) aus den homectld-Samples | Ersatz für `SD` und Pellets |
| **7 Doku + Migrations-Skript** (Skript wird nach der finalen Umstellung separat aufgerufen) | `p4d-migrate.sh`, `p4mqttd/README.md` vervollständigen (Protokoll, Fehlersuche), kurze `doc/p4.md` mit Verweis, Haupt-README (Funktionsliste, Doku-Tabelle, Disclaimer-Zusatz), `HISTORY.h` | Umstieg möglich |

### 7a. Stand Phase 0 (umgesetzt am 2026-11-11)

Angelegt bzw. geändert (noch nicht committet):

- `p4mqttd/`: `p4io.c/h`, `service.c/h`, `p4cmd.c` unverändert aus p4d kopiert (p4d selbst wurde nicht angefasst), `p4mqttd.c` (neu), `Makefile`, `p4mqttd.service`, `p4mqttd.default` (startet mit `-r`), `README.md` mit Disclaimer, `scripts/` (leer).
- `Makefile` (Wurzel): bei `P4 = 1` wird das eigenständige Makefile in `p4mqttd/` aufgerufen, `p4mqttd` und `p4` werden gebaut, installiert, in `SERVICES` aufgenommen, in `clean` und `clean-install` berücksichtigt. `Make.user.template`: `#P4 = 1`.
- `.gitignore`: die alten Einträge `p4` und `p4d` wurden zu `/p4` und `/p4d`, weil `p4` sonst auch `htdocs/p4/` und `p4mqttd/p4` verschluckt hätte. Neu: `p4mqttd/p4mqttd`, `p4mqttd/p4`, `p4mqttd/obj/`.
- `p4mqttd.c` kann: Optionen (`-d -u -T -i -r -P -l -t -n -v`), Semaphor `0x3da00001` um jede Transaktion, Serial öffnen mit Eingangspuffer leeren und `check()`, Status alle `-i` Sekunden lesen, `p4/status` und `p4/state` retained publizieren, Reconnect der seriellen Schnittstelle und von MQTT, sauberes Beenden mit Offline-Status.
- Abweichung vom Plan: Das `service.h` blieb unter dem Originalnamen (wie in 3.1 beschrieben), der Include-Pfad `-I..` hat keine Kollision mit dem `service.h` im Wurzelverzeichnis ergeben.

Geprüft: Der Unterordner baut auch allein: in einer Kopie, die nur `lib/` und `p4mqttd/` enthält, laufen `make`, `make DESTDIR=… install` und `make clean`; das Binary linkt nur jansson, crypto, zlib und die C++-Laufzeit (kein mariadb, lua oder websockets). `p4mqttd` und `p4` bauen ohne Warnungen mit `make -j4`; `-v`, `-h`; Start ohne Heizung (nicht vorhandenes tty, nicht erreichbarer Broker) versucht erneut und beendet sich sauber; `make P4=1 install -n` ruft das Unterverzeichnis auf, ohne `P4` nicht.

**Nicht geprüft** (keine Heizung und kein Test-Broker in dieser Umgebung): Lesen des Status von der echten S 3200, Verhalten des Semaphors zusammen mit einem laufenden p4d, tatsächliche Zustellung der retained Topics an einen Broker. Das ist der erste Test am Zielsystem (mit `-P`).

### 7b. Stand Phase 1 (umgesetzt am 2026-11-11)

`p4mqttd` 0.2.0 liest Status, Messwerte und Ein-/Ausgänge und publiziert sie im homectld-Format.

- **Sensor-Entdeckung**: Wertliste der S 3200 (`P4VA`) und die Digital-/Analog-Ein-/Ausgänge aus dem Service-Menü (`P4DI`, `P4DO`, `P4AO`) werden nacheinander gelesen (ohne zwischengeschaltetes Polling, damit die Listen-Iteration der Steuerung nicht gestört wird) und als `sensors.json` im Cache-Verzeichnis abgelegt. Neu gelesen wird bei geänderter Firmware oder per `kill -USR1`.
- **Polling**: alle Sensoren einmal je `-I` Sekunden (Default 60), jede Transaktion einzeln unter dem Semaphor mit 30 ms Pause dazwischen. Skalierung (Faktor, vorzeichenbehaftet oder nicht) in p4mqttd. Nicht lesbare Werte gehen als `valid:false` raus.
- **`P4UD`** (Status, Betriebsart, Uhrzeit der Steuerung) im Status-Takt.
- **Topics**: Messwerte auf `p4/sensor` (Option `-D` für den Übergang `homectld2mqtt/p4`), `p4/status` und `p4/state` retained. **Last Will** auf `p4/status` (`online:false`), dafür hat `lib/mqtt.h/.c` die abwärtskompatible Methode `Mqtt::setWill()` bekommen (andere Programme sind nicht betroffen).
- **Ausfallverhalten**: 5 fehlgeschlagene Transaktionen in Folge → Schnittstelle schließen und neu öffnen (alle 5 s), MQTT-Reconnect alle 5 s.
- **Simulator** `p4mqttd/scripts/p4sim.py`: simuliert die COM1-Schnittstelle der S 3200 über ein Pseudo-Terminal (check, state, version, value, Wertliste, Menüliste, DI/DO/AO), damit ohne Heizung getestet werden kann.

Abweichungen vom Plan:

- Das **`init`** (`action:"init"` mit Kommando-Topic) wird **noch nicht** gesendet. Aus demselben Grund gibt es das Neu-Einlesen der Sensoren vorerst nur per `SIGUSR1`; als MQTT-Kommando `initsensors` und im WEBIF folgt es in Phase 4. Es speichert das Topic dauerhaft in der homectld-Konfiguration (`mqttCmdTopic…`) und hätte ohne Kommando-Auswertung keinen Zweck. Es kommt mit Phase 3/4.
- **Alle** Sensoren werden gepollt, nicht nur die aktiven. Das ändert sich mit `p4/config` in Phase 2.
- `P4PA` (Kessel-Solltemperatur `0x1e0` aus p4d) gehört zu den Parametern und kommt mit Phase 3/4.
- **Terminal-Einstellungen unter der Sperre prüfen** (Abschnitt 3.3, Punkt 4) ist **nicht** umgesetzt. `Serial::open` stellt sie beim Öffnen ein, ein Fremdprozess ändert sie in der Praxis nicht.

Getestet gegen den Simulator mit privatem mosquitto (Port 18830, der produktive Broker wurde nicht benutzt): erste Entdeckung und Cache, Laden aus dem Cache, Neu-Lesen per `SIGUSR1`, Messwerte (auch negative Werte), alle Typen, retained `p4/status` und `p4/state`, Last Will bei `kill -9`, Ausfall und Wiederkehr der Schnittstelle (inklusive `valid:false`), das Tool `p4` gleichzeitig neben dem Daemon (gemeinsamer Semaphor).

**Nicht getestet**: echte S 3200 (Antwortzeiten, Umfang von Wert- und Menüliste), Zusammenspiel mit dem alten p4d, die Aufnahme der Daten in homectld (`dispatchOther`, die Seite von homectld wurde nicht angefasst).

### 7c. Stand Phase 2 (umgesetzt am 2026-11-11)

**p4mqttd 0.3.0**

- Liest das retained `p4/config` (zweiter MQTT-Client als Subscriber) und übernimmt `pollInterval`, `stateInterval`, `tsync`, `maxTimeLeak` und die Liste der aktiven Sensoren. Die Kommandozeilenwerte gelten als Default bis die Config da ist.
- **Polling nur der aktiven Sensoren.** Die erste Runde nach dem Start und nach dem Neu-Einlesen der Sensor-Definitionen deckt **alle** Sensoren ab (Entdeckung durch homectld, sonst könnte man neue Sensoren nie aktivieren). Fehlt `active` in der Config, werden alle gepollt. Ein neu aktivierter Sensor wird sofort gelesen, nicht erst nach Ablauf des Intervalls.
- **Zeit-Sync** wie in p4d (`scheduleTimeSyncIn`, frühestens 3:00, höchstens einmal täglich, nur bei Abweichung über `maxTimeLeak`), **nie im Nur-Lesen-Modus** (auch nicht mit `-P`).
- `scripts/p4sim.py` kennt jetzt `--drift` und das Setzen von Datum/Uhrzeit.

**homectld**

- Neues Modul `p4service.c` (im Makefile unter `OBJS`): `p4ReadConfig()` liest die Config-Items und fügt das Topic `<p4Topic>/sensor` zu den Sensor-Topics hinzu (nur bei `p4Enabled`), `p4Check()` veröffentlicht alle 5 s bei Änderung, nach dem Start und nach jedem Reconnect das retained `<p4Topic>/config`; die aktiven Sensoren werden aus `sensors["P4VA"|"P4DI"|"P4DO"|"P4AO"]` gebildet.
- Neue Config-Gruppe **"Heizung S 3200"** in `config.c`: `p4Enabled` (Default aus, mit Hinweis auf den Disclaimer), `p4Topic`, `p4PollInterval`, `p4StateInterval`, `p4TimeSync`, `p4MaxTimeLeak`.
- Eingehängt in `readConfiguration()` (`daemon.c`; ein Wechsel von Aktivierung oder Topic erneuert die MQTT-Verbindung), `performMqttRequests()` und `mqttDisconnect()` (`hass.c`).

Getestet (Simulator, privater mosquitto): ohne Config werden alle Sensoren gepollt; nach einer Config mit 3 aktiven nur noch diese 3; Aktivieren eines weiteren Sensors wird innerhalb weniger Sekunden wirksam; Zeit-Sync bei 120 s Abweichung (Abweichung danach 0); bei `tsync:false` kein Sync.

**Nicht getestet**: der homectld-Teil. `make` baut ihn ohne Fehler, gestartet habe ich keine zweite homectld-Instanz (Datenbank, Port und produktiver Broker würden kollidieren). Zu prüfen beim ersten Lauf: `p4/config` kommt retained am Broker an, `p4/sensor` wird abonniert, die Sensoren erscheinen inaktiv im Sensor-Setup und die Aktivierung ändert die Config. Ebenso nicht getestet: Zeit-Sync an der echten Heizung.

### 7d. Stand Phase 3 (umgesetzt am 2026-11-11)

Das Service-Menü der S 3200 ist im WEBIF **lesend** ansehbar.

**p4mqttd 0.4.0**

- Die gesamte Menüliste der Steuerung wird bei der Entdeckung mitgelesen (der Durchlauf, der die Ein-/Ausgänge sucht, speichert jetzt alle Einträge) und im Cache `sensors.json` unter `menu` abgelegt. Ein Cache aus Phase 2 ohne Menü führt automatisch zum erneuten Lesen.
- Kommando-Topic `<base>/cmd` (nicht retained, retained Kommandos werden ignoriert, höchstens 20 wartende) und `<base>/reply` mit `rid`.
- `menu` {`parent`}: die Einträge einer Ebene mit **aktuellen** Werten, Portierung von `performMenuByParent`/`updateParameter` aus p4d (Firmware, Digital-/Analog-Ausgänge, Messwerte mit Faktor, Parameter mit Einheit, Nachkommastellen und Kennzeichen `editable`). Gefiltert wie in p4d: leere Titel, Typen BusValues/Reset, die Adressen 9997-9999 und Einträge ohne Wert und ohne Unterebene. Nicht lesbare Werte kommen als `error:true`.
- `initmenu` (Alias `initsensors`): liest Menü und Sensoren neu, die Antwort kommt am Ende (`menuItems`, `sensors`).

**homectld**

- Neues Event `p4menu` (`webservice.h/.c`, `wsactions.c`, Recht `urView` wie in p4d), `performP4Menu()` schickt das Kommando mit einer `rid` an p4mqttd und merkt sich den Client, `dispatchP4Mqtt()` (aus `performMqttRequests()`) verarbeitet `<base>/status` (setzt `p4Online`) und `<base>/reply` und schickt das Ergebnis als Event `p4menu` an den anfragenden Client. Fehler gehen als `{"error": "..."}` an die Seite, nach 30 s ohne Antwort ebenfalls.
- Config: `p4PageTitle` (Default "S3200"). Zusätzlich abonniert homectld nun `<base>/status` und `<base>/reply`.
- WEBIF: Hauptmenü-Tab (Name `p4PageTitle`) nur bei `p4Enabled`; Dateien `htdocs/p4/menu.js` und `htdocs/p4/p4.css`, eingebunden in `index.html.in`, Hooks in `main.js` (Tab, Seitenwechsel, Event). `make -C htdocs check` prüft jetzt auch `p4/*.js`.

Abweichungen vom Plan:

- **Keine Tabelle `p4menu` in homectld.** Die Menüstruktur liegt im Cache von p4mqttd und kommt mit jeder `menu`-Antwort zusammen mit den Werten. homectld bleibt zustandslos, die Seite braucht ohnehin die laufende Heizung für die Werte. Gibt es später einen Grund (Anzeige ohne p4mqttd), wird die Tabelle nachgezogen.
- **`menu` und `menuvalues` sind ein Kommando** (`menu` mit Werten).
- Die **Zeit-Gruppen** ('Zeiten' mit den Wochentagslisten) werden weggelassen, sie kommen mit `timeget`/`timeset` in Phase 4.
- **Nur lesend**: `editable` wird mitgeliefert, die Seite bietet noch kein Bearbeiten an.
- Die **Schaltflächen** für `initmenu`/`initsensors` im WEBIF folgen wie geplant in Phase 4.

Getestet gegen den Simulator (mit erweitertem Menü und `getParameter`): Neu-Lesen eines alten Caches ohne Menü, `menu` für mehrere Ebenen (Titel und Rückweg, Messwert mit Faktor, Firmware, Parameter, ja/nein-Parameter, Ausgänge/Eingänge), unbekannte Ebene (leere Liste), unbekanntes Kommando, `initmenu`. `node --check` für `p4/menu.js`, `index.html` bekommt die versionierten Pfade `p4/...`.

**Nicht getestet**: homectld mit dem p4-Teil (Start einer zweiten Instanz nicht möglich) und die Seite im Browser. Beim ersten Lauf prüfen: Tab erscheint erst nach `p4Enabled` und Neuladen der Seite, Klick auf den Tab zeigt die Ebene 1, Navigation vor und zurück, Fehlermeldung bei gestopptem p4mqttd. Außerdem nicht an der echten Heizung: Dauer des Menü-Durchlaufs, Anzahl der Einträge, Dauer einer Ebene.

### 7e. Stand Phase 4 (umgesetzt am 2026-11-11)

Parameter und Zeitprogramme der Heizung sind im WEBIF änderbar, Menü, Sensoren und Zeitprogramme lassen sich per Schaltfläche neu lesen. **Das schreibt in die Heizungssteuerung** und ist nur gegen den Simulator getestet.

**p4mqttd 0.5.0**

- Kommandos `parget`, `parset`, `timeset`, `inittimes` (zusätzlich zu `menu`, `initmenu`/`initsensors` aus Phase 3).
- **Nur-Lesen-Schutz**: `parset` und `timeset` werden mit `readonly` abgelehnt, wenn der Daemon mit `-r` oder `-P` läuft, und zwar bevor irgendetwas gelesen oder gesendet wird.
- **Whitelist**: nur Adressen, die im Menü-Cache als änderbarer Parameter (Typen Par, ParSet, ParSet1, ParSet2, ParDig, ParZeit) ohne Unterebene vorkommen, und nur bekannte Zeitprogramm-Adressen (0 bis 0xdf aus der Liste der Steuerung). Alles andere: `error`.
- **Ablauf von `parset`**: aktuellen Wert lesen, Wert prüfen und umrechnen (`ConfigParameter::setValue`), `setParameter` aus p4io (liest nochmals, lehnt unveränderte Werte `nochange` und Werte außerhalb des Bereichs `range` ab, schreibt, liest zurück). Antwort mit `old` und dem zurückgelesenen `value`.
- **Protokollierung**: jede Änderung als `WRITE parameter 0x… '<alt>' -> '<neu>' by '<user>'` bzw. `WRITE time program …`, abgelehnte Änderungen als `Refused: …`. homectld protokolliert zusätzlich Benutzer, Adresse, alt, neu und Ergebnis.
- **Zeitprogramme**: Die Steuerung kann nur die ganze Liste nacheinander liefern (224 Einträge, etwa 10 s). Sie wird beim ersten Bedarf gelesen, im Speicher gehalten und in `times.json` abgelegt (an die Firmware gebunden); `inittimes` und `initmenu` lesen neu. Im Menü erscheinen die Zeit-Gruppen wie in p4d: je Wochentag ein Kopf und vier Bereiche.
- Menüeinträge der Parameter enthalten jetzt `min`, `max` und `default`, die Bearbeiten-Dialoge brauchen deshalb keine zweite Anfrage.
- In der **Kopie von `service.h`** ist das Parsen von Zahlen locale-unabhängig (`FroelingService::parseNumber`: `.` oder `,`). p4d verließ sich auf die deutsche Locale des Prozesses (`isFloat()` kennt nur `,`, `strtod()` hängt von der Locale ab). Das ist die einzige Änderung an den kopierten Protokolldateien (`p4io.c` ist unverändert).
- Fehler behoben: Ein Lesen der Zeitprogramme, das durch das Beenden abgebrochen wurde, galt als vollständig (partielle Liste).

**homectld**

- Neues Event `p4cmd` mit **Recht `urSettings`**, `performP4Cmd()` mit `what` = `parset`, `timeset`, `initmenu`, `inittimes`. Die Werte werden vorab auf erlaubte Zeichen geprüft (Ziffern, `: - , .` und `ja`/`nein`), der Benutzername des Clients geht als `user` mit.
- Gemeinsame Funktion `p4SendCommand()` (Rid, Pending mit Client, Menü-Ebene, Timeout 60 s, `inittimes` 180 s, `initmenu` 900 s). Nach der Antwort meldet homectld das Ergebnis per `result` (z. B. "Gespeichert: 78 -> 72") und schickt die Menü-Ebene neu (beendet auch den Fortschrittsdialog).
- WEBIF `htdocs/p4/menu.js`: Dialoge für Zahl (Bereich, Schrittweite aus den Nachkommastellen), ja/nein, Uhrzeit und Zeitbereiche (von/bis, `Löschen`), jeweils mit dem Hinweis, dass sofort in die Heizung geschrieben wird. Editierbare Einträge nur für Benutzer mit dem Recht Settings (oder Admin). Auf der obersten Ebene die Schaltflächen **"Menü und Sensoren neu lesen"** (mit Rückfrage) und **"Zeitprogramme neu lesen"**.

Abweichungen vom Plan:

- Das **`init` mit Kommando-Topic wird nicht gesendet.** Es dient dazu, schaltbare Sensoren anzusteuern; p4mqttd hat keine schaltbaren Sensoren (Digital- und Analogausgänge werden nur gelesen). Es würde nur ein totes Topic in der homectld-Konfiguration ablegen.
- Die Prüfung "Wert im zulässigen Bereich" macht allein `setParameter` aus p4d. Eine eigene zweite Prüfung wurde bewusst nicht ergänzt, weil `min`/`max` der Steuerung bei Faktor ungleich 1 nicht eindeutig skaliert sind (p4d vergleicht den skalierten Wert mit dem rohen Minimum). Das ist an der echten Heizung bei einem Parameter mit Nachkommastellen zu prüfen.
- Mail- und Statuslogik bleibt Phase 5.

Getestet gegen den Simulator (mit `setParameter`, Zeitprogrammen und Datum/Zeit): Parameter ändern, unveränderter Wert, falsches Format, Wert außerhalb des Bereichs, ja/nein-Parameter, unbekannte Adresse, eine Adresse, die kein Parameter ist, Zeitbereich setzen, löschen, ungültige Zeit, unbekanntes Zeitprogramm, erneutes Lesen der Zeitprogramme (zweimal hintereinander vollständig), Nur-Lesen-Modus mit `-r` und `-P` (alle Schreibzugriffe abgelehnt, Lesen geht), das Menü zeigt die geänderten Werte. `node --check` für `p4/menu.js`, homectld baut ohne Fehler.

**Nicht getestet**: homectld mit dem p4-Teil (Rechteprüfung, Zuordnung der Antworten, Fortschrittsdialog), die Dialoge im Browser und alles an der echten Heizung. Beim ersten Lauf zuerst mit `-r` Menü und Zeiten ansehen und mit dem alten p4d vergleichen, erst dann einen unkritischen Wert (z. B. ein Zeitprogramm) ändern, den alten Wert notieren und zurücksetzen.

### 7f. Seitenstruktur S3200 mit Tabs (11.11.2026)

Die Seite im Hauptmenü (Name `p4PageTitle`, Default "S3200") ist ein Rahmen mit einem **Sub-Menü** (Tabs) unterhalb des Hauptmenüs, so wie die Setup-Seiten es haben (gleiche Optik, `initScrollStrip`):

| Tab | Datei | Stand | Recht |
|---|---|---|---|
| Service-Menü | `htdocs/p4/menu.js` | fertig (Phase 3/4) | Ansehen `urView`, Ändern `urSettings` |
| Fehler | `htdocs/p4/errors.js` | Phase 5 | `urView` |
| Pellets | `htdocs/p4/pellets.js` | Phase 6 | `urControl` (Erfassen `urFullControl`) wie in p4d |
| Schema | `htdocs/p4/schema.js` (+ Anpassung `schema.js`) | **beschlossen**: eigenes Anlagenbild der Heizung, Phase 6b | `urView`, Bearbeiten wie die Schema-Seite |

- `htdocs/p4/page.js` hält die Tab-Liste. Jede Datei meldet ihren Tab mit `p4RegisterTab({ id, title, show, rights })` an, `show()` füllt den Container. Der gewählte Tab wird im Browser gemerkt, ein Tab ohne ausreichende Rechte erscheint nicht.
- In `main.js` sind nur zwei kleine Haken: der Seitenaufruf `initP4Page()` und das Aufbauen des Sub-Menüs in `prepareSetupMenu()`.
- Neue Tabs brauchen damit nur ihre Datei, einen `<script>`-Eintrag in `index.html.in` und das Event/Kommando im Backend.
- **Schema (beschlossen, Phase 6b)**: Die Heizung bekommt ein **eigenes Anlagenbild** (Kessel, Puffer, Heizkreise, Pellet-Förderung ...) als Tab "Schema", getrennt von der allgemeinen Schema-Seite im Hauptmenü. Stand der vorhandenen Schema-Seite (gelesen, nicht verändert): `schema.js` (858 Zeilen) zeigt ein Hintergrundbild `img/schema/schema-<config.schema>.png` und frei darauf platzierte Elemente, die Konfiguration steht in der Tabelle `schemaconf` (Schlüssel `ADDRESS` + `TYPE`, dazu Art, Größe, Eigenschaften, Funktion), Events `schema` / `storeschema`, Bearbeiten für Settings/Admin.
  Entwurf für das zweite Schema:
  - **Bild**: neues Config-Item `p4Schema` (Name des Bildes, `img/p4/schema/schema-<name>.png`, die Auswahl und das Hochladen läuft über die vorhandene Images-Verwaltung). Das Bild muss von dir kommen.
  - **Elemente trennen**: Die Elemente der Heizung sind `P4…`-Sensoren, `schemaconf` ist über `TYPE` schon getrennt. Das Event `schema` bekommt den Parameter `set` (`""` = allgemeines Schema ohne `P4…`, `"p4"` = nur `P4…`), `storeschema` ebenso. Dafür ist **keine Änderung am Datenbankschema** nötig.
  - **Einschränkung**: Ein Sensor kann nur in einem der beiden Schemas stehen (der Schlüssel ist Adresse + Typ). Für das Heizungsschema genügt das, solange die Elemente `P4…`-Sensoren sind. Sollen auch andere Sensoren (z. B. die Außentemperatur eines Funksensors) hinein, braucht `schemaconf` eine Spalte `SCHEMA` im Schlüssel. Das wird entschieden, wenn es gebraucht wird. Das Gleiche gilt für die allgemeinen Elemente (`UC`, `CN`).
  - **`schema.js` parametrisieren**: Bildname, Event-Parameter `set` und der Container als Argument von `initSchema()` statt der globalen `config.schema` / `currentPage == 'schema'`. Der Tab ruft es mit `set = 'p4'`. Das ist die einzige Änderung an bestehendem Code.
  - **Aufwand** grob: `schema.js` und `performSchema()`/`storeSchema()` mit `set` (klein bis mittel), Tab-Datei und Config-Item (klein). Das Platzieren der Elemente macht der vorhandene Editor.

Das Sub-Menü ist in der Seite eingebaut, im Browser aber noch **nicht getestet**.

### 7g. Widget-Vorgaben der Sensoren (11.11.2026, p4mqttd 0.6.0)

Jede Sensor-Meldung von p4mqttd enthält das Objekt `parameter` mit den Vorgaben für das Dashboard-Widget (Art des Widgets, Skala von/bis/Schritt, Spitzenwert, Symbol eines Schalters). Das ist der vorhandene Mechanismus des Sensor-`parameter` von homectld (`valuefacts.parameter`, die Vorgaben fließen beim Anlegen eines neuen Widgets in dessen Optionen ein). Die Regeln stehen als Tabelle `widgetRules` in `p4mqttd.c` (Typ, Einheit, Teil des Titels, erste passende Regel gewinnt), z. B. Abgas als Meter 0–300, Puffer als Balken 0–100, Pellet-Füllstand als Level-Widget, Pumpen- und Mischer-Symbole, Status als SymbolText.

- **Änderung in homectld**: `addValueFact()` übernimmt den `parameter` bei Sensoren vom Typ `P4…` jetzt auch für **bestehende** Einträge. Bisher geschah das nur beim Anlegen und bei Skript-Sensoren (`SC`), die vorhandenen `P4…`-Zeilen aus der Übernahme der p4d-Daten hätten sonst nie Vorgaben bekommen.
- Die Vorgaben gelten nur für **neu angelegte** Widgets, bestehende Widgets im Dashboard bleiben unverändert.
- Die inaktiven Sensoren bekommen ihre Vorgaben, sobald p4mqttd sie sendet (erste Runde nach dem Start, oder sobald sie aktiviert sind).
- **Bilder aus p4d** (p4mqttd 0.7.0): Die Pumpen verwenden die animierten Bilder von p4d (`img/icon/pump-on.gif` / `pump-off.png`), für die Gebläse (Saugzug) dreht sich das MDI-Lüftersymbol (Option `spin`, gleichwertig zu `vent-on.gif`). Der Status der Heizung (`P4UD:1`) liefert `image` mit dem animierten Statusbild von p4d (`img/state/<Icon-Set>/state-fire.gif` usw., gleiche Zuordnung der Betriebszustände wie `P4d::getTextImage`), für Zustände ohne eigenes Bild (z. B. 19 "Betriebsbereit") das Bild des Heizungstyps `img/p4/type/heating-<Typ>.png`. Icon-Set und Heizungstyp kommen mit `p4/config` von homectld (neues Config-Item `p4HeatingType`, Default `P4`, Auswahl aus `img/p4/type`; Standardwerte `light` / `P4`, solange die Config fehlt).
- **Dateien aus p4d übernommen**, alles was nur für die Heizung gebraucht wird liegt unter **`htdocs/img/p4/`**: `p4/schema/schema-p4-2hk-puffer*.png` und `schema-s4-puffer-1hk-solar.png` (7 Anlagenbilder, Grundlage für den Tab "Schema") und `p4/type/heating-*.png` (die Bilder der Heizungstypen, 26 Dateien einschließlich des `heating-P4.png`, das homectld schon hatte und das vorher in `img/type/` lag). Die Pumpen-, Lüfter- und Status-Bilder (`icon/`, `state/`) waren in homectld schon identisch vorhanden und bleiben dort, sie sind allgemein.
- Offen: Die Tabelle ist im Code. Das `TODO.md` von homectld sieht ohnehin vor, die Widget-Vorgaben in eine JSON-Datei zu legen, die Regeln von p4mqttd könnten dann mit dorthin wandern.

### 7h. Stand Phase 5: Fehler und Mails (11.11.2026, p4mqttd 0.9.0)

**p4mqttd**

- Liest die Fehlerliste der Steuerung am Stück unter einer Sperre (wie die anderen Listen) und veröffentlicht sie retained als `p4/errors` (`number`, `info`, `state` 1 gekommen / 4 gegangen / 2 quittiert, `time`, `text`, älteste zuerst). Gelesen wird beim Start, bei einem Wechsel des Betriebszustands von oder in eine Störung (`Fs::isError`), alle 10 Minuten und per Kommando `errors` (die Antwort enthält die Liste). Auf der echten Heizung: 49 Einträge in 1,6 s.
- `p4/state` enthält `error` (Störungszustand ja/nein).
- Der Simulator kennt die Fehlerliste.

**homectld**

- Neue Tabelle `p4errors` (`etc/configs/database.dat`, wird beim Start angelegt, Felder wie die `errors` von p4d). `p4MergeErrors()` führt die Liste zusammen (Port von `P4d::updateErrors`): die drei Einträge eines Fehlers ergeben eine Zeile (Schlüssel Nummer + Zeit des Kommens). Der erste Import (leere Tabelle) wird als Historie mit `MAILCNT = 1` abgelegt und nicht gemailt. Die Auswertung geschieht bei jedem `p4/errors` (auch dem retained nach einem Neustart, das Zusammenführen ist idempotent) und bei der Antwort auf `errors`.
- **Mails**: `p4SendErrorMail()` für alle Fehler, die nicht quittiert und noch nicht gemailt sind (je Fehler genau einmal), `p4SendStateMail()` beim Wechsel des Betriebszustands zu einem der Zustände aus `p4StateMailStates` (Default 0,1,3,19, leer = jeder Wechsel) mit der Tabelle der aktiven `P4…`-Sensoren. Empfänger, Skript und Aktivierung sind die vorhandenen der Gruppe "Mail" (`mail`, `mailScript`, `errorMailTo`, `stateMailTo`, `webUrl`), dazu der neue Schalter **`p4Mail` (Default aus)**, damit homectld und der alte p4d nicht beide mailen. Der Text ist für die Shell maskiert (`'` im Fehlertext).
- Neues Event `p4errors` (Recht `urView`): ohne Aktion die Tabelle, mit `action: "refresh"` wird die Liste erst aus der Steuerung gelesen.
- WEBIF: Tab **"Fehler"** (`htdocs/p4/errors.js`) mit der Tabelle (Zeit, Fehler, Status, Dauer, offene Fehler rot) und der Schaltfläche "Von der Heizung lesen".

Abweichungen vom Plan:

- **Kein `p4/event/error`.** homectld bekommt die ganze Liste ohnehin retained und wertet sie idempotent aus, ein zusätzliches Ereignis brächte nichts.
- `p4StateMailStates` ist ein Textfeld mit Zahlen statt einer Mehrfachauswahl (p4d hatte eine Auswahl der Zustände aus seiner Tabelle, die in homectld nicht vorliegt). Eine Auswahl ist möglich, wenn p4mqttd die Zustandsliste liefert.

Geprüft: Fehlerliste im Simulator (mit Kommando) und an der echten Heizung (49 Einträge, nach homectld übernommen: 16 Fehler, alle quittiert, `MAILCNT` 1), Tabelle wird angelegt, homectld baut und läuft. **Nicht geprüft**: der Tab im Browser und der **Mailversand** (`p4Mail` ist aus). Zum Test: `p4Mail` einschalten, einen Empfänger setzen und beim nächsten Fehler bzw. Zustandswechsel auf die Mail warten, den alten p4d vorher abschalten oder die Empfänger trennen.
- Die Historie des alten p4d (64 Zeilen `errors`) wird mit dem Migrationsskript in `p4errors` übernommen (Schlüssel Nummer + Zeit des Kommens).

### 7i. Stand Phase 6: Statusdauer und Pellets (11.11.2026)

Alles in homectld, p4mqttd ist unverändert (0.9.0).

- **Statusdauer** (`p4CalcStateDuration()`, jede Minute): Summiert die Minuten je Betriebszustand des heutigen Tages aus den Samples des Statussensors `P4UD:1` (Zustandsnummer als Wert, Text) und legt/aktualisiert die Sensoren `P4SD:<Zustand>` ("<Zustand> (Laufzeit/Tag)", Einheit min, Standard-Widget Chart). Alle schon bekannten Zustände ohne Zeit heute werden 0, um Mitternacht beginnt es neu (`date(time) = curdate()`). Voraussetzung: **`P4UD:1` muss aufgezeichnet werden** (`record = A`, gesetzt), p4d speicherte ihn dagegen unabhängig vom Flag.
- **Änderung in `store()`**: Sensoren der Art "text" mit Typ `P4…` speichern jetzt zusätzlich den Wert (die Zustandsnummer), vorher stand im Sample nur der Text. Gefunden beim ersten Lauf (der erste Sample hatte `NULL` als Wert), danach behoben.
- **Pellets**: Tabelle `p4pellets` (Datum, Menge kg, Preis, Notiz). `p4Pellets2Json()` ist die Portierung von `performPellets`: Tage zwischen den Lieferungen, Stoker-Stunden zwischen ihnen (Sample von `P4VA:173` nach dem Datum, `min(value)`), kg/h, €/t und eine Summe mit Schätzung des Verbrauchs seit der letzten Lieferung. Neues Config-Item `p4ConsumptionPerHour` (0 = aus den letzten beiden Lieferungen). Events `p4pellets` (Recht Control) und `p4pelletsadd` (Full Control; Hinzufügen, Ändern, Löschen, jeweils mit Log über den Benutzer).
- **Tab "Pellets"** (`htdocs/p4/pellets.js`) mit der Tabelle, Eingabefeldern für Berechtigte und einer Zeile zum Hinzufügen.

Geprüft: Die Tabellen werden angelegt, `P4UD:1` wird mit Zustandsnummer und Text aufgezeichnet (`19.00`, "Betriebsbereit"), der Sensor `P4SD:19` wurde angelegt, die Abfrage der heutigen Samples liefert die erwarteten Zeilen, homectld baut und läuft ohne Fehler im Log. **Nicht geprüft**: der berechnete Wert der Statusdauer (der Sensor ist inaktiv und ändert sich erst, wenn der Zustand wechselt), die Pellets-Auswertung mit echten Lieferungen (die Historie von p4d kommt mit dem Migrationsskript) und beide Tabs im Browser.

### 7j. Stand Phase 6b: Tab "Schema" (11.11.2026)

Die Heizung hat ein eigenes Anlagenbild im Tab "Schema". Es ist die vorhandene Schema-Seite (`schema.js`) mit dem Parameter `set = 'p4'`, ohne zweite Implementierung.

- **Bild**: neues Config-Item `p4Schema` (Auswahl aus `img/p4/schema/schema-<Name>.png`, Default `p4-2hk-puffer`, das war das Bild von p4d). Die sieben Anlagenbilder aus p4d liegen dort schon.
- **Elemente**: Die Elemente der Heizung sind die `P4…`-Sensoren. In `schemaconf` stehen aus der früheren Übernahme der p4d-Daten schon 32 Zeilen dieser Typen (`P4VA` 21, `P4DO` 4, `P4UD` 3, `P4AO`, `P4DI`, `P4SD`, `P4W1`), mit ihren Positionen. Texte und Linien im Heizungsschema haben die Typen **`P4UC`** und **`P4CN`** (statt `UC` und `CN`), damit sie getrennt vom Schema des Hauptmenüs bleiben. Die Spalte `TYPE` hat 8 Zeichen, das reicht.
- **Server** (`performSchema`): der Parameter `set`: `p4` liefert nur die Typen `P4…` und antwortet mit dem Event `p4schema`, ohne `set` kommen alle anderen Typen wie bisher (die `P4…`-Elemente erscheinen dort nicht mehr). `storeSchema` bleibt unverändert, der Typ gehört zum Element.
- **`schema.js`**: Die Variable `schemaSet` bestimmt das Bild (`schemaImage()`) und die Typen für Texte und Linien (`schemaTypeUC()`, `schemaTypeCN()`), sonst ist der Code derselbe. Bearbeiten wie gehabt über "Setup Schema" im Burger-Menü (Recht Settings), jetzt auch im Tab.
- **`htdocs/p4/schema.js`**: der Tab, `main.js` bekommt den Event `p4schema` und aktualisiert das Schema auch bei Sensor-Updates im Tab.

Offen: Das Schema des Hauptmenüs hat sonst Typen wie `UC`/`CN`; die zwei Text-Elemente (`UC`) aus dem Schema von p4d sind noch nicht übernommen, das macht das Migrationsskript (`UC` wird zu `P4UC`). Geprüft: `node --check`, homectld baut. **Nicht geprüft**: der Tab im Browser (Bild, Elemente an den übernommenen Positionen, Bearbeiten und Speichern), weil noch nicht installiert.

### 7k. Stand Phase 7: Doku und Migrationsskript (11.11.2026)

- **`p4mqttd/scripts/p4d-migrate.sh`**: übernimmt aus der Datenbank von p4d (`p4`) in die von homectld (`homectl`) die Samples (VA, DI, DO, AO, UD, SD, W1 → `P4…`), die Min/Max-Werte (zusammengeführt), die Fehler (`p4errors`, als Historie mit `mailcnt ≥ 1`), die Pellet-Lieferungen (`p4pellets`) und die Schema-Elemente (`UC` → `P4UC`, `CN` → `P4CN`, noch nicht platzierte Sensoren). Ohne `--run` nur ein Trockenlauf, nichts wird überschrieben (`INSERT IGNORE`, "not exists"), der Lauf ist wiederholbar und kann parallel zum Betrieb laufen. Optionen `--only`, `--from`, `--known-only`, `--from-db`, `--to-db`, `--mysql-opts`. Die Samples werden je Sensor (Primärschlüssel-Präfix) und Jahr übernommen.
- **Trockenlauf gegen die echten Datenbanken** (nur gelesen): 26,2 Mio. Samples (P4VA 18,5 Mio., P4DO 3,2 Mio., P4UD 2,7 Mio., P4SD 0,7 Mio., P4W1 0,3 Mio., P4AO 0,9 Mio., P4DI 2), 48 von 64 Fehlern (16 stehen schon aus der Heizung drin), 12 Pellet-Lieferungen, 2 Schema-Elemente, 35 Min/Max-Werte. Bei einem Sensor, dem One-Wire-Sensor `P4W1:0x5679341` ("Puffer Mitte", 323 014 Samples), gibt es in homectld keinen Eintrag mehr (er wurde dort am 11.11. um 14:38 gelöscht); mit `--known-only` werden solche Sensoren übersprungen.
- **Noch nicht ausgeführt**: der Lauf mit `--run`. Vorher ein Backup von `homectl`, die Dauer für 26 Mio. Zeilen ist offen (Schätzung: einige Minuten bis eine halbe Stunde).
- **Doku**: `doc/p4.md` (Einrichtung, Seite und Tabs mit Rechten, Mails, Zeit-Sync, Bilder, Übernahme der p4d-Daten), Eintrag in der Funktionsliste, der Doku-Tabelle und im Disclaimer des Haupt-README, `p4mqttd/README.md` vervollständigt (Aufgaben, Fehlersuche, Migration).
- **Offen**: `HISTORY.h` (auf Zuruf).

Reihenfolge: 0 → 1 → 2 reichen für den produktiven Betrieb der Messwerte. 3-5 bringen die Funktionen, die nur im p4d-WEBIF existierten. 6 und 7 sind danach optional.

## 8. Tests

- **Ohne Heizung**: `p4mqttd/scripts/p4sim.py` simuliert die COM1-Schnittstelle (Pseudo-Terminal): check, state, version, Wert- und Menüliste, Werte, Ein-/Ausgänge, Parameter lesen und schreiben, Zeitprogramme lesen und schreiben, Datum/Zeit. Die Tests der Phasen 1 bis 4 liefen damit gegen einen privaten mosquitto (Port 18830). Ein Mitschnitt der echten Heizung (Menü, Wertliste) könnte den Simulator später realistischer machen.
- **Protokoll**: `mosquitto_pub`/`mosquitto_sub` Skripte für jeden `cmd` unter `p4mqttd/scripts/` (wie die Beispiele in `doc/homematic.md`).
- **An der Heizung** (jeweils zuerst mit `READONLY=1`):
  - Die Checkliste dafür steht in Abschnitt 8a.
- Ausfall: USB-Stick abziehen, `comm:"lost"`, Sensoren werden ungültig, nach dem Einstecken wird alles wieder gültig.

### 8a. Checkliste: erster Test an der echten Heizung

Ziel: in vier Stufen von "p4mqttd allein" bis "Schreiben im WEBIF". Jede Stufe hat ein Abbruchkriterium, erst bei bestandener Stufe geht es weiter. Haken setzen und Auffälligkeiten (Zeiten, Fehler, Abweichungen) hinter den Punkt schreiben.

**Voraussetzungen**

- [ ] p4mqttd, Heizung (FTDI-Stick) und der alte p4d laufen auf **demselben Host** (der Semaphor gilt nur dort, nicht zwischen Containern oder Hosts, nicht mit `PrivateIPC=yes`).
- [ ] mosquitto läuft, `mosquitto_sub` und `mosquitto_pub` sind installiert.
- [ ] `Make.user`: `P4 = 1` (oder im Unterordner `p4mqttd/` einzeln bauen). Abhängigkeiten: `build-essential pkg-config libssl-dev libjansson-dev zlib1g-dev`.
- [ ] Der Stand von homectld, der installiert wird, ist gesichert (Backup der Datenbank, der bisherige Binary-Stand ist bekannt). `p4Enabled` ist Default **aus**, bis dahin ändert sich im Betrieb nichts.
- [ ] Der Stick ist mit dem festen Namen unter `/dev/serial/by-id/` erreichbar, die Regel `contrib/99-usbftdi.rules` ist installiert.

**Stufe 1: nur p4mqttd (homectld unberührt), mit `-P`**

- [ ] `cd p4mqttd && make && sudo make install`; in `/etc/default/p4mqttd` das Gerät eintragen und `-P` (nur lesend, nicht schneller als 60 s) setzen, **nicht** `-r` weglassen.
- [ ] `systemctl start p4mqttd`; parallel `mosquitto_sub -t 'p4/#' -v` und `journalctl -u p4mqttd -f`.
- [ ] `p4/status`: `online:true`, `comm:"ok"`, `firmware` passt zur Heizung (Bedienfeld: Service-Menü → Firmware).
- [ ] `p4/state`: Status und Betriebsart entsprechen dem Bedienfeld, die Uhrzeit stimmt.
- [ ] **Entdeckung**: das Log zeigt "Read … sensor definitions and … menu items in … s". Zeit und Anzahlen notieren (Wertliste, Ein-/Ausgänge, Menüeinträge). Eine zweite Zeile "Loaded … from cache" nach einem Neustart.
- [ ] **Messwerte**: `p4/sensor` liefert `P4VA`, `P4DI`, `P4DO`, `P4AO`, `P4UD`. Stichprobe von mindestens 5 Messwerten (Kesseltemperatur, Abgas, Puffer, Außen, Restsauerstoff) gegen das Bedienfeld und den alten p4d, dazu ein negativer Wert (Außentemperatur im Winter) und ein Ausgang.
- [ ] **Dauer**: "Polled … values in … s" liegt deutlich unter dem Intervall (60 s). Wenn nicht: Anzahl der Sensoren und Antwortzeit notieren.
- [ ] Der alte p4d läuft ungestört weiter (keine neuen Fehler im Log, Werte kommen weiter), `p4 state -d <Gerät>` funktioniert gleichzeitig.
- [ ] Neustart-Test: `systemctl restart p4mqttd`, Status und Werte kommen wieder. Stick abziehen und wieder einstecken: `comm` wird `lost`/`closed`, danach wieder `ok`.
- Abbruch: Fehler im Log des alten p4d, ausbleibende Werte, `comm` bleibt nicht `ok`. Dann `systemctl stop p4mqttd` und die Ausgaben sichern.

**Stufe 2: homectld anbinden (lesend)**

- [ ] homectld mit dem neuen Stand bauen und installieren; er startet wie vorher (Log prüfen, auch wegen der Änderung an `lib/mqtt`).
- [ ] *Config → Heizung S 3200*: `p4Enabled` an, speichern. Im Log von homectld: "Adding the topics of the S 3200 service …" und "Sending the config to p4mqttd [...]".
- [ ] `mosquitto_sub -t p4/config -v`: das retained `p4/config` liegt am Broker. Im Log von p4mqttd: "Config received: …".
- [ ] Im Sensor-Setup erscheinen die `P4…`-Sensoren (inaktiv), mit Titel und Einheit.
- [ ] Ein paar Sensoren aktivieren: nach wenigen Sekunden kommen Werte, p4mqttd meldet mehr aktive Sensoren, die Werte stimmen mit dem Bedienfeld überein. Einen Sensor wieder deaktivieren: er wird nicht mehr gepollt.
- [ ] Charts und Dashboard: ein `P4VA`-Sensor erscheint im Dashboard und im Chart.
- [ ] Zeit-Sync **aus** lassen (solange der alte p4d die Zeit der Heizung synchronisiert).
- Abbruch: homectld-Log mit Fehlern zu `p4`, Sensoren erscheinen nicht, Werte weichen ab.

**Stufe 3: Service-Menü lesend**

- [ ] Nach `p4Enabled` und Neuladen der Seite erscheint der Tab (Name aus `p4PageTitle`, Default "S3200").
- [ ] Ebene 1 lädt. **Dauer notieren**, Fortschrittsdialog verschwindet.
- [ ] Navigation in mehrere Unterebenen und zurück (Titel, Zurück-Schaltfläche).
- [ ] Vergleich mit dem Service-Menü des alten p4d bzw. des Bedienfelds: Titel, Werte, Einheiten, ja/nein-Werte, Ausgänge ("on (A)"), Firmware.
- [ ] Die Wochentagslisten ("Zeiten") erscheinen (beim ersten Mal etwa 10 s Wartezeit für das Lesen aller Zeitprogramme) und stimmen mit dem Bedienfeld überein.
- [ ] Sicht ohne Schreibrecht: ein Benutzer ohne Recht Settings sieht keine Bearbeiten-Dialoge und keine Schaltflächen zum Neulesen.
- [ ] p4mqttd stoppen und eine Ebene anfordern: eine Fehlermeldung statt eines hängenden Dialogs.
- Abbruch: Ebenen dauern länger als etwa 30 s (der Timeout in homectld), Werte weichen systematisch ab.

**Stufe 4: Schreiben**

- [ ] Vorher: `p4mqttd` **ohne** `-P` und ohne `-r` starten, den alten p4d stoppen (nur ein Schreibender). Die Hinweise im README zum Disclaimer sind gelesen.
- [ ] Zuerst ein **unkritischer Wert**: ein Zeitbereich eines nicht benutzten Programms. Den alten Wert notieren, im Dialog ändern (Speichern), Rückmeldung "Gespeichert: … → …", im Menü und am **Bedienfeld** nachsehen, dann zurücksetzen. Im Log von p4mqttd steht `WRITE time program …` mit Benutzer, alt und neu.
- [ ] Zeitbereich **löschen** (`Löschen`) und wieder setzen.
- [ ] Ein Parameter **ohne** Nachkommastellen, nur um einen Schritt ändern und zurücksetzen. Danach die Meldungen "Wert unverändert" (denselben Wert nochmals) und "außerhalb des Bereichs" (bewusst zu großer Wert) prüfen.
- [ ] Ein Parameter **mit Nachkommastellen** (Faktor ungleich 1): ändern, im Menü und am Bedienfeld nachsehen. **Besonders prüfen**, ob `min`/`max` im Dialog zu den Werten passen und ob ein gültiger Wert nicht fälschlich als "außerhalb des Bereichs" abgelehnt wird (siehe Abschnitt 7e).
- [ ] Ein ja/nein-Parameter nur wenn er harmlos ist.
- [ ] Ein Benutzer ohne Recht Settings kann nicht schreiben (kein Dialog; ein direkt gesendetes Event wird abgelehnt).
- [ ] Zeit-Sync: erst nach den Schreibtests, `p4TimeSync` einschalten und prüfen, dass nur bei einer Abweichung über dem Grenzwert und frühestens um 3:00 synchronisiert wird. Davor den alten p4d dauerhaft deaktivieren.
- Abbruch: eine Änderung wirkt anders als erwartet, die Heizung zeigt eine Störung, ein Wert lässt sich nicht zurücksetzen. Dann `systemctl stop p4mqttd`, den Wert am Bedienfeld herstellen und die Ausgaben sichern.

**Was ich zur Auswertung brauche**

- Das Log von p4mqttd ab dem Start (Entdeckung, erste Runde, Config) und `p4/status`, `p4/state` und zwei, drei Zeilen von `p4/sensor`.
- Die Zahlen: Anzahl Werte, Ein-/Ausgänge und Menüeinträge; Dauer von Entdeckung, einer Runde und einer Menü-Ebene; Dauer des ersten Lesens der Zeitprogramme.
- Abweichungen zum Bedienfeld oder zum alten p4d (Titel, Einheiten, Skalierung, Vorzeichen, Umlaute).
- Das Log von homectld zu `P4:` und `p4mqttd`.
- Bei Fehlern: eine Zeile mit `journalctl -u p4mqttd` und, falls möglich, die serielle Mitschrift (`p4mqttd -n -t -l 255 …`, nur kurz, die Ausgabe ist groß).

**Ergebnisse Stufe 4 (11.11.2026, Host p4d, p4mqttd 0.10.2 ohne `-P`/`-r`, alter p4d gestoppt)**

- `parset` mit Faktor 1 und mit Faktor 2 (Nachkommastellen, Skalierung von `min`/`max`) funktioniert, getestet im WEBIF durch den Benutzer; Werte am Bedienfeld bestätigt. Beispiel im Log: `WRITE parameter 0x4f: '10' -> '9'`.
- `timeset` funktioniert (Heizkreis-Zeiten, Benutzer-Test). Die S 3200 speichert Zeiten nur in **10-Minuten-Schritten** (`hh*10 + mm/10`): `22:11` wurde still zu `22:10` abgeschnitten, das Speichern meldete "ok" und der alte Wert blieb sichtbar. Behoben: p4mqttd lehnt Minuten, die kein Vielfaches von 10 sind, mit "Die Steuerung kennt nur Zeiten in 10-Minuten-Schritten" ab und meldet gleiche Werte als `nochange`, ohne zu schreiben.
- Einmalig meldete das Einlesen der Zeitprogramme "Liste endet nicht" (300 Schritte ohne Adresse `0xdf`), kurz nachdem ich parallel mit `p4 times` und `p4 menu` gelesen hatte. Danach lief es (224 Programme in 3 s). Ursache nicht belegt (Verdacht: Listenposition der Steuerung durch paralleles Lesen). Bei einem erneuten Fehler stehen jetzt die ersten Adressen im Log.
- Nicht getestet: Meldungen "Wert unverändert" und "außerhalb des Bereichs" bei `parset`, Zeitbereich löschen (`nn:nn`), Benutzer ohne Recht Settings.

**Ergebnisse Stufe 1 (11.11.2026, Host p4d, Firmware 50.04.05.03, Parallelmodus neben dem alten p4d)**

- Verbindung zur S 3200 und zum Broker auf gate: ok. `p4/status` `comm":"ok"`, `p4/state` "Betriebsbereit" / "Übergangsbetrieb".
- **Erste Entdeckung schlug fehl, behoben**: Der Daemon gab die Leitung zwischen den Requests frei, der alte p4d pollte dazwischen, die Steuerung verlor ihre Position in der Menüliste und die Liste endete nicht (5872 statt 1276 Einträge, dann Abbruch). `p4 menu` hält den Semaphor für den ganzen Lauf und kam nach 44 s sauber durch. Seit 0.5.0 (Nachbesserung) hält p4mqttd die Leitung für die **gesamte** Listenabfrage (Wertliste, Menü, Zeitprogramme), loggt alle 250 Einträge und bricht bei mehr als 20000 Einträgen ab.
- **Entdeckung jetzt**: 228 Werte, 1276 Menüeinträge (stimmt mit der Datenbank von p4d und `p4 menu` überein), davon 115 Ein-/Ausgänge (73 `P4DO`, 23 `P4DI`, 19 `P4AO`), zusammen 343 Sensoren, **51,4 s**, Cache `sensors.json` 194 KB. Der alte p4d wird dabei einmalig etwa eine Minute blockiert.
- **Messwerte**: erste Runde über alle 343 Sensoren in **30 s** (etwa 87 ms je Wert), danach nur die 7 aktiven (Config von homectld). Ein Wert aus der Stichprobe: Abgas 61 °C, Außen 7 °C, Vorläufe 34,5 / 35,5 °C, Pelletbehälter 65,7 %, Puffer oben 60 °C / unten 42 °C.
- **Auffälligkeit, kein Fehler von p4mqttd**: Die Uhr der Steuerung geht genau **eine Stunde nach** (12:43 gegen 13:43 Systemzeit), auch `p4 state` zeigt das. Der Zeit-Sync ist in homectld noch aus.
- **Zu prüfen**: Der Titel von Wert 254 kommt mit `%%` aus der Steuerung ("Kesselstellgröße < 40 %%"), ob p4d ihn auch so zeigt. Werte gegen Bedienfeld und alten p4d vergleichen.

## 9. Risiken und offene Punkte

1. **Serielle Antwortzeiten.** Menü mit allen Werten sind viele Requests (p4d liest sie im Hintergrund in die DB). Lösung: `menuvalues` nur für den geöffneten Knoten, dazu Cache. Messen, wie lange ein Knoten braucht (Phase 3).
2. **Bus-Konkurrenz.** Ein Benutzer-Kommando darf das zyklische Polling nicht aushungern und umgekehrt. Die Prioritäts-Queue muss das abbilden. Das Polling ist bei p4d durch den Semaphor serialisiert.
3. **Menü-Cache bei Firmware-Update der Heizung.** `initmenu` muss einfach auslösbar sein (Kommando im WEBIF). Evtl. Firmware-Version in `p4/status` und automatisches Neulesen bei Änderung.
4. **Config-Modell.** `p4/config` ist retained und wird von homectld geschrieben. Wenn homectld neu startet, bevor der Broker die Nachricht liefert, läuft p4mqttd mit Defaults. Nach dem Empfang wird nachgezogen.
5. **Gemeinsames Mail-System.** Es ist noch zu klären, ob die Mail-Funktion in homectld die Vorlagen (`mail-head.html`, `%weburl%`) so verwenden kann wie p4d. Die Konfiguration (`mailScript`) ist derzeit in p4d `p4d-mail.sh`, in homectld nicht geprüft.
6. **Mehrere Heizungen / Typ-Auswahl** (`heatingType` in p4d wählt Schema und Icons). Vorschlag: vorerst eine Heizung, `heatingType` entfällt oder wird ein Hinweisfeld in der Konfiguration.
7. **p4cmd.** Entschieden: wird als Tool `p4` übernommen (Abschnitt 3.4).
8. **Typnamen.** `P4PA` für Parameter-Sensoren ist neu, `PA` ist in homectld schon belegt ("Parameter", Kategorie `^PA`). Die Trennung per Präfix `P4` muss durchgängig bleiben (Widget-Defaults in `daemon.c`, Kategorie-Tabelle).
9. **Parallelbetrieb.** Hängt an der Semaphor-Disziplin von p4mqttd (Abschnitt 3.3). Ein Fehler dort (Sperre zu lang oder vergessen) stört auch den laufenden alten p4d. Vor dem ersten Parallelbetrieb an der echten Heizung mit dem Simulator und mit `READONLY` testen.

## 10. Entscheidungen

1. **Topics**: Basis `p4/`. Die Messwerte gehen auf `p4/sensor`. `homectld2mqtt/p4` dient nur in der Übergangsphase und wird nach der finalen Umstellung aus der Konfiguration entfernt.
2. **Alte Historie**: wird per Skript übernommen, das **nach der finalen Migration** separat aufgerufen wird (Abschnitt 6).
3. **Pellets und Statusdauer**: werden weiter gebraucht, Phase 6 bleibt im Plan.
4. **Anzeige**: eine Seite im **Hauptmenü** (Service-Menü, Fehler, Pellets), Name konfigurierbar (`p4PageTitle`, Default "S3200"), nur sichtbar wenn der p4-Dienst aktiviert ist (`p4Enabled`).

5. **Parallelbetrieb** von altem p4d und p4mqttd ist über den gemeinsamen Semaphor möglich (Abschnitt 3.3), ebenso das Tool `p4` neben dem Daemon.

### Offen aus dem Entwurf

Siehe den Abschnitt "Abschluss". Die Punkte aus Abschnitt 9 sind dort einzeln bewertet.

## Abschluss (2026-11-11)

**Ergebnis:** Alle Phasen (0 bis 7 und 6b) sind umgesetzt und installiert. `p4mqttd` läuft auf dem Host `p4d` als
systemd-Dienst (`enabled`, ohne `-P`, schreibend), homectld auf gate hat die Seite "S3200" mit den Tabs Service-Menü, Fehler,
Pellets und Schema. Der alte p4d ist abgeschaltet und deaktiviert, die Zeit der Steuerung wurde per Zeit-Sync korrigiert
(sie ging eine Stunde nach). Die Daten des alten p4d sind übernommen (Samples 26 Mio., Peaks, Fehler, Pellets, Alerts,
Schema-Texte), Dashboards, Widgets, Gruppen und Benutzer bewusst nicht.

**Bewertung der Punkte aus Abschnitt 9**

| Nr. | Punkt | Stand |
|---|---|---|
| 1 | Serielle Antwortzeiten | gemessen: Entdeckung einmal 51 s, eine Menü-Ebene wenige Sekunden, Fehlerliste 1,6 s, alle Zeitprogramme etwa 10 s; die Listen werden unter einer Sperre am Stück gelesen (wichtig, siehe 7b) |
| 2 | Bus-Konkurrenz | gelöst über den Semaphor je Transaktion und Kommandos zwischen den Polling-Schritten; keine eigene Prioritäts-Queue nötig |
| 3 | Menü-Cache bei Firmware-Update | gelöst: der Cache ist an die Firmware gebunden, `initmenu` per Kommando und Schaltfläche |
| 4 | Config-Modell | gelöst: retained `p4/config`, Defaults bis zum Empfang |
| 5 | Mail-System | umgesetzt, **nicht getestet** (siehe unten) |
| 6 | Typ der Heizung | gelöst: Config-Item `p4HeatingType` (Bilder in `img/p4/type`) |
| 7 | p4cmd | übernommen als Tool `p4` |
| 8 | Typnamen | durchgehend Präfix `P4…`, Migration bildet darauf ab |
| 9 | Parallelbetrieb | genutzt zum Vergleich, danach beendet |

**Noch offen** (nichts davon blockiert den Betrieb)

1. **Schreiben an der echten Heizung:** `parset` (Faktor 1 und 2) und `timeset` getestet (Ergebnisse Stufe 4 in 8a). Offen nur: Meldungen "Wert unverändert" / "außerhalb des Bereichs", Zeitbereich löschen, Benutzer ohne Recht Settings.
2. **Mails:** Mailtest vom gate erledigt (msmtp-Konfiguration von p4d übernommen, Testmail über `mail.sh` und den WEBIF-Button angekommen, `mail` aktiviert). Eine echte Fehler- oder Status-Mail aus P4 ist noch nicht ausgelöst worden.
3. **Im Browser nicht bestätigt:** Tab "Fehler", Tab "Schema" (Bild, Positionen, Bearbeiten und Speichern), Dialoge zum Ändern von Werten und Zeitbereichen.
4. **Schema-Darstellung verbessern**, generell und nicht nur für die Heizung (steht in `TODO.md`).
5. Kleinigkeiten: `p4StateMailStates` ist ein Textfeld statt einer Auswahl; die Historie des gelöschten One-Wire-Sensors `W1 90673985` ist nicht übernommen; eine Lücke von etwa einer Stunde am 11.11. (15:09 bis 16:10) bei den Sensoren, die nur p4mqttd/homectld aufzeichneten; Widget-Vorgaben gelten nur für neu angelegte Widgets.

**Nebenbei behobene Fehler in homectld** (stehen im HISTORY 0.1.52): Special-Symbol-Widget mit MDI-Symbol, WebSocket-Thread auf 100 % CPU (Eintrag ohne Verbindung in der Client-Liste), Filter im Dialog "Widget hinzufügen" nicht case sensitiv.

**Ablage:** Diese Datei bleibt lokal (nicht im Git). Quellen: `p4mqttd/`, `p4service.c`, `htdocs/p4/`, `htdocs/img/p4/`, `doc/p4.md`.
