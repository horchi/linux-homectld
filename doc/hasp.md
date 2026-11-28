[← Back to the overview](../README.md)

<a id="hasp-panel"></a>
# HASP Panel (openHASP Display)

homectld can drive an [openHASP](https://www.openhasp.com) touch panel (e.g. the Elecrow CrowPanel 5.0) as a
small wall or vehicle display. The pages of the panel are configured in the WEBIF under
*Setup -> HASP Panel*, stored in the tables `hasppages` / `hasppagewidgets` and sent to the panel via MQTT.

## Prerequisites

- make switch `HASP = 1` in `Make.user`: only then the setup page *HASP Panel* and its configuration items are
  offered, the panel topics are subscribed and `make install` installs the scripts for the panel
  (`openHASP/data-files/idle_*.cmd` and the empty `mqtt_on.cmd` -> `/etc/homectld/hasp/`, existing ones are kept)
- openHASP firmware on the panel, MQTT configured to the same broker as homectld
  (the group topic of the panel is `hasp/plates` by default)
- configuration item *HASP Panel -> haspMqttTopic* (`hasp/plates` or `hasp/<hostname>`); empty = disabled
- the scripts `idle_*.cmd` and the font for the MDI symbols (`/mdi.ttf`) have to be on the panel: button
  *Dateien hochladen* on the setup page (uploads them via the web editor of the panel with the display off -
  writing the flash makes the RGB display flicker - then the panel is rebooted). The font is a subset of the
  WEBIF font with only the symbols of the pages (`python3 -m fontTools.subset`, package `python3-fonttools`,
  installed by `make install-dependencies` with `HASP = 1`); with the complete font (1.3 MB, 7447 symbols) the
  panel runs out of memory (`MQTT RCV: Out of memory`, objects missing). After changing symbols upload again
- recommended: the firmware from `openHASP/` (see its README): LVGL objects in the PSRAM instead of a fixed
  64 KB block of internal RAM (about 11 KB per page), memory values and warnings / errors of the panel via MQTT

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
  and **Weather** (pseudo sensor *Wetter* in the sensor list, offered when the forecast sensor `WEA:0x01` of
  OpenWeatherMap is active: symbol, temperature and description of the current weather, below today's
  max / min and the rain probability (from 10 %); wide cards (one or two per row) show the next days as
  columns with symbol and max / min. The title is the place of the forecast unless one is set. All weather
  symbols are part of the font subset, a new card needs *Dateien hochladen* once)
- *Speichern & senden* stores the pages and sends them to the panel (also without a change, e.g. after a
  reboot of the panel); they are sent automatically whenever the panel comes online (openHASP forgets objects
  received via MQTT on reboot); further buttons: *Panel wecken* (display on, the idle cycle restarts),
  *Panel neu starten* and *Screenshot* (the screenshot page of the panel's web server, the IP is taken from the
  panel's status report)
- the dot at the top of the tool panel shows whether the panel is alive: green online, red offline (its LWT
  topic `hasp/<node>/LWT`), gray not known yet; when online the state of the display is added (an, gedimmt, aus)
- *zu Seite 1* (seconds, configuration item *haspHomeAfter*): after this time without use the panel switches
  back to page 1; use is a touch of an object (button, choice, timer, ...), a page change or a wake up - a touch of
  the background is not reported by the panel. 0 = off
- sensor **`HASP:0x01`** (*Panel Display*, IO Setup, section *HASP Panel*, activate it there): the display of
  the panel as an actor for the dashboard (Symbol widget), the scenes and LUA. State on = display on
  (idle `off`), off when dimmed or dark, invalid while the panel is offline; switching on wakes the panel
  (`idle off`, the idle cycle restarts), switching off turns the display off (`idle long`), a click toggles
- the idle times of the panel (*Leerlauf*, seconds) are set on the same page: after the short one the
  panel dims, after the long one the display goes off (what exactly happens is defined by the scripts
  `idle_short.cmd` / `idle_long.cmd` / `idle_off.cmd` on the panel). They are stored as the configuration
  items *haspIdleShort* / *haspIdleLong* and sent with the pages (`config/gui`, `idle1` / `idle2`);
  0 switches a stage off, both 0 leaves the panel's own setting untouched
- values are pushed on every sensor change; openHASP has no chart object, so charts are not available
- *Speicher Panel* in the tool panel: free internal RAM, largest free block, fragmentation and free PSRAM from the
  panel's status report (requested when the page is opened and with the refresh button; PSRAM and largest block
  only with the firmware from `openHASP/`). The internal RAM is shown in red below about 40 KB
- *Meldungen des Panels* above the pages: the last 50 warnings / errors the panel sends on `<topic>/state/log`
  (firmware from `openHASP/`, no USB needed), also in the journal (`HASP: Panel warning: ...`). Messages
  within 15 s after *Speichern & senden* (e.g. unknown attributes, lost MQTT messages) are shown
  as error dialog to the browser that sent the pages

[← Back to the overview](../README.md)
