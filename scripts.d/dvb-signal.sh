#! /bin/bash
#
# dvb-signal.sh - script sensor (switch) of homectld: DVB signal monitor for aligning the dish
#
#  toggle / start / stop: starts / stops a background loop which reads 'dvb-fe-tool --femon' and
#  publishes the signal level and C/N about once per second as the text of this sensor (widget
#  type SymbolText shows symbol and text, a click toggles the monitor). The state shows whether
#  the monitor runs; it stops itself after MAXRUN seconds, the frontend is a shared resource.
#
#  Needs the package 'dvb-tools' (dvb-fe-tool). Argument (settings of the sensor in the IO setup):
#  the adapter number, default 0.
#
#  Note: a lock and the C/N need a tuned frontend, e.g. a client streaming via minisatip / vdr;
#  without tuning only the raw signal strength is reported. If another process has the frontend
#  opened exclusively, dvb-fe-tool fails - the error is shown as text and the switch goes off.
#
# ---------------------------------------------------------------------------------------------------

COMMAND="$1"
ADDRESS="$2"
MQTTURL="$3"
ADAPTER="${4:-0}"

MAXRUN=1800                                   # [s] the monitor stops itself after this time
PIDFILE="/tmp/dvb-signal-${ADAPTER}.pid"
TEXTFILE="/tmp/dvb-signal-${ADAPTER}.txt"
SYMBOL="mdi:mdi-satellite-uplink"
SYMBOLON="mdi:mdi-satellite-variant"
TITLE="Sat-Signal"

export HOME=/var/lib/homectld

publish()      # $1 = state (true|false), $2 = text
{
   mosquitto_pub --quiet -L "${MQTTURL}" -m "{ \"type\":\"SC\",\"address\":${ADDRESS},\"kind\":\"status\",\"state\":$1,\"text\":\"$2\" }"
}

running()
{
   [[ -f "${PIDFILE}" ]] && kill -0 "$(cat "${PIDFILE}")" 2>/dev/null
}

# the background monitor (own session, so stop can kill it with dvb-fe-tool together)

monitor()
{
   echo $$ > "${PIDFILE}"
   : > "${TEXTFILE}"

   local start now last=0
   start=$(date +%s)

   # femon lines like: "Lock   (0x1f) Signal= -45.20dBm C/N= 12.30dB UCB= 0 postBER= 0 preBER= 3.10x10^-3 PER= 0"
   #   or with relative stats: "Signal= 64.12% C/N= 11.50dB ..."

   stdbuf -oL dvb-fe-tool -a "${ADAPTER}" --femon 2>&1 | while IFS= read -r line; do
      now=$(date +%s)
      (( now - start > MAXRUN )) && break
      (( now == last )) && continue              # at most one message per second
      last=${now}

      sig=$(sed -n 's/.*Signal= *\([-0-9.]*\) *\([a-zA-Z%]*\).*/\1 \2/p' <<< "${line}")
      cn=$(sed -n 's/.*C\/N= *\([-0-9.]*\) *\([a-zA-Z]*\).*/\1 \2/p' <<< "${line}")

      if [[ -z ${sig} && -z ${cn} ]]; then
         text=$(tr -d '"\\' <<< "${line}" | cut -c1-80)   # e.g. an error message of dvb-fe-tool
      else
         [[ ${line} == *Lock* ]] && text="Lock" || text="kein Lock"
         [[ -n ${sig} ]] && text+=" · Signal ${sig}"
         [[ -n ${cn} ]] && text+=" · C/N ${cn}"
      fi

      echo "${text}" > "${TEXTFILE}"
      publish true "${text}"
   done

   # ended (stopped, time is up or dvb-fe-tool failed) - keep an error text, otherwise 'aus'

   local off="aus"
   if [[ -s "${TEXTFILE}" ]] && ! grep -q "Signal\|C/N" "${TEXTFILE}"; then
      off=$(cat "${TEXTFILE}")
   fi

   rm -f "${PIDFILE}" "${TEXTFILE}"
   publish false "${off}"
}

start_monitor()
{
   running && return 0

   if ! command -v dvb-fe-tool >/dev/null 2>&1; then
      echo "dvb-fe-tool fehlt (Paket dvb-tools)" > "${TEXTFILE}"
      return 1
   fi

   setsid bash "$0" monitor "${ADDRESS}" "${MQTTURL}" "${ADAPTER}" >/dev/null 2>&1 < /dev/null &
   sleep 0.5                                  # let the monitor write its pid
}

stop_monitor()
{
   running || return 0

   kill -- -"$(cat "${PIDFILE}")" 2>/dev/null     # the whole session incl. dvb-fe-tool
   sleep 0.3
   rm -f "${PIDFILE}"
}

case "${COMMAND}" in
   monitor)
      monitor
      exit 0
      ;;
   start)
      start_monitor
      ;;
   stop)
      stop_monitor
      ;;
   toggle)
      if running; then
         stop_monitor
      else
         start_monitor
      fi
      ;;
   status|init)
      ;;
   *)
      echo "Usage: {start|stop|status|toggle|init}"
      exit 1
      ;;
esac

# the current state and text

if running; then
   STATE="true"
   VALUE=1
else
   STATE="false"
   VALUE=0
fi

TEXT="aus"
[[ -s "${TEXTFILE}" ]] && TEXT=$(cat "${TEXTFILE}")
[[ ${STATE} == "false" && ! -f "${PIDFILE}" ]] && rm -f "${TEXTFILE}"

if [ "${COMMAND}" != "init" ]; then
   publish ${STATE} "${TEXT}"
   exit 0
fi

PARAMETER="{\"cloneable\": false, \"widgettype\": 12, \"symbol\": \"${SYMBOL}\", \"symbolOn\": \"${SYMBOLON}\", \"colorOn\": \"rgb(40, 172, 45)\"}"
echo -n "{ \"type\":\"SC\",\"address\":${ADDRESS},\"kind\":\"status\",\"title\":\"${TITLE}\",\"unit\":\"\",\"value\":${VALUE},\"text\":\"${TEXT}\",\"parameter\": ${PARAMETER} }"

exit 0
