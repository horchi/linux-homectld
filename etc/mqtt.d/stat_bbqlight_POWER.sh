#! /bin/bash

MQTTURL="${1}"
INTOPIC="${2}"
MSG="${3}"
CALL_COUNT=${4}

LOGGER="logger -t sensormqtt -p kern.warn"
ACTION="update"

if [[ ${CALL_COUNT} -le 1 ]]; then
   ACTION="init"
fi

sensor()
{
   ADDRESS=${1}
   TITLE="${2}"
   UNIT="${3}"

   value="false"

   if [[ "${MSG}" == "ON" ]]; then
      value="true"
   fi

   if [[ "${ACTION}" == "init" ]]; then
      RESULT=$(jo \
                  action="${ACTION}" \
                  type="BBQ" \
                  address=${ADDRESS} \
                  kind="status" \
                  title="${TITLE}" \
                  unit="${UNIT}" \
                  valid=true \
                  rights=2 \
                  topic="cmnd/bbqlight/POWER${ADDRESS}" \
                  state=${value})
   else
      RESULT=$(jo \
                  action="${ACTION}" \
                  type="BBQ" \
                  address=${ADDRESS} \
                  kind="status" \
                  title="${TITLE}" \
                  unit="${UNIT}" \
                  valid=true \
                  rights=2 \
                  state=${value})
   fi

   ${LOGGER} "stat_bbqlight_POWER.sh: (${MQTTURL}) -> ${RESULT} (for topic ${INTOPIC})"
   mosquitto_pub --quiet -L ${MQTTURL} -m "${RESULT}"
}

# assume INTOPIC name is like
#   stat/bbqlight/POWER1
#   stat/bbqlight/POWER2
#   ...

ADDR=$(echo ${INTOPIC} | grep -Eo '[0-9]+$')

sensor ${ADDR} "BBQ Light ${ADDR}" "zst"
