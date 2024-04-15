
# General

- add write of victron registers to WEBIF
- squeezebox Player wählen im WEBIF

- io/setup Skripe auch auf der Platte löschen?
- hide widgets on condition like value < x, > y oder text is 'none', ...
- check refresh of systemd interface after user action

- scaling for charts and chart widgets
- add enable/disable/kill to systemd interface?

- warn or hold unsaved values of io/settings on opening sensors config dialog

# Notes / Idea

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
