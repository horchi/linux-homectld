

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

### DollaTek 5V ESP8266 ESP-01 2-Kanal-WiFi-Relaismodul
Modul: https://amzn.eu/d/e7EBxtd
Für dieses Modul funktioniert das (mit baudrate 115200)
https://templates.blakadder.com/LC-Relay-ESP01-2R-5V.html

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
etc/mqtt.d/stat_bbqlight_POWER.sh
etc/mqtt.d/tele_bbqlight_STATE.sh
```

As a example for a sensor with more values:
```
etc/mqtt.d/tele_tasmota_SENSOR.sh
```
If a converter is to be used for multiple topics, this can be mapped using symbolic links.

Details of the converters (parameters, message format, test): [doc/mqtt-converter.md](doc/mqtt-converter.md).
The converters can be managed in the WEBIF as well: *Setup -> MQTT Konverter* (admin only, the scripts run as root).
It lists the converters (script, alias = symbolic link to a script, skip = empty file, the messages of the topic
are dropped) and the topics received via the subscribed topics without a converter (with their last message, a
click creates a converter). The editor shows whether the topic is subscribed - a converter is only called for a
topic listed in *Konfiguration -> MQTT -> Zusätzliche sensor Topics* (directly or by a pattern like `stat/bbqlight/+`).
After storing a script its next call is the first one again (call count 1, e.g. for the init message).
