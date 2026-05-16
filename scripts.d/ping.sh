#! /bin/bash

COMMAND="$1"
ADDRESS="$2"
MQTTURL="$3"
JARGS="$4"
STATE="true"

LOGGER="logger -t sensormqtt -p kern.warn"
IP=`echo ${JARGS} | jq -r .ip`
TIMEOUT=`echo ${JARGS} | jq -r .timeout`

if [[ -z "${TIMEOUT}" || "${TIMEOUT}" == "null" ]]; then
   TIMEOUT=3
fi

if [[ -z "${IP}" ]]; then
   ${LOGGER} "ping.sh: IP argument missing, call with '{ \"ip\": \"8.8.8.8\"}'"
   STATE="false"
elif ping -q -c 1 -W ${TIMEOUT} ${IP} >/dev/null; then
   echo -n
else
   STATE="false"
fi

if [[ "${COMMAND}" == "init" ]]; then

   PARAMETER=$(jq -n '{
      cloneable: true,
      symbol: "mdi:mdi-router-wireless-off",
      symbolOn: "mdi:mdi-router-wireless",
      options: {
         note: "ip - the ip; timeout - ping timeout",
         example: { ip: "8.8.8.8", timeout: 3 }
         }
   }')

   RESULT=$(jq -n \
      --argjson address "$2" \
      --argjson value "${STATE}" \
      --argjson param "${PARAMETER}" \
      '{
         type: "SC",
         address: $address,
         kind: "status",
         valid: true,
         value: $value,
         parameter: $param
      }')

   echo -n ${RESULT}

elif [[ "${COMMAND}" == "toggle" ]]; then
   ${LOGGER} "ping.sh: called with IP: ${IP}"
   mosquitto_pub --quiet -L ${MQTTURL} -m "{ \"type\":\"SC\",\"address\":${ADDRESS},\"kind\":\"status\",\"state\":${STATE} }"
elif [ "${COMMAND}" == "status" ]; then
   mosquitto_pub --quiet -L ${MQTTURL} -m "{ \"type\":\"SC\",\"address\":${ADDRESS},\"kind\":\"status\",\"state\":${STATE} }"
fi

exit 0
