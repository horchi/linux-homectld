#! /bin/bash

LANG=C

COMMAND="$1"
ADDRESS="$2"
MQTTURL="$3"
JARGS="$4"

STATE="false"
LOGGER="logger -t sensormqtt -p kern.warn"

MAC=$(echo "${JARGS}" | jq -r .mac)

if [[ -z "${MAC}" || "${MAC}" == "null" ]]; then
   ${LOGGER} "bt-switch.sh: mac argument missing, call with '{ \"mac\": \"AA:BB:CC:DD:EE:FF\" }'"
fi

# ── state ──────────────────────────────────────────────────────────────────────

BT_INFO=$(bluetoothctl info "${MAC}" 2>/dev/null)

if echo "${BT_INFO}" | grep -q "Connected: yes"; then
   STATE="true"
   IMAGE="mdi:mdi-bluetooth-connect"
else
   STATE="false"
   IMAGE="mdi:mdi-bluetooth-off"
fi

NAME=$(echo "${JARGS}" | jq -r '.name // empty')
[ -z "${NAME}" ] && NAME=$(echo "${BT_INFO}" | grep "Name:" | sed 's/.*Name: //' | head -1)
[ -z "${NAME}" ] && NAME="${MAC}"

# ── commands ───────────────────────────────────────────────────────────────────

if [[ "${COMMAND}" == "init" ]]; then

   PARAMETER='{"cloneable": true, "symbol": "mdi:mdi-bluetooth-off", "symbolOn": "mdi:mdi-bluetooth-connect"}'
   RESULT="{ \"type\":\"SC\",\"address\":${ADDRESS},\"kind\":\"status\",\"text\":\"${NAME}\",\"valid\":true,\"state\":${STATE},\"parameter\": ${PARAMETER} }"
   echo -n ${RESULT}

elif [[ "${COMMAND}" == "toggle" ]]; then

   if [[ "${STATE}" == "true" ]]; then
      bluetoothctl disconnect "${MAC}" >/dev/null 2>&1
      ${LOGGER} "bt-switch.sh: disconnected ${MAC} (${NAME})"
   else
      bluetoothctl pair "${MAC}" >/dev/null 2>&1
      bluetoothctl connect "${MAC}" >/dev/null 2>&1
      ${LOGGER} "bt-switch.sh: connected ${MAC} (${NAME})"
   fi
   sleep 3
   exec "$0" status "${ADDRESS}" "${MQTTURL}" "${JARGS}"

elif [[ "${COMMAND}" == "status" ]]; then

   mosquitto_pub --quiet -L ${MQTTURL} -m "{ \"image\":\"${IMAGE}\",\"type\":\"SC\",\"address\":${ADDRESS},\"kind\":\"status\",\"state\":${STATE},\"text\":\"${NAME}\" }"

fi

exit 0
