#!/bin/bash
# Mini-Demo: Seite 3 im homectld-Look komplett per MQTT anlegen und mit Werten fuellen.
# Keine serielle Verbindung noetig: jedes openHASP-Kommando ist ein Topic unter hasp/plates/command/,
# das Argument ist die Nachricht. Alle Zeilen stehen mit festen Werten da und lassen sich einzeln
# kopieren und ausfuehren (Broker 192.168.200.101, Panel-Hostname panelwomo, im Ordner mqttdemo starten).

set -e

# Einmalig (HTTP, nicht MQTT): Symbolschrift mdi.ttf (2,3 KB, Teilmenge der Material Design Icons) auf das Panel laden.
# Panel-IP anpassen. Danach ist sie unter L:/mdi.ttf verfuegbar (text_font "mdi_<groesse>").
# curl -F "data=@mdi.ttf;filename=/mdi.ttf" http://192.168.200.50/edit

# alte Objekte auf Seite 3 loeschen, sonst werden sie doppelt angelegt
mosquitto_pub -h 192.168.200.101 -t hasp/plates/command/clearpage -m 3

# Seite anlegen: jede Zeile der jsonl-Datei als eigene Nachricht senden.
# Ein Payload fuer alles geht nicht: das MQTT-Paket ist in der Firmware auf 2048 Bytes begrenzt,
# ein laengerer Payload wird abgeschnitten (Objekte fehlen dann). Kommentarzeilen (skip) werden uebersprungen.
grep -v '"skip":true' mqtt-demo.jsonl | while IFS= read -r line; do
    mosquitto_pub -h 192.168.200.101 -t hasp/plates/command/jsonl -m "${line}"
done

# Seite anzeigen (Hintergrund und click der Seite stehen in der jsonl als Objekt mit id 0)
mosquitto_pub -h 192.168.200.101 -t hasp/plates/command/page -m 3

sleep 2

# Temperatur: Messwert in Zehntel Grad fuer den Linienmesser, Text separat
mosquitto_pub -h 192.168.200.101 -t hasp/plates/command/p3b21.val  -m 236
mosquitto_pub -h 192.168.200.101 -t hasp/plates/command/p3b22.text -m "23.6 °C"

# Spannung: Zehntel Volt fuer das Zeigerinstrument, Text separat
mosquitto_pub -h 192.168.200.101 -t hasp/plates/command/p3b32.val  -m 138
mosquitto_pub -h 192.168.200.101 -t hasp/plates/command/p3b33.text -m "13.8 V"

# Frischwasser: Fuellstand in Prozent fuer den Tank (Balken fuellt sich von unten), Texte separat
mosquitto_pub -h 192.168.200.101 -t hasp/plates/command/p3b44.val  -m 45
mosquitto_pub -h 192.168.200.101 -t hasp/plates/command/p3b42.text -m "45 l"
mosquitto_pub -h 192.168.200.101 -t hasp/plates/command/p3b43.text -m "45 % von 100 l"

# Kuehlschrank: das Symbol ist der Schalter, 1 = an (blau), 0 = aus (weiss); die Farbe wechselt von selbst
mosquitto_pub -h 192.168.200.101 -t hasp/plates/command/p3b52.val -m 0
sleep 1
mosquitto_pub -h 192.168.200.101 -t hasp/plates/command/p3b52.val -m 1

# Ereignisse des Panels mitlesen (Ctrl-C beendet), Kuehlschrank: hasp/panelwomo/state/p3b52 {"event":"up","val":1}
echo mosquitto_sub -h 192.168.200.101 -v -t hasp/panelwomo/state/+
