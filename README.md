# Linux - Home Control Daemon (homectld)

Written by: *Jörg Wendel (linux at jwendel dot de)*
Project page: https://github.com/horchi/linux-homectld

## License
This code is distributed under the terms and conditions of the GNU GENERAL PUBLIC LICENSE. See the file LICENSE for details.

# Some Screenshots

## As Camper Control
![](contrib/w-dash1.jpg?raw=true "Title")
![](contrib/w-dash2.jpg?raw=true "Title")
![](contrib/w-dash3.jpg?raw=true "Title")
![](contrib/w-chart1.jpg?raw=true "Title")

## As Pool Control
![](contrib/poold.jpg?raw=true "Title")

## Disclaimer
USE AT YOUR OWN RISK. No warranty.
This software is a private contribution and not to any products and can also cause unintended behavior. Use at your own risk!
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
The software was created for personal use, it's published here for free under the GPLv2.
The construction of the required hardware, especially in the handling of mains voltage (230V, 110V, ...), is carried out on one's own responsibility, local guidelines and regulations must also be observed especially in connection with water and pool!
All explanations about the hardware are only indications of how it is technically feasible, security and legal provisions are not discussed here!

## Donation
If you like the project and want to support it

[![paypal](https://www.paypalobjects.com/de_DE/DE/i/btn/btn_donate_SM.gif)](https://www.paypal.com/cgi-bin/webscr?cmd=_s-xclick&hosted_button_id=KUF9ZAQ5UTHUN)

## Prerequisites:
The described installation method is tested with Raspbian Buster, the homectld should work also with other Linux distributions and versions but the installation process should adapted to them, for example they use other init processes or use different tools for the package management, other package names, ...

Language pack 'de_DE.UTF-8' is required as language package (`dpkg-reconfigure locales`)
Set system timezone, for example Berlin `timedatectl set-timezone Europe/Berlin`

# Installation by package (only Raspbian Buster)

**The install package is not available yet! Please use the ""custom build" section below!**

### install

```
wget www.jwendel.de/homectld/install-deb.sh -O /tmp/install-deb.sh
sudo bash /tmp/install-deb.sh
```

### uninstall

```
dpkg --remove homectld
```

### uninstall (with remove of all configurations, data and the homectl database)

```
dpkg --purge homectld`
```

# Building and installing by source

## Preliminary
Update your system
```
apt update
apt dist-upgrade
```

Perform all following steps as root user! Either by getting root or by prefix each command with sudo.
To become root type: `sudo -i`

## Installation of the MySQL Database
It's not required to host the database local at the Raspberry. A remote database is supported as well.

```
apt -y install mariadb-server
```
or if you like mariadb-server 10.6 on ubuntu 20.04 follow this instructions:
https://community.hetzner.com/tutorials/how-to-install-mariadb-10-6-on-ubuntu-20-04

Set the database root (db admin) password during installation!

### Local database setup:
If the database server is located locally (on same host as the daemon):

```
> mysql -u root -Dmysql -p
  CREATE DATABASE homectl charset utf8;
  CREATE USER 'homectl'@'localhost' IDENTIFIED BY 'homectl';
  GRANT ALL PRIVILEGES ON homectl.* TO 'homectl'@'localhost' IDENTIFIED BY 'homectl';
  flush privileges;
```

### Remote database setup:
if the database is running remote, or you like to have remote access to the database:
```
> mysql -u root -Dmysql -p
 CREATE DATABASE homectl charset utf8;
 CREATE USER 'homectl'@'%' IDENTIFIED BY 'homectl';
 GRANT ALL PRIVILEGES ON homectl.* TO 'homectl'@'%' IDENTIFIED BY 'homectl';
 flush privileges;
```

### install the build dependencies

```
apt -y install build-essential libssl-dev libcurl4-openssl-dev uuid-dev libcap-dev libsystemd-dev
apt -y install libjansson-dev libmariadb-dev liblua5.3-dev mosquitto-clients jq bc
```

# If you integrate bluetooth devices

```apt -y install libbluetooth-dev```

# If you NOT use a global mosquitto borker somewhere in your local network
```apt -y install mosquitto```

#### Raspberry Pi
```
apt -y install wiringpi
```
#### Odroid with Ubuntu
```
apt -y install libwiringpi-dev libwiringpi2 odroid-wiringpi
```

### get and install libwebsock
We need to install it manually in case the version shipped with the distribution is to old
(we need at least version v4.3.0)

if the version shipped with the distribution is okay simple
```apt -y install libwebsockets-dev```

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

### get, build and install the homectld

```
cd /usr/src/
git clone https://github.com/horchi/linux-homectld/
cd linux-homectld
make clean all
make install
```
Now the home control daemon is installed in folder `/usr/local/bin`
Check `/etc/homectld/daemon.conf` file for setting of your database login. If you have used the defaults above no change is needed.

# One Wire Sensors:

### Enable One Wire Sensors at your Raspberry PI
To make the sensors available to the Raspberry PI you have to load the `w1-gpio` module by registering it in `/etc/modules`
(here in detail : http://www.netzmafia.de/skripten/hardware/RasPi/Projekt-Onewire/index.html)

```
echo "w1-gpio" >> /etc/modules
echo "w1_therm" >> /etc/modules
echo "dtoverlay=w1-gpio,gpioin=4,pullup=on" >> /boot/config.txt
```

Reboot to apply this settings!

The homectld checks automatically if there are 'One Wire Sensors' connected, each detected sensor will be
configurable via the web interface.

# Time to first start of homectld
```
systemctl start homectld
```

# And enable it for automatic start at boot
```
systemctl enable homectld
```


### to check it's current state call

```
systemctl status homectld
```

it should now 'enabled' and in state 'running'!

### also check the syslog about errors of the homectld, this will show all its current log messages

```
grep -i "error" /var/log/homectld.log
```


# The WEB interface:

The default username and password for login to the web interface is:
```
User: homectl
Password: homectl
```

## Connect to the WEB interface:

The default Port is 61109. You reacg the WEB interface with this url (replace your-ip):

http://your-ip:61109

### URL Parameters

kiosk   - start in kiosk mode:

// 0 - with menu,    normal dash symbols, use normal-widget-height-factor
// 1 - without menu, big dash symbols,    use kiosk-widget-height-factor
// 2 - with menu,    big dash symbols,    use kiosk-widget-height-factor
// 3 - with menu,    big dash symbols,    use normal-widget-height-factor

heightFactor
group
backTime
page - specify the start page (dashboard, setup, chart, ...)
dash - specify the start page of the dashboard (dashbord ID)

For example to start in kiosk mode 2

http://your-ip:61109/index.html&kiosk=2



### Fist steps to enable data logging:

1. Log in to the web interface
2. Go to Setup -> IO Setup
3. Select the values you like to display and record and store your settings

### Points to check
- reboot the device to check if homectld is starting automatically during startup


# Backup
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
```

To import the backup:
```
gunzip NAME-dump.sql.gz
mysql -u homectl -phomectl -homectl <  NAME-dump.sql.gz
```
replace NAME with the name of the dump

# Additional Hardware

## BMS
If you like to connect a Battery-Management-Systems like them used commonly from LionTron a you can use
the `bmsmqtt` service shipped with the `homectld`. Jus configure it in `/etc/default/bmsmqtt`, enable and start it:
```
systemctl enable bmsmqtt.service
systemctl start bmsmqtt.service
```

## One Wire Sensors
The One Wire Sensors are managed by the `w1mqtt` service shipped with `homectld`, this service is enables by default and can be disabled if not needed.
Disable `w1mqtt`
```
systemctl disable w1mqtt.service
```
The `w1mqtt` service is configured in `/etc/default/w1mqtt`

## 433 MHz Radio Sensors (RTL-433)

A RTL Radio/USB adapter like the https://amzn.eu/d/bKBwTOO is needed. As Sensor you van use any compatible, mostly all 433Mht radio sensors.
For the interface to the `homectld` you can use `rtl-433` which is includes in most linix distributions, whis can configured to write the data directly
to MQTT where it's read and paresed by the `homectld`.
To install the `rtl-433` package:
```
apt -y install rtl-433
```
To start the rtl-433 service a Systemd unit file is shipped with the `homectld`, install, enable and start it by:
```
cd linux-homectld
cp contrib/rtl433.service /etc/systemd/system/
systemctl enable rtl433.service
systemctl start rtl433.service
```
To change the MQTT borker connection (host, port, ...) adjust it in `/etc/systemd/system/rtl433.service` and restart the service.

## Caravan Industries (CI) Bus Interface

The CI bus adapter https://www.fischl.de/usblini/ is a lightweight solution.
Details descibed here ./thetford/README.md

# Information / HINTS

## MySQL HINTS
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

## Raspberry PI's GPIO information's
https://www.raspberrypi.org/documentation/usage/gpio/
https://projects.drogon.net/raspberry-pi/wiringpi/pins/
http://wiringpi.com/reference/setup/

Overview of IO pins
```
apt -y install python3-gpiozero
pinout
```

# My notes to remember

## Update Material Design Icons
wget https://cdn.jsdelivr.net/npm/@mdi/font@6.5.95/css/materialdesignicons.min.css
copy to htdocs/css/

## CCU

Rolladen an Hommatic CCU mit homectld ID 2 'HMB:2' komplette schließen
mosquitto_pub --quiet -L mqtt://192.168.200.101:1883/homectld2mqtt/nodered -m '{ "id":"HMB:2","bri":0 }'
auf 50%
mosquitto_pub --quiet -L mqtt://192.168.200.101:1883/homectld2mqtt/nodered -m '{ "id":"HMB:2","bri":50 }'

# Hardware which I use for my pool controller

- Relay Board with at least 8 relays to switch 230V and a max current which fit the needs of your components (lights, pumps, ...)
    for example: https://www.amazon.de/gp/product/B014L10Q52/ref=ppx_yo_dt_b_asin_title_o00_s00?ie=UTF8&psc=1

- At least 3 One Wire sensors
    for example: https://www.amazon.de/gp/product/B00CHEZ250/ref=ppx_yo_dt_b_search_asin_title?ie=UTF8&psc=1

- Power sockets to connect lights and pumps
    for example: https://www.amazon.de/gp/product/B07CM9DLHY/ref=ppx_yo_dt_b_search_asin_image?ie=UTF8&psc=1

- 230V Magnet Valve (for the shower)
  for example: https://www.amazon.de/gp/product/B072JBBHJS/ref=ppx_yo_dt_b_asin_title_o03_s00?ie=UTF8&psc=1

- Button (for shower)
  for example: https://www.amazon.de/gp/product/B002LE8EJC/ref=ppx_yo_dt_b_search_asin_image?ie=UTF8&psc=1

- PH probe
  for example: https://www.amazon.de/gp/product/B081QK9TX2/ref=ppx_yo_dt_b_asin_title_o06_s00?ie=UTF8&psc=1

- Arduino Pro Mini 328 (for PH probe and PH minus fluid control)
  for example: https://www.amazon.de/gp/product/B015MGHLNA/ref=ppx_yo_dt_b_asin_title_o05_s00?ie=UTF8&psc=1
  USB Interface for programming: https://www.amazon.de/gp/product/B07KVT6HNL/ref=ppx_yo_dt_b_asin_title_o04_s00?ie=UTF8&psc=1

- House-pump to inject PH Minus
  for example: https://www.amazon.de/gp/product/B07YWY29XL/ref=ppx_yo_dt_b_asin_title_o07_s00?ie=UTF8&psc=1
  better but more expensive may be: https://www.amazon.de/dp/B06ZZDLTJ7/?coliid=I37J0L29HIAMDR&colid=344R3XZTD8676&psc=1&ref_=lv_ov_lig_dp_it

- ... to be completed !
