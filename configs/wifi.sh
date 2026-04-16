#! /bin/bash

LANG=C

COMMAND="$1"
ADDRESS="$2"
MQTTURL="$3"

STATE="false"
IMAGE="mdi:mdi-wifi-off"

LOGGER="logger -t sensormqtt -p kern.warn"

SSID=""
BARS=""

LINE=$(nmcli -t -f IN-USE,SSID,BARS dev wifi | awk -F: '$1=="*"')

SSID=$(printf '%s\n' "${LINE}" | awk -F: '{
  $1=""; $NF="";
  sub(/^:/,""); sub(/:$/,"");
  print
}')

BARS=$(printf '%s\n' "$LINE" | awk -F: '{print $NF}')

if [ -n "${SSID}" ]; then
   STATE="true"

   STRENGTH=$(printf "%s" "$BARS" | tr -cd '*' | wc -c)
   [ "${STRENGTH}" -lt 1 ] && STRENGTH=1
   [ "${STRENGTH}" -gt 4 ] && STRENGTH=4
   IMAGE="mdi:mdi-wifi-strength-${STRENGTH}"
fi

if [[ "${COMMAND}" == "init" ]]; then
   PARAMETER='{"cloneable": false, "symbol": "mdi:mdi-wifi-off", "symbolOn": "mdi:mdi-wifi"}'
   RESULT="{ \"type\":\"SC\",\"address\":${ADDRESS},\"kind\":\"status\",\"text\":\"${SSID}\",\"valid\":true,\"state\":${STATE},\"parameter\": ${PARAMETER} }"
   echo -n ${RESULT}
elif [[ "${COMMAND}" == "toggle" ]]; then
   ${LOGGER} "wifi.sh: toggle called"
   mosquitto_pub --quiet -L ${MQTTURL} -m "{ \"image\":\"${IMAGE}\",\"type\":\"SC\",\"address\":${ADDRESS},\"kind\":\"status\",\"state\":${STATE},\"text\":\"${SSID}\" }"
elif [[ "${COMMAND}" == "status" ]]; then
   mosquitto_pub --quiet -L ${MQTTURL} -m "{ \"image\":\"${IMAGE}\",\"type\":\"SC\",\"address\":${ADDRESS},\"kind\":\"status\",\"state\":${STATE},\"text\":\"${SSID}\" }"
fi

exit 0
