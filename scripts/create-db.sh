#!/bin/bash
#
# create-db.sh [<name>]
#
# initial setup of the local MariaDB/MySQL database for homectld:
#   database, user and grants (for 'localhost' and remote '%')
#   <name> is used as database name, user and password (default 'homectl', see NAME in Make.config)
#   the tables are created by the daemon itself at startup
#
# the database root login is tried via unix socket first (running as root),
# otherwise the root password is prompted
#
# used by 'make install-db' and the postinst of the debian package

NAME=${1:-homectl}

if ! pgrep -x mariadbd >/dev/null && ! pgrep -x mysqld >/dev/null; then
   echo "No local database server running, skipping database setup" >&2
   echo "For a remote database see 'Remote database setup' in doc/installation.md" >&2
   exit 1
fi

PRIVS="INDEX, CREATE ROUTINE, ALTER ROUTINE, TRIGGER, REFERENCES, LOCK TABLES, CREATE VIEW, EXECUTE, \
CREATE TEMPORARY TABLES, ALTER, SELECT, INSERT, DELETE, CREATE, DROP, UPDATE"

COMMAND="CREATE DATABASE IF NOT EXISTS \`${NAME}\` CHARSET utf8;
CREATE USER IF NOT EXISTS '${NAME}'@'localhost' IDENTIFIED BY '${NAME}';
CREATE USER IF NOT EXISTS '${NAME}'@'%' IDENTIFIED BY '${NAME}';
GRANT ${PRIVS} ON \`${NAME}\`.* TO '${NAME}'@'localhost';
GRANT ${PRIVS} ON \`${NAME}\`.* TO '${NAME}'@'%';
FLUSH PRIVILEGES;"

if mysql -u root -e "SELECT 1" >/dev/null 2>&1; then
   echo "${COMMAND}" | mysql -u root || exit 1
else
   echo "Enter your database root password:" >&2
   echo "${COMMAND}" | mysql -u root -p || exit 1
fi

echo "Database '${NAME}' with user '${NAME}' (password '${NAME}') is ready"
