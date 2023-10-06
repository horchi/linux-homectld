#!/bin/bash

temp=`cat /sys/class/thermal/thermal_zone0/temp`
temp=$(echo $temp*0.001 | bc)
temp=`echo ${temp} | sed s/","/"."/g`

RESULT="{ \"type\":\"SC\",\"address\":$2,\"kind\":\"value\",\"valid\":true,\"value\":${temp},\"unit\":\"°C\" }"
echo -n ${RESULT}

if [ "$1" != "init" ]; then
   mosquitto_pub -L "$3" -m "${RESULT}"
fi
