[← Back to the overview](../README.md)

# The WEB interface

The default username and password for login to the web interface is:
```
User: homectl
Password: homectl
```

## Connect to the WEB interface

The default Port is 61109. You reacg the WEB interface with this url (replace your-ip):

http://your-ip:61109

### URL Parameters

kiosk   - start in kiosk mode:

// 0 - with menu,    normal dash symbols, use normal-widget-height-factor
// 1 - without menu, big dash symbols,    use kiosk-widget-height-factor
// 2 - with menu,    big dash symbols,    use kiosk-widget-height-factor
// 3 - with menu,    big dash symbols,    use normal-widget-height-factor

The big dash symbols can also be enabled permanently by the config option
'Große Dashboard Symbole' (Setup -> Konfiguration -> WEB Interface), the URL parameter kiosk has priority.

heightFactor
group
backTime
page - specify the start page (dashboard, setup, chart, ...)
dash - specify the start page of the dashboard (dashbord ID)

For example to start in kiosk mode 2

http://your-ip:61109/index.html&kiosk=2

### iOS home screen app

The WEB interface can be added to the iOS home screen (Safari -> share -> 'Add to Home Screen') and runs
as a standalone app then. The status bar is transparent, the menu row is fixed at the top (since iOS 27
the status bar blurs scrolling content below it, a fixed element stays sharp). Note: iOS caches the
page settings per icon, after an update of homectld delete and re-add the icon if the layout looks odd.

## First steps with the WEB interface

### To enable some sensors

1. Log in to the web interface
2. Go to Setup -> IO Setup
3. Enable the desired sensors (for example, script sensors)

Script sensors are the scripts in `/etc/homectld/scripts.d` (templates `example_sensor.sh` and
`example_switch.sh`), one sensor per script. `dvb-signal.sh` is a switch for aligning a satellite
dish: a click starts a monitor which reads `dvb-fe-tool --femon` (package `dvb-tools`) and shows
lock, signal level and C/N about once per second as the text of the widget (type SymbolText), a
second click or 30 minutes stop it. The adapter number is the argument of the sensor (IO setup,
default 0); a lock and the C/N need a tuned frontend, e.g. a running stream of minisatip.

Some sensor categories are only available once the corresponding interface service has been activated and is running.
You can implement this interface service yourself using C/C++, Python, a shell script, and many other languages.
The interface between these services and homectld is MQTT, it's not yet fully documented here; for now, you can only refer to the examples and the code.

Services are already provided for some common components/devices:
- Theford refrigerators
- Mopeka gas level sensor
- One-Wire sensors
- Votronic MPP charge controller
- Victron (at least the interface to the inverter is already working with it)
- RTL433 wireless sensors
- BMS (the widely used version, which is also used by Liontron)

### Points to check
- reboot the device to check if homectld is starting automatically during startup

[← Back to the overview](../README.md)
