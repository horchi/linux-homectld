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
- device list (name, type, serial, rf address, firmware, position, last heard, signal) with *Abmelden*

The blinds appear as `HMB` sensors as before, the names are set in the sensor setup.

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
