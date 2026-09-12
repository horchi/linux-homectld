#!/bin/bash

COMMAND="$1"
MIN_STRENGTH="${2:-0}"   # Default = 0

# check ob bereits ein 'nmcli' rescan aktiv ist
#  wenn nicht neuen anstoßen

if ! pgrep -f "nmcli.*wifi.*rescan" > /dev/null; then
   # im hintergrund komplett vom aufrufenden c++ prozess entkoppelt
   nohup nmcli dev wifi rescan >/dev/null 2>&1 < /dev/null &
fi

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
   # stored wifi profiles; 'iface' is the interface the profile is bound to ('' = any),
   # 'ssid' the network name (may differ from the profile name, e.g. 'hierimhaus 2')
   LC_ALL=C.UTF-8 nmcli -t -f name,uuid,autoconnect,autoconnect-priority,active,device,state,type connection show \
   | grep 'wireless' \
   | while IFS=: read -r name uuid autoconnect priority active device state type; do
        iface=$(nmcli -e no -g connection.interface-name connection show uuid "${uuid}")
        ssid=$(nmcli -e no -g 802-11-wireless.ssid connection show uuid "${uuid}")
        jq -n --arg network "${name}" --arg uuid "${uuid}" --arg ssid "${ssid}" --arg iface "${iface}" \
              --arg autoconnect "${autoconnect}" --arg priority "${priority}" --arg active "${active}" \
              --arg device "${device}" --arg state "${state}" --arg type "${type}" \
              '{network: $network, uuid: $uuid, ssid: $ssid, iface: $iface, autoconnect: $autoconnect,
                priority: $priority, active: $active, device: $device, state: $state, type: $type}'
     done \
   | jq -s '.' | json_pp -json_opt canonical,utf8

elif [ "${COMMAND}" == "wifi-dev" ]; then
   # the present wifi devices (USB sticks are all 'wlan0', only the MAC is unique)
   LC_ALL=C.UTF-8 nmcli -t -f device,type,state device status \
   | grep ':wifi:' \
   | while IFS=: read -r device type state; do
        nmcli -e no -g GENERAL.VENDOR,GENERAL.PRODUCT,GENERAL.HWADDR,GENERAL.DRIVER device show "${device}" \
        | jq -sR --arg device "${device}" --arg state "${state}" 'split("\n")
          | {device: $device, state: $state, vendor: .[0], product: .[1], mac: .[2], driver: .[3]}'
     done \
   | jq -s '.' | json_pp -json_opt canonical,utf8

else
   echo "Usage: $0 { wifi-list [0-4] | wifi-con | wifi-dev }"
fi

exit 0
