
# Alpicool / MANTUM BLE Fridge

MQTT integration for Alpicool-platform compressor fridges (MANTUM IceCube,
BrassMonkey, Vevor, Iceco and other rebrands sharing the same OEM hardware).

Communication via BLE service `FFE0` / characteristic `FFE1`.

## Dependencies

```
apt install python3-bleak python3-paho-mqtt
```

## Installation

```
make install
```

Adjust MAC and settings in `/etc/default/alpicool2mqtt`.

## Find the device MAC

```
alpicool.py -D
```

Scans for BLE devices advertising the FFE0 service. Put the fridge in
pairing/discoverable mode first (usually hold the power button 3 s).

## Verify protocol on first run

The Alpicool BLE protocol is reverse-engineered. On first use, check the
raw response frame to confirm byte offsets match your firmware version:

```
alpicool.py -M AA:BB:CC:DD:EE:FF -s -v 3
```

Expected output includes `Raw frame: fefe06 ...` — compare against the
layout documented in `alpicool.py` (class `AlpicoolStatus`).

## Usage

```
alpicool.py [-h] [-i [I]] [-m [M]] [-p [P]] [-v [V]] [-l]
            [-T [T]] [-t [T]] [-M [M]] [-s] [-D]

  -i [I]   interval [seconds] (default 30)
  -m [M]   MQTT host
  -p [P]   MQTT port (default 1883)
  -v [V]   verbosity level 0-3 (default 0)
  -l       log to syslog (default: console)
  -T [T]   MQTT topic (default: homectld2mqtt/alpicool)
  -t [T]   sensor type string (default: ALPICOOL)
  -M [M]   device MAC address
  -s       show current status and exit
  -D       discover Alpicool/FFE0 devices
```

## MQTT

Published on `topic` (JSON per value):

| address | title       | kind   | writable | unit |
|---------|-------------|--------|----------|------|
| 0       | Power       | status | yes      |      |
| 1       | Target Temp | value  | yes      | °C   |
| 2       | Actual Temp | value  | no       | °C   |
| 3       | Battery     | value  | no       | V    |
| 4       | Mode        | text   | yes      | Max/Eco |

Control commands are received on `topic/in`:

```json
{"address": 1, "value": -5}
{"address": 0, "value": 1}
{"address": 4, "value": "Eco"}
```

## References

- Protocol: <https://github.com/klightspeed/BrassMonkeyFridgeMonitor>
