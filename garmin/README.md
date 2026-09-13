# Garmin Connect activities

Shows the activities of a Garmin account (e.g. Fenix watch) in the WEBIF (tab *Aktivitäten*),
grouped by activity type, and allows to correct the activity type and the name.
Uses the unofficial python library [garminconnect](https://github.com/cyberjunky/python-garminconnect).

## Installation

```
apt install python3-venv
cd garmin
make install
```

or set `GARMIN = 1` in `Make.user`, then the top level `make install` includes it.

This creates a venv at `/usr/local/lib/homectld-garmin` with the module and installs `garmin.py`
to `/usr/local/bin`.

## Login (once)

```
garmin.py login
```

Asks for the Garmin email and password and, if two factor authentication is enabled, for the MFA code.
The tokens are stored in `/etc/homectld/garmin` and are valid for about a year. The password is
not stored. Repeat the login when the daemon reports that the tokens expired.

## Sync

The activities are fetched on demand only (button *Abgleich* in the WEBIF), the daemon does not
poll Garmin. Data volume: about 3-4 kB per activity for the list, about 20-30 kB for the details
of one activity. The GPS track of an activity is loaded on demand only (button *Track* in the
details), simplified by Garmin to 1000 points or at least one point per 15 seconds (about 80 byte
per point).

## Commands

```
garmin.py status
garmin.py types
garmin.py activities --since 2026-01-01
garmin.py activities --all [--limit 100]
garmin.py details <id>
garmin.py track <id> [--points 1000]
garmin.py settype <id> windsurfing_v2
garmin.py settypes windsurfing_v2 < ids.txt     # bulk, one id per line, one login
garmin.py renames < lines.txt                   # bulk, "id<TAB>new name" per line
garmin.py rename <id> "new name"
garmin.py delete <id>                          # deletes the activity at Garmin!
```

All commands print JSON to stdout.
