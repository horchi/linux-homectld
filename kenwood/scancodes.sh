#!/bin/bash
#
# Sendet nacheinander NEC Codes ueber die Kenwood ESP32 Bridge an das Radio,
# um unbekannte Tastencodes (Power, Answer, Hang up, Voice, ...) zu finden.
#
# Aufruf: ./scancodes.sh [Startcode] [Endcode]
#         ./scancodes.sh 28 255
#
# Der MQTT Broker wird aus MQTT_HOST in ../Make.user gelesen.
#

usage()
{
   echo "Aufruf: $(basename "$0") [-h] [Startcode] [Endcode]"
   echo
   echo "  Startcode   erster NEC Code (0..255), Standard 28"
   echo "  Endcode     letzter NEC Code (0..255), Standard 255"
   echo "  -h          diese Hilfe"
   echo
   echo "Sendet die Codes nacheinander per 'raw' Kommando an homectld2mqtt/kenwood/in,"
   echo "der MQTT Broker wird aus MQTT_HOST in ../Make.user gelesen."
   echo "Nach jedem Code wird auf eine Taste gewartet: beliebige Taste = naechster Code,"
   echo "w = denselben Code noch einmal senden, q = abbrechen."
}

[ "$1" = "-h" ] || [ "$1" = "--help" ] && { usage; exit 0; }

MAKE_USER="$(dirname "$0")/../Make.user"
HOST="$(sed -n 's/^MQTT_HOST[[:space:]]*=[[:space:]]*//p' "$MAKE_USER" 2>/dev/null | tail -1)"

if [ -z "$HOST" ]; then
   echo "MQTT_HOST nicht in $MAKE_USER gefunden"
   exit 1
fi

FROM="${1:-28}"
TO="${2:-255}"
TOPIC="homectld2mqtt/kenwood/in"

case "$FROM$TO" in
   *[!0-9]*) echo "Start- und Endcode muessen Zahlen sein"; usage; exit 1 ;;
esac

echo "Sende Codes $FROM bis $TO an $TOPIC auf $HOST"
echo
echo "Lautstaerke am Radio vorher auf moderat stellen!"
echo "Nach jedem Code wird auf eine Taste gewartet:"
echo "  beliebige Taste  -> naechster Code"
echo "  w                -> denselben Code noch einmal senden"
echo "  q                -> abbrechen"
echo
echo "Reagiert das Radio, die Nummer notieren. Den Standby-Code danach mit 'w'"
echo "wiederholen, um zu pruefen ob der Lenkraddraht auch im Standby gelesen wird."
echo
read -n 1 -s -p "Start mit beliebiger Taste ..."
echo

for c in $(seq $FROM $TO); do
   while true; do
      printf 'code %d (0x%02X) ' $c $c
      mosquitto_pub -h "$HOST" -t "$TOPIC" -m "{\"type\":\"KENWOOD\",\"action\":\"raw\",\"code\":$c}"
      read -n 1 -s key
      echo
      [ "$key" = "q" ] && break 2
      [ "$key" = "w" ] || break
   done
done
