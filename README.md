# Linux - P4 Daemon (p4d)

This daemon is fetching data from the S 3200 and store it in a MySQL database. The data that should be fetched can be configured with a WEBIF or daemon. The collected data of the S 3200 will be displayed with the WEBIF, too.

Written by: *Jörg Wendel (linux at jwendel dot de)*
Documentation and Testing: *Thomas Unsin*
Homepage: https://github.com/horchi/linux-p4d

## License
This code is distributed under the terms and conditions of the GNU GENERAL PUBLIC LICENSE. See the file LICENSE for details.

## Disclaimer
USE AT YOUR OWN RISK. No warranty.

Die Software wurde für den Eigengebrauch erstellt. Sie wird kostenlos unter der
GPLv2 veröffentlicht.

Es ist kein fertiges Produkt, die Software entstand als Studie was hinsichtlich der Kommunikation
mit der s3200 Steuerung möglich ist und kann Bastlern als Basis und Anregung für eigene Projekte dienen.

Es besteht kein Anspruch auf Funktion, jeder der sie einsetzen möchte
muss das Risiko selbst abschätzen können und wissen was er tut, insbesondere auch in
Hinblick auf die Einstellungen der Heizungsparameter und den damit verbundenen Risiken
hinsichtlich Fehlfunktion, Störung, Brand, etc. Falsche Einstellung können unter anderem
durch Bedienfehler und Fehler in dieser Software ausgelöst werden!
Die Vorgaben, Vorschriften und AGB des Herstellers der Heizung bleiben Maßgebend!
Ich kann  nicht ausschließen das es zu Fehlfunktionen oder unerwartetem Verhalten,
auch hinsichtlich der zur Heizung übertragenen Daten und damit verbundenen, mehr oder
weniger kritischen Fehlfunktionen derselben kommen kann!

## Prerequisits:
- USB-Serial Converter based on FTDI chip
- USB-Serial converter must be connected to COM1 on Fröling mainboard
- A Linux based operating system is required


# Installation by package (actually available for Raspian Buster)

## install
```
wget  www.jwendel.de/p4d/install-deb.sh -O /tmp/install-deb.sh
sudo bash /tmp/install-deb.sh
```

## uninstall
`dpkg --remove p4d`

## uninstall (with remove of all configurations, data and the p4d database)
`dpkg --purge p4d`

# Installation by source (working for most linux plattforms)

## Prerequisits:
-  the described installation is tested with Raspbian buster, the p4d should work also with other Linux distributions and versions but the installation process should adapted to them, for example they use other init processes
   or use different tools for the package management, other package names, ....
- de_DE.UTF-8 is required as language package (Raspberry command: `dpkg-reconfigure locales`)

## Preliminary:
Update your package data:
`sudo apt update`
and, if you like, update your installation:
`sudo apt dist-upgrade`

Perform all the following steps as root user! Either by getting root or by prefix each command with sodo.

## Installation of the MySQL Database:
It's not required to host the database on the Raspberry. A remote database is supported as well!

`apt install mysql-server`
some distributions like raspbian buster switched to mariadb, in this case:
`apt install mariadb-server`

(set database password for root user during installation)

### Local database setup:
If the database server is located localy on same host as the p4d:

```
> mysql -u root -Dmysql -p
  CREATE DATABASE p4 charset utf8;
  CREATE USER 'p4'@'localhost' IDENTIFIED BY 'p4';
  GRANT ALL PRIVILEGES ON p4.* TO 'p4'@'localhost' IDENTIFIED BY 'p4';
```
### Remote database setup:
if the database is running remote (on a other host):
```
> mysql -u root -Dmysql -p
 CREATE DATABASE p4 charset utf8;
 CREATE USER 'p4'@'%' IDENTIFIED BY 'p4';
 GRANT ALL PRIVILEGES ON p4.* TO 'p4'@'%';
```

## Installation of the Apache Webserver:
Run the following commands to install the Apache webserver and required packages
```
apt update
apt install apache2 libapache2-mod-php php-mysql php-ds php-gd php-mbstring
```

