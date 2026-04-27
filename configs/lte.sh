#!/bin/bash

LANG=C

COMMAND="$1"
ADDRESS="$2"
MQTTURL="$3"

STATE="false"
IMAGE="mdi:mdi-cellphone-arrow-down"

LOGGER="logger -t sensormqtt -p kern.warn"

# Call the central LTE script to get current status data

STATUS_DATA=$(/usr/local/bin/lte-autoconnect.sh status)

# Parse the values from the status output

MODEM_STATE=$(echo "$STATUS_DATA" | grep "MODEM_STATUS" | cut -d: -f2)
PROVIDER=$(echo "$STATUS_DATA" | grep "PROVIDER" | cut -d: -f2)
TECH=$(echo "$STATUS_DATA" | grep "ACCESS_TECH" | cut -d: -f2)
INTERNET=$(echo "$STATUS_DATA" | grep "INTERNET" | cut -d: -f2)
RSSI_VAL=$(echo "$STATUS_DATA" | grep "SIGNAL_RSSI_VALUE" | cut -d: -f2 | cut -d. -f1)
QUALITY=$(echo "$STATUS_DATA" | grep "SIGNAL_QUALITY" | cut -d: -f2)

# Set display text and signal icons

if [[ "$INTERNET" == "CONNECTED" ]]; then
   STATE="true"

   # Combine Provider, Tech and Signal for the dashboard

   DISPLAY_TEXT="${PROVIDER} (${TECH} / ${QUALITY}%)"

   # Map signal strength to the exact MDI icon names (including your system's required prefix)

   if [ "$RSSI_VAL" -lt 70 ]; then
      IMAGE="mdi:mdi-signal-cellular-3"
   elif [ "$RSSI_VAL" -lt 85 ]; then
      IMAGE="mdi:mdi-signal-cellular-2"
   elif [ "$RSSI_VAL" -lt 100 ]; then
      IMAGE="mdi:mdi-signal-cellular-1"
   else
      IMAGE="mdi:mdi-signal-cellular-outline"
   fi

elif [[ "$MODEM_STATE" == *"connecting"* || "$MODEM_STATE" == *"enabling"* ]]; then

   # Icon for the connection build-up phase

   STATE="false"
   PROVIDER="Connecting..."
   IMAGE="mdi:mdi-sync"

else
   DISPLAY_TEXT="Offline"
   IMAGE="mdi:mdi-signal-cellular-outline"
fi

# Handle automation system commands

if [[ "${COMMAND}" == "init" ]]; then

   PARAMETER='{"cloneable": false, "symbol": "mdi:mdi-cellphone-arrow-down", "symbolOn": "mdi:mdi-signal-cellular-3"}'
   RESULT="{ \"type\":\"SC\",\"address\":${ADDRESS},\"kind\":\"status\",\"text\":\"${PROVIDER}\",\"valid\":true,\"state\":${STATE},\"parameter\": ${PARAMETER} }"
   echo -n ${RESULT}

elif [[ "${COMMAND}" == "toggle" || "${COMMAND}" == "status" ]]; then

   # Publish status to MQTT broker

   mosquitto_pub --quiet -L ${MQTTURL} -m "{ \"image\":\"${IMAGE}\",\"type\":\"SC\",\"address\":${ADDRESS},\"kind\":\"status\",\"state\":${STATE},\"text\":\"${DISPLAY_TEXT}\" }"
fi

exit 0
