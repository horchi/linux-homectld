#! /bin/bash

MQTTURL="$1"
MSG="$2"

LOGGER="logger -t sensormqtt -p kern.warn"

sensor()
{
   ADDRESS=${1}
   ELEMENT="${2}"
   TITLE="${3}"
   UNIT="${4}"

   _MSG=`echo ${MSG} | jq -r "${ELEMENT}"`

   value="false"

   if [[ "${_MSG}" == "ON" ]]; then
      value="true"
   fi

   RESULT=$(jo \
               action="update" \
               type="BBQ" \
               address=${ADDRESS} \
               kind="status" \
               title="${TITLE}" \
               unit="${UNIT}" \
               valid=true \
               rights=2 \
               state=${value})

   ${LOGGER} "tele_bbqlight_STATE.sh: (${MQTTURL}) -> ${RESULT}"
   mosquitto_pub --quiet -L ${MQTTURL} -m "${RESULT}"
}

sensor 1 '.POWER' 'BBQ Light' 'zst'
