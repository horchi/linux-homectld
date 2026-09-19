
# Lenkrad-Fernbedienung für die Kenwood Bridge (Planung)

Tastenfeld im Lenkrad, das per ESP-NOW Tastendrücke an den Kenwood-ESP (`kenwood.ino`) schickt.
Der Kenwood-ESP setzt sie in NEC-Codes auf den Lenkraddraht um, genau wie die Kommandos aus homectld.
Versorgt wird das Bedienteil aus der Hupenleitung über einen Supercap, ohne Akku. Fahrzeug: Fiat
Ducato 244 (2002) ohne Airbag, im Lenkrad liegt nur die Hupenleitung, eine Wickelfeder oder
zusätzliche Leitungen ins Lenkrad gibt es nicht.

Stand: Konzept geklärt, noch nichts gebaut.

## Konzept

```
   Lenkrad                                          Radioschacht
   +---------------------------+                    +-------------------------+
   | ESP32-C3 Super Mini       |     ESP-NOW        | D1 Mini ESP32 (kenwood) |
   | 5 Tasten, Supercap        |  --------------->  | WLAN + MQTT + ESP-NOW   |
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

| Anzahl | Teil                          | Bemerkung / Bezug |
| :----- | :---------------------------- | :---------------- |
| 1      | ESP32-C3 Super Mini           | 22,5 x 18 mm, USB-C, Chip-Antenne, GPIO 0 bis 5 weckfähig; die rote Power-LED wird entfernt! [Amazon, 2er-Pack](https://www.amazon.de/dp/B0DMNBWTFD) |
| 1      | Supercap 1,5 F / 5,5 V        | KORCHIP DCL5R5155HF, Coin Type 19 x 6,5 mm, [Reichelt 244443](https://www.reichelt.de/de/de/shop/produkt/superkondensator_coin_type_1_5_f_5_5_v_1000_h-244443) |
| 1      | Regler 3,3 V LP2950           | LP 2950 ACZ3,3, TO-92, 75 µA Eigenverbrauch, 100 mA, max. 30 V Eingang, [Reichelt 122756](https://www.reichelt.de/de/de/shop/produkt/ldo-regler_fest_3_3_v_to-92-122756) |
| 1      | Z-Diode 15 V / 0,5 W          | ZF 15, DO-35, [Reichelt 23116](https://www.reichelt.de/de/de/zenerdiode-15-v-0-5-w-do-35-zf-15-p23116.html) |
| 1      | Schottky 1N5819               | Taiwan Semiconductor, 40 V / 1 A, DO-41, [Reichelt 219559](https://www.reichelt.de/de/de/shop/produkt/schottkydiode_40_v_1_a_do-41-219559), 0,10 €, ab Lager |
| 1      | Elko 100 µF / 25 V            | RAD 100/25, [Reichelt 15102](https://reichelt.de/de/de/elko-radial-100-f-25-v-rm-2-5-85-c-2000h-20--rad-100-25-p15102.html) |
| 1      | Kappensatz APEM Navimec       | 1Z03136123 grau, Ø 29,5 mm, 4x Pfeil 1x Power, ein Satz für alle fünf Tasten, [Reichelt 178283](https://www.reichelt.de/de/de/shop/produkt/kappe_fuer_multimec_5_29_5mm_gr_4x_pfeil_1x_power-178283) |
| 4      | Taster Multimec 5E, THT, NO   | 5ETH935 [Reichelt 178326](https://www.reichelt.de/de/de/shop/produkt/taster_5e_multimec_-_tht_no-178326) |
| 1      | Taster Multimec 5G, THT, NC/NO| 5GTH935NCNO [Reichelt 178343](https://www.reichelt.de/de/de/shop/produkt/taster_5g_multimec_-_tht_nc_no-178343), der NO-Kontakt wird genutzt |
| 1      | Lochrasterplatine 2,54 mm     | Schalter 10,3 x 10,3 mm, Ausschnitt in der Speiche Ø 30,3 mm, Bauhöhe 12 mm plus Platine |
| 1      | 1 kOhm / 0,6 W                | vorhanden |

Board-Kennung für arduino-cli: `esp32:esp32:nologo_esp32c3_super_mini`.

### Verdrahtung der Tasten

Aus dem Deep Sleep wecken beim ESP32-C3 nur GPIO 0 bis 5. Der Super Mini führt alle sechs heraus,
GPIO 2 ist ein Strapping-Pin, der beim Booten high sein muss, und beim Wecken ist die Taste ja
gerade gedrückt, also nicht als Taste verwenden. Bleiben GPIO 0, 1, 3, 4 und 5: fünf Tasten, jede
direkt an ihrem Weckpin gegen Masse, ohne Dioden. Nach dem Aufwachen liest der Sketch, welche Taste
gedrückt ist.

```
                             ESP32-C3 Super Mini
                             +-----------------+
             Taste 1         |                 |
        GND --o  o-----------| GPIO 0          |  oben,   Volume +
             Taste 2         |                 |
        GND --o  o-----------| GPIO 1          |  unten,  Volume -
             Taste 3         |                 |
        GND --o  o-----------| GPIO 3          |  rechts, Track +
             Taste 4         |                 |
        GND --o  o-----------| GPIO 4          |  links,  Track -
             Taste 5         |                 |
        GND --o  o-----------| GPIO 5          |  Mitte,  ATT
                             |                 |
                             +-----------------+
