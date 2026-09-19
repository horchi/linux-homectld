# kenwood-remote: Lenkrad-Fernbedienung (Sketch)

Tastenfeld im Lenkrad auf Basis des ESP32-C3 Super Mini, das Tastendrücke per ESP-NOW an den
Kenwood-ESP (`../`) schickt. Konzept, Hardware, Verdrahtung und Energiebudget stehen in
[../README-remote.md](../README-remote.md).

Stand: Sketch-Gerüst, noch nicht am Gerät getestet. Die Empfängerseite in `kenwood.ino` fehlt noch.

## Ablauf im Sketch

1. Aufwachen aus dem Deep Sleep durch eine der fünf Tasten (GPIO 0, 1, 3, 4, 5 auf low).
2. Tasten-GPIOs lesen und die gedrückte Taste ermitteln, entprellt.
3. WLAN im Stationsmodus ohne Verbindung starten, festen Kanal setzen, ESP-NOW initialisieren.
4. Paket mit KENWOOD Adresse an die MAC des Kenwood-ESP senden, auf Bestätigung warten.
5. Bleibt die Taste gedrückt, nach 400 ms alle 250 ms wiederholen (Lautstärke), höchstens 30 mal.
6. Zurück in den Deep Sleep.

## Paket

```
struct RemotePacket
{
   uint8_t magic;    // 0x4B
   uint8_t address;  // KENWOOD Adresse der Taste (1 Volume +, 2 Volume -, 5 Track +, 6 Track -, 3 ATT)
   uint8_t count;    // Anzahl Tastendruecke (immer 1, Wiederholungen kommen als eigene Pakete)
   uint8_t seq;      // laufende Nummer, gegen Doppelverarbeitung
};
```

## Konfiguration

In `../../Make.user`:

```
KENWOOD_MAC = b0:cb:d8:98:6b:40     # WLAN-MAC des Kenwood-ESP (DHCP-Log oder Boot-Log)
WIFI_CHANNEL = 6                    # fester Kanal des Routers
KENWOOD_REMOTE_PORT = /dev/ttyACM0  # USB Port des Super Mini zum Flashen
```

Pins, Adressen und Wiederholzeiten in `config-tmpl.h`.

```bash
make            # kompilieren (Board esp32:esp32:nologo_esp32c3_super_mini)
make upload     # flashen per USB, ein OTA gibt es hier nicht
make clean
```

Serielle Konsole: `picocom -b 115200 /dev/ttyACM0`. Da der ESP nach jedem Tastendruck schläft,
erscheinen die Meldungen nur während der etwa 200 ms Wachzeit, für Tests den Deep Sleep in
`config-tmpl.h` per `DebugStayAwake` abschalten.
