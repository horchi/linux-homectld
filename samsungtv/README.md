# Samsung Tizen TV

Bridge between a Samsung Tizen TV (2016+, e.g. QLED GQ65Q8F) and homectld via MQTT,
based on the python module [samsungtvws](https://github.com/xchwarze/samsung-tv-ws-api)
(also used by the Home Assistant integration).

| Address | Sensor        | Kind   | Function                                         |
|---------|---------------|--------|--------------------------------------------------|
| 0       | TV            | status | on: Wake-on-LAN, off: KEY_POWER (REST PowerState) |
| 1       | TV Lautstärke | value  | UPnP RenderingControl (port 9197)                |
| 2       | TV Stumm      | status | UPnP RenderingControl                            |
| 3       | TV Quelle     | text   | disabled, the Q8F ignores KEY_HDMI1..4 (see script) |
| 4       | TV App        | text   | start app via REST (list via -A), shows the visible app |
| 10..    | TV Home, TV Zurück, ... | status | push button for each key of `-K` (default HOME,BACK,MENU,SOURCE,GUIDE,INFO,UP,DOWN,LEFT,RIGHT,ENTER,PLAY,PAUSE,STOP,FF,REWIND,CHUP,CHDOWN,VOLUP,VOLDOWN), a click sends the key |

Free keys / apps (e.g. by scripts) to the topic `homectld2mqtt/samsungtv/in`:

    mosquitto_pub -t homectld2mqtt/samsungtv/in -m '{"key": "KEY_HOME"}'
    mosquitto_pub -t homectld2mqtt/samsungtv/in -m '{"app": "Netflix"}'

## TV settings

- *Settings → General → Network → Expert Settings → Power On with Mobile* (Wake-on-LAN) on
- fixed IP (DHCP reservation)

## Install

    Make.user: SAMSUNGTV = 1
    make install

adjust `/etc/default/samsungtv2mqtt` (IP, MAC), then pair once (confirm the popup on the TV):

    samsungtv.py -H <ip> --pair
    samsungtv.py -H <ip> -s
    systemctl restart samsungtv

The token is stored in `/etc/homectld/samsungtv.token`.

## Remote control widget

The HTML widget template 'Samsung TV Fernbedienung' (delivered with homectld, etc/configs/htmlWidgetTemplates.json)
looks like the small Samsung remote
(power, d-pad / OK, back, home, play / pause, volume rocker (click: mute, shows the volume), channel rocker
(click: guide) and the app buttons). Add an HTML widget to the dashboard and select the template, the parameters are
the sensors SAMTV:0x.. (default addresses), adjustable per widget. The app buttons need `data-set` with
a text value (homectld 0.1.50).
