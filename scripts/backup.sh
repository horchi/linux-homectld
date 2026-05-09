#!/bin/bash

LC_ALL=C.UTF-8

BACKUP_DIR="/root/backup/database"
HOST=`hostname`
DATE=`date +%Y%m%d`

TABLES="config dashboards dashboardwidgets deconzl deconzs groups homematic iostates peaks samples schemaconf sensoralert users valuefacts valuetypes"

mkdir -p ${BACKUP_DIR}/${DATE}

for table in ${TABLES}; do
   echo "backup table ${table}"
   MYSQL_PWD=homectl mysqldump --opt -u homectl homectl ${table} | gzip > "${BACKUP_DIR}/${DATE}/${table}-dump.sql.gz"
done

FILES="/etc/default/bmsmqtt \
/etc/default/fwpn \
/etc/default/i2cmqtt \
/etc/default/logitechmediaserver \
/etc/default/minisatip \
/etc/default/mopeka2mqtt \
/etc/default/openvpn \
/etc/default/squeezelite \
/etc/default/thetford2mqtt \
/etc/default/vdr \
/etc/default/victronmqtt \
/etc/default/votromqtt \
/etc/default/w1mqtt \
/etc/udev/rules.d/99-usbftdi.rules \
/root/source/linux-homectld/Make.user \
/usr/src/linux-homectld/Make.user"

EXISTING_FILES=""

for f in ${FILES}; do
   [ -f "${f}" ] && EXISTING_FILES="${EXISTING_FILES} ${f}"
done

echo "backup config files ${EXISTING_FILES} .."
tar -czf "${BACKUP_DIR}/${DATE}/config.tgz" ${EXISTING_FILES} 2> >(grep -v "Removing leading" >&2)

echo "backup user images .."
tar -czf "${BACKUP_DIR}/${DATE}/user-images.tgz" "/var/lib/homectld/img/user/" 2> >(grep -v "Removing leading" >&2)

echo "backup package list .."
dpkg --get-selections | grep install > "${BACKUP_DIR}/${DATE}/packages.txt"
