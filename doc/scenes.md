[← Back to the overview](../README.md)

# Scenes

A scene is a list of steps that homectld performs one after the other - for example a multi media automation:
switch on the receiver and the TV, wait until the TV is up, select the app and the input, set the volume,
dim the lights. Scenes are sensors of the type **SCN** and are shown on the dashboard like any other actor.

## Create and edit a scene

1. *Setup -> IO Setup*, page **Szenen**: `+` creates a new scene (`SCN:0x02`, ...), activate it like the other sensors.
2. The gear opens the scene dialog. Every step has an action and, below, an optional condition and a text.
   `+` below the last step adds a step, the arrows move a step, the trash can deletes it.
3. **Speichern** stores the steps. **Speichern und Testen** stores and starts the scene (the dialog stays open);
   as the start is a toggle, it aborts a scene that is already running.
   While the scene runs (and after it), the dialog shows its state above the steps: the running step is framed in
   blue, a failed step (error, timeout, unknown sensor, command not allowed) in red with the message below it,
   a step which failed but went on (*weitermachen*) in orange.
4. The scene is started from the dashboard with a **Symbol** widget on the sensor `SCN:0x..`.

**JSON** shows the steps as text (see below) - for steps that the list cannot edit, e.g. the color of a light.

## Running a scene

- A click on the symbol starts the scene, a **click on a running scene aborts it**.
- While it is running, the widget is grayed (like a script) and the text of the sensor shows the progress
  (`Schritt 3/7: warte auf Samsung TV`). At the end the text is `fertig`, `abgebrochen`,
  `Timeout bei Schritt n: ...` or the error. The state of the sensor is 1 while the scene is running.
