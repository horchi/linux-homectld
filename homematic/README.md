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
`/etc/default/hmmqtt` (options), `/var/lib/hmmqtt/devices.json` (device list, only if missing) and the udev rule
`/etc/udev/rules.d/77-cul-mm-ignore.rules` (keeps ModemManager away from the CUL, it would probe it with
AT commands). The service is enabled by `systemctl enable --now hmmqtt`.

## CUL firmware (culfw)

The busware CUL868 is delivered without firmware. Flash culfw (target `CUL_V3`, ATmega32U4):

```
apt install dfu-programmer
curl -LO http://culfw.de/culfw-1.67.tar.gz && tar xzf culfw-1.67.tar.gz
# a new CUL starts in the bootloader (lsusb: 03eb:2ff4 atmega32u4 DFU bootloader),
# else plug it in with the HWB button pressed
dfu-programmer atmega32u4 erase --force
dfu-programmer atmega32u4 flash culfw-1.67/Devices/CUL/CUL_V3.hex
dfu-programmer atmega32u4 launch
```

Without `--force` the erase is skipped ('Chip already blank') and the flash fails with 'Memory write error'.
After it the CUL is `03eb:204b` and `/dev/serial/by-id/usb-busware.de_CUL868-if00`.

Check: `screen /dev/serial/by-id/usb-busware.de_CUL868-if00` (or similar), `V` answers `V 1.67 CUL868`,
`VH` the target `CUL_V3`.

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
| `homematic/devices` | `[{"serial","type","model","rfAddress","firmware","lastSeen","rssi","runtimeDown","runtimeUp","half","channels":[{"channel","kind"}]}]` (running times of blinds in s and the characteristic, if known) |
| `homematic/state/<channel>` | blind: `{"level":76.0,"working":true,"direction":"down","rssi":-68,"ts":...}` (level in %, with a characteristic the visible one and `actorLevel` the one of the actor) |
| `homematic/pairing` | `{"active":true,"remaining":45}` |

Service → clients:

| Topic | Content |
|---|---|
| `homematic/event/paired` | `{"serial","type","rfAddress","result":"ok\|unsupported\|failed","text"}` |
| `homematic/event/removed` | `{"serial","result":"ok\|failed"}` |
| `homematic/event/runtime` | `{"serial","type","runtimeDown","runtimeUp","result":"ok\|failed","text"}` |

Clients → service:

| Topic | Content |
|---|---|
| `homematic/set/<channel>` | blind: `{"level":50}` or `{"stop":true}` |
| `homematic/get/<channel>` | status request, the answer comes via `state/...` |
| `homematic/pair` | `{"enable":true,"duration":60}` / `{"enable":false}` |
| `homematic/remove` | `{"serial":"OEQ1853240"}` |
| `homematic/runtime` | blind: `{"serial":"OEQ1853240"}` reads the running times, `{"serial":..,"down":52.3,"up":55.1}` (s) writes them and reads them back, optional `"half":66` sets the characteristic (`-1` removes it) |

## Pairing

`homematic/pair` enables pairing (60 s by default). Press the config button of the device: it sends its
device info, the service writes itself as central into the device (list 0, registers `0A`-`0C`), adds it
to the device list and publishes `event/paired`. A device of an unknown type is reported as `unsupported`.

`homematic/remove` clears the central in the device and removes it from the list.

## Running Times of the Blinds

The actor calculates the position from its reference running times (channel 1, list 1, registers `0B`/`0C`
top → bottom and `0D`/`0E` bottom → top, 0.1 s). `homematic/runtime` writes them (CONFIG start / write index /
end, no new pairing needed) and reads them back (CONFIG `04`, answer INFO `02`/`03`), the result comes as
`event/runtime` and in the device list (also stored in the device file). The WEBIF edits them and helps to
measure them (see [doc/homematic.md](../doc/homematic.md)).

Characteristic: the actor's % is the share of the running time, not of the window height (the roll is thick at
the top, at the end only the slats close). `half` is the level of the actor at half the height of the window,
the service maps the levels of MQTT (`set` and `state`) linearly between 0 → 0, 50 → `half` and 100 → 100.

## Notes

- Each frame expecting an answer is repeated up to 3 times (800 ms each).
- The CUL may send 1 % of the time (duty cycle), `LOVF` in the log means the limit is reached.
- Never run the CUL and the CCU with the same hmId at the same time (both would answer) – use `-s` for tests.
- Protocol reference: FHEM `10_CUL_HM.pm`, `HMConfig.pm`.
