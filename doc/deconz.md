[← Back to the overview](../README.md)

<a id="deconz"></a>
# deCONZ (Zigbee)

homectld uses [deCONZ](https://www.dresden-elektronik.de/funk/software/deconz.html) of dresden elektronik as
Zigbee gateway, e.g. with a ConBee or RaspBee stick. The devices are paired and managed in the web app
*Phoscon* of deCONZ, homectld takes them over by the REST API and gets their changes at once by the websocket
of deCONZ (no polling).

## Sensor types

| Type | Content |
|------|---------|
| `DZL` | lights (on / off, dimmable, color, color temperature) and switchable plugs |
| `DZLG` | groups of lights (the groups of Phoscon) |
| `DZS` | sensors: switches (button events), motion sensors, temperature, humidity, pressure, light level |

The sensors appear in *Setup -> IO Setup* in the sections *DECONZ Lampen*, *DECONZ Lampen Gruppe* and
*DECONZ Sensoren* and are activated there like all others.

## Setup

1. Install and start deCONZ (package or docker container), pair the devices in Phoscon.
   For the stick use the path below `/dev/serial/by-id/`, it doesn't change with other USB devices.
2. WEBIF *Setup -> Konfiguration -> DECONZ*: set *deCONZ HTTP URL* to the REST API of deCONZ,
   e.g. `http://192.168.200.101:8081`, leave *deCONZ API key* empty
3. In Phoscon unlock the gateway for apps (*Settings -> Gateway -> Advanced -> Authenticate app*) and restart
   homectld within the 60 seconds: `systemctl restart homectld`. homectld registers itself, stores the key
   as *deCONZ API key* and reads all devices. Without unlock the log shows
   `Initiale Registrierung bei deconz ... fehlgeschlagen`
4. Activate the wanted sensors in *Setup -> IO Setup*

**Note:** the websocket of deCONZ is expected at `192.168.200.101` port `443` (hardcoded in `deconz.c`, not yet
taken from the configuration), the websocket port of deCONZ has to be set to 443 resp. the address adjusted there.

If deCONZ forgets the API key after a restart (it isn't stored in its database), it can be entered there by hand:
```
sqlite3 zll.db
  insert into auth (apikey,createdate,lastusedate,devicetype,useragent)
     values('<key>','2021-12-25T07:23:29','2021-12-25T07:23:29','homectld','homectld');
```

## Devices

- at each start of homectld and each (re)connect of the websocket all lights, groups and sensors are read
  from deCONZ; new devices are added, the name of Phoscon is taken each time (a renamed device gets its new
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
