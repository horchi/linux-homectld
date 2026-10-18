

# u-blox 7 USB GPS Monitor

MQTT integration for u-blox 7 USB GPS receivers (and compatible NMEA-0183 serial
hardware) designed for location and time sync tracking.

## Receivers

The script reads NMEA 0183 from a serial port, that is what practically every GPS receiver
delivers: USB pucks / dongles (u-blox 7 / 8 / M8 / M9 like VK-162, VK-172, BU-353, GlobalSat),
GPS modules on a USB-serial adapter or a UART of the board (u-blox NEO-6M / 7M / M8N, MediaTek
MTK3339 / Adafruit Ultimate GPS, Quectel L76 / L80, SiRF). Needed are the sentences `RMC`
(position, speed, date, fix state) and `GGA` (satellites, altitude), both are in the default
output of all of them; the talker (`GP` GPS only, `GN` combined GPS / GLONASS / Galileo, `GL`,
`GA`, `GB`) does not matter.

- USB receivers show up as `/dev/ttyACMx` (USB CDC, the baud rate is ignored) or `/dev/ttyUSBx`
  (PL2303 / CP2102 / FTDI adapter, the baud rate must match the receiver: 9600 is the usual
  default, 4800 for old SiRF, 38400 / 115200 for some newer modules; option `-b`)
- the udev rule below maps the u-blox 7, other receivers need their own vendor / product id
  (`lsusb`) or the device is given with `-d`
- the receiver settings (Static Hold, Automotive) are u-blox specific (UBX protocol); other chips
  ignore the binary request, the log shows `CFG-NAV5 not acknowledged`, the position data is not
  affected. MediaTek has a comparable 'static navigation' (PMTK386), not implemented
- multi-GNSS receivers (u-blox 8 and newer) see more satellites and drift less at standstill,
  the u-blox 7 uses GPS only (GLONASS alternatively, not at the same time)
- the update rate of the receiver (usually 1 Hz) is irrelevant, the script publishes every `-i` seconds
- sensor 11 *Heading* is the course over ground of the last movement (>= 5 km/h) and is kept while
  standing: the axis of the vehicle for the dish alignment of the WEBIF

Communication via localized serial port abstraction using custom UDev rules.

## Dependencies

apt -y install python3-serial python3-nmea2 python3-paho-mqtt

## Installation

```
make install
```

Adjust parameters and interval settings in `/etc/default/gps2mqtt` (`GPS_DEVICE` for the serial
device and baud rate, e.g. `-d /dev/ttyS1 -b 38400` for a module on the UART of the board).

## Setup the Device Port

USB receivers get their port number (`ttyACMx`, `ttyUSBx`) by the plug order, therefore the script
reads a persistent symlink (`/dev/ttyGps`) managed via UDev by default. Ensure your
`/etc/udev/rules.d/99-gps.rules` contains (vendor / product id of the u-blox 7, see `lsusb`):

```text
SUBSYSTEM=="tty", ATTRS{idVendor}=="1546", ATTRS{idProduct}=="01a7", SYMLINK+="ttyGps", MODE="0660", GROUP="dialout"
```

Apply the interface routing instantly via terminal:
```bash
sudo udevadm control --reload-rules && sudo udevadm trigger
```

A module on a UART of the board (e.g. `/dev/ttyS1` on the ODROID M1 header, see
`contrib/armbian/README.md`) has a fixed name, the rule is not needed then: set the port with
`GPS_DEVICE="-d /dev/ttyS1 -b 38400"` in `/etc/default/gps2mqtt`.

## Verify NMEA Data stream on first run

On first use, check if the parser is communicating properly with the u-blox
hardware by verifying the current telemetry output matrix:

```bash
gpsmqtt.py -s
```

Expected output includes the individual elements (`status: Fix`, `latitude`, `longitude`, etc.)
evaluated directly from the raw NMEA sentences.

## Usage

```
gpsmqtt.py [-h] [-i [I]] [-d [D]] [-b [B]] [-m [M]] [-p [P]] [-v [V]] [-l]
           [-T [T]] [-c [C]] [-t [T]] [-s]

   -i [I] interval [seconds] (default 30)
   -d [D] serial device (default /dev/ttyGps)
   -b [B] baud rate (default 9600, ignored by USB receivers)
   -m [M] MQTT host (default: localhost)
   -p [P] MQTT port (default 1883)
   -v [V] verbosity level 0-3 (default 0)
   -l log to syslog (default: console)
   -T [T] MQTT topic (default: homectld2mqtt/gps)
   -c [C] MQTT command topic (default: <topic>/in)
   -t [T] sensor type string (default: GPS)
   -s show current status and exit
```

## MQTT

Published on `topic` (Sequential single-value JSON bursts per sensor):


| address | title      | kind   |  unit |
|---------|------------|--------|-------|
| 0       | status     | status |       |
| 1       | latitude   | value  |  °    |
| 2       | longitude  | value  |  °    |
| 3       | speed      | value  |  km/h |
| 4       | satellites | value  |       |
| 5       | altitude   | value  |  m    |
| 6       | time_utc   | text   |       |
| 7       | date_utc   | text   |       |
| 8       | town       | text   |       |

Each sensor element is sequentially serialized into an individual payload string:

```json
{"address": 3, "type": "GPS", "title": "speed", "value": 0.4, "unit": "km/h"}
{"address": 1, "type": "GPS", "title": "latitude", "value": 50.300876, "unit": "°"}
```

## Receiver settings (u-blox)

The script registers its command topic (`-c`, default `<topic>/in`) at homectld with an `init`
message. homectld answers with the receiver settings of *Setup -> Konfiguration -> GPS* and sends
them again at its start and whenever they change:

```
{ "action": "config", "staticHold": <cm/s>, "dynModel": 0|4, "galileo": true|false }
```

The script writes them into the receiver (UBX CFG-NAV5) each time the serial port is opened and on
every message: *Static Hold* freezes the position below the given speed (no drift at standstill),
*dynModel* 4 is the dynamic platform model 'automotive' (0 = 'portable', the factory default).
*galileo* enables Galileo on a u-blox 8 (UBX CFG-GNSS: GPS + Galileo + GLONASS, SBAS / QZSS as
augmentation, BeiDou off - the M8 tracks three major systems at a time); the receiver restarts its
navigation after the change. The u-blox 7 has no Galileo and answers with NAK (log: `CFG-GNSS ... not
acknowledged`), nothing else changes.
The u-blox 7 has no flash, the settings live in its battery buffered RAM only - that is why they are
re-applied on every port open. Check the result in the log (`-v 1`): `u-blox: CFG-NAV5 set, ...`.
