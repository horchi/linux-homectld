#! /bin/bash

COMMAND="$1"
ADDRESS="$2"
MQTTURL="$3"
SERVICE="fwpn"

LOGGER="logger -t sensormqtt -p kern.warn"
SYSTEMCTL=systemctl
MSQ_DEV="offline"

SYMBOL="mdi:mdi-toggle-switch-off-outline"
SYMBOLON="mdi:mdi-toggle-switch"

# DIR=`dirname "$0"`
# ${DIR}/sysctl "$1" "$2" "$3" "fwpn"

export HOME=/var/lib/homectld

case "${COMMAND}" in
    start)
        ${SYSTEMCTL} start --quiet ${SERVICE}
        ;;

    stop)
        ${SYSTEMCTL} stop --quiet ${SERVICE}
        ;;

    restart)
        ${SYSTEMCTL} restart --quiet ${SERVICE}
        ;;

    toggle)
        ${SYSTEMCTL} is-active --quiet ${SERVICE}
        if [ $? == 0 ]; then
            ${SYSTEMCTL} stop --quiet ${SERVICE}
        else
           ${SYSTEMCTL} start --quiet ${SERVICE}
        fi
        ;;

    status)
        ;;

    init)
        ;;

    *)
       echo "Usage: {start|stop|status|toggle|init}"
       ;;

esac

# check state

${SYSTEMCTL} is-active --quiet ${SERVICE}
RETVAL="$?"

if [ ${RETVAL} == 0 ]; then
   STATE="true"
else
   STATE="false"
fi

# ${LOGGER} "firewall: command $COMMAND; ret $RETVAL; state $STATE"

# check MSQ device

DEV=$(fwpn status | awk '$3 == "MASQUERADE" && $7 !~ /^tun/ {print $7; exit}')

if [[ -n ${DEV} ]]; then
   MSQ_DEV=${DEV}
fi

if [ "${COMMAND}" != "init" ]; then
   mosquitto_pub --quiet -L ${MQTTURL} -m "{ \"type\":\"SC\",\"address\":${ADDRESS},\"kind\":\"status\",\"state\":${STATE}, \"text\":\"${MSQ_DEV}\" }"
   exit 0
fi

PARAMETER="{\"cloneable\": false, \"unit\":\"vtxt\",\"symbol\": \"${SYMBOL}\", \"symbolOn\": \"${SYMBOLON}\"}"
RESULT="{ \"type\":\"SC\",\"address\":${ADDRESS},\"kind\":\"status\",\"value\":${STATE},\"parameter\": ${PARAMETER} }"
echo -n ${RESULT}

exit ${RETVAL}
