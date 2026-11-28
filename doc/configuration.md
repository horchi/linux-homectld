[← Back to the overview](../README.md)

<a id="configuration"></a>
# Configuration

The settings of *Setup -> Konfiguration*, one section per category (the buttons on the left). Every card shows the
title, the name of the item (e.g. `interval`) and a short description; the `?` behind the title opens the explanation
of the item in a dialog. The items are stored in the table `config` of the database, a change takes effect when it is
stored (*Speichern*), except where a restart of homectld is mentioned. The defaults are the ones of a fresh installation.

The settings of some categories are explained with their topic, they are only listed here:
[GPS](gps.md), [Garmin](garmin.md), [HASP Panel](hasp.md), [deCONZ](deconz.md), [free@home](freeathome.md).

## Daemon

- **Titel** (`instanceName`, default `Home Control`): name of the installation, shown as title of the browser tab, in
  the online / offline state of the WEB interface and in the mails of the S 3200.
- **Application Icon** (`instanceIcon`, default `home.png`): favicon of the WEB interface (and icon of the iOS home
  screen app), one of the PNG files in `htdocs/img/`.
- **Breitengrad** / **Längengrad** (`latitude` / `longitude`, decimal degrees, default 50.3 / 8.79): position of the
  installation for the sun times and the weather, see [sun times](lights.md#sun-times).
- **Standort aus GPS** (`gpsLocation`): take the position from the GPS receiver instead of latitude / longitude (useful
  in a motorhome); without a current position the last known one is used.
- **Intervall der Aufzeichung** (`interval`, seconds, default 60): the interval of the main loop of the daemon, the
  values of the sensors are stored in the database (table `samples`) in this interval, the base of the charts.
- **Historie [Tage]** (`aggregateHistory`, days, default 365): samples older than this are aggregated, 0 switches the
  aggregation off (all samples are kept). The aggregation runs every night at 1:00.
- **Aggregate Interval der historisierten Daten [m]** (`aggregateInterval`, minutes, default 15): the aggregation builds
  one sample (the average) per sensor and interval from the samples older than *Historie* and deletes the single samples.
  The samples of the GPS tours are not aggregated.
- **Openweathermap API Key** (`openWeatherApiKey`): key of a (free) account at
  [openweathermap.org](https://openweathermap.org/api), enables the weather sensors (`WEA`, forecast for the position
  above); empty = no weather.
- **Weather Refresh Interval [m]** (`weatherInterval`, minutes, default 15): how often the weather is fetched.
- **Log Eloquence** (`eloquence`, default `Info`): which messages go to the log (syslog), one switch per topic: `Info`,
  `Detail`, `Debug` / `Debug2`, `WebSock`, `Mqtt`, `Db`, `Deconz`, `Weather`, `HomeMatic`, `Lua`, `Lmc`, `Script`,
  `Gps`, ... (the `Debug...` ones are very verbose). Errors are always logged. The option `-l` at the start of
  homectld has priority.
- **Ausgeblendete Kategorien** (`hiddenCategories`): categories of the setup page *Konfiguration* which are not needed
  (e.g. `GPS`, `Garmin`, `Heizung S 3200`) get no button, their settings are kept. `Daemon` and `User Defined` can't be
  hidden.

## GPS

Tour recording (`gpsTour...`), u-blox receiver (`gpsUblox...`) and dish alignment (`gpsSat...`), see
[GPS Tours](gps.md#recording) and [dish alignment](gps.md#dish-alignment).

## Garmin

*Distanz aus dem Track ab Abweichung* (`garminTrackDeviation`), see [Garmin Activities](garmin.md).

## WEB Interface

- **Port des Web Interfaces** (`webPort`, default 61109): TCP port of the WEB interface, a change needs a restart of
  homectld (`systemctl restart homectld`), then use the new port in the URL.
- **Use SSL for WebInterface** (`webSSL`): HTTPS instead of HTTP (and `wss://` for the websocket). The certificate is
  read from `/etc/homectld/cert.pem`, its key from `/etc/homectld/cert.key`; needs a restart of homectld.
- **Farbschema** (`style`, default `dark`): the color scheme, one of the files `htdocs/stylesheet-<name>.css`
  (`dark`, `light`, `blue`, `yellow`).
- **Background image** (`background`): image behind the pages of the WEB interface, from `htdocs/img/backgrounds/`;
  empty = none.
- **Schematische Darstellung** (`schema`, default `default.svg`): the picture of the page *Schema* of the main menu
  (`htdocs/img/schema/schema-<name>.png` or `.svg`), the values are placed on it with *Setup Schema* in the burger menu;
  empty = no page *Schema*.
- **Hintergrundfarbe des Schemas** (`schemaBackground`): CSS color behind the picture of the schema (visible with
  transparent pictures like the SVGs), e.g. `#202020`; empty = the one of the color scheme. Can also be set in the schema
  setup (*Hintergrund*).
- **Liste anzeigen** (`showList`): shows the page *List* in the main menu (all values of the dashboard as list).
- **Große Dashboard Symbole** (`bigDashSymbols`): see [URL parameters](webif.md#url-parameters).
- **Windy App Spot ID** / **Windy App ID** (`windyAppSpotID` / `windyAppID`): see [Windy App](windy.md).

## MQTT

- **MQTT Broker Url** (`mqttUrl`, default `tcp://localhost:1883`): the MQTT broker (mosquitto) all services
  communicate over, e.g. `tcp://192.168.200.101:1883`. Without it most interfaces (one wire, converters, HomeMatic,
  S 3200, GPS, ...) don't work.
- **User** / **Password** (`mqttUser` / `mqttPassword`): login at the broker, empty = anonymous.
- **Zusätzliche sensor Topics** (`mqttSensorTopics`): see [MQTT Converter](mqtt-converter.md#how-it-works).
- **MQTT Topic des Arduino Interface** (`arduinoTopic`, default `homectld2mqtt/arduino`): base topic of an Arduino
  I/O board (sketch in `arduino/`); its values are read from `<topic>/out`, the interval is sent to `<topic>/in`.
  Empty = no Arduino.
- **Intervall der Arduino Messungen** (`arduinoInterval`, seconds, default 10): measuring interval sent to the Arduino.

## Home Automation Interface

homectld can publish the values of the sensors to MQTT for another home automation system (e.g. Home Assistant,
openHAB, Node-RED). Requires the *MQTT Broker Url*.

- **MQTT Data Topic Name** (`mqttHaDataTopic`): the topic for the values, empty = off. The placeholders select the
  kind of the interface:
  - with `<NAME>` (e.g. `homectld2mqtt/<TYPE>/<NAME>/state`): one topic per sensor (retained), `<NAME>` is the name of
    the sensor (umlauts replaced, spaces as `_`), `<TYPE>` is `light` or `sensor`
  - with `<GROUP>`: one topic per group of the sensors, the values of the group as JSON
  - without placeholder: one topic, all values as JSON grouped by the groups
- **Config Topic** (`mqttHaHaveConfigTopic`): additionally send the discovery messages of Home Assistant
  (`homeassistant/<sensor|light>/<instance>/<name>/config`, retained), Home Assistant then creates the entities by
  itself; lights can be switched via `homectld2mqtt/light/<name>/set`. Only with one topic per sensor (`<NAME>`).

## HASP Panel

The MQTT topic and the idle times of the openHASP panel, see [HASP Panel](hasp.md#prerequisites).

## Mail

Mails are sent by a script, the receivers are comma separated lists.

- **Mail Benachrichtigung** (`mail`): switches all mails on / off: the alert mails (*Setup -> Alerts*, sent to the
  receivers of the alert) and the mails of the S 3200 ([mails](p4.md#further-functions)). The test mails (*Setup ->
  Alerts*, *Setup -> Commands*) are sent anyway, to check the mail setup.
- **Skript zum senden von Mails** (`mailScript`, default `/usr/local/bin/mail.sh`): called with the arguments
  subject, body, content type and receivers. The delivered `mail.sh` uses the mail command of the package
  *GNU Mailutils*, that needs a working mail transport (e.g. `msmtp` with `/etc/msmtprc`).
- **Status Mail Empfänger** (`stateMailTo`): receivers of the state mails of the S 3200 and of the command *Test Mail*
  (*Setup -> Commands*), use it to test the mail setup.
- **Fehler Mail Empfänger** (`errorMailTo`): receivers of the fault mails of the S 3200.
- **URL der Visualisierung** (`webUrl`): URL of the WEB interface for the mails, inserted by `%weburl%` in the texts of
  the alert mails. Set automatically to `http(s)://<first IP>:<webPort>` if it is empty or doesn't contain the port.

## DECONZ

*deCONZ HTTP URL* and *deCONZ API key*, see [deCONZ setup](deconz.md#setup) (make switch `DECONZ`).

## free@home

*SysAP Adresse*, *Benutzer* and *Passwort* of the Local API, see [free@home setup](freeathome.md#setup)
(make switch `FREEATHOME`, a change needs a restart of homectld).

## HomeMatic

- **Anbindung** (`homeMaticBridge`): see [HomeMatic](homematic.md).
- **MQTT Topic des Dienstes hmmqtt** (`homeMaticTopic`, default `homematic`): base topic of the service `hmmqtt`, has to
  match its option `-T` (`/etc/default/hmmqtt`); only used with *Anbindung* `hmmqtt`.

## Heizung S 3200

The connection, the page, mails and time synchronization are explained in [Fröling S 3200](p4.md), here the
remaining items:

- **Status Mail bei den Zuständen** (`p4StateMailStates`, default `0,1,3,19`): numbers of the states of the heating
  that send a state mail when they are reached, comma separated (0 Störung, 1 Brenner aus, 3 Heizen,
  19 Betriebsbereit); empty = a mail on every change. Needs *Status- und Fehler-Mails*.
- **Pellet Verbrauch / Stoker Stunde [kg]** (`p4ConsumptionPerHour`): pellets per hour of the stoker, used to estimate
  the consumption since the last delivery (page *Pellets*); 0 = calculated from the last two deliveries.
- **Hintergrundfarbe des Schemas** (`p4SchemaBackground`): CSS color behind the plant picture of the tab *Schema*,
  empty = the one of the color scheme, also in the schema setup (*Hintergrund*).
- **Typ der Heizung** (`p4HeatingType`, default `P4`): picture of the heating type
  (`htdocs/img/p4/type/heating-<type>.png`), shown as state of the heating if there is no own animated picture for the
  current state.
- **MQTT Topic des Dienstes p4mqttd** (`p4Topic`, default `p4`): base topic of `p4mqttd`, has to match its option `-T`.
- **Intervall der Messwerte [s]** (`p4PollInterval`, default 60): the active sensors of the heating are read by
  `p4mqttd` in this interval.
- **Intervall der Status Prüfung [s]** (`p4StateInterval`, default 10): interval in which `p4mqttd` reads the state of
  the heating (state changes, faults).
- **Zeitsynchronisation bei Abweichung über [s]** (`p4MaxTimeLeak`, default 10): minimal drift of the clock of the
  heating for the time synchronization (*Zeitsynchronisation*).
- Buttons (need the right *settings*), `p4mqttd` answers no other requests while reading:
  - **Menü neu lesen**: reads the menu structure of the service menu and the in/outputs (pumps, mixers, ...) from the
    control again, e.g. after a firmware update (takes some minutes).
  - **Sensoren neu lesen**: reads the list of the values (temperatures, level, ...) again.
  - **Zeitprogramme neu lesen**: takes over the time programs of the control (if they were changed there).

  If reading fails, `p4mqttd` keeps the lists read before.

### Dynamic pellet filling

Sets the start times of the 1st and 2nd filling of the pellet container in the control automatically (replaces the
script `DynamischePelletaustragung.py`). The decision runs every minute with the state of the heating and the level of
the container; state and the last changes are shown on the page *Pellets*. Needs write access of `p4mqttd` (not in
read only mode).

- **Dynamische Pelletbefüllung** (`p4FillEnabled`): switches the function on.
- **Befüllung: Standardzeit 1. Befüllung** / **2. Befüllung** (`p4FillTime1` / `p4FillTime2`, `HH:MM`, default
  `16:00` / `00:00`): the times set every day, `00:00` = this filling off. An invalid time switches the function off
  (log).
- **Befüllung: sofort unter Füllstand [%]** (`p4FillMinLevel`, default 20): if the heating is *Betriebsbereit* and the
  level is below this value, the filling is started at once (in 5 minutes).
- **Befüllung: sofort nach Wartezeit [min]** (`p4FillHold`, default 5): how long *Betriebsbereit* and the low level
  have to last before the immediate filling.
- **Befüllung: Leerfahrt nach [Tage]** (`p4FillDaysToZero`, default 8): if the container wasn't empty for this many
  days, both times are set to `00:00` until it is empty (to burn the fines at the bottom); 0 = never.
- **Befüllung: verschieben bei Zuständen** (`p4FillDelayStates`, default `Vorbereitung,Vorwärmen,Zünden,Heizen`):
  if a filling is due within 15 minutes in one of these states, it is delayed by 30 minutes (texts of the states,
  comma separated).
- **Befüllung: Adresse 1. Befüllung** / **2. Befüllung** (`p4FillAddress1` / `p4FillAddress2`, default 60 / 516): the
  addresses (decimal) of the parameters *Start der 1. / 2. Pelletsbefüllung* in the service menu.
- **Befüllung: Sensor Füllstand** (`p4FillLevelAddress`, default 113): address (decimal) of the `P4VA` sensor
  *Füllstand im Pelletsbehälter*, the sensor has to be active.

## VDR

- **VDRs (Video Disk Recorder)** (`vdr`): one entry per VDR as `Name;Host:Port;MAC`, enables the page *VDR* (remote
  control) in the main menu. `Host:Port` is the osd2web plugin of the VDR (host name or IP), e.g.
  `Wohnzimmer;vdr:4444;00:1e:06:43:48:8e`. The MAC is used to switch the VDR on by Wake-on-LAN; it may be missing,
  then it is determined on storing while the VDR is running. With `;OFF` at the end the remote control may also switch
  the VDR off, otherwise only on.

## Logitech Media Server

The page *Music* of the main menu, the control of a player of the Logitech / Lyrion Media Server.

- **LMC Host** (`lmcHost`): host of the media server, empty = no page *Music*. The cover images are loaded from port
  9000 of this host.
- **LMC Port** (`lmcPort`, default 9090): the CLI port of the media server.
- **MAC of LMC Player** (`lmcPlayerMac`): MAC of the player to control (the player id of the media server); without it
  the page doesn't connect.
