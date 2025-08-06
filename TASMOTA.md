

# I/O Bordas with TASMOTA

## Build
https://www.ratzownal.de/wie-komiliere-ich-die-tasmota-firmware/

## Flush
https://www.az-delivery.de/en/products/usb-adapter-fur-esp8266-01?ls=de
https://www.ratzownal.de/wie-flashe-ich-tasmota-auf-den-esp-01/

## Configure

### Relais Board I/O
https://www.ei-ot.de/2025/01/27/tasmota-2-kanal-relais-modul-konfigurieren/?srsltid=AfmBOoqFtrLwJ7M3Er7KP3ILVZdLt3hvLbsOSvAqX2jKc-PwJvdoOxvp

### Relais Board serial
https://tasmota.github.io/docs/devices/LC-Technology-WiFi-Relay/#lc-technology-wifi-relay-single-relay


## MQTT Topic Name

`%prefix%/%topic%/`

where topic is configured and prefix is a key part of the topic structure, typically consists of three prefixes:
```
cmnd for commands
stat for status updates
tele for telemetry sensor data
```

## Usage via MQTT

Assuming topic ist configured to bbqlight.

### Switch via MQTT

mosquitto_pub -t bbq_cmnd/bbqlight/POWER0 -m TOGGLE
mosquitto_pub -t bbq_cmnd/bbqlight/POWER0 -m ON
mosquitto_pub -t bbq_cmnd/bbqlight/POWER0 -m OFF

### State via MQTT

We have to read this two topics

stat/bbqlight/POWER -> for state change
tele/bbqlight/STATE -> for cyclic state updates

# homectld Interface

For communication with the homectld the MQTT interface of TASMOTA is used. The TASMOTA messages has to be converted to format expectet by the homectld.
A therefore the MQTT message converter scripts at /etc/homectld/mqtt.d/ can be used. The script has to be named like the topic written by TASMOTA where / has to be replaced by an underline character.
Example:
```
Topic                 Converter Script Name
stat/bbqlight/POWER   stat_bbqlight_POWER.sh
```
As an example for a simple TASMOTA switch (light,relais, ...) you find a example in the source tree:
```
configs/mqtt.d/stat_bbqlight_POWER.sh
configs/mqtt.d/tele_bbqlight_STATE.sh
```

As a example for a sensor with more values:
```
configs/mqtt.d/tele_tasmota_SENSOR.sh
```
