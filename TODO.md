
# General

- squeecebox Player wählen im WEBIF
- store dnsmasq.d/womo.conf
- bluetooth for Mopeka-Pro

- io/setup Skripe auch auf der Platte löschen?
- add write of victron registers to WEBIF
- warn or hold unsaved values of io/settings on opening sensors config dialog
- hide widgets on condition like value < x, > y oder text is 'none', ...
- Multi Sensor Widgets

- scaling for charts and chart widgets
- add enable/disable to systemd interface?

- config option for valuefact rights (digital outputs, ..)

# Notes

- apply IO settings without restart (initInput / initOutput)
    -> done (exept interrupt routine, resting interrupt routine crashes)

# Pool

- wenn Pumpe > x Minuten Aus PH blass anzeigen,
  die Berechung des Ph-Minus stoppen und Säurepumpe deaktivieren

- Kontrolle der Säurepumpe für Arduino implementieren
  -> dazu Json Command mit Laufzeit vom Raspi senden
  - Not Begrenzung der Laufzeit im Raspi
