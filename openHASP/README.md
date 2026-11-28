# openHASP für Elecrow CrowPanel Advance 5.0 (ESP32-S3, 800x480)

Eigenes openHASP-Build, da die offiziellen Elecrow-Builds nur die alte (nicht-Advance) Serie unterstützen.

* `upstream.conf`                          openHASP-Commit, von dem die Firmware gebaut wird
* `patches/openhasp.patch`                 unsere Änderungen an Upstream-Dateien (esp32.cpp/.h, device.h, platformio.ini,
  LVGL im PSRAM, Log per MQTT, statusupdate mit PSRAM, MQTT-Warteschlange, siehe *Speicher und Meldungen*)
* `overlay/`                               unsere eigenen Dateien, werden über den Upstream-Baum kopiert
  (`user_setups/custom/crowpanel-advance.ini` Pinout/Panel/Touch der Advance 5.0, `platformio_override.ini`,
  `src/custom/my_custom.*` Summer und Timer)
* `build.sh`                               holt openHASP (einmalig), wendet Patch + Overlay an, baut, kopiert nach `firmware/`
* `export.sh`                              schreibt Änderungen aus `src/` zurück nach `overlay/` + `patches/`
* `src/`                                   openHASP-Arbeitskopie (nicht versioniert, von build.sh angelegt)
* `venv/`                                  PlatformIO (nicht versioniert, von build.sh angelegt)
* `firmware/`                              fertige Binaries (`*_full_*` für Offset 0x0, `*_ota_*` für OTA-Update)
* `flash.sh`                               erase + write-flash seriell (nur beim ersten Mal / Partitionswechsel)
* `ota.sh`                                 Firmware-Update über WLAN (Web-Updater des Panels)
* `data-files/`                            Dateien fuer das Geraet (pages.jsonl, online.cmd, idle_*.cmd, mqtt_on.cmd leer) und MQTT-Beispiele

## Bauen

    ./build.sh            # nach dem Clone: holt openHASP, wendet unsere Änderungen an, baut (~15 min)
    ./ota.sh <ip>         # Firmware über WLAN auf das Panel

Oder wie bei den anderen ESP32-Projekten über make (Port, IP und Web-Passwort aus `Make.user`:
`HASP_PORT`, `HASP_OTA_HOST`, `HASP_OTA_PWD`):

    make install-deps     # venv mit PlatformIO und esptool
    make                  # build.sh
    make upload           # flash.sh über USB (löscht den Flash komplett, nur beim ersten Mal)
    make upload-ota       # ota.sh über WLAN
    make monitor          # serielle Konsole (picocom)

Nur `overlay/` und `patches/` sind im homectld-Repo versioniert, der openHASP-Klon in `src/` nicht.
Änderungen an der Firmware werden in `src/` gemacht und mit `./export.sh` zurück nach `overlay/` und
`patches/` geschrieben, dann committet. `./build.sh --reset` verwirft Änderungen in `src/` und stellt
den Stand aus `overlay/` + `patches/` wieder her. Die Version steht in `platformio.ini`
(`HASP_VER_REV`, im Patch) und besteht aus zwei Teilen: openHASP-Version des Upstream-Stands und unsere
Anpassung, z.B. `0.7.0.1-homectld.3` = openHASP 0.7.0.1 (Commit aus `upstream.conf`), homectld-Anpassung 3.
Bei einer Änderung an `overlay/` / `patches/` die homectld-Nummer erhöhen, bei einem neuen Upstream-Stand den
openHASP-Teil übernehmen. Der Dateiname der Images trägt die Version zusammen mit dem Upstream-Commit.
Bisher: homectld.2 Summer, Timer, PSRAM, Log per MQTT (als 0.7.0.2 gebaut); homectld.3 Einrichtungsbildschirm.

Ergebnis landet in `src/build_output/firmware/` und wird nach `firmware/` kopiert.

## Backlight

