

# u-blox 7 USB GPS Monitor

MQTT integration for u-blox 7 USB GPS receivers (and compatible NMEA-0183 serial
hardware) designed for location and time sync tracking.

Communication via localized serial port abstraction using custom UDev rules.

## Dependencies

apt -y install python3-serial python3-nmea2 python3-paho-mqtt

## Installation

```
make install
```

Adjust parameters and interval settings in `/etc/default/gps2mqtt`.

## Setup the Device Port

The daemon communicates over a persistent hardware symlink mapping (`/dev/ttyGps`)
managed via UDev. Ensure your `/etc/udev/rules.d/99-gps.rules` contains:

```text
SUBSYSTEM=="tty", ATTRS{idVendor}=="1546", ATTRS{idProduct}=="01a7", SYMLINK+="ttyGps", MODE="0660", GROUP="dialout"
```

Apply the interface routing instantly via terminal:
```bash
sudo udevadm control --reload-rules && sudo udevadm trigger
```

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
gpsmqtt.py [-h] [-i [I]] [-m [M]] [-p [P]] [-v [V]] [-l]
           [-T [T]] [-t [T]] [-s]

   -i [I] interval [seconds] (default 30)
   -m [M] MQTT host (default: localhost)
   -p [P] MQTT port (default 1883)
   -v [V] verbosity level 0-3 (default 0)
   -l log to syslog (default: console)
   -T [T] MQTT topic (default: homectld2mqtt/gps)
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

Each sensor element is sequentially serialized into an individual payload string:

```json
{"address": 3, "type": "GPS", "title": "speed", "value": 0.4, "unit": "km/h"}
{"address": 1, "type": "GPS", "title": "latitude", "value": 50.300876, "unit": "°"}
```
