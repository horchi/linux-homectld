#!/bin/bash
#
# p4d-migrate.sh - takes over the history of the stand alone p4d (database 'p4') into the homectld
#
#   The sensors of the S 3200 have the prefix P4 in the homectld: VA -> P4VA, DO -> P4DO, DI -> P4DI,
#   AO -> P4AO, UD -> P4UD, SD -> P4SD, W1 -> P4W1, SC, CV, PA (the sensors have to exist as valuefacts there, see below).
#
#   What is taken over:
#     facts       the own titles of the sensors (usrtitle), only where the homectld has none; a sensor with an own title
#                 which is missing in the homectld is created (the others are created by p4mqttd / homectld on start)
#     samples     the measured values and states (WEA is not stored by the p4d)
#     peaks       min / max of the sensors (merged with the ones of the homectld)
#     errors      table errors -> p4errors (as history, they are not mailed)
#     pellets     table pellets -> p4pellets
#     alerts      the alerts of the sensors (table sensoralert, type VA -> P4VA ...), only simple ones (no 'subid'),
#                 the receivers and texts as they are - the mails are sent if 'Mail Benachrichtigung' is on in the homectld
#     schema      the elements of the schema of the p4d with their positions, UC -> P4UC (texts), CN -> P4CN (lines);
#                 existing elements without a position are filled, the sensors in the functions get the prefix
#
#   Nothing which exists in the homectld is overwritten (INSERT IGNORE, 'not exists'), what the homectld wrote
#   in the meantime stays, so the script can be called again and again, also while both are running.
#   The old database is only read.
#
#   Without --run nothing is written (dry run: shows what would be taken over).
#
# Usage:  p4d-migrate.sh [--run] [--only facts,samples,peaks,errors,pellets,alerts,schema] [--from YYYY-MM-DD] [--known-only]
#                        [--from-db p4] [--to-db homectl] [--mysql-opts "-uroot -p..."]
#   --run          really write
#   --only         only these parts (comma separated), default all
#   --from         samples: only those since this day (default all)
#   --known-only   samples: only those of sensors which exist as valuefacts in the target (default: all, a missing
#                  sensor is shown, e.g. one you deleted in the homectld; its samples are taken over anyway)
#   --from-db      database of the p4d (default p4)
#   --to-db        database of the homectld (default homectl)
#   --types        types of the p4d for the samples and peaks, default "VA DI DO AO UD SD W1 SC CV PA"
#   --mysql-opts   options for the mysql client (user, password, host), default none (socket, root)
#
# Both databases have to be on the same database server. Take a backup of the target database before
# the first run (mysqldump homectl > homectl.sql), the samples are some 26 million rows - the run takes
# a while, it shows its progress. Stop the p4d (and p4mqttd with -P) is not required.
#

# the types of the p4d which are taken over (the homectld types have the prefix P4)

TYPES="VA DI DO AO UD SD W1 SC CV PA"

FROMDB=p4
TODB=homectl
RUN=no
ONLY="facts,samples,peaks,errors,pellets,alerts,schema"
SINCE=""
KNOWN_ONLY=no
MYSQL_OPTS=""

while [ $# -gt 0 ]; do
   case "$1" in
      --run)        RUN=yes ;;
      --only)       ONLY="$2"; shift ;;
      --from)       SINCE="$2"; shift ;;
      --known-only) KNOWN_ONLY=yes ;;
      --types)      TYPES="$2"; shift ;;
      --from-db)    FROMDB="$2"; shift ;;
      --to-db)      TODB="$2"; shift ;;
      --mysql-opts) MYSQL_OPTS="$2"; shift ;;
      -h|--help)    sed -n '2,/^$/p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
      *)            echo "unknown option '$1' (see --help)"; exit 1 ;;
   esac
   shift
done


sql()
{
   mysql ${MYSQL_OPTS} -N -B -e "$1"
}

# a statement which writes: a failure stops the script (the statements before are committed, a new run continues)

sqlw()
{
   if ! mysql ${MYSQL_OPTS} -N -B -e "$1"; then
      echo "ERROR: the SQL statement failed, stopped (the parts before are done, run it again to continue)" >&2
      exit 1
   fi
}

has()
{
   [[ ",${ONLY}," == *",$1,"* ]]
}

say()
{
   echo "$(date +%H:%M:%S) $*"
}

# --------------------------------------------------------
# checks

if ! command -v mysql >/dev/null; then
   echo "mysql client not found"; exit 1
