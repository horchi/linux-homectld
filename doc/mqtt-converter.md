[← Back to the overview](../README.md)

<a id="mqtt-converter"></a>
# MQTT Converter Scripts

Devices which publish their data by MQTT in their own format (e.g. TASMOTA, see [TASMOTA](../tasmota/TASMOTA.md)) are
integrated by a small converter script per topic. The script gets the message of the device and publishes it
again in the format of homectld. The scripts are in `/etc/homectld/mqtt.d`, they can be written with any editor
or in the WEBIF on the setup page *MQTT Konverter*.

## How it works

1. homectld receives only the topics it has subscribed: its own ones and the ones of
   *Setup -> Konfiguration -> MQTT -> Zusätzliche sensor Topics* (comma separated, patterns with `+` / `#` allowed,
   e.g. `tele/tasmota/SENSOR, stat/bbqlight/+`). **A converter of a topic not subscribed is never called.**
2. For each message of such a topic homectld looks for `/etc/homectld/mqtt.d/<topic>.sh`, the `/` of the topic
   replaced by `_`: `stat/bbqlight/POWER` -> `stat_bbqlight_POWER.sh`.
3. If there is one it is called (as root) with
   - `$1` the MQTT url to publish to (`mqtt://<broker>/homectld2mqtt/scripts`)
   - `$2` the topic
   - `$3` the message (as received, e.g. `ON` or a JSON)
   - `$4` the call count of this script since the start of homectld (1 at the first call, e.g. to send the
     message with `"action": "init"` once)
4. The script publishes one message per sensor to `$1`, e.g. by `mosquitto_pub -L $1 -m '<json>'`, homectld
   takes it like the messages of its own MQTT sensors.

Without a converter the message has to be in the format of homectld already, else the log shows
`Can't parse json` resp. `Missing 'type'`.

### Kinds of converters

| Kind | File | Meaning |
|------|------|---------|
| Script | executable script | converts the messages of the topic |
| Alias | symbolic link to a script | one script for several topics, e.g. `stat_bbqlight_POWER1.sh -> stat_bbqlight_POWER.sh`, the script gets the topic as `$2` |
| Skip | empty file | the messages of the topic are dropped, e.g. answers of a device on a topic subscribed by a pattern (`stat/bbqlight/RESULT` via `stat/bbqlight/+`) |

### Message to homectld

```json
{ "type": "TASMOTA", "address": 1, "kind": "value", "title": "Actual Load", "unit": "W", "valid": true, "value": 473 }
```

| Field | Content |
|-------|---------|
| `type` | sensor type, free text in capitals (e.g. `TASMOTA`, `BBQ`), the sensors appear in *Setup -> Sensor Setup* under this type |
| `address` | number of the sensor within the type |
| `kind` | `value` (number in `value`), `status` (on / off in `state`), `text` (text in `text`) |
| `title`, `unit` | title and unit of the sensor (the title can be changed in the sensor setup) |
| `valid` | `false` marks the value as invalid (faded) |
| `action` | `init` once with the first message (`$4` = 1), else `update` (default) |
| `topic` | with `init`: command topic to switch the sensor from the WEBIF; for a TASMOTA topic (`cmnd/...`) homectld sends `ON` / `OFF` / `TOGGLE`, else a JSON `{ "type", "address", "value" }` |
| `rights` | with `init`: rights needed to switch (e.g. `2` = control) |
| `battery` | battery charge [%], optional |
| `maxage` | seconds, without a new message for this time the sensor gets invalid, optional |

`jq` (taking values out of the JSON of the device) and `jo` (building the JSON) help, see the examples in
`etc/mqtt.d/` of the source tree (not installed by `make install`, copy the ones you need to `/etc/homectld/mqtt.d`):

```bash
#! /bin/bash

MQTTURL="${1}"
INTOPIC="${2}"
MSG="${3}"
CALL_COUNT=${4}

sensor()
{
   value=$(echo "${MSG}" | jq -r "${2}")
   RESULT=$(jo type="TASMOTA" address=${1} kind="value" title="${3}" unit="${4}" valid=true value=${value})
   mosquitto_pub --quiet -L ${MQTTURL} -m "${RESULT}"
}

sensor 1 '.data.Total_in' 'Total Consumption' 'kWh'
sensor 3 '.data.Power_curr' 'Actual Load' 'W'
```

## Setup page 'MQTT Konverter'

*Setup -> MQTT Konverter*, only for admins (also checked by the daemon) - the scripts run as **root**, whoever has
the admin login of the WEBIF can run any command on the server.

- **Converters** (left): all files of `mqtt.d` with their topic and kind, ⚠ if the topic isn't subscribed
- **Received topics without converter** (below): the topics received since the start of homectld via the
  subscribed topics which have no converter, ✓ = homectld understands the message itself (JSON with `type`),
  ? = probably needs a converter; a click creates a converter for it
- **Editor**: topic (exactly one, no `+` / `#`), below by which subscription it is received or a warning if it
  isn't subscribed; kind (script, alias, skip); the script with syntax highlighting (Ctrl-S stores), a new one
  starts with a template; the last message of the topic
- **Buttons**: *Speichern*, *Duplizieren* (the same for another topic), *Alias anlegen* (another topic with this
  script), *Löschen* (a script with aliases can't be deleted or turned into an alias / skip, the aliases first)
- after storing a script its next call is the first one again (`$4` = 1), so it sends its init message again

### Test

*Testen* runs the script **of the editor** (also unsaved changes) with the *Testnachricht* and shows what it would
send to homectld, without sending it. The test message is the last message received on the topic (as long as it
isn't edited, *Letzte Nachricht* takes it again), without one any message of the device can be entered there:

- the script runs in `/var/cache/homectld/mqtttest` with a stand-in for `mosquitto_pub` first in the `PATH`,
  it notes the message (`-m`) instead of publishing it
- shown are the messages (formatted JSON), marked red if it's no valid JSON or `type`, `address` or the value
  (`value` / `state` / `text`) is missing, and the output of the script (stdout / stderr, exit code)
- it runs as a regular call (`$4` = 2), the init message of the first call isn't shown
- the script may run at most 5 seconds
- **Note:** a script calling `mosquitto_pub` with its full path (`/usr/bin/mosquitto_pub`) publishes for real during
  the test, same for other commands with an effect (e.g. `curl` to the device) - they run as written

[← Back to the overview](../README.md)
