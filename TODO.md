
# ....

- .bashrc specials?

# Important

- sensor last und changesAt müssen in Millisekunden sien
  changesAt darf NUR bei einer echten Änderung gesetzt werden
   -> sensor->>setValue|Sate|Text() implementieren und Zugriffe auf value/test/state von außen verhindern

- recover Output states also for MCPO !
  -> move ioStates from config table in a new separate table with type, address as KEY to
     support all kind of outputs

# General

- auto setup standard Dashbord pages at first installation
- add write of victron registers to WEBIF
- hide widgets on condition like value < x, > y oder text is 'none', ...
- scaling for charts / chart widgets

- warn or hold unsaved values of io/settings on opening sensors config dialog

# Notes / Ideas

- config option for valuefact rights (digital outputs, ..)
- apply IO settings without restart (initInput / initOutput)
    -> done (exept interrupt routine, resetting of interrupt routine crashes)

# Pool

- wenn Pumpe > x Minuten aus PH blass anzeigen,
  die Berechung des Ph-Minus stoppen und Säurepumpe deaktivieren

- Kontrolle der Säurepumpe für Arduino implementieren
  -> dazu Json Command mit Laufzeit vom Raspi senden
  - Not Begrenzung der Laufzeit via Arduino
