# Lenkrad-Fernbedienung für die Kenwood Bridge (Planung)

Batteriebetriebenes Tastenfeld im Lenkrad, das per ESP-NOW Tastendrücke an den Kenwood-ESP
(`kenwood.ino`) schickt. Der Kenwood-ESP setzt sie in NEC-Codes auf den Lenkraddraht um, genau wie
die Kommandos aus homectld. Fahrzeug: Fiat Ducato 244 (2002) ohne Airbag, im Lenkrad liegt nur die
Hupenleitung, eine Wickelfeder oder zusätzliche Leitungen ins Lenkrad gibt es nicht.

Stand: Konzept geklärt, noch nichts gebaut.

## Konzept

```
   Lenkrad                                          Radioschacht
   +---------------------------+                    +-------------------------+
   | XIAO ESP32C3              |     ESP-NOW        | D1 Mini ESP32 (kenwood) |
   | 5 Tasten, LiPo            |  --------------->  | WLAN + MQTT + ESP-NOW   |
   | Deep Sleep, Wake per Taste|      (Funk)        | NEC -> Lenkraddraht     |
   +---------------------------+                    +-------------------------+
```

* Das Bedienteil schläft im Deep Sleep, wird von einer Taste geweckt, sendet ein Paket mit der
  Tastennummer und schläft wieder. Latenz vom Druck bis zum Radio 200 bis 300 ms.
* ESP-NOW läuft unterhalb von WLAN-Verbindung und IP: kein Router, kein DHCP, keine IP, Adressierung
  über die MAC-Adresse. Das Bedienteil taucht im Netz nicht auf. Der Kenwood-ESP bleibt dabei normal
  im WLAN (MQTT, OTA), ESP-NOW läuft auf demselben Kanal mit.
* homectld sieht jeden Tastendruck als Ereignis des zugehörigen KENWOOD Sensors und kann ihn auch
  für Szenen nutzen.

## Hardware

| Teil                            | Bemerkung                                                        |
| :------------------------------ | :--------------------------------------------------------------- |
| Seeed Studio XIAO ESP32C3       | 21 x 17,5 mm, USB-C, Akku-Pads mit Ladeschaltung (50 mA), externe Antenne (U.FL, liegt bei), Deep Sleep ca. 44 µA, keine Power-LED |
| LiPo EEMB 852040, 3,7 V, 620 mAh | 40 x 20 x 8,5 mm, mit Schutzschaltung (PCM), Stecker abschneiden und Adern an BAT+ / BAT- auf der Rückseite des XIAO löten, Polarität vorher messen |
| 5 Tastschalter 6 x 6 mm         | langer Stößel, in einem Rahmen bündig im Lenkrad, Kappen darüber wie bei Original-Lenkradtasten |
| 5 Dioden 1N4148                 | Weck-Verknüpfung, siehe Verdrahtung                              |

Board-Kennung für arduino-cli: `esp32:esp32:XIAO_ESP32C3`.

### Verdrahtung der Tasten

Aus dem Deep Sleep wecken beim ESP32-C3 nur GPIO 0 bis 5. Der XIAO führt davon GPIO 2, 3, 4, 5
(D0 bis D3) heraus, GPIO 2 ist ein Strapping-Pin, der beim Booten high sein muss, und beim Wecken
ist die Taste ja gerade gedrückt, also nicht als Taste verwenden. Damit fünf Tasten funktionieren,
zieht jede Taste ihren eigenen GPIO nach Masse und über eine Diode zusätzlich den gemeinsamen
Weckpin D1 (GPIO 3). Nach dem Aufwachen liest der Sketch, welche Taste noch gedrückt ist.

```
                                        XIAO ESP32C3
                                       +--------------+
             Taste 1                   |              |
        GND --o  o--+------------------| D4  (GPIO 6) |
                    |                  |              |
   +------->|-------+                  |              |
   |         Taste 2                   |              |
   |    GND --o  o--+------------------| D5  (GPIO 7) |
   |                |                  |              |
   +------->|-------+                  |              |
   |         Taste 3                   |              |
   |    GND --o  o--+------------------| D6  (GPIO 21)|
   |                |                  |              |
   +------->|-------+                  |              |
   |         Taste 4                   |              |
   |    GND --o  o--+------------------| D7  (GPIO 20)|
   |                |                  |              |
   +------->|-------+                  |              |
   |         Taste 5                   |              |
   |    GND --o  o--+------------------| D10 (GPIO 10)|
   |                |                  |              |
   +------->|-------+                  |              |
   |                                   |              |
   +-----------------------------------| D1  (GPIO 3) |  Weckpin, interner Pull-up
                                       +--------------+

   Dioden 1N4148, Anode am Weckpin-Bus, Kathode (Strich) zur Taste
```

Jeder Tasten-GPIO mit internem Pull-up, low = gedrückt. Die Dioden zeigen mit der Kathode zur Taste:
Eine gedrückte Taste zieht über ihre Diode den Weckpin mit nach Masse, die Dioden der anderen Tasten
sperren, so bleiben deren GPIOs high und die Tasten schließen sich nicht gegenseitig kurz.

