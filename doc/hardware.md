[← Back to the overview](../README.md)

# Hardware and Interfaces

## One Wire Sensors

### Enable One Wire Sensors at your Raspberry PI
To make the sensors available to the Raspberry PI you have to load the `w1-gpio` module by registering it in `/etc/modules`
(here in detail : http://www.netzmafia.de/skripten/hardware/RasPi/Projekt-Onewire/index.html)

```
echo "w1-gpio" >> /etc/modules
echo "w1_therm" >> /etc/modules
echo "dtoverlay=w1-gpio,gpioin=4,pullup=on" >> /boot/config.txt
```

### Enable One Wire Sensors at your Odroid N2+
```
echo "w1-gpio" >> /etc/modules-load.d/1wire.conf
echo "w1_therm" >> /etc/modules-load.d/1wire.conf
```
Addd this two lines into the [generic] section of the /boot/config.ini
```
overlays="w1-gpio"
overlay_param="gpiopin=476"
```

Reboot to apply this settings!

The homectld checks automatically if there are 'One Wire Sensors' connected, each detected sensor will be
configurable via the web interface.

## I2c Bus

### Rasperyy Pi

How to enable i2c on a Rasperyy Pi is well documented in the www

### Odroid

for Odroid I use Armbian (noble), you find a script to check the i2c bus in the contrib folder.
It supports the n2+ and the m1!
To check:
```
./contrib/check-i2c-header.sh
...
...
=== Ergebnis: I2C auf Pins 3/5 verfügbar ===
    Scan: i2cdetect -y 2
```
-> all fine. If Not try to fix the device tree by calling:
```
./contrib/armbian-enable-header-i2c.sh
Board: Hardkernel ODROID-N2Plus

→ N2+ erkannt: DTB-Patch

DTB: /boot/dtb/amlogic/meson-g12b-odroid-n2-plus.dtb (→ /boot/dtb-6.18.8-current-meson64/amlogic/meson-g12b-odroid-n2-plus.dtb)
i2c2_sda_x phandle: 0x82  (130)
i2c2_sck_x phandle: 0x83  (131)
Aktuell status:    status=okay
Aktuell pinctrl-0:                              pinctrl-0 = <0x82 0x83>;
Bereits korrekt — kein Patch nötig.
```

If you use the i2c2mqtt service shipped with the homectld you hav to use tbe i2c bus number provided by the
check scipt, in case of the output above 2. Therefor use the device /dev/i2c-2.

To scan the bus:
```
root@odroidn2 (gpiod-dev u=) ~/source/homectld> i2cmqtt -d /dev/i2c-2 -S
start scan of i2c device '/dev/i2c-2' ..
0x38
.. done
```
And to read this sensor (In my case it's a DHT20) call:
```
root@odroidn2 (gpiod-dev u=) ~/source/homectld> i2cmqtt -d /dev/i2c-2 --dht 0x38 -s
-----------------------
DHT38
Temperature 23.95 °C
Humidity 51 %
-----------------------
```
A INA219 current/power monitor (default i2c address 0x40) is read with:
```
root@odroidn2 (gpiod-dev u=) ~/source/homectld> i2cmqtt -d /dev/i2c-2 --ina 0x40 -s
-----------------------
INA40 (shunt 0.1000 Ohm)
Bus voltage   12.416 V
Shunt voltage 21.340 mV
Current       0.213 A
Power         2.650 W
-----------------------
```
The shunt resistor defaults to 0.1 Ohm, which is the value most breakout boards are equipped with.
If your board uses another one append it to the address, e.g. ```--ina 0x40:0.01``` for a 10 mOhm shunt.
Voltage, current and power are published as three values with the addresses 0, 1 and 2.

calling:
```i2cmqtt --help```
show the options and supported sensor types. You can start it as a service and provide the data by MQTT to the homerctld.

## Additional Hardware

### BMS
If you like to connect a Battery-Management-Systems like them used commonly from LionTron a you can use
the `bmsmqtt` service shipped with the `homectld`. Jus configure it in `/etc/default/bmsmqtt`, enable and start it:
```
systemctl enable bmsmqtt.service
systemctl start bmsmqtt.service
```

### One Wire service (w1mqtt)
The One Wire Sensors are managed by the `w1mqtt` service shipped with `homectld`, this service is enables by default and can be disabled if not needed.
Disable `w1mqtt`
```
systemctl disable w1mqtt.service
```
The `w1mqtt` service is configured in `/etc/default/w1mqtt`

### 433 MHz Radio Sensors (RTL-433)

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

### Caravan Industries (CI) Bus Interface

The CI bus adapter https://www.fischl.de/usblini/ is a lightweight solution.
Details described in [thetford/README.md](../thetford/README.md)

### Raspberry PI's GPIO information
https://www.raspberrypi.org/documentation/usage/gpio/
https://projects.drogon.net/raspberry-pi/wiringpi/pins/
http://wiringpi.com/reference/setup/

Overview of IO pins
```
apt -y install python3-gpiozero
pinout
```

## Hardware which I use for my pool controller

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

- peristaltic pump to inject PH Minus
  for example: https://www.amazon.de/gp/product/B07YWY29XL/ref=ppx_yo_dt_b_asin_title_o07_s00?ie=UTF8&psc=1
  better but more expensive may be: https://www.amazon.de/dp/B06ZZDLTJ7/?coliid=I37J0L29HIAMDR&colid=344R3XZTD8676&psc=1&ref_=lv_ov_lig_dp_it

- ... to be completed !

[← Back to the overview](../README.md)
