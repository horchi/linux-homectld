#!/bin/bash

mode="unknown"
STATE_FILE="/run/lan-mode.state"

if [ -r ${STATE_FILE} ]; then
   mode=`cat ${STATE_FILE}`
fi

RESULT="{ \"type\":\"SC\",\"address\":$2,\"kind\":\"value\",\"valid\":true,\"text\":\"${mode}\",\"unit\":\"\" }"
echo -n ${RESULT}

if [ "$1" != "init" ]; then
   mosquitto_pub -L "$3" -m "${RESULT}"
fi
