[← Back to the overview](../README.md)

<a id="freeathome"></a>
# free@home (Busch-Jaeger / ABB)

homectld uses the *free@home Local API* of the System Access Point 2.0 (SysAP, firmware 2.6 or newer): the
configuration of all devices by REST, their changes at once by the websocket of the SysAP (no polling), the
commands by REST. The SysAP stays the central unit, its switches, timers, scenes and actions work as before,
homectld is one more client (as for deCONZ). Wired and wireless devices are the same for it.

The devices are paired and named in the free@home app, homectld takes them over.

## Sensor types

| Type | Content |
|------|---------|
| `FHL` | lights: switch actuators (also plugs), dimming actuators, color temperature (brightness / white) |
| `FHB` | blinds: shutter, blind and attic window actuators (position 0 = closed, 100 = open as for HomeMatic) |
| `FHS` | sensors: wall switches (button events), movement detectors, weather station (brightness, rain, temperature, wind) |

The sensors appear in *Setup -> IO Setup* in the sections *free@home Lampen*, *free@home Rollläden* and
*free@home Sensoren* and are activated there like all others. The lights, blinds and sensors are actors /
sources of the light rules like the ones of deCONZ and HomeMatic: time control, light rules, shading, scenes,
HASP panel (see [Light and Blind Control](lights.md)).

## Setup

1. make switch `FREEATHOME = 1` in `Make.user` (or `make config`), `make install`: only then the configuration
   items *free@home* are offered and homectld connects to the SysAP
2. In the free@home app enable the Local API (*free@home Einstellungen -> Local API*), the app shows the user
   (a generated name) and its password
3. WEBIF *Setup -> Konfiguration -> free@home*: *SysAP Adresse* (host or IP of the SysAP, optional with port,
   https - with `http://` in front without TLS), *Benutzer* and *Passwort* of the Local API, then restart homectld
   (`systemctl restart homectld`). The certificate of the SysAP (self signed) isn't checked.

The log shows the connection: `FREEATHOME: Websocket at 'wss://192.168.200.50:443/fhapi/v1/api/ws'`, after the
connect `FREEATHOME: SysAP '...' (<uuid>), 42 channels`. The configuration is read at each connect of the websocket
and when the SysAP reports added / removed devices; the address of a channel (device serial / channel) is kept in
the table `fahchannels`.

## Lights

- switching, brightness (dimming actuators, 1 - 100 %), the color temperature of color temperature actuators
  (the SysAP knows 0 - 100 %, homectld maps it to 2200 - 6500 K)
- the light rules dim by the relative dimming of the actor (holding / releasing the button)
- no transition time per command (a parameter of the actor in free@home), the commands are not checked /
  repeated (unlike deCONZ, the SysAP reports reliably)
- no colors (RGB actuators: brightness only)

## Blinds

Position, stop when moving (a click on a moving blind stops it), the moving direction comes from the SysAP.
free@home counts the position the other way round (0 = open), homectld converts it.

## Sensors

- wall switches (switch / dimming sensor): the button events like deCONZ for the light rules -
  short *on* `1002`, short *off* `2002`, long *up* `1001` / release `1003`, long *down* `2001` / release `2003`.
  A wall switch bound to an actor in free@home switches it anyway (the SysAP), a rule is in addition.
- movement detector: motion (unit `mov`), for the light rules
- weather station: brightness `lx`, rain alarm, outside temperature `°C`, wind speed `m/s`

After a (re)connect the SysAP sends all datapoints once more (about 60 s), they update the sensors but are no
events for the light rules.