Check from a remote PC if connection works a webpage with the content `It Works!` will be displayed

## Installation of the p4d daemon:
### install the build dependencies
```
apt install build-essential libssl-dev libxml2-dev libcurl4-openssl-dev libssl-dev libmariadbclient-dev libmariadb-dev-compat wiringpi
```

#### for the MQTT interface to home assistant install the paho.mqtt library:
```
cd /usr/src/
git clone https://github.com/eclipse/paho.mqtt.c.git
cd paho.mqtt.c
make
sudo rm -f /usr/local/lib/libpaho*
sudo make install
```
You can safely ignore this error message (may fixed once a day at paho.mqtt.c.git):
```install: Aufruf von stat für „build/output/doc/MQTTClient/man/man3/MQTTClient.h.3“ nicht möglich: Datei oder Verzeichnis nicht gefunden```

### get the p4d and build it
```
cd /usr/src/
git clone https://github.com/horchi/linux-p4d/
cd linux-p4d
make clean all
make install
```
#### For older linux distributions which don't support the systemd init process use this instead of 'make install'
```
make install INIT_SYSTEM=sysV
```
#### If you like a other destination than '/usr/local' append PREFIX=your-destination (e.g. PREFIX=/usr) to all make calls above

- Now P4 daemon is installed in folder `/usr/local/bin` and its config in /etc/p4d/
- Check `/etc/p4d.conf` file for setting db-login, ttyDeviceSvc device (change device if required),
  check which `/dev/ttyUSB?` devices is used for USB-Serial converter (`/dev/ttyUSB0`, `/dev/ttyUSB1`, `/dev/ttyACM0`)

## Time to first start of p4d
```
systemctl start p4d
```
### to check it's state call
```
systemctl status p4d
```
it should now 'enabled' and in state 'running'!

### also check the syslog about errors of the p4d, this will show all its current log messages
```
grep "p4d:" /var/log/syslog
```

## Aggregation / Cleanup
The samples will recorded in the configured interval (parameter interval in p4d.conf), the default is 60 Seconds.
After a while the database will grow and the selects become slower. Therefore you can setup a automatic aggregation in the `p4d.conf` with this two parameters:
The `aggregateHistory` is the history for aggregation in days, the default is 0 days -> aggegation turned OFF
The `aggregateInterval` is the aggregation interval in minutes - 'one sample per interval will be build' (default 15 minutes)

For example:
```
aggregateHistory = 365
aggregateInterval = 15
```

Means that all samples older than 365 days will be aggregated to one sample per 15 Minutes.
If you like to delete 'old' samples you have to do the cleanup job by hand, actually i don't see the need to delete anything, I like to hold my data (forever :o ?).
Maybe i implement it later ;)

## Install the WEB interface:
```
 cd /usr/src/linux-p4d
 make install-web
 make install-pcharts
 make install-apache-conf
 systemctl restart apache2.service
```

The default username and password for the login is
User: *p4*
Pass: *p4-3200*

### PHP Settings:
Modify the php.ini (/etc/php/*.*/apache2/php.ini) and append (or edit) this line ```set max_input_vars = 5000```

### Fist steps to enable data logging:

1. Log in to the web interface
2. Goto Setup -> Aufzeichnung -> Init
  - Select the values you like to record and store your selection (click 'Speichern')
3. Menu -> Init
4. Menu -> Aktualisieren

After this you can set up the schema configuration. The schema configuration seems not to be working with the firefox!

### Setup and configure sending mails:
The next steps are an example to setup the sending of mails. If another tool is preferred it can be used as well. The config is based on GMX. If you have another provider please search in the Internet for the required config.
- Install required components
  - `apt-get install ssmtp mailutils`
- The mailscript `p4d-mail.sh` is copied during the `make install` to the folder `/usr/local/bin`. This script is used in our case to send mails
- Change revaliases file edit file /etc/ssmtp/revaliases and add line (gmx is used as an example)
  - `root:MyMailAddress@gmx.de:mail.gmx.net:25`