- Scenes run in parallel, but every scene only once (starting a running scene doesn't start it again).
- The steps are performed by the daemon, without blocking it. A restart of homectld aborts the running scenes,
  they are not continued.

## Steps

| Action | Parameters | Meaning |
|--------|------------|---------|
| **Setzen** (`set`) | sensor, value | sets the sensor - see below |
| **Warten** (`wait`) | seconds | pause |
| **Warten bis** (`waitfor`) | condition, timeout, *bei Timeout weitermachen* | waits until the condition is true; after the timeout (default 30 s) the scene is aborted, or goes on if *weitermachen* is set |
| **Beenden** (`stop`) | - | ends the scene (useful with a condition: *nur wenn ...*) |
| **Szene** (`scene`) | scene, *starten* / *abbrechen* | starts or aborts another scene |
| **Befehl** (`shell`) | command line | runs a program from the whitelist in the background (e.g. `/usr/sbin/etherwake -i eno1 00:1e:06:43:48:8e`), the scene goes on at once; the output is logged in the journal (timeout 15 s) - see *Commands* below |

**Value of `set`:**

| Value | Effect |
|-------|--------|
| *umschalten* | toggles |
| *an* / *aus* | switches the state on / off |
| number | level: brightness, position, volume, ... |
| text (list of the choices if the sensor has some) | choice: input of the receiver, app of the TV, ... |

**Condition** (*nur wenn* of any step, and the condition of *Warten bis*): sensor, operator
(`==  !=  <  >  <=  >=`) and value (*an* / *aus*, number or text). A step whose condition is not true is skipped.
*an* means: the state of a status sensor is set, otherwise the value is not 0. A text is compared with the
text of the sensor (without case).

Example - movie evening (the sensors are chosen in the dialog):

1. Setzen: Denon power, *an* - nur wenn Denon power *aus*
2. Setzen: Samsung TV power, *an* - nur wenn Samsung TV power *aus*
3. Warten bis: Samsung TV power == *an*, max. 60 s
4. Setzen: Samsung TV app, `Netflix`
5. Warten: 2 s
6. Setzen: Denon source, `TV`
7. Setzen: light, 10

## Stored format

The steps are stored as JSON in the settings of the sensor (*JSON* in the dialog shows the same):

```json
{ "steps": [
    { "do": "set",     "key": "DENON:0x01", "value": true, "if": { "key": "DENON:0x01", "op": "==", "value": false } },
    { "do": "set",     "key": "SAMTV:0x04", "value": "Netflix" },
    { "do": "wait",    "sec": 2.5 },
    { "do": "waitfor", "key": "SAMTV:0x01", "op": "==", "value": true, "timeout": 60, "continue": false },
    { "do": "set",     "key": "DZL:0x03", "action": "color", "hue": 40, "saturation": 80, "bri": 60 },
    { "do": "scene",   "id": 2, "mode": "start", "title": "Licht aus" },
    { "do": "stop" }
] }
```

| Field | Meaning |
|-------|---------|
| `do` | `set`, `wait`, `waitfor`, `stop`, `scene`, `shell` |
| `key` | sensor as `<type>:<address>` (`SAMTV:0x04`) |
| `value` | `set`: missing or `"toggle"` toggles, `true` / `false` (or `"on"` / `"off"`) the state, number a level, text a choice; condition: bool, number or text |
| `action` | `set`: `"color"` (`hue` 0-360, `saturation`, `bri` in %) or `"ct"` (`ct` in mired, `bri`) for color lights |
| `continue` | `set`: a failing step (e.g. unknown sensor) doesn't abort the scene; `waitfor`: go on after the timeout |
| `sec` | `wait`: seconds |
| `op`, `value`, `timeout` | `waitfor`: condition and timeout in seconds (default 30) |
| `cmd` | `shell`: command line, the program with its absolute path and the arguments |
| `id`, `mode` | `scene`: address of the `SCN` sensor, `"start"` (default) or `"stop"` |
| `if` | `{ "key", "op", "value" }` - the step is only performed if the condition is true; optional `"field"`: `value`, `state` or `text` (default: text for a text, the state for status sensors, else the value) |
| `title` | text for the state text of the scene (default: `setze <sensor>`, `warte auf <sensor>`) |

Sensors with an MQTT command topic (e.g. `DENON`, `SAMTV`, `BBQ`) get the command like a click at their widget;
all others are switched like by the widget (`DO`, `GPIO`, `SC`, `DZL`, `DZLG`, `HMB`). A text value only works
for sensors with a command topic.

## Commands (`shell`)

Only programs which the administrator allowed can be run. The list is a file that the web interface cannot change,
`/etc/homectld/scene-commands-whitelist.conf` (JSON, the programs with their absolute path):

```json
[ "/usr/sbin/etherwake", "/usr/bin/ping" ]
```

- The command is **not run by a shell**: the line is split into arguments (`'...'` and `"..."` group words), pipes, `;`,
  `&&`, `$(...)`, `*` and redirections have no effect, they are passed as plain text.
- The program must be written with its absolute path and has to be in the list exactly as written. Without the file
  (or if it isn't valid JSON) all `shell` steps are rejected, the scene is aborted with the reason in its text
  (unless `continue` is set).
- The file is read at every `shell` step, a change is effective at once.
- The arguments are free. So do **not** list programs that run other programs or write files by their arguments, e.g.
  shells, interpreters (`python`, `perl`, `awk`), `env`, `xargs`, `find`, `tee`, `dd`, `curl` or `ssh`.
- The command doesn't block the scene and its exit code doesn't abort it (a non-zero one is logged). To know that it
  worked, follow it by *Warten bis* on a sensor (e.g. the power of the TV after a Wake-on-LAN).
- Commands run as the user of homectld.

## Notes

- A *set* only sends the command, it doesn't wait for the device. If the next step depends on it, use
  *Warten bis* (the device has to report its state, as the sensors of `denon` and `samsungtv` do)
  instead of a fixed pause.
- A sensor that is invalid (e.g. a TV that is off reports only its power) is never true in a condition.
- New scenes get the right *control* (needed for the click at the widget). A scene created with an older
  version has the right 0 in the table `valuefacts`: `update valuefacts set rights = 2 where type = 'SCN';`
  and restart homectld.
- Messages of the scenes are in the journal: `journalctl -u homectld | grep Scene`.
