#! /bin/bash

COMMAND="$1"
ADDRESS="$2"
MQTTURL="$3"

STATE="true"

RESULT="{ \"type\":\"SC\",\"address\":$2,\"kind\":\"status\",\"valid\":true,\"value\":${STATE} }"
echo -n ${RESULT}

if [ "${COMMAND}" == "toggle" ]; then
   DIR=`dirname "$0"`
   ${DIR}/sysctl "restart" "$2" "$3" "homectld.service"
fi

if [ "${COMMAND}" == "status" ]; then
   mosquitto_pub --quiet -L ${MQTTURL} -m "{ \"type\":\"SC\",\"address\":${ADDRESS},\"kind\":\"status\",\"state\":${STATE} }"
fi

exit 0
