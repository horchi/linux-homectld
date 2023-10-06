#!/bin/bash

temp=`/usr/local/bin/hhact hum dht_inside`
temp=`echo ${temp} | sed s/","/"."/g`

if [[ -z "${temp}" ]]; then
   exit 0
fi

RESULT="{ \"type\":\"SC\",\"address\":$2,\"kind\":\"value\",\"valid\":true,\"value\":${temp},\"unit\":\"%\" }"

echo -n ${RESULT}

if [ "$1" != "init" ]; then
   mosquitto_pub -L "$3" -m "${RESULT}"
fi
