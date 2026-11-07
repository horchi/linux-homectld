[← Back to the overview](../README.md)

# Installation

## Prerequisites
The described installation method and dependencies fit to ubuntu 24.04, the homectld work also with other Linux distributions

Language pack 'de_DE.UTF-8' is required as language package:
```
dpkg-reconfigure locales
dpkg-reconfigure keyboard-configuration
reboot
```

Set system timezone, for example Berlin
```
timedatectl set-timezone Europe/Berlin
```

Change hostname
```
hostnamectl set-hostname new-name
```
## Building and installing by source

### Preliminary
Update your system
```
apt update
apt dist-upgrade
```

Perform all following steps as root user! Either by getting root or by prefix each command with sudo.
To become root type: `sudo -i`

### Installation of the MySQL Database
It's not required to host the database local at the Raspberry. A remote database is supported as well.

```
apt -y install mariadb-server
```
or if you like mariadb-server 10.6 on ubuntu 20.04 follow this instructions:
https://community.hetzner.com/tutorials/how-to-install-mariadb-10-6-on-ubuntu-20-04

Set the database root (db admin) password during installation!

#### Local database setup
If the database server is located locally (on same host as the daemon):

```
> mysql -u root -Dmysql -p
  CREATE DATABASE homectl charset utf8;
  CREATE USER 'homectl'@'localhost' IDENTIFIED BY 'homectl';
  GRANT ALL PRIVILEGES ON homectl.* TO 'homectl'@'localhost' IDENTIFIED BY 'homectl';
  flush privileges;
```

#### Remote database setup
if the database is running remote, or you like to have remote access to the database:
```
> mysql -u root -Dmysql -p
 CREATE DATABASE homectl charset utf8;
 CREATE USER 'homectl'@'%' IDENTIFIED BY 'homectl';
 GRANT ALL PRIVILEGES ON homectl.* TO 'homectl'@'%' IDENTIFIED BY 'homectl';
 flush privileges;
```

### Install the build dependencies

The short way, after `Make.user` is set up:
```
make install-dependencies
```
It installs the packages below via apt, only the missing ones, installed packages are never upgraded.
libgpiod (version 2 needed) and libwebsockets (4.3.0 needed) are built from source if the versions of
the distribution are too old (e.g. Ubuntu 24.04 / Armbian noble ship libgpiod 1.6), the sources go to
`SRC_DIR` from `Make.user` (default `/usr/src`). The optional parts follow the switches of `Make.user`:
WOMO adds mariadb-server, mosquitto and the network tools (dnsmasq, NetworkManager, chrony, LTE modem,
iptables, openvpn), MOPEKA the python BLE modules, THETFORD paho-mqtt and usblini, GARMIN the python venv,
GPS the python modules of the u-blox GPS service (`gps.u-blox`, installed with `make install` like the others).
Always call it via make, the script `contrib/install-dependencies.sh` gets the switches from there.
`DRYRUN=1 make install-dependencies` only shows the commands. The manual way:

```
apt -y install build-essential libssl-dev libcurl4-openssl-dev uuid-dev libcap-dev libsystemd-dev cmake pkg-config net-tools nodejs
apt -y install libjansson-dev libmariadb-dev liblua5.3-dev mosquitto-clients jq jo bc aptitude util-linux-extra libgpiod2 libgpiod-dev
```

### Bluetooth devices (optional)

```
apt -y install libbluetooth-dev
```

### Local mosquitto broker (if you do NOT use a global one in your network)
```
apt -y install mosquitto
```
#### Make the broker accessible via network

Put this lines into your /etc/mosquitto/mosquitto.conf:
```
listener 1883
allow_anonymous true
```
and restart mosquitto ```systemctl restart mosquitto```

### GPIO library on the Raspberry Pi

Mit libgpiod (besser) - zwingend Version 2 !
```
apt -y install libgpiod-dev
```
zur Not fallback auf wiringpi
```
apt -y install wiringpi
```
### GPIO library on the Odroid

Mit libgpiod (besser) - zwingend Version 2 !
```
apt -y install libgpiod-dev
```
zur Not fallback auf wiringpi
```
apt -y install libwiringpi-dev libwiringpi2 odroid-wiringpi
```

### Get and install libwebsockets
We need to install it manually in case the version shipped with the distribution is to old
(we need at least version v4.3.0)

if the version shipped with the distribution is okay simple
```
apt -y install libwebsockets-dev
```

else get it from git
```
cd /usr/src/
git clone https://github.com/warmcat/libwebsockets.git
cd libwebsockets
mkdir build
cd build
cmake ..
make install
ldconfig
```

### Get, build and install the homectld

```
cd /usr/src/
git clone https://github.com/horchi/linux-homectld/
cd linux-homectld
make clean all
make install
```
Now the home control daemon is installed in folder `/usr/local/bin`
Check `/etc/homectld/daemon.conf` file for setting of your database login. If you have used the defaults above no change is needed.

## First start of homectld
```
systemctl start homectld
```

### Enable the automatic start at boot
```
systemctl enable homectld
```

### Check its current state

```
systemctl status homectld
```

it should now 'enabled' and in state 'running'!

### Check the log for errors

```
grep -i "error" /var/log/homectld.log
```

## Backup
Backup the data of the homectl database including all recorded values:

```
homectld-backup.sh
```

This will create the at least this dumps:

```
config-dump.sql.gz
errors-dump.sql.gz
jobs-dump.sql.gz
menu-dump.sql.gz
samples-dump.sql.gz
schemaconf-dump.sql.gz
sensoralert-dump.sql.gz
smartconfig-dump.sql.gz
valuefacts-dump.sql.gz
timeranges-dump.sql.gz
hmsysvars-dump.sql.gz
scripts-dump.sql.gz
...
...
```

To import the backup:
```
gunzip NAME-dump.sql.gz
mysql -u homectl -phomectl -homectl <  NAME-dump.sql.gz
```
replace NAME with the name of the dump

## MySQL hints
If you cannot figure out why you get Access denied, remove all entries from the user table that
have Host values with wildcards contained (entries that match `%` or `_` characters).
 A very common error is to insert a new entry with `Host='%'` and `User='some_user'`, thinking that this allows you to specify localhost to connect from the same machine.
The reason that this does not work is that the default privileges include an entry with `Host='localhost'` and `User=''`. Because that entry has a Host value `localhost` that is more specific than `%`, it is used in preference to the new entry when connecting from localhost! The correct procedure is to insert a second entry with `Host='localhost'` and `User='some_user'`, or to delete the entry with `Host='localhost'` and `User=''`.
After deleting the entry, remember to issue a `FLUSH PRIVILEGES` statement to reload the grant tables.

To analyze this you can show all users:
```
use mysql
SELECT host, user FROM user;
```

## Developer notes

### Update Material Design Icons
wget https://cdn.jsdelivr.net/npm/@mdi/font@6.5.95/css/materialdesignicons.min.css
copy to htdocs/css/

[← Back to the overview](../README.md)