Die Advance steuert die Hintergrundbeleuchtung ueber einen Hilfs-MCU (STC8H1K28) an I2C 0x30 auf dem
Touch-Bus (SDA 15 / SCL 16): Byte 0 = hellste Stufe, 245 = aus. Der Treiber sitzt in
`src/src/dev/esp32/esp32.cpp` (Flag `HASP_BACKLIGHT_I2C_ADDR`) und haengt an den normalen
openHASP-Backlight-Funktionen. Damit funktionieren `backlight`-Kommando, Web-UI-Slider, Idle-Dimmen
und Aufwachen beim Touch. Beim Start wird wie in Elecrows Demo geprueft, ob 0x30 und der GT911 (0x5D)
antworten; falls nicht, Kommando 250 an 0x30 und ein kurzer LOW-Puls auf GPIO 1 (max. 10 Versuche).

openHASP dimmt bei Idle nicht von selbst, sondern fuehrt `L:/idle_short.cmd`, `L:/idle_long.cmd`
und `L:/idle_off.cmd` aus. Idle-Zeiten stehen in der Web-UI unter HASP Settings (Short/Long idle).

## Dateien hochladen (curl)

Der Web-Editor nimmt Dateien per Multipart-POST auf `/edit` an. Der Feldname ist egal, der
Dateiname muss mit `/` beginnen (sonst wird `/` vorangestellt). Bei gesetztem Web-Passwort `-u user:pass`
ergaenzen.

    IP=192.168.200.123
    for f in data-files/*.cmd; do
        curl -F "data=@$f;filename=/$(basename $f)" http://$IP/edit
    done

    # pruefen, was auf dem Geraet liegt
    curl "http://$IP/list?dir=/" | jq

    # einzelne Datei, z.B. pages.jsonl
    curl -F "data=@pages.jsonl;filename=/pages.jsonl" http://$IP/edit

    # Datei loeschen (HTTP DELETE, Parameter path)
    curl -X DELETE "http://$IP/edit?path=/mqtt-demo.jsonl"

Dasselbe macht `./upload.sh <ip> [dateien]` (ohne Dateiangabe: die drei idle_*.cmd aus `data-files/`).
Nach dem Hochladen der cmd-Dateien ist kein Neustart noetig, sie werden beim naechsten Idle-Wechsel gelesen.

## Bekannte Einschränkungen

* Rev. V1.0 der Advance hat statt des STC8-MCU einen PCA9557 IO-Expander an 0x18; dafuer ist der
  Treiber nicht gebaut (Board-Revision steht auf der Platine).
* Getestet 2026-09-06 auf echter Hardware: Display, Touch (GT911) und Backlight (backlight 20 / off / on) funktionieren.

## Speicher und Meldungen

* LVGL-Objekte liegen im PSRAM (`-D LV_MEM_CUSTOM=1` in `crowpanel-advance.ini`, Allokation über
  `hasp_malloc()`). Upstream nutzt einen festen 64-KB-Block im internen RAM, der mit 3 homectld-Seiten schon
  etwa zur Hälfte belegt war (~11 KB pro Seite). Freier interner RAM mit 3 Seiten: vorher ~80 KB, jetzt ~150 KB.
  Die Zeile *LVGL Memory* der Info-Seite des Panels entfällt damit.
* `statusupdate` enthält zusätzlich `heapMaxBlock`, `psramFree` und `psramSize` (homectld zeigt sie auf der
  Seite *HASP Panel*).
* Warnungen und Fehler gehen als JSON auf `<node>/state/log` (`{"level","tag","text"}`, Log-Slot 2, ersetzt
  durch Syslog, wenn ein Syslog-Host gesetzt ist; gleiche Zeilen höchstens alle 10 s). Kein USB nötig.
  Reine Debug-Ausgaben (`QUE jsonl`, Schrift-Metriken der FreeType-Lib) sind dafür auf VERBOSE/INFO gesetzt.
* MQTT-Warteschlange 256 statt 64 Einträge (nur Zeiger, Nachrichten im PSRAM). Läuft sie über, weil die GUI
  beschäftigt ist, meldet das Panel `Queue full, message lost: <topic>` statt `Out of memory` (upstream
  hatten beide Fälle dieselbe Meldung). Vorher trat das auf, wenn homectld bei laufendem Panel neu startete.

## Firmware-Update über WLAN

    ./ota.sh <ip> [firmware.bin]

Lädt die neueste `*_ota_*.bin` aus `firmware/` über den Web-Updater (`/update`) hoch, das Panel startet
danach mit der neuen Firmware neu. Dasselbe geht von Hand im Web-UI des Panels (Firmware Update) oder mit
dem MQTT-Kommando `update http://<server>/<pfad>.bin`. Der serielle Weg (`flash.sh`) ist nur noch bei
einem Wechsel der Partitionstabelle oder nach einem abgebrochenen Update nötig.

## Summer und Timer (custom-Modul, `src/src/custom/my_custom.cpp`)

Eigene Kommandos der Firmware (`HASP_USE_CUSTOM=1` in der Umgebung), erreichbar über MQTT
`command/custom/<name>` oder lokal über das `action`-Attribut von Objekten:

* `custom/beep [an_ms [aus_ms [anzahl]]]` - Summer der Platine über den STC8-MCU (I2C 0x30). Die Kommandos
  hängen von der Platinen-Revision ab: V1.2 (Backlight 0..245) 246 an / 247 aus, V1.1 (Backlight 0x05..0x10)
  0x15 / 0x16; eingestellt über `HASP_BUZZER_I2C_ON_CMD` / `_OFF_CMD` in der Umgebung. Ohne Parameter
  200 ms einmal, `beep 0` bricht ab.
* `custom/timer <kommando>` - Countdown, läuft komplett auf dem Panel:
  `config <seite> <label> <bar> <button> <sekunden>` bindet die Objekte (Label mm:ss, Bar 0..total,
  Button-Text Start/Stop/Aus) und setzt die Vorgabe, `set <s>`, `add <s>` (auch negativ; nach unten bis eine
  Stufe über 0, nach oben bis 60 Minuten),
  `start`, `stop`, `toggle`, `reset` (zurück auf die Vorgabe), `ack` (Alarm aus), `state`.
  Nach Ablauf weckt der Timer das Display (`idle off`), piept 60 s lang alle 2 s dreimal kurz und meldet
  `state/timer` `{"state":"alarm",...}`; jedes Timer-Kommando während des Alarms beendet ihn.
  Zustandswechsel kommen auf `state/timer` (`running`, `stopped`, `alarm`).

* `custom/setup` - Einrichtungsbildschirm (eigener LVGL-Screen, unabhängig von den Seiten): WLAN aus der
  Netzsuche, Passwort, Hostname und MQTT-Server mit Bildschirmtastatur. Vorbelegt mit der aktuellen
  Einstellung (das WLAN-Passwort ist nicht lesbar, bleibt die Maske stehen, bleibt das WLAN unverändert).
  *Speichern* prüft die WLAN-Verbindung (blockiert bis ~8 s), speichert und startet neu. Ist kein WLAN
  eingerichtet (frisch geflasht), erscheint er statt des openHASP-Einrichtungsbildschirms (OOBE, Patch in
  `hasp_oobe.cpp`), dann ohne *Abbrechen*, mit Hinweis auf den Access Point `HASP-xxxxxx` / `haspadmin`.
  Seit homectld.3.

Der homectld baut daraus das Widget *Timer* (Pseudo-Sensor TIMER:0x01) und sendet nach den Objekten
`custom/timer config ...`; die Buttons tragen `action` (`{"down":"custom/timer add 60"}` usw.), das Label
`{"up":"custom/timer toggle","long":"custom/timer reset"}`. Objekte mit `action` melden ihre Touch-Events
nicht mehr per MQTT.

## Seiten (data-files/pages.jsonl)

Eine Seite mit System-Informationen, keine Bedienelemente, kein MQTT. Uhr und Datum ueber das
Label-Attribut `template` (strftime-Format, aktualisiert sich selbst). Hostname, IP, SSID, Hardware-ID,
Modell und Version ueber die openHASP-Platzhalter `%hostname%`, `%ip%`, `%ssid%`, `%mac%`, `%model%`,
`%version%` im Label-Text. Sie werden beim Setzen des Textes ersetzt; da beim Laden der Seiten noch kein
WLAN steht, setzt `online.cmd` IP und SSID nach dem Verbinden erneut (p1b21/p1b22).
Mehr stellt openHASP ohne MQTT nicht bereit (Uptime, RSSI, Heap gibt es nur auf state/statusupdate).

* Schriftgroessen im Build (`text_font`): 12, 16, 24, 32.
* Kommentarzeilen in jsonl brauchen `"skip":true`, sonst legt openHASP daraus ein leeres Objekt an.

Hochladen: `./upload.sh <ip>` (pages.jsonl und alle *.cmd), aktivieren ohne Neustart mit
`clearpage all` und `run L:/pages.jsonl` (Konsole, Telnet oder MQTT command/clearpage, command/run).

## Startseiten (data-files/welcome/)

Seiten für ein neues Panel, bis homectld seine Seiten sendet (er löscht vorher Seite 1..12 per `clearpage`):
`pages.jsonl` liegt im Flash und wird beim Booten geladen, ohne MQTT. Seite 1 *Willkommen* mit dem Knopf
*Einrichten* (Einrichtungsbildschirm der Firmware, `custom/setup`), Seite 2 was homectld ist, mit Hostname,
Hardware-ID und Firmware des Panels; Wischen wechselt. Eigene Seiten brauchen keine neue Firmware, nur
eine andere `pages.jsonl` (und ggf. Bilder / Schrift) im Flash.

* `welcome.py` erzeugt `pages.jsonl` und `mdi.ttf` (Teilmenge der MDI-Schrift mit den Symbolen der Seiten;
  homectld überschreibt sie beim *Dateien hochladen*). Als Modul für eigene Seiten nutzbar: `Pages`,
  `page_welcome()`, `page_homectld()` (z.B. als zusätzliche Seite hinter eigenen), `setup_button()`, `make_font()`.
* `img2bin.py <bild> <out.bin> [breite höhe]` wandelt ein Bild in das LVGL-7-Binärformat für ein `img`-Objekt
  (`"src":"L:/out.bin"`, RGB565, mit Alpha falls vorhanden; die Bild-Decoder der Firmware sind aus). Große
  Motive als Bild, da die Firmware MDI-Symbole nur bis 120 px zeichnet.
* `online.cmd` ohne die Objekte der System-Info-Seite (zeigt nur kurz Hostname und IP).

    ./upload.sh <ip> data-files/welcome/{pages.jsonl,online.cmd,mdi.ttf} data-files/mqtt_on.cmd

Ausprobieren ohne Neustart über Telnet (`nc <ip> 23`): `clearpage 1`, `clearpage 2`, `run L:/pages.jsonl`
(nach einer neuen `mdi.ttf` neu starten); Kontrolle per Screenshot `http://<ip>/screenshot?q=0` (BMP).

## MQTT-Demo (mqttdemo/mqtt-demo.jsonl, mqtt-demo.sh)

Seite 3 im Look des homectld-Dashboards (dunkles Theme): vier Karten in `#272727` mit Radius 3 auf
schwarzem Grund, Titel oben links, grosse weisse Werte, Ein-Farbe `#2177AD`, Skalen `#cccccc`, Zeiger
`#f08080`, kritischer Bereich `#ff5757`. Farben stammen aus `htdocs/stylesheet-dark.css`, `dashboard.js`
und `checkbox.css`.

| Karte | Objekt | Wert setzen | Text setzen |
|---|---|---|---|
| Temperatur | `linemeter` p3b21, val in Zehntel Grad (-200..500) | `p3b21.val 236` | `p3b22.text "23.6 °C"` |
| Bordspannung | `gauge` p3b32, val in Zehntel Volt (100..150), ab 145 rot | `p3b32.val 138` | `p3b33.text "13.8 V"` |
| Frischwasser | `bar` p3b44 senkrecht als Tank (0..100 %, blaue Fuellung von unten), Wert p3b42, Untertitel p3b43 | `p3b44.val 45` | `p3b42.text "45 l"` |
| Kuehlschrank | `btn` p3b52 mit `toggle`: das MDI-Symbol `fridge-variant-outline` ist der Schalter, weiss = aus, `#059eeb` = an (Zustand 01), Schrift `mdi.ttf` per FreeType | `p3b52.val 1` | |

Die Datei wird nicht auf das Geraet kopiert, sondern per MQTT gesendet (Gruppen-Topic `hasp/plates`,
Zustandsmeldungen kommen auf `hasp/<hostname>/state/...`):

    # zeilenweise senden: das MQTT-Paket ist im Build auf 2048 Bytes begrenzt (MQTT_MAX_PACKET_SIZE),
    # ein groesserer Payload wird abgeschnitten und die restlichen Objekte fehlen
    grep -v '"skip":true' mqttdemo/mqtt-demo.jsonl | while IFS= read -r l; do
        mosquitto_pub -h <broker> -t hasp/plates/command/jsonl -m "$l"; done
    mosquitto_pub -h <broker> -t hasp/plates/command/page -m 3
    mosquitto_sub -h <broker> -v -t 'hasp/<hostname>/state/+'   # Schalter: {"event":"up","val":1}

MDI-Symbole, zwei Wege:

1. Eingebaute Schrift: feste Auswahl (Liste `src/src/font/md-icons.json`, Abschnitt "include"), Codepunkt
   F0xxx nach E0xx verschoben (F1xxx nach F0xx), z.B. `fridge-outline` F028F -> `\uE28F`. Groessen 12/16/24/32
   als Bitmap; jede andere Zahl > 8 rendert FreeType aus der eingebetteten `openhasp.ttf`, z.B. `text_font:112`.
2. Eigene TTF per FreeType: Datei `<name>.ttf` in den Flash laden und `"text_font":"<name>_<groesse>"` setzen.
   `mqttdemo/mdi.ttf` (2,3 KB) ist eine Teilmenge der Material Design Icons mit den Original-Codepunkten,
   u.a. fridge-variant-outline U+F15F9 (im Text als rohes UTF-8-Zeichen, nicht als \u-Escape, da > FFFF).
   Hochladen: `curl -F "data=@mqttdemo/mdi.ttf;filename=/mdi.ttf" http://<panel-ip>/edit`
   Weitere Icons aufnehmen (Codepunkte von pictogrammers.com/library/mdi):
   `./venv/bin/pyftsubset src/src/font/MaterialDesign-Webfont/materialdesignicons-webfont.ttf --unicodes=F15F9,F028F,... --output-file=mqttdemo/mdi.ttf --no-hinting`

Achtung: mit der kompletten MDI-Schrift (unten) meldete das Panel laufend `MQTT RCV: Out of memory` und
verlor Objekte (getestet 2026-11-27, Firmware noch ohne PSRAM-Patch). Die Meldung stand upstream auch für eine
volle Warteschlange; vermutlich war die GUI mit der großen Schrift zu lange blockiert. homectld lädt deshalb
beim Button *Dateien hochladen* eine Teilmenge mit den Symbolen seiner Seiten hoch (siehe doc/hasp.md).

Fuer den Betrieb mit homectld (Symbole benutzerkonfigurierbar) statt Teilmenge die komplette MDI-Schrift laden,
dieselbe Datei wie das Webinterface (7.4.47, 7447 Symbole, 1,3 MB; die FS-Partition hat ~12 MB):
`curl -F "data=@htdocs/mds/fonts/materialdesignicons-webfont.ttf;filename=/mdi.ttf" http://<panel-ip>/edit`
danach Panel neu starten. Name -> Codepunkt liefert `htdocs/mds/css/materialdesignicons.min.css`
(`.mdi-<name>::before{content:"\F15F9"}`), homectld kann die Tabelle beim Start einlesen und das Zeichen
als UTF-8 in den Text senden. Ungetestet: Heap-Bedarf der grossen Schrift und Renderzeit beim ersten Aufbau.

Symbol als Schalter: `btn` mit `toggle:true`, Hintergrund per `bg_opa:0` (und `bg_opa01..03` fuer die
Zustaende checked/pressed) unsichtbar, `text_color` = aus, `text_color01` = an. Der Zustand kommt mit `val`.
Das Standard-Theme gibt Containern (`obj`) einen Farbverlauf; `bg_grad_dir:0` schaltet ihn ab.
Die Seite selbst ist in jsonl als `{"page":3,"id":0,...}` ansprechbar (Hintergrund, `click:false` gegen p3b0-Meldungen).
Styling-Suffixe: zwei Ziffern am Attribut = Teil und Zustand, z.B. `bg_color10` = Indikator (Schalter
"an"-Bahn, Gauge-Zeiger), `bg_color20` = Knopf bzw. Haupt-Ticks, `bg_color01` = Zustand "checked".
Vor erneutem Senden `clearpage 3`, sonst werden die Objekte doppelt angelegt (macht das Skript).

`./mqtt-demo.sh` (im Ordner mqttdemo, Broker und Hostname stehen fest im Skript) fuehrt das alles vor;
jede Zeile laesst sich einzeln kopieren und ausfuehren.
