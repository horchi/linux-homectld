#! /bin/bash

COMMAND="$1"
ADDRESS="$2"
MQTTURL="$3"

case "${COMMAND}" in
   start)
      echo 1 >> /tmp/foo
      ;;

   stop)
      rm -f /tmp/foo
      ;;

   toggle)
      if [[ -f /tmp/foo ]]; then
         rm -f /tmp/foo
      else
         echo 1 >> /tmp/foo
      fi
      ;;

   status)
      ;;

   init)
      ;;

   *)
      echo "Usage: {start|stop|status|toggle}"
      ;;

esac

if [[ -f /tmp/foo ]]; then
   RETVAL=0
else
   RETVAL=1
fi

if [ ${RETVAL} == 0 ]; then
   STATE="true"
else
   STATE="false"
fi

if [ "${COMMAND}" != "init" ]; then
   mosquitto_pub --quiet -L ${MQTTURL} -m "{ \"type\":\"SC\",\"address\":${ADDRESS},\"kind\":\"status\",\"state\":${STATE} }"
fi

echo -n "{ \"type\":\"SC\",\"address\":${ADDRESS},\"kind\":\"status\",\"value\":${STATE} }"

exit ${RETVAL}
