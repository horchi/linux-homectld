#!/bin/bash
#
# aggregate-samples-manual.sh <date> <interval>
#
# aggregates all single samples (aggregate 'S') up to <date> to the given interval [minutes],
# the same as the daemon does with 'aggregateHistory' / 'aggregateInterval' (HomeCtl::aggregate())
#   the samples of the GPS tours (aggregate 'T') are kept
#
# the database login is taken from /etc/homectld/daemon.conf (DbHost, DbPort, DbName, DbUser, DbPass)

CONF=${CONF:-/etc/homectld/daemon.conf}

if [[ ${#} -ne 2 ]] || ! [[ ${2} =~ ^[1-9][0-9]*$ ]]; then
   echo "Usage: "
   echo "  aggregate-samples-manual.sh <date> <interval>"
   echo "    - aggregates all samples up to <date> (like '2024-01-01') in the given <interval> [minutes]"
   exit 1
fi

DATE=${1}
INT=${2}

confItem()
{
   local value
   value=$(sed -n "s/^[[:space:]]*${1}[[:space:]]*=[[:space:]]*\(.*[^[:space:]]\)[[:space:]]*$/\1/p" ${CONF} 2>/dev/null | tail -1)
   echo "${value:-${2}}"
}

DB_HOST=$(confItem DbHost localhost)
DB_PORT=$(confItem DbPort 3306)
DB_NAME=$(confItem DbName homectl)
DB_USER=$(confItem DbUser homectl)
DB_PASS=$(confItem DbPass homectl)

sql()
{
   # for 'localhost' the socket is used, like the daemon does (a port forces TCP with the mariadb client)

   if [ "${DB_HOST}" = "localhost" ]; then
      mysql -u "${DB_USER}" -p"${DB_PASS}" -D "${DB_NAME}" -e "${1}"
   else
      mysql -h "${DB_HOST}" -P "${DB_PORT}" -u "${DB_USER}" -p"${DB_PASS}" -D "${DB_NAME}" -e "${1}"
   fi
}

BUCKET="CONCAT(DATE(time), ' ', SEC_TO_TIME((TIME_TO_SEC(time) DIV $((INT*60))) * $((INT*60)))) + INTERVAL ${INT} MINUTE"

# check statement

echo "Database '${DB_NAME}' at '${DB_HOST}', aggregating samples up to '${DATE}' to ${INT} minutes"
sql "select count(*) samples, min(time) first, max(time) last from samples where aggregate = 'S' and time <= '${DATE}';" || exit 1

read -p "Continue? [y/N] " -n 1 c
echo

if [ "${c}" != "y" ]; then
   exit 0
fi

sql "replace into samples
       select address, type, 'A' as aggregate,
         ${BUCKET} time,
         unix_timestamp(sysdate()) as inssp, unix_timestamp(sysdate()) as updsp,
         round(sum(value)/count(*), 2) as value, text, count(*) samples
       from
         samples
       where
         aggregate = 'S' and time <= '${DATE}'
       group by ${BUCKET}, address, type;" || exit 1

sql "delete from samples where aggregate = 'S' and time <= '${DATE}';" || exit 1

echo "done"
