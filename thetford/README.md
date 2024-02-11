# Linux - Home Control Daemon (homectld)

# CI-Bus interface for thetford N400 refrigerator

## License
This code is distributed under the terms and conditions of the GNU GENERAL PUBLIC LICENSE. See the file LICENSE for details.

## Disclaimer
USE AT YOUR OWN RISK. No warranty.
This software is a private contribution and not to any products and can also cause unintended behavior. Use at your own risk!
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
The software was created for personal use, it's published here for free under the GPLv2.
All explanations about the hardware are only indications of how it is technically feasible, security and legal provisions are not discussed here!

## Donation
If you like the project and want to support it

[![paypal](https://www.paypalobjects.com/de_DE/DE/i/btn/btn_donate_SM.gif)](https://www.paypal.com/cgi-bin/webscr?cmd=_s-xclick&hosted_button_id=KUF9ZAQ5UTHUN)


Install the hardware and software (python) as described at the usblini homepage:
https://www.fischl.de/usblini/usblini_python_raspberrypi/

For example:
```
apt update
apt install -y python3 python3-pip
pip install git+https://github.com/EmbedME/pyUSBlini

apt install python3.7-tk  # (maybe needed forUSBliniGUI.py)

bash -c $'echo \'SUBSYSTEM=="usb", ATTRS{product}=="USBlini", MODE="0666"\' > /etc/udev/rules.d/50-USBlini.rules'
udevadm control --reload-rules
```

I use this adapter as master, not yet tried as a slave.
For the Thetford N400 series refrigerators a ready-made example for integration into the homectld can be found here thetford/README.md

# CI-Bus / LIN-Bus Adapter

Here you can find the suitable adapter https://www.fischl.de/usblini/

# Install thetford python script
```
apt install -y python3-paho-mqtt
cd thetford
make install
systemctl start thetford.service
```
## IMPORTANT
Adjust configuration in `/etc/default/thetford2mqtt`

## Check if device USBlini was detected

```
lsusb |grep -i Microc
Bus 001 Device 011: ID 04d8:e870 Microchip Technology, Inc.
lsusb -d 04d8:e870
Bus 001 Device 011: ID 04d8:e870 Microchip Technology, Inc.
```

# Usage

Just call `thetford.py -v 1` to retrieve the available values
```
root@womo {master u=} ~/build/homectld/thetford> ./thetford.py -h
usage: thetford [-h] [-i [I]] [-v [V]] [-c [C]]

optional arguments:
  -h, --help  show this help message and exit
  -i [I]      interval [seconds] (default 5)
  -m [M]      MQTT host
  -p [P]      MQTT port
  -v [V]      Verbosity Level (0-3) (default 0)
  -c [C]      Sample Count
```
Example:
```
/usr/bin/python3 /usr/local/bin/thetford.py -m localhost -v 0 -l
```
If you install it like the service file '`thetford.service` and enable/start it like
```
sudo apt install mosquitto mosquitto-clients
sudo cp thetford.py /usr/local/bin/
sudo cp thetford.service /etc/systemd/system/
sudo cp thetford2mqtt /etc/default/
sudo systemctl enable thetford
sudo systemctl start thetford
```
It runs in the background and writing the specified MQTT topic.
Don't forget to adjust the MQTT settings in /etc/default/thetford2mqtt.

## MQTT message examples

To check what is written on the topic call `mosquitto_sub -t n4000`
```
{"type": "N4000", "address": 0, "value": 7, "kind": "text", "title": "Supply", "text": "230V"}
{"type": "N4000", "address": 10, "value": 0, "title": "Mode", "kind": "text", "text": "Manuell"}
{"type": "N4000", "address": 1, "value": 5, "kind": "value", "title": "Level"}
{"type": "N4000", "address": 2, "value": false, "kind": "status", "title": "D+"}
{"type": "N4000", "address": 3, "value": 0, "kind": "text", "title": "Status", "text": "Online"}
{"type": "N4000", "address": 4, "value": 226, "kind": "value", "title": "ExtSupply", "unit": "V"}
{"type": "N4000", "address": 5, "value": 0.1, "kind": "value", "title": "IntSupply", "unit": "V"}
```

# Thetford N4000 CI-Bus

Bus ID: 0x0c

### Werte Byte 0:
Betriebsmodus

0x03 - Gas Betrieb
0x05 - 12V Betrieb
0x07 - 230 Betrieb
0x0B - Automatik Betrieb (Gas)
0x0D - Automatik Betrieb (12V)
0x0F - Automatik Betrieb (230V)

### Werte Byte 1:
Leistungsstufe
0x00-0x04 - Level (Stufe 1-5)

### Werte Byte 2:
D+
0x00               - D+ liegt nicht an
0x01               - D+ liegt nicht an
0x40 (b01000000)   - D+ liegt an
0x41               - D+ liegt an

### Werte Byte 3:
Bei Störung der Fehlercode ansonsten 0x00

### Werte Byte 4:
0xE0-E5    - Spannung der 230V Versorgung (224V - 229V)

### Werte Byte 5:
0x84-0x92  - Spannung der 12V Versorgung (13,2V - 14,8V)

### Werte Byte 6 und 7:
0x00 - immer


# Thetford T24000 CI-Bus

```01 02 00 17 00 81 00 00```

Bus ID: 0x0c

### Werte Byte 0:
01 - Tag
09 - Nacht

### Werte Byte 1:
Leistungsstufe
0x00-0x04 - Level (Stufe 1-5)

### Werte Byte 2:
00 ?

### Werte Byte 3:
Bei Störung der Fehlercode ansonsten 0x00

### Werte Byte 4:
00 ?

### Werte Byte 5:
0x84-0x92  - Spannung der 12V Versorgung (13,2V - 14,8V)

### Werte Byte 6 und 7:
0x00 - immer ?