fi

for db in "$FROMDB" "$TODB"; do
   if [ "$(sql "select count(*) from information_schema.schemata where schema_name = '${db}'")" != "1" ]; then
      echo "database '${db}' not found (or no access)"; exit 1
   fi
done

for t in valuefacts samples peaks schemaconf p4errors p4pellets; do
   if [ "$(sql "select count(*) from information_schema.tables where table_schema = '${TODB}' and table_name = '${t}'")" != "1" ]; then
      echo "table '${TODB}.${t}' is missing - start the homectld (with the S 3200 support) once, it creates the tables"; exit 1
   fi
done

say "from '${FROMDB}' to '${TODB}'$( [ "$RUN" = yes ] && echo ', WRITING' || echo ', dry run (nothing is written, --run does)')"

SINCE_COND=""
[ -n "$SINCE" ] && SINCE_COND=" and time >= '${SINCE}'"

# --------------------------------------------------------
# facts - the own titles (usrtitle), an existing one in the homectld stays;
#   a missing sensor is created (factor 1, p4mqttd delivers the scaled values), name, title, unit, ... are
#   updated by p4mqttd / homectld on start

if has facts; then
   say "facts ..."
   list=$(printf "'%s'," $TYPES); list=${list%,}
   src="from ${FROMDB}.valuefacts s where s.type in (${list}) and ifnull(s.usrtitle, '') <> ''"

   n_fill=$(sql "select count(*) ${src} and exists (select 1 from ${TODB}.valuefacts t
                    where t.address = s.address and t.type = concat('P4', s.type) and ifnull(t.usrtitle, '') = '')")
   n_new=$(sql "select count(*) ${src} and not exists (select 1 from ${TODB}.valuefacts t
                   where t.address = s.address and t.type = concat('P4', s.type))")

   sql "select concat('  P4', s.type, ':0x', lower(hex(s.address)), '  \'', s.usrtitle, '\'',
                      if(t.address is null, '  (new sensor)', ''))
          from ${FROMDB}.valuefacts s left join ${TODB}.valuefacts t on t.address = s.address and t.type = concat('P4', s.type)
         where s.type in (${list}) and ifnull(s.usrtitle, '') <> '' and ifnull(t.usrtitle, '') = ''
          order by s.type, s.address" | while IFS= read -r line; do say "$line"; done

   if [ "$RUN" = yes ]; then
      sqlw "update ${TODB}.valuefacts t join ${FROMDB}.valuefacts s
               on t.address = s.address and t.type = concat('P4', s.type)
               set t.usrtitle = s.usrtitle
             where s.type in (${list}) and ifnull(s.usrtitle, '') <> '' and ifnull(t.usrtitle, '') = ''"
      sqlw "insert ignore into ${TODB}.valuefacts (address, type, inssp, updsp, state, record, unit, options, factor, name, title, usrtitle, rights)
           select s.address, concat('P4', s.type), s.inssp, s.updsp, s.state, s.record, s.unit, 0, 1, s.name, s.title, s.usrtitle, 0
             ${src}"
      say "facts: ${n_fill} titles set, ${n_new} sensors created"
   else
      say "facts: ${n_fill} titles would be set, ${n_new} sensors created"
   fi
fi

# --------------------------------------------------------
# samples - by sensor (the primary key starts with address, type) and year

