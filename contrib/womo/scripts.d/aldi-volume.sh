#!/bin/bash

COMMAND="$1"
ADDRESS="$2"
MQTTURL="$3"
JARGS="$4"
STATE="true"

LOGGER="logger -t sensormqtt -p kern.warn"
USER=`echo ${JARGS} | jq -r .user`
PASSWORD=`echo ${JARGS} | jq -r .password`

if [[ -z "${USER}" || -z "${PASSWORD}" ]]; then
   ${LOGGER} "aldi-volume.sh: USER/PASSWORD argument missing, call with '{ \"user\": \"015212345\", \"password\": \"foobar\"}'"
   STATE="false"
   exit 1
fi

echo "Melde an bei Aldi Talk..."
echo "user $USER, pwd: $PASSWORD"

# 1. Login-Token abrufen

# USER_AGENT simuliert die offizielle Smartphone-App
USER_AGENT="Mozilla/5.0 (Linux; Android 10; Mobile) AppleWebKit/537.36"

# 1. Login-Token abrufen (mit -L für Redirects)
RESPONSE=$(curl -s -L -A "$USER_AGENT" -X POST "https://alditalk-kundenportal.de" \
  -H "Content-Type: application/json" \
  -d "{\"username\": \"$NUMMER\", \"password\": \"$PASSWORT\"}")

TOKEN=$(echo "$RESPONSE" | jq -r '.token // empty')

echo "<- $RESPONSE"
echo "token $TOKEN"
exit 0

# Fehlerprüfung für den Login
if [ -z "$TOKEN" ]; then
    echo "Fehler beim Login! Bitte Rufnummer und Passwort prüfen."
    echo "Server-Antwort: $(echo "$RESPONSE" | jq -r '.message // "Unbekannter Fehler / Verbindung abgelehnt."')"
    exit 1
fi

echo "Verbindung erfolgreich. Rufe Tarifdaten ab..."

# 2. Datenvolumen und Tarif abrufen

DATA=$(curl -s -X GET "https://alditalk-kundenportal.de" -H "Authorization: Bearer $TOKEN")

# Daten sauber formatiert ausgeben

echo "$DATA" | jq -r '.[] | "Tarif: \(.name)\nRestvolumen: \(.data_volume_remaining) MB\nGesamtvolumen: \(.data_volume_total) MB\nGültig bis: \(.valid_until)"'
