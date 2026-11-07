[← Back to the overview](../README.md)

# Light and Blind Control

Lights and groups of deCONZ (`DZL`, `DZLG`), HomeMatic blinds (`HMB`) and the outputs (`DO`, `GPIO`) can be
switched by time and by switches / motion sensors, configured in the WEBIF, no NodeRed and no Lua needed.
The setup of deCONZ itself is described in [deCONZ](deconz.md).

## Sun times

The sensor `SUN:0x01` *Sonne* shows dawn, sunrise, sunset and dusk of the day. They are calculated for the
position of the configuration (latitude / longitude), with *Daemon -> Standort aus GPS* for the position of
the GPS receiver (useful in a motorhome). The dashboard switches day / night by them.

## Time control

*Sensor Setup*, setup of a light, group or blind -> dialog *Zeitsteuerung*:

- one or more time ranges, start and end either a fixed time (`06:30`) or a sun event (Morgendämmerung,
  Sonnenaufgang, Sonnenuntergang, Abenddämmerung) with an offset in minutes, e.g. *Sonnenuntergang -30*;
  the resolved time of today is shown next to it
- optional valid on some days of the year only (*Gültig* day / month from - to), e.g. to close a blind
  later in summer
- lights: on at the start, off at the end, optional with a brightness
- blinds: *Zu* at the start, *Auf* at the end, with the positions for both [%]
- switching only happens at the limits of a range; switched by hand it stays until the next limit
- a limit missed while homectld was stopped (restart, power off of the motorhome) is caught up at the start,
  up to 2 hours back and per light / blind only the last one (closed at 22:00 and opened at 06:30 -> only
  opened); it waits until deCONZ and MQTT are ready (at most 2 minutes). The time up to which the limits are
  done is stored once a minute in `/var/cache/homectld/actortimes.json`
- *Zeitsteuerung aktiv* switches the whole time control of the actor off without losing the ranges

The outputs (`DO`, `GPIO`) have their time ranges in the tab *Zeiten* of their setup, with the same sun events.

## Rules of switches and motion sensors

*Sensor Setup*, setup of a deCONZ switch or motion sensor (`DZS`) -> dialog *Regeln*, one or more rules:

- trigger: a button code of the switch or *Bewegung* of the motion sensor
- action: *Umschalten*, *An*, *Aus* or *Folgen* (on with motion, off when the sensor reports no motion anymore)
- action *Dimmen (Taste halten)*: holding the button (e.g. `1001`) dims the targets, the button of *Stopp bei Taste*
  (release, e.g. `1003`) stops it; *Übergang* is the time over the full range (default 4 s). Off targets go on
  with the lowest brightness and get brighter, else darker from 50 % on and brighter below; holding again within
  5 s after the release dims in the other direction
- targets: one or more lights, groups or outputs
- optional brightness [%] and transition [s] (fading time)
- optional off timer *Aus nach* [s], restarted by each event, switches off only what the rule switched on
- optional time range (fixed or sun times), e.g. motion switches the light only at night

## Shading of the blinds

Dialog *Zeitsteuerung* of a blind, section *Beschattung*: while the sun shines on the window the blind moves
to the shading position and later back.

- position at shading [%]
- sun range: azimuth from - to [°] (direction the window faces ± about 50°) and minimal elevation [°],
  the current sun position is shown (*Sonne jetzt*)
- optional brightness sensor (lux) and outside temperature sensor, each with a threshold
- delays for shading / end [min] (against passing clouds)
- the blind only moves down (a lower blind stays), moves back only if it still stands at the shading
  position; moved by hand there's no shading for this blind until the next day; the time control has priority

Without a brightness sensor only the sun position and the temperature are checked.

## Dashboard

- the info dialog of a widget (click on its title) shows in the tab *Regeln* the time control, the rules of a
  sensor, the rules switching a light, a running off timer and the shading state
- long press on a dimmable light opens a brightness slider, on a color light the color wheel

[← Back to the overview](../README.md)
