[← Back to the overview](../README.md)

# HomeMatic

The HomeMatic blinds are connected either

- via NodeRed and a CCU (as before), or
- directly via a **CUL868 USB stick** (busware) and the service `hmmqtt` - no CCU, no NodeRed needed

Select it at *Setup -> HomeMatic -> Anbindung* (`Aus`, `Node-RED`, `hmmqtt`).

## hmmqtt with a CUL stick

Supported so far: the blind actuators `HM-LC-Bl1PBU-FM`, `HM-LC-Bl1-FM`, `HM-LC-Bl1-SM`, `HM-LC-Bl1-PB-FM`.

1. Flash the culfw onto the CUL (it is delivered without firmware), see [homematic/README.md](../homematic/README.md)
2. Add `HOMEMATIC = 1` to `Make.user`, `make install`
3. Set the device and the address of the central in `/etc/default/hmmqtt`:
   `-d /dev/serial/by-id/usb-busware.de_CUL868-if00 -i <hmId>`.
   Coming from a CCU take its address (`/etc/config/ids` of the CCU, `BidCoS-Address`),
   then the devices paired to the CCU keep working without new pairing.
   Never run the CCU and the CUL with the same address at the same time
4. `systemctl enable --now hmmqtt`
5. WEBIF *Setup -> HomeMatic -> Anbindung* = `hmmqtt`

With `hmmqtt` the setup menu has the page *HomeMatic*:

- state of the service and the CUL
- *Anlernen* (60 s, then press the config button of the device)
- status request of all devices
- device list (name, type, serial, rf address, firmware, position, running times, last heard, signal) with
  *Abmelden*, for blinds *Lesen* (running times of the actor) and *Laufzeiten*

The blinds appear as `HMB` sensors as before, the names are set in the sensor setup.

### Running times of the blinds

The blind actor has no position sensor and doesn't detect the end positions: it calculates the position (%)
from its reference running times down and up. If they don't fit the blind, the % values are wrong. The running
times are registers of the actor, they can be written at any time, a new pairing is not needed (factory
default 50 s, values set at a former CCU are kept).

*Laufzeiten* opens a panel with the running times down / up and the level at half height:

- enter the values (e.g. measured with a stop watch) and *Schreiben*: the running times are written to the
  actor and read back, the result is shown in the event line
- *Ab messen* / *Auf messen* as a stop watch: the blind drives as usual from one end position to the other
  (it has to stand at the top for *Ab messen*, at the bottom for *Auf messen*), click *Halbe Höhe* when the
  window is half covered / half free (optional) and *Ist unten* / *Ist oben* at the end position - the times
  are put into the fields. Nothing is written to the actor while measuring. If a running time of the actor
  is too short to reach the end position, enter a longer one and write it first.

*Halbe Höhe*: the % of a blind is the share of its running time, not of the window height (the roll is thick
at the top, at the end only the slats close), e.g. 50 % is almost closed. With the level of the actor at half
height (empty: none) the service maps the %: the ones of the WEBIF are then the visible ones (0 → 0,
50 → half height, 100 → 100, linear between).

After writing, the % values are correct from the next run into an end position on (there the actor
synchronizes its position).

## CCU via Node-RED (notes)

Close the blind `HMB:2` completely via Node-RED / CCU (MQTT):
```
mosquitto_pub --quiet -L mqtt://192.168.200.101:1883/homectld2mqtt/nodered -m '{ "id":"HMB:2","bri":0 }'
```
to 50 %:
```
mosquitto_pub --quiet -L mqtt://192.168.200.101:1883/homectld2mqtt/nodered -m '{ "id":"HMB:2","bri":50 }'
```

[← Back to the overview](../README.md)
