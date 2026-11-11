# Denon / Marantz AV receiver

Bridge between a Denon (or Marantz) AV receiver of the AVR-X series (tested with the AVR-X3000) and
homectld via MQTT. The status is polled by HTTP (`/goform/...Xml...`), commands are sent by HTTP
(`/goform/formiPhoneAppDirect.xml?<command>`), the commands are the same ASCII commands as of the
telnet port 23 (`PWON`, `MV45`, `MUON`, `SINET`, `Z2ON`, ...). No library besides paho-mqtt is needed.

| Address | Sensor                | Kind   | Function                                                      |
|---------|-----------------------|--------|---------------------------------------------------------------|
| 0       | AVR                   | status | power (PWON / PWSTANDBY)                                      |
| 1       | AVR Lautstärke        | value  | display in dB                                                 |
| 5, 6    | AVR Lauter / Leiser   | status | push buttons, one step `-S` dB (default 1), never above `-V` (0 - 98, 80 = 0 dB, default 75) |
| 2       | AVR Stumm             | status | mute                                                          |
| 3       | AVR Quelle            | text   | source, the names as renamed at the receiver                  |
| 4       | AVR Surround          | text   | surround mode (shows the name of the receiver, sets MS<mode>) |
| 10 - 15 | AVR Zone 2 ...        | -      | power (Z2ON / Z2OFF), volume, mute, source, louder (14) / quieter (15) of zone 2 |

The sensors except the power ones are reported only while their zone is on (invalid 2 minutes after
power off). The power sensors are never restored by a restart of homectld (`"restore": false`).

Free commands (scripts, actions) to the command topic `homectld2mqtt/denon/in`:

    mosquitto_pub -t homectld2mqtt/denon/in -m '{"cmd": "MVUP"}'

## Receiver settings

- *Network → Network Standby* on (otherwise the receiver doesn't answer in standby and can't be switched on)
- fixed IP (DHCP reservation)

## Install

    Make.user: DENON = 1
    make install

adjust `/etc/default/denon2mqtt` (host), then

    denon.py -H <host> -s          # status of both zones
    systemctl restart denon

## Widget

`htmlwidget-denon.json` is an HTML widget snippet 'Denon AVR' looking like the front of the receiver (power, display
with source / surround mode / volume in dB, volume buttons and mute, the source buttons, zone 2 with the same).
It grows with the widget keeping the aspect ratio, but never gets smaller than its natural size (352 x 445 px).
Import it at *Setup → HTML Widgets → Import*, the
parameters are the sensors DENON:0x.. (default addresses) and the names of the 8 source buttons `s1` .. `s8` (the
names as renamed at the receiver), adjustable per widget. The volume buttons, power and mute need the sensors
to be active.
