[← Back to the overview](../README.md)

<a id="hasp-panel"></a>
# HASP Panel (openHASP Display)

homectld can drive an [openHASP](https://www.openhasp.com) touch panel (e.g. the Elecrow CrowPanel 5.0) as a
small wall or vehicle display. The pages of the panel are configured in the WEBIF under
*Setup -> HASP Panel*, stored in the tables `hasppages` / `hasppagewidgets` and sent to the panel via MQTT.

## Prerequisites

- openHASP firmware on the panel, MQTT configured to the same broker as homectld
  (the group topic of the panel is `hasp/plates` by default)
- configuration item *HASP Panel -> haspMqttTopic* (`hasp/plates` or `hasp/<hostname>`); empty = disabled
- for MDI symbols the complete Material Design Icons font has to be uploaded to the panel once as `/mdi.ttf`
  (the file of the WEBIF: `htdocs/mds/fonts/materialdesignicons-webfont.ttf`), e.g.
  `curl -F "data=@materialdesignicons-webfont.ttf;filename=/mdi.ttf" http://<panel-ip>/edit`, then reboot the panel

## Pages and widgets

- a page has up to 4 rows, each row 1..3 widgets (cards); pages are changed by swiping left/right on the panel
- per slot a sensor is selected (filter and sorting by type / name), *Setup* opens the widget dialog
- when a sensor is selected its settings are copied **once** from its dashboard widget (if it is on a dashboard,
  otherwise the sensor defaults); afterwards the HASP settings are independent of the dashboard
- widget types: **Symbol** (MDI symbol, color shows the state, acts as toggle button; with the option
  *Spin bei Aktivität* a ring turns around the symbol while the sensor is on, also for SymbolValue), **SymbolValue**,
  **Value**, **Text**, **Meter** (gauge), **MeterLinear** (vertical bar with scale, thermometer for °C),
  **Time** (clock and date, rendered by the panel), **Level** (MDI symbol filled by percentage in the
  bar color, e.g. `mdi-water`, `mdi-propane-tank`, `mdi-car-battery`; range from the scale of the widget)
  (the fill refers to the drawn height of the symbol; the option *Füllung als Fläche* fills a rectangle
  behind the symbol instead, for frame symbols without a filled variant, e.g. `mdi-crop-landscape` as a tank)
  and **Choice** (the choices of the sensor as button matrix, 2 per row up to 4 choices, otherwise 3;
  the current value is the highlighted button in the 'on' color, a touch sends the choice to the
  command topic of the sensor like the dashboard choice widget; without a command topic the selection is reverted)
  and **Balance** (horizontal bar with zero in the middle for bipolar values like the battery current
  or the grid power: negative values fill to the left in the first color (default red), positive to the
  right in the second (default green); *Skala Max* is the full scale, the range is symmetric unless a
  negative *Skala Min* is given; *Richtung tauschen* mirrors the bar for sensors reporting discharge positive;
  Level and Balance exist as dashboard widgets too)
  and **Timer** (pseudo sensor *Timer* in the sensor list: countdown of 1..60 minutes with mm:ss, progress bar
  and step buttons (*Schritte*, comma separated minutes or seconds with suffix `s`, e.g. `-30s,30s,5,30`,
  default -1,1,5, up to 6) plus Start-Stop; it runs
  in the panel firmware and keeps running without WLAN,
  the board buzzer beeps for a minute when the time is up, any touch on the card stops it; a touch on the
  time starts/stops, a long press resets to the configured minutes; needs the custom firmware >= 0.7.0.2,
  see openHASP/README.md)
- the pages are sent after saving, with the button *An Panel senden* and automatically whenever the panel
  comes online (openHASP forgets objects received via MQTT on reboot); further buttons: *Panel wecken*
  (display on, the idle cycle restarts), *Panel neu starten* and *Screenshot* (the screenshot page of the
  panel's web server, the IP is taken from the panel's status report)
- the idle times of the panel (*Leerlauf*, seconds) are set on the same page: after the short one the
  panel dims, after the long one the display goes off (what exactly happens is defined by the scripts
  `idle_short.cmd` / `idle_long.cmd` / `idle_off.cmd` on the panel). They are stored as the configuration
  items *haspIdleShort* / *haspIdleLong* and sent with the pages (`config/gui`, `idle1` / `idle2`);
  0 switches a stage off, both 0 leaves the panel's own setting untouched
- values are pushed on every sensor change; openHASP has no chart object, so charts are not available

[← Back to the overview](../README.md)