Vorschlag für die Belegung, Adressen wie in `README.md`:

| Taste | Funktion  | Adresse |
| :---- | :-------- | :------ |
| 1     | Volume +  | 1       |
| 2     | Volume -  | 2       |
| 3     | Track +   | 5       |
| 4     | Track -   | 6       |
| 5     | Mute      | 12      |

### Stromversorgung

* Deep Sleep 44 µA, ein Tastendruck etwa 150 ms bei 100 mA, also rund 4 µAh. Bei 50 Drücken am Tag
  sind das 0,2 mAh plus 1 mAh Schlafstrom pro Tag. Der 620-mAh-LiPo reicht damit rechnerisch gut
  anderthalb Jahre, begrenzt durch die Selbstentladung. Laden über den USB-C des XIAO, festes 50 mA,
  der Akku ist in etwa 13 Stunden voll.
* **Hitze**: Ein Lenkrad in der Sonne erreicht im Sommer über 60 °C, die Grenze für LiPo-Zellen.
  Akku mit Schutzschaltung, tief in der Nabe, nicht hinter der besonnten Oberfläche. LiFePO4 wäre
  hitzefester, lässt sich mit der Ladeschaltung des XIAO (4,2 V) aber nicht laden.
* **Hupenleitung**: Beim 244er läuft die Hupe über einen Schleifkontakt, kein Airbag. In Ruhe liegt
  die Leitung meist über die Relaisspule auf ca. 12 V, der Hupenkontakt legt sie nach Masse. Daraus
  ließen sich gefahrlos 5 mA ziehen (2,2k in Reihe, Hupenrelais braucht ca. 100 mA). Zum Laden eines
  LiPo mit nur 5 mA wäre ein eigener Ladebaustein nötig (z.B. MCP73831 mit 200k hinter einem 5-V-
  Regler), fummelig. Sauber wäre die Leitung nur mit Supercap statt Akku: 2,2k, Schottky, HT7333,
  Supercap 1 F / 5,5 V, dann batterielos. Entscheidung: zunächst LiPo mit USB-Laden, Hupenleitung
  nur messen: Ruhespannung gegen Masse (erwartet ca. 12 V) und ob 2,2k nach Masse die Hupe
  unbeeindruckt lässt.

## Funk (ESP-NOW)

* **Kanal**: Beide Seiten müssen auf demselben Funkkanal sein. Der Kenwood-ESP nimmt den Kanal des
  Routers, das Bedienteil kennt keinen Router und bekommt den Kanal fest einprogrammiert. Deshalb im
  Router einen festen Kanal einstellen, kein Auto-Kanal.
* **Adressen**: Das Bedienteil sendet an die WLAN-MAC des Kenwood-ESP (`b0:cb:d8:98:6b:40`, aus dem
  DHCP-Log). Der Kenwood-ESP nimmt nur Pakete von der eingetragenen MAC des Bedienteils an.
* **Paket**: Tastennummer bzw. direkt die KENWOOD Adresse plus optional Anzahl (Volume +3), ein
  Byte Zähler gegen Doppelverarbeitung. Der Kenwood-ESP ruft dieselbe Funktion wie beim MQTT-Kommando
  und meldet die Taste wie gewohnt an homectld.
* **Updates**: Das Bedienteil hat keine WLAN-Verbindung, also kein OTA. Sketch-Updates per USB-C.
* Die feste IP .33 des Kenwood-ESP bleibt für MQTT und OTA wichtig, für ESP-NOW spielt sie keine Rolle.

## Noch zu tun

1. Teile besorgen: XIAO ESP32C3, LiPo EEMB 852040, Taster, Dioden, Rahmen für den Tastenblock.
2. Router auf festen WLAN-Kanal stellen.
3. Hupenleitung messen (Ruhespannung, 2,2k-Test), entscheidet nur über die Supercap-Option.
4. Sketch für das Bedienteil (`kenwood/kenwood-remote/`, Gerüst vorhanden): Deep Sleep, Wake auf D1, Taste ermitteln, ESP-NOW senden.
5. Empfängerseite in `kenwood.ino`: ESP-NOW init, Absender prüfen, Paket auf `sendKey()` abbilden.
6. Tastenblock ins Lenkrad, Akku und Board in der Nabe.

## Verworfene Alternativen

* **ESP32-C3 Super Mini**: billiger, fünf direkt weckfähige Pins, aber Power-LED (1 bis 2 mA) muss
  abgelötet werden, kein Ladeteil, Chip-Antenne.
* **Fertige BLE-Lenkradfernbedienung** (Satechi-Typ): fester Tastensatz, ESP32 müsste BLE-HID-Host
  neben WLAN spielen, deutlich mehr Software.
* **Universal-Funkfernbedienung mit Empfänger** (KEY1/KEY2 Widerstandsausgang für Android-Radios):
  Empfänger an einen ADC des Kenwood-ESP wäre möglich, Qualität laut Rezensionen wechselhaft.
* **Tasten auf der Lenkstockverkleidung**: drahtgebunden an den Kenwood-ESP, ohne Funk und Akku, aber
  nicht im Lenkrad.