if has samples; then
   say "samples ..."
   total_src=0
   total_new=0

   for type in $TYPES; do
      to="P4${type}"

      for address in $(sql "select address from ${FROMDB}.valuefacts where type = '${type}' order by address"); do
         range=$(sql "select ifnull(year(min(time)), 0), ifnull(year(max(time)), 0) from ${FROMDB}.samples where address = ${address} and type = '${type}'")
         first=${range%%$'\t'*}
         last=${range##*$'\t'}

         [ "$first" = "0" ] && continue

         src=$(sql "select count(*) from ${FROMDB}.samples where address = ${address} and type = '${type}'${SINCE_COND}")
         have=$(sql "select count(*) from ${TODB}.samples where address = ${address} and type = '${to}'")
         known=$(sql "select count(*) from ${TODB}.valuefacts where address = ${address} and type = '${to}'")

         [ "$src" = "0" ] && continue

         if [ "$known" = "0" ] && [ "$KNOWN_ONLY" = yes ]; then
            say "  ${to}:0x$(printf '%x' $address)  ${src} rows skipped (no such sensor in ${TODB})"
            continue
         fi

         note=""
         [ "$known" = "0" ] && note="  (no sensor ${to}:0x$(printf '%x' $address) in the valuefacts of ${TODB}, the samples are taken over anyway)"

         if [ "$RUN" = yes ]; then
            for year in $(seq "$first" "$last"); do
               sqlw "insert ignore into ${TODB}.samples (address, type, aggregate, time, inssp, updsp, value, text, samples)
                    select address, '${to}', aggregate, time, inssp, updsp, value, text, samples
                      from ${FROMDB}.samples
                     where address = ${address} and type = '${type}'
                       and time >= '${year}-01-01' and time < '$((year + 1))-01-01'${SINCE_COND}"
            done

            now=$(sql "select count(*) from ${TODB}.samples where address = ${address} and type = '${to}'")
            say "  ${to}:0x$(printf '%x' $address)  ${src} rows in ${FROMDB}, ${have} -> ${now} in ${TODB}${note}"
            total_new=$((total_new + now - have))
         else
            say "  ${to}:0x$(printf '%x' $address)  ${src} rows in ${FROMDB}, ${have} already in ${TODB}${note}"
         fi

         total_src=$((total_src + src))
      done
   done

   if [ "$RUN" = yes ]; then
      say "samples: ${total_new} rows new (of ${total_src} in ${FROMDB})"
   else
      say "samples: ${total_src} rows in ${FROMDB} would be checked"
   fi
fi

# --------------------------------------------------------
# peaks - min / max, merged

if has peaks; then
   say "peaks ..."
   list=$(printf "'%s'," $TYPES); list=${list%,}
   n=$(sql "select count(*) from ${FROMDB}.peaks where type in (${list})")

   if [ "$RUN" = yes ]; then
      sqlw "insert into ${TODB}.peaks (address, type, inssp, updsp, timemin, timemax, minv, maxv)
           select s.address, concat('P4', s.type), s.inssp, s.updsp, s.timemin, s.timemax, s.minv, s.maxv
             from ${FROMDB}.peaks s where s.type in (${list})
           on duplicate key update
              ${TODB}.peaks.timemin = if(values(minv) < ${TODB}.peaks.minv, values(timemin), ${TODB}.peaks.timemin),
              ${TODB}.peaks.minv    = least(${TODB}.peaks.minv, values(minv)),
              ${TODB}.peaks.timemax = if(values(maxv) > ${TODB}.peaks.maxv, values(timemax), ${TODB}.peaks.timemax),
              ${TODB}.peaks.maxv    = greatest(${TODB}.peaks.maxv, values(maxv))"
      say "peaks: ${n} rows merged"
   else
      say "peaks: ${n} rows would be merged"
   fi
fi

# --------------------------------------------------------
# errors - the history is not mailed (mailcnt >= 1)

if has errors; then
   say "errors ..."
   n=$(sql "select count(*) from ${FROMDB}.errors e where not exists
              (select 1 from ${TODB}.p4errors x where x.number = e.number and x.time1 = e.time1)")

   if [ "$RUN" = yes ]; then
      sqlw "insert into ${TODB}.p4errors (inssp, updsp, time1, time4, time2, number, info, state, text, mailcnt)
           select inssp, updsp, time1, time4, time2, number, info, state, text, greatest(ifnull(mailcnt, 0), 1)
             from ${FROMDB}.errors e where not exists
              (select 1 from ${TODB}.p4errors x where x.number = e.number and x.time1 = e.time1)"
      say "errors: ${n} rows taken over"
   else
      say "errors: ${n} of $(sql "select count(*) from ${FROMDB}.errors") rows would be taken over"
   fi
fi

# --------------------------------------------------------
# pellets

if has pellets; then
   say "pellets ..."
   n=$(sql "select count(*) from ${FROMDB}.pellets p where not exists
              (select 1 from ${TODB}.p4pellets x where x.time = p.time and x.amount = p.amount)")

   if [ "$RUN" = yes ]; then
      sqlw "insert into ${TODB}.p4pellets (inssp, updsp, time, amount, price, comment)
           select inssp, updsp, time, amount, price, comment
             from ${FROMDB}.pellets p where not exists
              (select 1 from ${TODB}.p4pellets x where x.time = p.time and x.amount = p.amount)"
      say "pellets: ${n} deliveries taken over"
   else
      say "pellets: ${n} of $(sql "select count(*) from ${FROMDB}.pellets") deliveries would be taken over"
   fi
fi

# --------------------------------------------------------
# alerts - table sensoralert, the type gets the prefix, those with a link to another alert (subid) are not taken

if has alerts; then
   say "alerts ..."
   list=$(printf "'%s'," $TYPES); list=${list%,}
   cond="from ${FROMDB}.sensoralert a where a.type in (${list}) and ifnull(a.subid, 0) = 0 and not exists
            (select 1 from ${TODB}.sensoralert x where x.type = concat('P4', a.type) and x.address = a.address
                and x.min = a.min and x.max = a.max and ifnull(x.msubject, '') = ifnull(a.msubject, ''))"
   n=$(sql "select count(*) ${cond}")
   linked=$(sql "select count(*) from ${FROMDB}.sensoralert where type in (${list}) and ifnull(subid, 0) <> 0")
   [ "$linked" != "0" ] && say "alerts: ${linked} alerts with a link to another alert are not taken"

   if [ "$RUN" = yes ]; then
      sqlw "insert into ${TODB}.sensoralert (inssp, updsp, kind, subid, lgop, address, type, state, min, max, rangem, delta, maddress, msubject, mbody, lastalert, maxrepeat)
           select a.inssp, a.updsp, a.kind, a.subid, a.lgop, a.address, concat('P4', a.type), a.state, a.min, a.max, a.rangem, a.delta, a.maddress, a.msubject, a.mbody, a.lastalert, a.maxrepeat
             ${cond}"
      say "alerts: ${n} taken over"
   else
      say "alerts: ${n} would be taken over"
   fi
fi

# --------------------------------------------------------
# schema - the elements: UC -> P4UC, CN -> P4CN, the sensors get the prefix, too
#   - existing elements without properties (placeholders of an earlier take over, the position is missing) are filled
#   - missing ones are inserted
#   - the sensors in the functions ("VA:0x0", getItem(...)) get the prefix, too

if has schema; then
   say "schema ..."
   list=$(printf "'%s'," $TYPES UC CN); list=${list%,}

   # the type in the functions: getItem("VA:0x1f") -> getItem("P4VA:0x1f")
   REWRITE="regexp_replace(s.function, '([\\\\x22\\\\x27])(VA|DI|DO|AO|UD|SD|W1|SC|CV|PA):0x', '\\\\1P4\\\\2:0x')"

   n_fill=$(sql "select count(*) from ${FROMDB}.schemaconf s join ${TODB}.schemaconf t
                  on t.address = s.address and t.type = concat('P4', s.type)
                 where s.type in (${list}) and s.properties is not null and ifnull(t.properties, '') = ''")
   n_new=$(sql "select count(*) from ${FROMDB}.schemaconf s where s.type in (${list}) and not exists
                  (select 1 from ${TODB}.schemaconf x where x.address = s.address and x.type = concat('P4', s.type))")

   if [ "$RUN" = yes ]; then
      sqlw "update ${TODB}.schemaconf t join ${FROMDB}.schemaconf s
               on t.address = s.address and t.type = concat('P4', s.type)
               set t.state = s.state, t.kind = s.kind, t.width = s.width, t.height = s.height, t.showunit = s.showunit,
                   t.showtitle = s.showtitle, t.usrtext = s.usrtext, t.function = ${REWRITE}, t.properties = s.properties
             where s.type in (${list}) and s.properties is not null and ifnull(t.properties, '') = ''"
      sqlw "insert ignore into ${TODB}.schemaconf (address, type, inssp, updsp, state, kind, width, height, showunit, showtitle, usrtext, function, properties)
           select s.address, concat('P4', s.type), s.inssp, s.updsp, s.state, s.kind, s.width, s.height, s.showunit, s.showtitle, s.usrtext, ${REWRITE}, s.properties
             from ${FROMDB}.schemaconf s where s.type in (${list})"
      # elements taken over earlier have the old types in their functions
      sqlw "update ${TODB}.schemaconf t
               set t.function = ${REWRITE//s.function/t.function}
             where t.type like 'P4%' and t.function rlike '[\\\\x22\\\\x27](VA|DI|DO|AO|UD|SD|W1|SC|CV|PA):0x'"
      say "schema: ${n_fill} elements filled (position, ...), ${n_new} new"
   else
      say "schema: ${n_fill} elements would be filled (position, ...), ${n_new} new"
   fi
fi

say "done$( [ "$RUN" = yes ] || echo ' (dry run)')"