- Change `ssmtp.conf` file. Edit file `/etc/ssmtp/ssmtp.conf` (gmx is used as an example)
```
root=MyMailAddress@gmx.de
mailhub=mail.gmx.net
hostname=gmx.net
UseSTARTTLS=YES
AuthUser=MyMailAddress@gmx.de
AuthPass=MyPassword
```
- Start the WEBIF and Login, go to the Setup page
- Configure the "Mail Benachrichtigungen" options (status and Fehler Mail Empfänger)
- If no Status options are set you will get a mail for each status change
- Set the script option `/usr/local/bin/p4d-mail.sh`

If the heating values are added as attachment to the mail please check the next steps.
- Check if `heirloom-mailx` is installed (`ls -lah /etc/alternatives/mail`)
- If output link is `/etc/alternatives/mail -> /usr/bin/mail.mailutils`
- Remove `heirloom-mailx` (`apt remove heirloom-mailx`)

### Configure Time sync:
With the next steps you can enable a time synchronization of the p4d and the heating:
If you enable this feature you can set the max time difference between the p4d systemtime and the time of the heating. The p4d will set the time of the heating (once a day) to his the systemtime. Therefore it is recommended to hold your system time in sync, e.g. by running the `ntp` daemon or systemd-timesyncd.

- Check if ntpd (openntpd, systemd-timesyncd, ...) is active and running. If not you have to install one of it.
- Start the WEBIF and login, go to the Setup page
- Enable "Tägliche Zeitsynchronisation" and set the max time difference in seconds in the line "Maximale Abweichung"
- Save configuration

### One Wire Sensors:
The p4d checks automatically if there are 'One Wire Sensors' connected, each detected sensor will be
configurable via the web interface after reading the sensor facts by clicking [init].

To make the sensors availaspble to the raspi you have to load the `w1-gpio` module, this can be done by calling `modprobe w1-gpio` or automatically at boot by registering it in `/etc/modules`:
```
sudo -i
echo "w1-gpio" >> /etc/modules
echo "w1_therm" >> /etc/modules
echo "dtoverlay=w1-gpio,gpioin=4,pullup=on" >> /boot/config.txt
exit
```
look also: http://www.netzmafia.de/skripten/hardware/RasPi/Projekt-Onewire/index.html

### Points to check
- reboot the device to check if p4d is starting automatically during startup


## Backup
Backup the data of the p4 database including all recorded values:

```
p4d-backup
```

This will create the following files:

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
```

To import the backup:
```
gunzip NAME-dump.sql.gz
mysql -u p4 -pp4 -Dp4 <  NAME-dump.sql
```
replace NAME with the name of the dump

#### ATTENTION:
The import deletes at first all the data and then imports the dumped data. To append the dumped data you have to modify the SQL statements inside the dump files manually.

## Additional information

### MySQL HINTS
If you cannot figure out why you get Access denied, remove all entries from the user table that have Host values with wildcars contained (entries that match `%` or `_` characters). A very common error is to insert a new entry with `Host='%'` and `User='some_user'`, thinking that this allows you to specify localhost to connect from the same machine. The reason that this does not work is that the default privileges include an entry with `Host='localhost'` and `User=''`. Because that entry has a Host value `localhost` that is more specific than `%`, it is used in preference to the new entry when connecting from localhost! The correct procedure is to insert a second entry with `Host='localhost'` and `User='some_user'`, or to delete the entry with `Host='localhost'` and `User=''`. After deleting the entry, remember to issue a `FLUSH PRIVILEGES` statement to reload the grant tables.

To analyze this you can show all users:
```
use mysql
SELECT host, user FROM user;
```

## Alternative install by script of Philipp:

```
cd ..
cd p4d
wget http://hungerphilipp.de/files/p4d/install.sh
chmod +x install.sh
./install.sh" or "sudo ./install.sh/
```

## Donation
If this project help you, you can give me a cup of coffee :)

[![paypal](https://www.paypalobjects.com/de_DE/DE/i/btn/btn_donate_SM.gif)](https://www.paypal.com/cgi-bin/webscr?cmd=_s-xclick&hosted_button_id=KUF9ZAQ5UTHUN)
