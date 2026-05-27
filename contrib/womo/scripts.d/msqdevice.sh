#!/bin/bash

CMD="/usr/local/bin/msq.device"

if [[ "$1" == "toggle" ]]; then
   if [[ ${dev} == "wlan0" ]]; then
      ${CMD} -s "usb0";
   elif [[ ${dev} == "usb0" ]]; then
      ${CMD} -s "wwan0";
   else
      ${CMD} -s "wlan0";
   fi
fi

CHOICES=$(ip -o link show | awk -F': ' '{print $2}' | grep -v -E '^(lo|eth|en)' | paste -sd, -)
DEVICE=`${CMD}`
RESULT="{ \"type\":\"SC\",\"address\":$2,\"kind\":\"text\",\"text\":\"${DEVICE}\",\"choices\":\"${CHOICES}\"}"
echo -n ${RESULT}

if [ "$1" != "init" ]; then
   mosquitto_pub -L "$3" -m "${RESULT}"
fi
