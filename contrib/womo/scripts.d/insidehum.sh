#!/bin/bash

# ping and wait up to 1 second to avtivate the sensor
ping -q -c 1 -W 1 dht >/dev/null

temp=`/usr/local/bin/hhact hum dht`
temp=`echo ${temp} | sed s/","/"."/g`

VALID="true"

if [[ -z "${temp}" ]]; then
   temp=0
   VALID="false"
fi

RESULT="{ \"type\":\"SC\",\"address\":$2,\"kind\":\"value\",\"valid\":${VALID},\"value\":${temp},\"unit\":\"%\" }"
echo -n ${RESULT}

if [ "$1" != "init" ]; then
   mosquitto_pub -L "$3" -m "${RESULT}"
fi
