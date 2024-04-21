#!/bin/bash

COMMAND="$1"
ADDRESS="$2"
MQTTURL="$3"

STATE="false"

MOUNT_POINT="/gate.video"

if grep -q "${MOUNT_POINT}" /proc/mounts; then
   STATE="true"
fi

if [[ "${COMMAND}" == "toggle" ]]; then
   if [[ "${STATE}" == true ]]; then
      sync
      umount "${MOUNT_POINT}"
   else
      mount "${MOUNT_POINT}"
   fi
fi

if grep -q "${MOUNT_POINT}" /proc/mounts; then
   STATE="true"
else
   STATE="false"
fi

RESULT="{ \"type\":\"SC\",\"address\":${ADDRESS},\"kind\":\"status\",\"valid\":true,\"state\":${STATE} }"

echo -n ${RESULT}

if [ "${COMMAND}" != "init" ]; then
   mosquitto_pub -L "$3" -m "${RESULT}"
fi
