#! /bin/bash

COMMAND="$1"
ADDRESS="$2"
MQTTURL="$3"

DIR=`dirname "$0"`

${DIR}/sysctl "${COMMAND}" "${ADDRESS}" "${MQTTURL}" "lte-modem.service" "mdi:mdi-signal-off" "mdi:mdi-signal"

if [[ "${COMMAND}"  =~ ^(start|stop|toggle)$ ]]; then
   sleep 1
   mosquitto_pub --quiet -L ${MQTTURL} -m '{ "action": "trigger", "script": "lte.sh" }'
   mosquitto_pub --quiet -L ${MQTTURL} -m '{ "action": "trigger", "msqdevice": "lte.sh" }'
fi
