#!/bin/bash

COMMAND="$1"
MIN_STRENGTH="${2:-0}"   # Default = 0

nmcli dev wifi rescan

if [ "${COMMAND}" == "wifi-list" ]; then
   LC_ALL=C.UTF-8 nmcli -f bssid,ssid,mode,chan,rate,signal,bars,security,active,in-use -t dev wifi \
   | sed 's/\\:/-/g' \
   | jq -sR --argjson min "${MIN_STRENGTH}" 'split("\n") | map(select(length > 0)) | map(split(":"))
     | map({
         "id": .[0],
         "network": .[1],
         "mode": .[2],
         "channel": .[3],
         "rate": .[4],
         "signal": (. [5] | tonumber),
         "bars": .[6],
         "security": .[7],
         "active": .[8],
         "inuse": .[9],
         "strength": (. [6] | gsub("_";"") | length)
       })
     | map(select(.strength >= $min))
   ' | json_pp -json_opt canonical,utf8

elif [ "${COMMAND}" == "wifi-con" ]; then
   LC_ALL=C.UTF-8 nmcli -t -f name,autoconnect,autoconnect-priority,active,device,state,type connection show \
   | grep 'wireless' \
   | jq -sR 'split("\n")
     | map(select(length > 0))
     | map(split(":"))
     | map({
         "network": .[0],
         "autoconnect": .[1],
         "priority": .[2],
         "active": .[3],
         "device": .[4],
         "state": .[5],
         "type": .[6]
       })
   ' | json_pp -json_opt canonical,utf8

else
   echo "Usage: $0 { wifi-list [0-4] | wifi-con }"
fi

exit 0
