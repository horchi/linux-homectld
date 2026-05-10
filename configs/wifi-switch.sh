#! /bin/bash

LANG=C

COMMAND="$1"
ADDRESS="$2"
MQTTURL="$3"

LOGGER="logger -t sensormqtt -p kern.warn"

# ── state ──────────────────────────────────────────────────────────────────────

if nmcli radio wifi 2>/dev/null | grep -q "enabled"; then
   STATE="true"
   IMAGE="mdi:mdi-wifi"
else
   STATE="false"
   IMAGE="mdi:mdi-wifi-off"
fi

SSID=$(nmcli -t -f active,ssid dev wifi 2>/dev/null | grep '^yes:' | cut -d: -f2 | head -1)
[ -z "$SSID" ] && SSID="Aus"

# ── commands ───────────────────────────────────────────────────────────────────

if [[ "${COMMAND}" == "init" ]]; then

   PARAMETER='{"cloneable": false, "symbol": "mdi:mdi-wifi-off", "symbolOn": "mdi:mdi-wifi"}'
   RESULT="{ \"type\":\"SC\",\"address\":${ADDRESS},\"kind\":\"status\",\"text\":\"${SSID}\",\"valid\":true,\"state\":${STATE},\"parameter\": ${PARAMETER} }"
   echo -n ${RESULT}

elif [[ "${COMMAND}" == "toggle" ]]; then

   if [[ "$STATE" == "true" ]]; then
      nmcli radio wifi off
      $LOGGER "wifi-switch.sh: WiFi disabled"
   else
      nmcli radio wifi on
      $LOGGER "wifi-switch.sh: WiFi enabled"
   fi
   sleep 2
   exec "$0" status "$ADDRESS" "$MQTTURL"

elif [[ "${COMMAND}" == "status" ]]; then

   mosquitto_pub --quiet -L ${MQTTURL} -m "{ \"image\":\"${IMAGE}\",\"type\":\"SC\",\"address\":${ADDRESS},\"kind\":\"status\",\"state\":${STATE},\"text\":\"${SSID}\" }"

fi

exit 0