```

Jeder Tasten-GPIO mit internem Pull-up, low = gedrückt. Alle fünf sind als Weckquelle eingetragen
(Maske über GPIO 0, 1, 3, 4, 5), der Pull-up bleibt im Deep Sleep aktiv.

Vorschlag für die Belegung, Adressen wie in `README.md`:

| Taste | Navimec  | Funktion  | Adresse |
| :---- | :------- | :-------- | :------ |
| 1     | oben     | Volume +  | 1       |
| 2     | unten    | Volume -  | 2       |
| 3     | rechts   | Track +   | 5       |
| 4     | links    | Track -   | 6       |
| 5     | Mitte    | ATT       | 3       |

### Stromversorgung aus der Hupenleitung

Beim 244er Ducato läuft die Hupe über einen Schleifkontakt in der Lenksäule, kein Airbag. Die Leitung liegt
über die Spule des Hupenrelais dauerhaft auf 13 V, auch bei Zündung aus, der Hupenkontakt im Lenkrad
legt sie nach Masse. Gemessen: 120 Ohm nach Masse ergeben 65 mA und die Hupe bleibt still, bei
100 Ohm hupt es. Die Relaisspule hat damit etwa 80 Ohm, die Anzugsschwelle liegt bei rund 70 mA.
Das Bedienteil darf also nur einen Bruchteil davon ziehen, und zwar so begrenzt, dass auch ein Fehler
in der Elektronik die Hupe nicht auslösen kann.

```
                                          Schottky
                          1k / 0,6 W       1N5819
 Hupenleitung  o---------[=========]--------|>|--------+-----------+-----------------+
 13 V                                                  |           |              In |
                                                       |          _|_          +-----+-----+ OUT
                                              100 µF -----        /_\ ZF 15    |  LP2950   |--------+------------+------o  3V3  Super Mini
                                              25 V   -----         |  15 V     |   3,3 V   |        |            |
                                                       |           |           +-----+-----+      -----          |
                                                       |           |                 | GND        ----- Supercap |
                                                       |           |                 |              |  1,5 F     |
                                                       |           |                 |              |  5,5 V     |
 Masse         o---------------------------------------+-----------+-----------------+--------------+------------+------o  GND  Super Mini
```

* **1 kOhm**: begrenzt den Strom hart, im Kurzschlussfall bei 14,4 V auf 13 mA, ein Fünftel der
  Anzugsschwelle. Im Normalbetrieb fließen 5 bis 8 mA nur während des Nachladens. Verlust in der
  Relaisspule dabei 14 mW.
* **Schottky**: Beim Hupen fällt die Leitung auf 0 V, die Diode sperrt und der Supercap versorgt
  weiter, auch bei minutenlangem Hupen.
* **Supercap 1,5 F**: Bei 3,3 V speichert er bis zur Abschaltschwelle des Chips rund 120 µAh, etwa
  30 Tastendrücke ohne Nachladung. Entscheidend ist die gehaltene Taste (Lautstärke): dabei bleibt
  der Funk an und zieht rund 90 mA, die Leitung liefert nur 8 mA nach, der Cap überbrückt mit 1,5 F
  etwa 5 s Halten. Da ständig nachgeladen wird, ist er sonst praktisch immer voll. Nach dem Anklemmen ist er in zwei bis drei Minuten geladen. Ist die Batterie im Winter abgeklemmt, entlädt er
  sich über Tage und ist nach dem Anklemmen wieder da, nichts zu warten, kein Alterungsproblem bei
  Hitze im Lenkrad.
* **LP2950-3.3**: 3,3 V direkt auf den 3V3-Pin des Super Mini, das umgeht seinen Bordregler.
  Sein Eingang verträgt 30 V. Im Bordnetz kommen kurze Spitzen darüber vor, die über den 1k den
  100-µF-Kondensator aufladen würden, die Z-Diode 15 V parallel zum Kondensator klemmt das ab, der
  1k begrenzt ihren Strom dabei auf höchstens 45 mA. Zum Flashen per USB die Leitung trennen, damit
  sich die beiden Regler nicht in die Quere kommen.
* **Verbrauch**: Ein Tastendruck kostet etwa 150 ms bei 100 mA, also rund 4 µAh. Ruhestrom aus der
  Hupenleitung: Super Mini im Deep Sleep ca. 50 µA (Power-LED entfernt), LP2950 75 µA, Leckstrom des
  Supercaps 10 bis 30 µA, die Z-Diode sperrt unter 15 V, zusammen rund 150 µA, also 3,5 mAh am Tag
  und gut 1 Ah im Jahr aus der Starterbatterie.

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

1. Teile sind bestellt
2. Router auf festen WLAN-Kanal stellen!
3. Sketch für das Bedienteil (`kenwood/kenwood-remote/`, Gerüst vorhanden): Deep Sleep, Wake auf GPIO 0/1/3/4/5, Taste ermitteln, ESP-NOW senden.
4. Empfängerseite in `kenwood.ino`: ESP-NOW init, Absender prüfen, Paket auf `sendKey()` abbilden.
5. Tastenblock ins Lenkrad, Versorgung und Board in der Nabe, Abgriff an der Hupenleitung.

## Alternativen

* **Seeed XIAO ESP32C3**: Vorteil externe Antenne, aber nur drei direkt weckfähige Pins (Dioden nötig)
   -> Rückfallebene, falls der Funk aus der Nabe mit der Chip-Antenne nicht reicht.
