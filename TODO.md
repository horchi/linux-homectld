
# General

- apply IO settings without restart (initInput / initOutput)

- improve init and topic communication of i2smqtt process
- pullup/pulldown für Digitale Eingänge konfigurierbar (also for i2smqtt)
- change web communication of analog calibration to plain json object?
- Rechte der valuefacts konfigurierbar


# Pool

- wenn Pumpe > x Minuten Aus PH blass anzeigen,
  die Berechung des Ph-Minus stoppen und Säurepumpe deaktivieren

- Kontrolle der Säurepumpe für Arduino implementieren
  -> dazu Json Command mit Laufzeit vom Raspi senden
  - Not Begrenzung der Laufzeit im Raspi
