#! /bin/bash

# Enable / disable a music folder in LMS (Lyrion Music Server)
#   on:  symlink <musicdir>/<name> -> <source> and trigger LMS rescan
#   off: remove the symlink and trigger LMS rescan

# Parameters:
#   1 - command {init|start|stop|status|toggle}
#   2 - address
#   3 - answer topic
#   4 - json arguments, e.g.
#       { "source": "/tank/mediaroot/Weihnachtslieder", "musicdir": "/tank/mediaroot/music" }
#       optional: "name" (link name, default basename of source), "host" (default localhost), "port" (default 9090)

COMMAND="$1"
ADDRESS="$2"
MQTTURL="$3"
JARGS="$4"

LOGGER="logger -t lms-folder"

export HOME=/var/lib/homectld

if [[ "${COMMAND}" == "init" ]]; then

   PARAMETER=$(jq -n '{
      cloneable: true,
      symbol: "mdi:mdi-folder-music-outline",
      symbolOn: "mdi:mdi-folder-music",
      colorOn: "rgb(40, 172, 45)",
      options: {
         note: "source: folder to add\nmusicdir: music folder of LMS\nname: name of the link (optional, default: basename of source)\nhost: LMS host (optional, default: localhost)\nport: LMS CLI port (optional, default: 9090)",
         example: { source: "/tank/mediaroot/Weihnachtslieder", musicdir: "/tank/mediaroot/music" }
         }
   }')

   RESULT=$(jq -n \
      --argjson address "${ADDRESS}" \
      --argjson param "${PARAMETER}" \
      '{
         type: "SC",
         address: $address,
         kind: "status",
         valid: true,
         value: false,
         parameter: $param
      }')

   echo -n ${RESULT}
   exit 0
fi

SOURCE=$(echo "${JARGS}" | jq -r '.source // empty')
MUSICDIR=$(echo "${JARGS}" | jq -r '.musicdir // empty')
NAME=$(echo "${JARGS}" | jq -r '.name // empty')
LMS_HOST=$(echo "${JARGS}" | jq -r '.host // "localhost"')
LMS_CLI_PORT=$(echo "${JARGS}" | jq -r '.port // 9090')

if [[ -z "${SOURCE}" || -z "${MUSICDIR}" ]]; then
   ${LOGGER} "lms-folder.sh: argument missing, call with '{ \"source\": \"/path/to/folder\", \"musicdir\": \"/path/to/lms/music\" }'"
   exit 1
fi

[[ -z "${NAME}" ]] && NAME=$(basename "${SOURCE}")
LINK="${MUSICDIR}/${NAME}"

isOn()
{
   [[ -L "${LINK}" ]]
}

rescan()
{
   echo "rescan" | timeout 3 nc -q 1 ${LMS_HOST} ${LMS_CLI_PORT} > /dev/null 2>&1 \
      || ${LOGGER} "LMS rescan request failed"
}

enable()
{
   isOn && return

   if [[ -e "${LINK}" ]]; then
      ${LOGGER} "${LINK} exists but is no symlink, not touching it"
      return
   fi

   if [[ ! -d "${SOURCE}" ]]; then
      ${LOGGER} "${SOURCE} not found"
      return
   fi

   ln -sr "${SOURCE}" "${LINK}" && rescan
}

disable()
{
   isOn || return
   rm -f "${LINK}" && rescan
}

case "${COMMAND}" in
   start)
      enable
      ;;

   stop)
      disable
      ;;

   toggle)
      if isOn; then
         disable
      else
         enable
      fi
      ;;

   status)
      ;;

   *)
      echo "Usage: {start|stop|status|toggle|init}"
      exit 1
      ;;

esac

if isOn; then
   STATE="true"
else
   STATE="false"
fi

mosquitto_pub --quiet -L ${MQTTURL} -m "{ \"type\":\"SC\",\"address\":${ADDRESS},\"kind\":\"status\",\"state\":${STATE} }"

exit 0
