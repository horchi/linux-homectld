
# General

- add write of victron registers to WEBIF

- hide widgets on condition like value < x, > y oder text is 'none', ...
- scaling for charts / chart widgets

- add filter expression to systemd list view

- warn or hold unsaved values of io/settings on opening sensors config dialog

# Notes / Ideas

- config option for valuefact rights (digital outputs, ..)
- apply IO settings without restart (initInput / initOutput)
    -> done (exept interrupt routine, resting interrupt routine crashes)
- io/setup Skripe auch auf der Platte löschen?

# Pool

- wenn Pumpe > x Minuten aus PH blass anzeigen,
  die Berechung des Ph-Minus stoppen und Säurepumpe deaktivieren

- Kontrolle der Säurepumpe für Arduino implementieren
  -> dazu Json Command mit Laufzeit vom Raspi senden
  - Not Begrenzung der Laufzeit via Arduino
