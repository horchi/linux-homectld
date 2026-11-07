[← Back to the overview](../README.md)

# Stand Alone Server (camper)

Some specials for a 'Stand Alone Server' like I use in my camper.
Since the mini PC (in my case, an Odroid N2+) is being used as a server in this scenario, the network setup is different: eth0 has a static IP address and cannot access the internet. Internet access is provided by a Wi-Fi dongle configured as wlan0. A Wi-Fi access point for the RV’s “internal” network is connected to eth0.

The server also handles IP masquerading to provide all internal clients with Internet access.
Additionally, dnsmasq runs as a DNS server/relay and DHCP service.
Crony is used instead of systemd-timesyncd because the latter proved to be too “fussy” in this setup.

To enable the install some of the provided service configurations
enable it by setting 'WOMO = 1' in Make.user in the projects (source) root directory of the homectld.
`make config` edits Make.user interactively (switches and personalization, needs `apt install dialog`),
the possible settings and their defaults are in Make.user.template.

## IP masquerading, firewall, openvpn

If you use this 'server' for IP masquerading and/or as firewall or like to use openvpn:

```apt -y install iptables openvpn```

## Static/Fixed IP

to get a fixed IP for your eth0 device you find a teplate in contrib/womo/netplan/
just copy it to /etc/netplan:
```
cp contrib/womo/netplan/01-netcfg.yaml /etc/netplan/
```
and adjust the settings, such as the IP address, to your preferences.
The route set there can be deleted, but it generally doesn't cause any problems. In my setup,
it is used to route SatIp broadcat signals from the Minisatip service which I'm running on the Odroid.

## DNS/DHCP

```apt -y install dnsmasq dnsmasq-utils```

if you call 'make install' of the homectld AFTER the install of dnsmasq the setup (/etc/dnsmasq*) will prepared automatically,
just adjust your inrternal internal subnet by setting ```SUBNET = SUBNET = 192.168.220``` in Make.user (to your subnet)

## Time synchronization

Falls es Aufgrund der Art der Netzwerk/Internet Anbindung mit dem systemd-timesyncd
zu Probmelmen kommt auf chroony umstellen
```
apt -y install chrony
systemctl disable --now systemd-timesyncd
systemctl enable --now chrony
```
### Check
```
chronyc tracking
timedatectl
```

## IP masquerading

to act as a router some settings IP table rules are necessary, this are prepared in the fwpn script.
which is also set up as a systems service (fwpn.service). All you have to do is update your subnet in /etc/default/fwpn.
The other options in fwpn can also be overridden via /etc/default/fwpn.
Note that the MSQ_DEV in fwpn can be switched via the msqdevice.sh script service in the web frontend if the internet is accessed via usb0 rather than wlan0, for example

## Alfa Tube-U4G LTE USB Modem

```
apt install modemmanager usb-modeswitch libqmi-utils network-manager
```

Für den Internet Zugangmittels einem Alfa Tube-U4G LTE USB Modem wird das Script `lte-autoconnect.sh` nebst zugehörigen Service `lte-modem.service`
installiert. Damit kann die Verbindung mittels:
```
systemctl start lte-modem.service
systemctl stop lte-modem.service
```
gestartet bzw. wieder beendet werden. Das ist auch ins Dashboard der GUI integriert.
Sofern die Verbindung nach jedem boot automatisch aktiviert werden soll kann man den Service stattdessen enabeln:
```
systemctl enable lte-modem.service
```

### Check der Verbindung und Signal Qualität
```
mmcli -m $(mmcli -L | grep -oP 'Modem/\K[0-9]+') --signal-get
```

### Zyklischen Check der LTE Verbindung mit Auto Reconnect bei Verbindungsverlust einrichten

Alle 5 Minuten mittels crontab Eintrag checken
```(crontab -l 2>/dev/null | grep -Fv "/usr/local/bin/lte-autoconnect.sh"; echo "*/5 * * * * /usr/local/bin/lte-autoconnect.sh > /dev/null 2>&1") | crontab -```
Wenn der Service 'lte-modem.service' absichtlich 'aus' ist führt das NICHT zu Neu-Verbindung!

### Hinweis zum USB Port

Das Tube-U4G kann manchmal viel Strom ziehen (besonders bei schlechtem Empfang beim Senden).
Da am Odroid bereits viele USB-Geräte hängen achte darauf, dass der Tube-U4G an einem der USB 3.0 Ports (blau) hängt.
Falls es Verbindungsabbrüche gibt, könnte ein aktiver USB-Hub mit eigener Stromversorgung nötig sein, aber der N2+ liefert an
den blauen Ports normalerweise genug Saft.

[← Back to the overview](../README.md)
