
# Important

- sensor last und changesAt müssen in Millisekunden sien
  changesAt darf NUR bei einer echten Änderung gesetzt werden
   -> sensor->>setValue|Sate|Text() implementieren und Zugriffe auf value/test/state von außen verhindern

- recover Output states also for MCPO !
  -> move ioStates from config table in a new separate table with type, address as KEY to
     support all kind of outputs

# General

- add write of victron registers to WEBIF
