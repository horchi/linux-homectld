[← Back to the overview](../README.md)

<a id="deconz"></a>
# deCONZ (Zigbee)

homectld uses [deCONZ](https://www.dresden-elektronik.de/funk/software/deconz.html) of dresden elektronik as
Zigbee gateway, e.g. with a ConBee or RaspBee stick. The devices are paired, renamed and removed on the WEBIF
page *Setup -> Zigbee* (or in the web app *Phoscon* of deCONZ), homectld takes them over by the REST API and
gets their changes at once by the websocket of deCONZ (no polling).

## Sensor types

| Type | Content |
|------|---------|
| `DZL` | lights (on / off, dimmable, color, color temperature) and switchable plugs |
| `DZLG` | groups of lights (the groups of Phoscon) |
| `DZS` | sensors: switches (button events), motion sensors, temperature, humidity, pressure, light level |

The sensors appear in *Setup -> IO Setup* in the sections *DECONZ Lampen*, *DECONZ Lampen Gruppe* and
*DECONZ Sensoren* and are activated there like all others.

## Setup

1. Install and start deCONZ (package or docker container).
   For the stick use the path below `/dev/serial/by-id/`, it doesn't change with other USB devices.
2. WEBIF *Setup -> Konfiguration -> DECONZ*: set *deCONZ HTTP URL* to host and port of the REST API of deCONZ,
   e.g. `192.168.200.101:8081` (an `http://` in front is skipped), leave *deCONZ API key* empty
3. In Phoscon unlock the gateway for apps (*Settings -> Gateway -> Advanced -> Authenticate app*) and press
   *API-Key holen* on the page *Setup -> Zigbee* within the 60 seconds (or restart homectld within them:
   `systemctl restart homectld`). homectld registers itself, stores the key as *deCONZ API key*, reads all
   devices and connects to the websocket. Without unlock the page resp. the log shows the error of deCONZ
   (`link button not pressed`). *Neu registrieren* gets a new key the same way (e.g. after deCONZ lost it).
4. Pair the devices on the page *Setup -> Zigbee* (see below) or in Phoscon, activate the sensors of devices
   paired in Phoscon in *Setup -> IO Setup*

The events (buttons, motion, values, lights switched elsewhere) come by the websocket of deCONZ. It runs on the
host of the *deCONZ HTTP URL*, the port is reported by deCONZ itself (its config `websocketport`, set in deCONZ
resp. its docker container), if it can't be queried 443 is taken. The log shows it at the start:
`DECONZ: Websocket at '192.168.200.101:443'`.

If deCONZ forgets the API key after a restart (it isn't stored in its database), it can be entered there by hand:
```
sqlite3 zll.db
  insert into auth (apikey,createdate,lastusedate,devicetype,useragent)
     values('<key>','2021-12-25T07:23:29','2021-12-25T07:23:29','homectld','homectld');
```

Docker: stop the container properly before removing / recreating it, deCONZ stores its database on SIGTERM:
```
docker stop -t 60 deconz
docker rm deconz
```
A `docker rm -f` alone kills deCONZ at once, everything changed since its last store is lost - after the start
deCONZ reports old values of the sensors again (e.g. the temperature of days ago with its old `lastupdated`) until
each sensor reports the next time, and homectld takes these old values when it reconnects.

## Setup page 'Zigbee'

*Setup -> Zigbee* (admin only, offered if the *deCONZ HTTP URL* is set):

- **gateway**: URL, state of the websocket, name, version of deCONZ and of the stick firmware, channel and
  PAN ID of the Zigbee network, the API key (*API-Key holen* / *Neu registrieren*, see above)
- **pairing**: *Suche starten* opens the Zigbee network for new lights and sensors (as the search of Phoscon,
  its duration is set by deCONZ, countdown, *Suche beenden* closes it at once). Bring the device into its pairing mode then.
  A new device comes at once (websocket event `added` of deCONZ, while searching `lights/new` and
  `sensors/new` are checked every 5 s too), it is marked as *neu* at the top of the list (also a device first seen by homectld within the last 5 minutes otherwise, e.g. paired in Phoscon or taken 60 s after a restart of deCONZ) and its sensors are
  activated in homectld at once (devices paired in Phoscon arrive the same way, but inactive as before)
- **devices**: all devices of deCONZ grouped by their MAC, with manufacturer, model, software version,
  reachable, battery and last seen; per resource of the device (e.g. temperature, humidity and pressure of an
  Aqara sensor) the kind, the name at deCONZ (editable, Enter / leaving the field stores it at deCONZ, max. 32
  characters, homectld takes the new name), the sensor of homectld (`DZS:0x12`) with *aktiv* and its own title
  and the current value. The filter searches names, titles, models and MACs
- **removing** (*Entfernen*): deletes all resources of the device at deCONZ and their sensors in homectld
  (valuefact; the recorded values stay, dashboards, rules and Lua scripts using them have to be adapted by
  hand); a light can additionally leave the network and reset to factory defaults (option of the dialog).
  A device deleted in Phoscon keeps its sensor in homectld (delete it in the sensor setup if wanted)

## Devices

- at each start of homectld and each (re)connect of the websocket all lights, groups and sensors are read
  from deCONZ; new devices are added (also at once by the event `added` of deCONZ), the name of Phoscon is taken each time (a renamed device gets its new
  name, an own title of the sensor setup stays)
- internal resources of deCONZ are skipped: *Daylight*, *CLIPDaylightOffset* (day segments of Phoscon) and
  *Configuration tool* (the coordinator itself)
- the info dialog of the dashboard (click on the title of the widget) and the *Info* button of the sensor setup
  show the MAC of the device as the deCONZ log shows it (e.g. `00158D0002310239`) and in the tab *JSON* the
  last event of deCONZ
- devices on battery show the battery charge (info dialog, column *Batterie* of the sensor setup), also from
  the battery events of deCONZ and for switches already at the init

## Lights and groups

- switch, dim (long press on the widget: brightness slider) and color (long press on a color light: color
  wheel); RGB+CCT lights additionally have the mode *Weiß* with their own white LEDs (slider warm <-> cold
  in K and brightness), lights reporting a nonsense range of the color temperature get 2200 - 6500 K
- a group is on as soon as one of its lights is on (`any_on` of deCONZ)
- time control, rules of switches / motion sensors and the off timers: see
  [Light and Blind Control](lights.md)
- during a soft transition (switching on / off with fading) deCONZ reports only some intermediate values,
  the end value follows some seconds later - the widget may show a wrong brightness for a short time

## Sensors

- **switches** (`ZHASwitch`): the value is the button event of deCONZ (e.g. `1002` = button 1 short release),
  the codes are taken by the rules of the switch; at the init the last event is not taken (it would trigger
  the rules again), only the battery
- **motion sensors** (`ZHAPresence`): the presence is the state of the sensor (`value` stays 0), in HTML
  widgets e.g. `{{DZS:0x23.state ? ja : nein}}`, for the rules the trigger *Bewegung*
- **temperature, humidity, pressure, light level**: value with unit °C, %, hPa, lx
- a measuring sensor (temperature, humidity, pressure) without data for 3 hours gets invalid (faded), also at
  the init if the data at deCONZ (`lastupdated`) are older (e.g. empty battery); the Aqara sensors report hourly

[← Back to the overview](../README.md)
