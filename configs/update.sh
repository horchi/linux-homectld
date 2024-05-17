#! /bin/bash

COMMAND="$1"
ADDRESS="$2"
MQTTURL="$3"

GIT_ROOT=/root/source/linux-homectld
STATE="true"
LOGGER="logger -t homectld -p kern.warn"

if [[ ! -d "${GIT_ROOT}" ]]; then
	STATE="false"
fi

if ping -q -c 1 -W 1 8.8.8.8 >/dev/null; then
	echo -n
else
	STATE="false"
fi

RESULT="{ \"type\":\"SC\",\"address\":$2,\"kind\":\"status\",\"valid\":true,\"value\":${STATE} }"
echo -n ${RESULT}

if [[ "${COMMAND}" == "toggle" ]]; then
	if [[ ! -d "${GIT_ROOT}" ]]; then
		${LOGGER} "Abort update, directory ${GIT_ROOT} not found"
	elif [[ "${STATE}" == "false" ]]; then
		${LOGGER} "update.sh: internet connection is not available"
	else
		cd "${GIT_ROOT}"
		${LOGGER} "update.sh: git pull"
		git pull >/dev/null 2>&1 | ${LOGGER}
		${LOGGER} "update.sh: rebuild"
		make -s clean >/dev/null 2>&1 | ${LOGGER}
		make -j >/dev/null 2>&1 | ${LOGGER}
		${LOGGER} "update.sh: install"
		make linstall >/dev/null 2>&1 | ${LOGGER}
		${LOGGER} "update.sh: restart"
		/bin/systemctl restart homectld.service 2>&1 | ${LOGGER}
	fi
fi

if [ "${COMMAND}" == "status" ]; then
   mosquitto_pub --quiet -L ${MQTTURL} -m "{ \"type\":\"SC\",\"address\":${ADDRESS},\"kind\":\"status\",\"state\":${STATE} }"
fi

exit 0
