# hmmqtt – HomeMatic (BidCos-RF) via CUL to MQTT

Classic HomeMatic devices (BidCos-RF, not HomeMatic IP) without a CCU: a CUL stick (CC1101, 868 MHz,
culfw) on the host. The service `hmmqtt` talks BidCos with the devices and is controlled by MQTT.

Supported device types so far: blind actuators `HM-LC-Bl1PBU-FM`, `HM-LC-Bl1-FM`, `HM-LC-Bl1-SM`,
`HM-LC-Bl1-PB-FM`. More types are added as classes derived from `HmDevice` (`devices.c`).

## Build / install

In `Make.user`:

```
HOMEMATIC = 1
```

`make install` then builds and installs `hmmqtt`, the systemd unit `hmmqtt.service`,
`/etc/default/hmmqtt` (options) and `/var/lib/hmmqtt/devices.json` (device list, only if missing).

## CUL firmware (culfw)

The busware CUL868 is delivered without firmware. Flash culfw (target `CUL_V3`, ATmega32U4):

```
apt install dfu-programmer
# plug in the CUL with the HWB button pressed -> bootloader
dfu-programmer atmega32u4 erase
dfu-programmer atmega32u4 flash CUL_V3.hex
dfu-programmer atmega32u4 reset
```

Check: `screen /dev/ttyACM0` (or similar), `V` answers e.g. `V 1.67 CUL868`.

## Options (`/etc/default/hmmqtt`)

| Option | |
|---|---|
| `-d <device>` | the CUL, use the fixed name in `/dev/serial/by-id/` |
| `-i <hmId>` | address of the central, take the one of the former CCU (`/etc/config/ids`, `BidCoS-Address`) – the devices paired to the CCU keep working |
| `-f <file>` | device list (default `/var/lib/hmmqtt/devices.json`) |
| `-u <url>` | MQTT url (default `tcp://localhost:1883`) |
| `-T <topic>` | MQTT base topic (default `homematic`) |
| `-s` | sniff: only log the received frames (no MQTT, no sending) – for a first test while the CCU still runs |
| `-l <level>` / `-t` / `-n` | log level / log to the terminal / don't fork |

Test in the foreground: `hmmqtt -n -t -s -d /dev/serial/by-id/... -i 4F5EE7`

## MQTT

Channel = `<serial>:<channel>`, e.g. `OEQ1853240:1`.

Service → clients (retained):

| Topic | Content |
|---|---|
| `homematic/status` | `{"state":"online","cul":"V 1.67 CUL868","hmId":"4F5EE7"}` |
| `homematic/devices` | `[{"serial","type","model","rfAddress","firmware","lastSeen","rssi","channels":[{"channel","kind"}]}]` |
| `homematic/state/<channel>` | blind: `{"level":76.0,"working":true,"direction":"down","rssi":-68,"ts":...}` (level in %) |
| `homematic/pairing` | `{"active":true,"remaining":45}` |

Service → clients:

| Topic | Content |
|---|---|
| `homematic/event/paired` | `{"serial","type","rfAddress","result":"ok\|unsupported\|failed","text"}` |
| `homematic/event/removed` | `{"serial","result":"ok\|failed"}` |

Clients → service:

| Topic | Content |
|---|---|
| `homematic/set/<channel>` | blind: `{"level":50}` or `{"stop":true}` |
| `homematic/get/<channel>` | status request, the answer comes via `state/...` |
| `homematic/pair` | `{"enable":true,"duration":60}` / `{"enable":false}` |
| `homematic/remove` | `{"serial":"OEQ1853240"}` |

## Pairing

`homematic/pair` enables pairing (60 s by default). Press the config button of the device: it sends its
device info, the service writes itself as central into the device (list 0, registers `0A`-`0C`), adds it
to the device list and publishes `event/paired`. A device of an unknown type is reported as `unsupported`.

`homematic/remove` clears the central in the device and removes it from the list.

## Notes

- Each frame expecting an answer is repeated up to 3 times (800 ms each).
- The CUL may send 1 % of the time (duty cycle), `LOVF` in the log means the limit is reached.
- Never run the CUL and the CCU with the same hmId at the same time (both would answer) – use `-s` for tests.
- Protocol reference: FHEM `10_CUL_HM.pm`, `HMConfig.pm`.
