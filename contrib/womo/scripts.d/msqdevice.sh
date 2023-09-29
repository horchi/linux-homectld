#!/bin/bash

CMD="/usr/local/bin/msq.device"
dev=`${CMD}`

if [[ "$1" == "toggle" ]]; then
   if [[ ${dev} == "wlan0" ]]; then
      ${CMD} -s "usb0";
   else
      ${CMD} -s "wlan0";
   fi

   /bin/systemctl restart fwpn
fi

dev=`${CMD}`
RESULT="{ \"type\":\"SC\",\"address\":$2,\"kind\":\"text\",\"text\":\"${dev}\",\"choices\":\"wlan0,usb0\"}"
echo -n ${RESULT}

if [ "$1" != "init" ]; then
   mosquitto_pub -L "$3" -m "${RESULT}"
fi
