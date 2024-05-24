#!/bin/bash

COMMAND="$1"
ADDRESS="$2"
MQTTURL="$3"
JARGS="$4"

LOGGER="logger -t homectld -p kern.warn"

# Argument examples
#  '{ "mountpoint" : "/usbHdd", "device": "/dev/sda3" }'
#  '{ "mountpoint" : "/mnt/bar"}'

# MOUNT_POINT="/usbHdd"
# MOUNT_DEVICE="/dev/sda3"

MOUNT_POINT=`echo ${JARGS} | jq -r .mountpoint`
MOUNT_DEVICE=`echo ${JARGS} | jq -r .device`

STATE="false"

if [[ -z "${MOUNT_POINT}" ]]; then
   ${LOGGER} "mount.sh: mountpoint argument missing, call with '{ \"mountpoint\" : \"/usbHdd\", \"device\": \"/dev/sda3\" }'"
   STATE="false"
elif grep -q "${MOUNT_POINT}" /proc/mounts; then
   STATE="true"
fi

if [[ "${COMMAND}" == "init" ]]; then
   PARAMETER='{"cloneable": true, "symbol": "mdi:mdi-harddisk", "symbolOn": "mdi:mdi-harddisk", "color": "rgb(193, 193, 193)", "colorOn": "rgb(235, 197, 5)"}'
   RESULT="{ \"type\":\"SC\",\"address\":${ADDRESS},\"kind\":\"status\",\"valid\":true,\"state\":${STATE}, \"parameter\": ${PARAMETER} }"
   echo -n ${RESULT}
fi

if [[ -z "${MOUNT_POINT}" ]]; then
   RESULT="{ \"type\":\"SC\",\"address\":${ADDRESS},\"kind\":\"status\",\"valid\":true,\"state\":${STATE} }"
   mosquitto_pub -L "$3" -m "${RESULT}"
   exit
fi

if [ "${COMMAND}" == "status" ]; then
   RESULT="{ \"type\":\"SC\",\"address\":${ADDRESS},\"kind\":\"status\",\"valid\":true,\"state\":${STATE} }"
   mosquitto_pub -L "$3" -m "${RESULT}"
elif [[ "${COMMAND}" == "toggle" ]]; then
   if [[ "${STATE}" == true ]]; then
      sync
      umount "${MOUNT_POINT}"
   elif [[ "${MOUNT_DEVICE}" != "null" ]]; then
      mount "${MOUNT_DEVICE}" "${MOUNT_POINT}"
   else
      mount "${MOUNT_POINT}"
   fi

   if grep -q "${MOUNT_POINT}" /proc/mounts; then
      STATE="true"
   else
      STATE="false"
   fi

   RESULT="{ \"type\":\"SC\",\"address\":${ADDRESS},\"kind\":\"status\",\"valid\":true,\"state\":${STATE} }"
   mosquitto_pub -L "$3" -m "${RESULT}"
fi
