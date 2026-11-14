# p4mqttd - Fröling S 3200 to MQTT

`p4mqttd` is the communication daemon for the Fröling S 3200 boiler control. It talks to the control over the serial
line (FTDI USB-serial converter at COM1 of the Fröling mainboard) and to the [homectld](../README.md) over MQTT.

It has **no WEB interface and no database**. Display, history, charts, alerts, mails and the user rights are provided by
the homectld. The stand alone [p4d](https://github.com/horchi/linux-p4d) (with its own WEBIF and database) is not
needed any more and will not be developed further.

Written by: *Jörg Wendel (linux at jwendel dot de)*

## License
This code is distributed under the terms and conditions of the GNU GENERAL PUBLIC LICENSE. See the file LICENSE for details.

## Disclaimer
USE AT YOUR OWN RISK. No warranty.
This software is a private contribution and not related Fröling. It may not work with future updates of the S3200 firmware and can also cause unintended behavior. Use at your own risk!
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Die Software wurde für den Eigengebrauch erstellt. Sie wird kostenlos unter der GPLv2 veröffentlicht.

Es ist kein fertiges Produkt, die Software entstand als Studie was hinsichtlich der Kommunikation
mit der s3200 Steuerung möglich ist und kann Bastlern als Basis und Anregung für eigene Projekte dienen.

Es besteht kein Anspruch auf Funktion, jeder der sie einsetzen möchte
muss das Risiko selbst abschätzen können und wissen was er tut, insbesondere auch in
Hinblick auf die Einstellungen der Heizungsparameter und den damit verbundenen Risiken
hinsichtlich Fehlfunktion, Störung, Brand, etc. Falsche Einstellung können unter anderem
durch Bedienfehler und Fehler in dieser Software ausgelöst werden!
Die Vorgaben, Vorschriften und AGB des Herstellers der Heizung bleiben maßgebend!
Ich kann  nicht ausschließen das es zu Fehlfunktionen oder unerwartetem Verhalten,
auch hinsichtlich der zur Heizung übertragenen Daten und damit verbundenen, mehr oder
weniger kritischen Fehlfunktionen derselben kommen kann!

**Writing parameters or time ranges of the heating control can cause malfunctions, damage or fire.
Know what you change! Use the read only mode (`-r`) as long as you don't need to write.**

## Prerequisites

- USB-serial converter based on FTDI chip, connected to COM1 of the Fröling mainboard
  (udev rules: [contrib/99-usbftdi.rules](../contrib/99-usbftdi.rules))
- A Linux based operating system
- an MQTT broker (mosquitto) and the homectld

## What it does

The daemon reads the state, the measured values (`P4VA`), the digital in- and outputs (`P4DI`, `P4DO`) and the analog
outputs (`P4AO`) of the control and publishes them in the format of the homectld (see below). It shows the service
menu, writes parameters and time ranges (not in the read only mode), publishes the error list and the state, and sets
the clock of the control. The homectld does the rest ([doc/p4.md](../doc/p4.md)): the pages, history, charts, alerts,
mails, the duration of the states, the pellets and the schema picture.

The sensors are read once from the control (value list and the in/outputs of the service menu, takes a few seconds up
to some minutes) and stored in `sensors.json` in the cache directory. They are read again if the firmware changes or
on `kill -USR1`, on the MQTT command `initmenu` / `initsensors` or with the button in the page of the homectld.

The configuration comes from the homectld (*Config -> Heizung S 3200*, `p4Enabled` switches it on): it publishes the
retained topic `p4/config`, p4mqttd reads it right after the start and on every change. The command line values
are only the defaults until it arrives. Only the sensors which are **active** in the homectld are polled. The first
sweep after the start (and after reading the sensor definitions) covers all sensors, this is how the homectld gets to
know them and you can activate them in the sensor setup, a newly activated sensor is read immediately.
The synchronization of the clock of the control (`tsync`) is done like in the p4d: not before 3:00, at most once a day,
only if the drift is larger than `maxTimeLeak`. It writes to the control and is therefore never done in the read only mode.

## Build and install

`p4mqttd` is built **on its own**. After cloning the repository only this directory is needed, nothing of the homectld is
built and none of its libraries (database, libwebsockets, lua, ...) is required. The sources of `../lib` are used directly
and compiled into `p4mqttd/obj`, so the whole repository has to be cloned, but only this directory is built.

```
cd /usr/src/
git clone https://github.com/horchi/linux-homectld/
apt install build-essential pkg-config libssl-dev libjansson-dev zlib1g-dev
cd linux-homectld/p4mqttd
make
sudo make install
```

`make install` installs `p4mqttd` and the command line tool `p4` to `/usr/local/bin` (`PREFIX=` changes this), the systemd
unit `p4mqttd.service` and `/etc/default/p4mqttd` (only if it doesn't exist). `make uninstall` and `make clean` are available.

The log goes to `/var/log/p4mqttd.log` instead of the syslog: `make install` adds `/etc/rsyslog.d/10-p4mqttd.conf`
(rsyslog is restarted if it changed) and `/etc/logrotate.d/p4mqttd` (daily, 5 generations). This needs rsyslog, on
systems with journald only install it with `apt install rsyslog`.

Adjust the serial device in `/etc/default/p4mqttd` (use the fixed name in `/dev/serial/by-id/`), then:

```
systemctl enable --now p4mqttd
```

Together with the homectld on the same machine it is also built by the main `make` / `make install` if `P4 = 1` is set
in `Make.user` (see `Make.user.template`), that just calls the Makefile of this directory.

## Options (`P4MQTTD_OPTS` in `/etc/default/p4mqttd`)

| Option | Meaning |
|---|---|
| `-d <device>` | serial device of the S 3200 (default `/dev/ttyUSB0`) |
| `-u <url>` | MQTT url (default `tcp://localhost:1883`) |
| `-T <topic>` | MQTT base topic (default `p4`) |
| `-D <topic>` | topic of the sensor data (default `<base topic>/sensor`, `homectld2mqtt/p4` during a transition) |
| `-C <dir>` | cache directory (default `/var/cache/p4mqttd`) |
| `-i <seconds>` | interval of the state check (default 10) |
| `-I <seconds>` | interval of the polling of the values (default 60) |
| `-r` | read only, refuse all writing requests |
| `-P` | parallel mode beside the old p4d (implies `-r`, poll interval at least 60 s) |
| `-l <level>` | eloquence (log level) |
| `-t` | log to stdout |
| `-n` | don't fork |
| `-v` | show version |

The default file shipped with the daemon starts it **read only** (`-r`).

## Service menu (commands)

The menu structure is read together with the sensor definitions (the whole menu list of the control) and stored in
the cache file, so it is available at once. A command is a JSON message to `p4/cmd`, the answer comes on `p4/reply`
with the same `rid`:

| Command | Meaning |
|---|---|
| `{"cmd":"menu","rid":"m1","parent":1}` | the items of one level of the menu with their **current values** (they are read from the control, a level takes a moment). `parent` is the level, 1 is the top. The answer has `items` (`id`, `parent`, `child`, `type`, `address`, `title`, `unit`, `digits`, `value`, `editable`), `parent`, `last` (the level above) and `title` |
| `{"cmd":"errors","rid":"m3"}` | reads the error list from the control now and publishes it, the answer has the list (`errors`), too |
| `{"cmd":"initmenu","rid":"m2"}` | reads the menu and the sensor definitions from the control again (takes a while, the answer comes when it is done: `menuItems`, `sensors`), the same as `kill -USR1`, `initsensors` is an alias |

`status` in the answer is `ok`, `notready` (no communication with the control yet) or `error` (with `message`).
Commands which were retained are ignored. The weekday lists of the time programs ('Zeiten') are part of the
menu answer: for each weekday a head (an item without value) and four ranges (`range` 1 to 4, `address` is the address of the
time program of this weekday, `value` is `hh:mm - hh:mm` or `nn:nn - nn:nn` for a range which is not set).

### Writing

**These commands write to the heating control.** They are refused with `readonly` in the read only mode (`-r`, `-P`), and
only the parameters and time programs of the service menu are accepted. Every write is logged (user, address, old and
new value). `user` is optional, the homectld sends the name of the logged in user.

| Command | Meaning |
|---|---|
| `{"cmd":"parlist","rid":"p0"}` | the parameters of the service menu (the addresses `parget` / `parset` accept): `count` and `parameters` with `address`, `type`, `title` and `path` (the way through the menu). From the cache, the control isn't asked |
| `{"cmd":"parget","rid":"p1","address":480}` | title, value, unit, digits, min, max, default and type of a parameter |
| `{"cmd":"parset","rid":"p2","address":480,"value":"72","user":"joerg"}` | sets a parameter. `value` is a number (`.` or `,`), `hh:mm` for a time of day, `ja` / `nein` (also `1` / `0`, `true` / `false`) for a switch. The answer has `old` and the `value` read back |
| `{"cmd":"timeset","rid":"p3","address":3,"range":1,"value":"06:00 - 22:00","user":"joerg"}` | sets range 1 to 4 of the time program at `address`, `nn:nn - nn:nn` deletes it. The control knows only steps of 10 minutes |
| `{"cmd":"inittimes","rid":"p4"}` | reads the time programs from the control again |

`status` of the answers of `parset` / `timeset`: `ok`, `nochange` (the value is already set), `range` (out of range, with
`min` / `max`), `format`, `readonly`, `error` (address isn't part of the menu) or `comm` (communication error).
The time programs can't be requested one by one from the control, they are all read once (about 10 seconds), kept in
memory and in `times.json` of the cache directory and read again by `inittimes` or `initmenu`. A change at the control
itself is only seen after that.

### Using the commands in scripts

The commands and the answers are **not retained** (a retained command is ignored). The daemon works the commands off
one after the other (at most 20 are waiting) and copies the `rid` into the answer, so several clients can ask at the
same time. A command which isn't JSON gets no answer, so use a timeout.

`p4/reply` is shared by all clients (the homectld, too): everybody gets every answer and has to pick his own by the
`rid`. Choose a unique `rid` with a prefix of your own, **the prefix `homectld-` is reserved for the homectld**.
Subscribe before the command is sent:

```
RID="myscript-$$-$(date +%s)"
mosquitto_sub -h <broker> -t p4/reply -W 30 | jq -c --unbuffered "select(.rid==\"$RID\")" | head -n1 &
sleep 0.5
mosquitto_pub -h <broker> -t p4/cmd -m "{\"cmd\":\"parget\",\"rid\":\"$RID\",\"address\":480}"
wait
```

As an option to the filter `select(.rid==\"$RID\")`: with **`replyTo`** in the command the answer is sent to this
topic instead of `p4/reply`. It has to be a topic below `p4/reply/` (without wildcards), take a name of your own.
Nobody else answers there, so the first message is yours - no filter and no `jq` needed:

```
mosquitto_sub -h <broker> -t p4/reply/myscript -C 1 -W 30 &
sleep 0.5
mosquitto_pub -h <broker> -t p4/cmd -m '{"cmd":"parget","rid":"1","address":480,"replyTo":"p4/reply/myscript"}'
wait
```

Two scripts using the same `replyTo` topic at the same time still have to tell their answers apart by the `rid`.
The homectld reads `p4/cmd`, `p4/reply` and `p4/reply/#`, too: it drops what isn't its own (unknown or missing `rid`,
no JSON) and removes a command or an answer which was published retained by mistake at the broker.

## Topics

Retained:

| Topic | Content |
|---|---|
| `p4/config` | written by the homectld: `{"pollInterval":60,"stateInterval":10,"tsync":true,"maxTimeLeak":10,"active":{"P4VA":[0,1],"P4DO":[1],"P4DI":[],"P4AO":[]}}`, without `active` all sensors are polled |
| `p4/cmd`, `p4/reply`, `p4/reply/<name>` | commands to the daemon and its answers (on `p4/reply/<name>` with `replyTo`), not retained, see above |
| `p4/status` | `{"online":true,"comm":"ok","tty":"/dev/ttyUSB0","lastOk":1788000000,"firmware":"50.04.00.01","version":"p4mqttd 0.9.0","readonly":true,"parallel":false}`, the last will of the connection sets `online` to `false` |
| `p4/errors` | the error list of the control as it keeps it (oldest first), `{"errors":[{"number":16,"info":163,"state":1,"time":1788000000,"text":"Raumaustragung kontrollieren"},...],"read":1788000100}`, `state` is 1 came, 4 gone, 2 acknowledged. The three entries of one error are merged by the homectld. Read at the start, if the state changes from or to an error state, every 10 minutes and on the command `errors` |
| `p4/state` | `{"state":3,"stateinfo":"Heizen","mode":1,"modeinfo":"Automatik","time":1788000000,"error":false}` |

`comm` is `ok`, `lost` (the line is reopened) or `closed`.

Sensor data (not retained, topic `p4/sensor`, the homectld subscribes it), the format of the homectld:

```
{"type":"P4VA","address":1,"title":"Abgas","unit":"°C","value":77.5,"maxage":180}
{"type":"P4DO","address":12,"title":"Saugzug","kind":"status","state":true,"text":"Auto","maxage":180}
{"type":"P4AO","address":3,"title":"Saugzug Leistung","unit":"%","value":62,"text":"Auto","maxage":180}
{"type":"P4UD","address":1,"title":"Heizungsstatus","unit":"zst","kind":"text","value":3,"text":"Heizen"}
{"type":"P4VA","address":1,"title":"Abgas","unit":"°C","valid":false}
```

The values are scaled (factor of the control) and signed or unsigned as the control defines it. `P4UD` are the
state (1), the mode (2) and the time (3) of the control. `valid:false` is sent for a value which could not be read.
`text` of an output is the mode: `Auto` or `Manual` (an unknown mode character of the control is passed as it is).

The `title` is the name of the control. The control calls the in/outputs of every heating circuit the same
('Heizkreispumpe', 'HK Mischer AUF'), so an in/output below a numbered level of the menu gets its name, e.g.
'Heizen / Heizkreis 02 / Service' -> `Heizkreis 02 Pumpe`, `Heizkreis 02 Mischer AUF`. The homectld takes the
title with every message, an own name belongs in its user title.

Every sensor message also carries the default options of its dashboard widget as `parameter` (kind of widget, scale,
peak, symbol of a switch), chosen by type, unit and title (the table `widgetRules` in `p4mqttd.c`), e.g.
`"parameter":{"widgettype":6,"scalemin":0,"scalemax":100,"scalestep":20,"showpeak":true}`. The homectld takes them
when a widget is created, widgets which are already placed in a dashboard keep their options. The pumps use the
animated images of the p4d (`img/icon/pump-on.gif`, `pump-off.png`), the fans an MDI fan which turns while it is on.
The state of the control (`P4UD:1`) carries `image`: the animated image of the p4d for the state
(`img/state/<icon set>/state-fire.gif` ... for the states which have one, e.g. heating up, burning, door open,
cleaning, waiting) or else the picture of the heating type (`img/p4/type/heating-<type>.png`). The icon set and the heating type
come with the config of the homectld (defaults `light` and `P4`).

## Parallel operation with the old p4d

The serial line is protected by the SysV semaphore `0x3da00001`, exactly as in the p4d and in the tool `p4`.
The daemon holds it for each request/response cycle, so `p4mqttd`, `p4` and the old `p4d` can run side by side
on the same host. With `-P` the daemon only reads (no writing, no time synchronization) - use it to compare the
values with the p4d before the p4d is switched off. The semaphore is local to a host and to an IPC namespace, it
doesn't work between containers or hosts (and not with `PrivateIPC=yes` in a systemd unit).

## Command line tool `p4`

`p4` is the command line tool of the p4d. It talks directly to the serial line and uses the same semaphore, so it
works beside the daemon. Call `p4 -h` for the commands. Note that `p4 setp` and `p4 tsync` write to the heating control
and are **not** blocked by the read only mode of the daemon.

## Troubleshooting

- `p4 state -d /dev/ttyUSB0` checks the communication without the daemon (it takes the same semaphore, so it can run beside it).
- `journalctl -u p4mqttd -f` or start it in the foreground: `p4mqttd -n -t -l 7 -d /dev/ttyUSB0`
- `mosquitto_sub -t 'p4/#' -v` shows what is published.
- Without a heating: `scripts/p4sim.py` simulates the serial interface of the control (a pseudo terminal, see the head of the script),
  start the daemon with `-d` on its link.
- `ipcs -s` shows the semaphore (key `0x3da00001`).
- The lists of the control (values, menu, time programs, errors) are read in one piece while the daemon holds the serial
  line: the control keeps a position in such a list and a request of another process in between would throw it off
  (the old p4d polls, too). During the first start (about a minute, 1276 menu items on a P4) the p4d waits.
- `comm` in `p4/status` is not `ok`: the device name (use `/dev/serial/by-id/...`), the rights of the user for the device
  (`dialout`), another program which has the line open without the semaphore.
- No sensors in the homectld: the service is not switched on there (`p4Enabled`), the MQTT topic differs (`-T`) or the broker.
  `mosquitto_sub -t 'p4/#' -v` tells which side is silent.

## Taking over the data of the p4d

`scripts/p4d-migrate.sh` takes over the data of the stand alone p4d (database `p4`) into the database of the homectld
(`homectl`). Both databases have to be on the same database server. The old database is only **read**.

| Part (`--only`) | Source (`p4`) | Target (`homectl`) |
|---|---|---|
| `samples` | `samples` of the types VA, DI, DO, AO, UD, SD, W1, SC, CV, PA | `samples` with the type `P4…` (VA -> P4VA, ...), the address stays |
| `peaks` | `peaks` | `peaks`, merged: the smaller minimum and the larger maximum win |
| `errors` | `errors` | `p4errors` (as history, `mailcnt >= 1`, they are not mailed) |
| `pellets` | `pellets` | `p4pellets` |
| `alerts` | `sensoralert` (type VA -> P4VA, ...) | `sensoralert`, only simple alerts (no link to another alert), receivers and texts as they are |
| `schema` | `schemaconf` | `schemaconf` with the type `P4…` (UC -> P4UC texts, CN -> P4CN lines): the elements with their position, size, colors and the functions; elements which exist without a position (placeholders of an earlier take over) are filled, the sensors in the functions (`getItem("VA:0x0")`) get the prefix (`P4VA`) |

Not taken over: dashboards, widgets, groups, users and the configuration (set the few values you need by hand), the menu and
the time programs (p4mqttd reads them from the control).

```
cd p4mqttd/scripts
mysqldump homectl > homectl.sql                       # a backup of the target first
./p4d-migrate.sh                                       # dry run: shows what would be taken over, writes nothing
./p4d-migrate.sh --run                                 # takes over everything
./p4d-migrate.sh --only schema,errors --run            # only these parts
./p4d-migrate.sh --known-only --run                    # samples only of sensors which exist in the homectld
```

| Option | Meaning |
|---|---|
| `--run` | really write, without it it is a dry run |
| `--only a,b` | only these parts (`samples,peaks,errors,pellets,alerts,schema`) |
| `--from YYYY-MM-DD` | samples: only since this day |
| `--known-only` | samples: skip sensors which are no valuefacts in the target (e.g. one you deleted there), by default they are taken over anyway and shown with a note |
| `--types "VA DO ..."` | the types of the p4d for samples and peaks, default `VA DI DO AO UD SD W1 SC CV PA` |
| `--from-db`, `--to-db` | names of the databases, default `p4` and `homectl` |
| `--mysql-opts "..."` | options for the mysql client (user, password, host) |

Good to know:
- **It can be repeated** and run while both systems are running: `INSERT IGNORE` and "not exists" keep everything the homectld
  wrote in the meantime. A statement which fails stops the script, what was done before stays, a new run continues.
- **Samples** are taken sensor by sensor and year by year, every statement is committed at once (some 26 million rows took
  about 40 minutes beside the running homectld). It shows its progress.
- The sensors have to exist as valuefacts in the homectld to be shown there (the homectld creates the `P4…` sensors from the
  messages of p4mqttd), the script tells which ones don't.
- Do the take over of the **schema** after the homectld has created the sensors, the positions of the elements in the heating
  schema (tab *Schema*) come from here. The picture is the one of the configuration *Bild des Schemas* (`p4-2hk-puffer` like
  in the p4d).
