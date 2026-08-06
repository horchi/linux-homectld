#! /bin/bash

COMMAND="$1"
ADDRESS="$2"
MQTTURL="$3"
JARGS="$4"
DIR=`dirname "$0"`
STATE="false"

if [ "${COMMAND}" == "init" ]; then
   PARAMETER="{\"cloneable\": true, \"symbol\": \"${SYMBOL}\", \"symbolOn\": \"${SYMBOLON}\"}"
   RESULT="{ \"type\":\"SC\",\"address\":${ADDRESS},\"kind\":\"status\",\"value\":${STATE},\"parameter\": ${PARAMETER} }"
   echo -n ${RESULT}
   exit 0
fi

SERVICE=`echo ${JARGS} | jq -r .service`

if [[ -z "${SERVICE}" ]]; then
   ${LOGGER} "system.sh: SERVICE argument missing, call with '{ \"service\": \"your service\"}'"
   exit 1
fi

${DIR}/sysctl "${COMMAND}" "${ADDRESS}" "${MQTTURL}" "${SERVICE}" "mdi:mdi-replay"
