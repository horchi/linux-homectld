
BACKUP_DIR="/root/backup/database"
HOST=`hostname`
DATE=`date +%Y%m%d`

TABLES="dashboards dashboardwidgets deconzl deconzs groups homematic schemaconf sensoralert states valuetypes samples peaks users scripts valuefacts config"

mkdir -p ${BACKUP_DIR}/${DATE}

for table in ${TABLES}; do
   echo backup $table
   MYSQL_PWD=homectl mysqldump --opt -u homectl homectl ${table} | gzip > "$BACKUP_DIR/$DATE/${table}-dump.sql.gz"
done
