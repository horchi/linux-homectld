/*
 * -----------------------------------
 * Daemon /  Revision History
 * -----------------------------------
 *
 */

#define _VERSION     "0.1.36"
#define VERSION_DATE "25.10.2026"

#ifdef GIT_REV
#  define VERSION _VERSION "-GIT" GIT_REV
#else
#  define VERSION _VERSION
#endif

/*
 * ------------------------------------

2026-10-25: version 0.1.36
    - added: sun times (sun.c): dawn, sunrise, sunset and dusk of the day (civil twilight -6°, sunrise / sunset
             -0.833°), new text sensor SUN:0x01 'Sonne' with all four times (Plain Text widget shows them as table,
             on the HASP panel the tabs are replaced by blanks); recalculated once a day, on a change of the
             configuration and if the position moved more than 10 km; midnight sun / polar night are handled
             (without dawn / dusk sunrise / sunset are used, without these the sun is up / down all day)
    - added: config 'Standort aus GPS' (Daemon, default off): position for the sun times and the weather from the
             GPS receiver (GPS:0x0a), also the last known one, else latitude / longitude of the configuration
    - added: time ranges of the outputs (tab 'Zeiten'): start and end either a fixed time or a sun event
             (Morgendämmerung, Sonnenaufgang, Sonnenuntergang, Abenddämmerung), each with an offset in minutes;
             the resolved time of today is shown next to it; existing ranges stay valid
    - added: time control of deCONZ lights / groups (DZL, DZLG) and HomeMatic blinds (HMB), dialog 'Zeitsteuerung'
             (Sensor Setup -> Setup): switched only at the limits of the ranges (unlike DO / GPIO), switching by
             hand stays until the next limit; on / off (brightness optional) resp. position at 'Zu' / 'Auf';
             a limit may be 'keine'; limits missed by a restart are not caught up; a moving blind gets LEVEL, not STOP
    - added: light rules without LUA (lightrules.c), dialog 'Regeln' of the deCONZ switches / motion sensors (DZS):
             trigger button code or motion, action Umschalten / An / Aus / Folgen, targets (lights, groups, outputs),
             brightness, transition, optional time range for switching on, off timer retriggered by each event
             which switches off only what the rule switched on; each button event / motion is an event, also the
             same value again; the timers are kept in memory only
    - added: info dialog of the dashboard: tab 'Regeln' with the rules of a sensor resp. the rules switching a
             light and a running off timer; the time ranges are shown resolved ('Sonnenuntergang -30 (16:44)')
    - change: dashboard: day / night of the clock by the sun times of the daemon (also with the GPS position)
    - change: deCONZ groups are switched with an explicit state by the time control and the light rules
             (toggling by the WEBIF as before)
    - change: openweathermap: requested for the position of 'Standort aus GPS'

2026-10-18: version 0.1.35
    - bugfix: WEBIF: a websocket connection closed without the close handshake (page reload, mobile client vanished)
              kept its entry in the client list; its queue grew with every broadcast and the service thread armed the
              writable callback on every loop, 100 % CPU of one core until the restart; the client is removed now,
              a choked connection is retried every 200 ms instead of every loop

2026-10-18: version 0.1.34
    - added: GPS: view 'Sat' on the map page for aligning the dish: bearing line to the TV satellite (config
             'TV-Satellit: Orbitposition', default Astra 19.2 E), the vehicle axis and the outline of the motorhome on
             the map, azimuth, elevation,
             LNB skew and the angle relative to the vehicle (vehicle from above), the vehicle axis is the course of
             the last movement (new sensor GPS:0x0b 'Heading' of gpsmqtt.py, kept while standing, switch 'rückwärts
             eingeparkt' for + 180°); the text of the DVB signal monitor with start / stop, its sensor is configured
             as 'TV-Satellit: Signal-Sensor' (e.g. SC:0x30 of dvb-signal.sh), without it the view is not offered;
             the view opens at the maximum zoom; for a crank mast the turns for the elevation ('TV-Satellit: Kurbel',
             degrees per turn, default 12 = Kathrein HDM 140)
    - change: GPS map: the current position is shown as motorhome symbol (mdi-rv-truck) instead of the blue dot
    - added: GPS: receiver option 'Galileo aktivieren' (u-blox 8: GPS + Galileo + GLONASS via UBX CFG-GNSS, the
             u-blox 7 answers with NAK), sent with the other receiver settings
    - bugfix: GPS: after a daemon restart the live map stayed empty (world map) as long as the position did not
              change (receiver with static hold at standstill), the unchanged position is taken over now
    - added: contrib/armbian/enable-header-dtd.sh: option --uart1 enables UART1 of the ODROID M1 header (/dev/ttyS1,
             e.g. for a GPS module), documented in contrib/armbian/README.md
    - change: make git: the commit message is taken from the HISTORY block via a file, line feeds and '$' in the
              text stay intact (were lost by the shell quoting)

2026-10-06: version 0.1.33
    - bugfix: dashboard menu: the buttons were sorted as strings (order 10 before 2), with more than 10 dashboards
              the order looked random and every drag & drop scrambled it further
    - change: drag & drop of dashboard buttons and widgets: dragged to the left the element is placed in front
              of the drop target instead of behind it
    - added: dashboard widget type 'Balance' like the HASP one: horizontal bar with zero in the middle for
             bipolar values (negative to the left in the first color, positive to the right in the second),
             scale from 'Skala Max' (symmetric) or a negative 'Skala Min', option 'Richtung tauschen'
    - added: dashboard widget type 'Level' like the HASP one: MDI symbol filled by percentage in the bar
             color (range from the scale), the outline variant of the symbol as frame, option 'Füllung als Fläche'
    - bugfix: GPS tours: the GPS drift at standstill (100-200 m, 2-24 km/h shown) kept storing points and
              interrupted the pause every few minutes, a night stop of 9.5 h counted as 4.9 h pause and 10 km;
              movement for the pause now means leaving the radius 'gpsTourPauseRadius' (default 200 m) around
              the position of the last movement, no points are stored while paused
    - added: GPS tours: each automatic pause is stored as event 'Stillstand' (type 'halt', new column
             gpstourevents.endtime, added automatically) and listed with begin, end and duration; a pause /
             night stop entered during the halt takes over its begin, end and duration, the halt is not listed twice;
             the tour head shows the driving time (duration minus pauses)
    - added: GPS: u-blox receiver settings 'Static Hold' [km/h] and dynamic model 'Automotive' (Setup -> GPS), sent
             via MQTT to gpsmqtt.py (registers its command topic with 'init', default <topic>/in) which writes
             them into the receiver (UBX CFG-NAV5) on every port open; the u-blox 7 keeps them in RAM only
    - added: Make.user switch GPS: 'make install' installs the GPS service (gps.u-blox),
             'make install-dependencies' installs its python modules
    - bugfix: GPS map: zooming (buttons, wheel, pinch) stopped the following of the position in the live view and
              in the map of the tour being recorded, only dragging stops it now; after a zoom the position is centered again
    - added: GPS map: zoom buttons (+ / -) in the button column on the right, in the style of the other map buttons
    - change: GPS tours: the recording state symbol of the tool bar is replaced by the color of the 'Touren' button
              and of the 'Map' tab of the main menu (visible on every page): red while recording, orange while paused;
              new theme color --orange, --red added to the light / blue / yellow themes
    - bugfix: GPS map: a tour with a tiny extent (recorded at standstill) opened at the maximum zoom, nothing to
              zoom in any more; the tour and 'Zentrieren' fit to zoom 17 at most, the tiles go to zoom 19 now (was 18)
    - change: the live position is logged with the new eloquence flag 'Gps' (formerly always), on movement
              only and at most every 10 seconds
    - bugfix: gpsmqtt.py: only the talkers GP / GN were read, the RMC sentence of combined receivers ('$GNRMC',
              u-blox 8 and newer) was missed by a typo (speed stayed 0, no date / fix state); RMC and GGA of any
              talker are used now; options -d (device) and -b (baud rate) for other receivers; receiver notes in the README
    - bugfix: GPS: gpsmqtt.py published the position 0/0 (and the geocoding of it) before the first fix after its
              start; the position sensors are published only with a position now, the daemon ignores 0/0 for
              the live map and the tour
    - change: 'make restart' does the full install (optional parts included) and restarts, besides the daemon,
              the running services of the repository (w1mqtt, bmsmqtt, votromqtt, i2cmqtt, victronmqtt, mopeka,
              thetford, gpsmqtt); 'make lrestart' stays daemon only

2026-10-04: version 0.1.32
    - added: widget option 'Spin bei Aktivität' for symbol widgets: while the sensor is on the dashboard symbol
             turns (formerly hard coded for mdi-snowflake) and the HASP Symbol / SymbolValue widgets show a
             turning ring (spinner) around the symbol; existing snowflake widgets need the option set once
    - bugfix: LUA sensors (CV / DO) with watch() dependencies were re-evaluated on every process() call (each
              incoming MQTT value) as long as the result did not change, since the check compared the change
              time of the dependency with the change time of the result; now compared with the last run of the
              script. Scripts accumulating time between the calls (e.g. the compressor runtime of the day)
              saw only 1-5 s per call and lost them by rounding - broken since the end of July
    - added: LUA: every(seconds) requests a periodic run of the script in its own interval, in addition
             to the watch() dependencies
    - change: LUA: no forced run of all scripts once per minute any more, a script runs on its watch()
              changes and its every() interval; without both declarations it runs every 60 seconds (as before);
              a saved script runs again at once. Scripts with watch() only that depend on the passing of
              time (hold times, timeouts) need an every() now
    - change: contrib/lua-examples/danfoss-compressor-time.lua: counts seconds in the script (every(10)),
              only the result is rounded to minutes; takes the stored minutes over after a daemon restart
    - bugfix: HASP: a stale sensor (device offline, e.g. ALPICOOL) kept its last state on the panel,
              it is shown as 'off' now like on the dashboard
    - bugfix: int columns read from the database could carry stale upper bytes (bound as 4 byte MYSQL_TYPE_LONG
              into the 8 byte value): the periodic evaluation built sensor keys like 'CV:0xffffffff00000003',
              the LUA bookkeeping (watch / every / last run) was split between two keys per sensor
    - bugfix: LUA scripts were run during the initialization (GPIO setup) before the bindings existed, the
              failed first run ('watch' nil) counted as done and the script never collected its watch() / every()
    - bugfix: an unchanged ADS (i2c analog) value cleared the pending evaluation of the LUA sensors triggered by
              another sensor; CV results (e.g. 'Strom (Verbraucher)' from BMS:0x02) lagged behind by minutes
    - added: HASP setup: buttons 'Panel wecken' (sends 'idle off': display on, idle cycle restarts),
             'Panel neu starten' (reboot, with confirmation) and 'Screenshot' (opens the screenshot page of
             the panel's web server; the IP comes from the panel's statusupdate, requested with every page send)

2026-10-04: version 0.1.31
    - added: HASP: widget type 'Balance' - horizontal bar with zero in the middle for bipolar values
             (battery current, grid power): negative to the left (red), positive to the right (green),
             symmetric scale from 'Skala Max' or asymmetric with a negative 'Skala Min', option 'Richtung tauschen'
    - added: HASP: widget type 'Timer' (pseudo sensor TIMER:0x01) - countdown of 1..60 minutes running in the
             panel firmware with the board buzzer as alarm, step buttons (option 'Schritte', default -1 / +1 / +5)
             and Start-Stop act locally, time display and buttons grow with the card height;
             needs the firmware 0.7.0.2 (commands custom/timer and custom/beep, update over WLAN with openHASP/ota.sh)

2026-10-03: version 0.1.30
    - change: GPS tours: the tab bar shows only the recording state symbol, the details
              (distance, points) moved behind 'aktiv' / 'pausiert' in the head of the tour
    - change: GPS tours: mdi icons for the 'Live' / 'Touren' buttons, the tool bar looks like the dashboard
              menu (background, button size with the big dashboard symbols / kiosk) instead of its own style by display width
    - added: GPS tours: each stored point is pushed to the WEBIF ('gpstourpoint' with distance and
             points), the tour list / head / map update per point; the map of the tour being recorded
             grows with the points and can optionally follow the position (button like the live view)
    - change: GPS tours: the tour map no longer re-fetches all points on state changes, only the events
    - added: WEBIF reloads itself after an update: index.html carries a build id (meta 'webif-build',
             genindex.sh), the page compares it with the server's index.html on every socket connect;
             iOS home screen apps kept the old start page from their own cache, even after a force quit
    - change: index.html: home screen icon also as 'apple-touch-icon' (the 'precomposed' variant is deprecated),
              switched with the instance icon like the favicon
    - bugfix: HASP: the objects of a card are sent in one jsonl message with a pause between the messages;
              one message per object overran the receive queue of the panel (64), cards were missing now and then
    - bugfix: HASP: CSS color names of the widget settings (e.g. 'green', 'gray' of older dashboard widgets)
              were not understood, the panel showed the default colors (blue for 'on')
    - added: HASP: idle times of the panel (dim / display off) on the HASP setup page, sent with the pages
    - bugfix: script sensors: a 'title' reported by a script replaced the script name in the valuefact,
              the script could not be called any more ('scripts.d/<title>: not found')
    - added: script sensor dvb-signal.sh: switch which monitors the DVB signal (dvb-fe-tool --femon) about
             once per second for aligning the dish, lock / level / C/N as text of the widget
    - change: setup: the sensor filter of the HASP page is kept for the session, the signal filter of the Wifi page permanently
    - change: HASP: Level widget: the fill refers to the drawn height of the symbol (glyph bounds from the
              MDI font; a drop looked full at 83 %), option 'Füllung als Fläche' fills a rectangle behind
              frame symbols without a filled variant (e.g. mdi-crop-landscape as a tank)
    - added: HASP: widget type 'Choice' (button matrix of the choices of the sensor, the current value is
             highlighted, a touch sends the choice to the command topic like the dashboard choice widget)
    - bugfix: HASP: touch on a symbol of a sensor with a MQTT command topic (e.g. KENWOOD, VICTRON) was
              ignored as 'not controllable', it is switched via the topic now like from the WEBIF
    - added: GPS: view 'Tankstopps' (gas station icon): all fuel stops of all tours, fuel stops can be
             entered outside of a tour (with date / time), consumption from full tank to full tank
             across the tours and the average over all fills
    - change: GPS tours: the consumption chain of a tour starts at the last full tank before the tour
              (any tour or standalone), the odometer of the tour start is used only without one ('ab Tour-Start');
              the tour details show date and odometer of that full tank
    - bugfix: GPS live map: following stopped itself after the first automatic pan

2026-09-27: version 0.1.29
    - change: iOS home screen app: status bar 'black-translucent' without viewport-fit=cover / theme-color and the
              menu fixed at the top (like the epgd WEBIF), which avoids the iOS 27 status bar blur over the menu
    - change: footer: height by content, single-line status message, clear of the rounded display corners in the iOS app

2026-09-25: version 0.1.28
    - added: widget dialog: button 'Reset' resets the widget's settings to the defaults of the
             valuefact, as if it was just added to the dashboard
    - added: dashboard widgets whose sensor (valuefact) no longer exists or is inactive are shown
             marked (dashed red frame, 'Sensor fehlt' / 'Sensor inaktiv') instead of hidden / empty
    - added: choice widget: option 'Buttons' shows the choices as a wrapping button field instead of a list
    - change: symbol widget: a stale sensor (e.g. ALPICOOL offline) is displayed as 'off' (symbol, color, no animation)
    - added: config option 'Große Dashboard Symbole' (WEB Interface), the URL parameter 'kiosk' has priority

2026-09-20: version 0.1.27
    - bugfix: restored status sensors of MQTT devices (e.g. TASMOTA via mqtt.d) showed 'on' after a start
              until their first message: the kind was unknown and the value (-1) was published instead of
              the state; the kind is stored and restored with the IO state (new column iostates.kind)
    - change: i2cmqtt -h describes the supported chips (purpose, channels, ranges, addresses)
    - added: make install-dependencies (contrib/install-dependencies.sh): installs the build / runtime dependencies for
             Armbian and Ubuntu, libgpiod 2 and libwebsockets from source if needed, optional parts
             (WOMO incl. mariadb-server and mosquitto, MOPEKA, THETFORD, GARMIN) according to Make.user

2026-09-19: version 0.1.26
    - added: kenwood: receiver for the steering wheel remote (ESP-NOW, enabled by KENWOOD_REMOTE_MAC),
             remote sketch kenwood/kenwood-remote for the ESP32-C3 Super Mini
    - bugfix: MQTT converter scripts (/etc/homectld/mqtt.d) were only called for JSON payloads
              (since 05/2026, e.g. TASMOTA 'OFF' failed with 'Can't parse json'), checked before parsing again
    - bugfix: crash (atoi(nullptr)) in switchCommand() when the state of a sensor was restored on its
              MQTT init message; the stored state is passed now, TASMOTA gets ON/OFF instead of TOGGLE
    - bugfix: switching TASMOTA devices sent JSON instead of ON/OFF/TOGGLE (since 05/2026), devices are
              recognised by their command topic (cmnd/...) independent of the type set by the converter script
    - bugfix: sensors restored from iostates at start were shown dimmed (invalid) in the WEBIF until the
              first new data arrived (since 0.1.17); restored data is marked valid, 'last' is stored and
              restored too (new column iostates.last, added automatically); the IO state is now stored
              for sensors without recording as well, so all sensors show their last data after a start

2026-09-19: version 0.1.25
    - added:  kenwood: OTA firmware update for the ESP32 sketches (make upload-ota, also alpicool),
              scanned NEC codes of the DMX8019DABS, presets, power via ACC relay, ESP-NOW remote (planning)
    - change: Sensor setup in the WEBIF shows newly registered sensors without page reload
    - added:  'Add Widget' dialog allows selecting several widgets at once, across filters
    - bugfix: WEBIF: after a login the login page came back (it was stored as start page),
              now the dashboard opens; enter in the login fields submits (form with default button)
    - bugfix: WEBIF: no dashboard was active when none was remembered (e.g. after a login with
              fresh storage), now the first one is selected
    - change: replyResult() takes a printf-style format (status, client, format, ...), old signature removed
    - change: cDbTable::find() / fetch() and cDbStatement::fetch() return bool instead of int (yes/no)

2026-09-17: version 0.1.24
    - change: build: the unused defines _POOL / _WOMO are gone (the make variable WOMO still selects contrib/womo)
    - bugfix: build with GCC 15 (Ubuntu 26.04): sol.hpp optional<T&>::emplace called a non-existing construct()

2026-09-16: version 0.1.23
    - added:  contrib/cleanup-false-zeros.sql: removes the false 0 samples
              and repairs the peaks (dry run by default, see the header)
    - change: the peaks (min / max in the widget) are maintained also for sensors without recording

2026-09-16: version 0.1.22
    - change: Garmin: edit / bulk edit sends type, name and location in one request per activity
              (garmin.py edit, one call instead of one per field)
    - added:  Garmin: filter on the location (with suggestions), the name filter matches the name only
    - added:  Garmin: shift / ctrl click on a check box selects the range from the last clicked one
    - added:  Garmin: sort the lists by a click on the column header
    - bugfix: progress dialog: the 5 minute timer of an earlier dialog closed a later one
              (e.g. the one of the bulk edit), the dialog of the bulk edit lives as long as the daemon waits
    - bugfix: progress dialog: the spinner was cut on the right (fixed width 125px vs. the dialog margins)
    - added:  make gzip-web (gz): only refresh the gzip twins of the web files, no install
    - added:  Garmin: distance from the GPS track when Garmin's deviates by more than the configured
              percentage (config 'Distanz aus dem Track ab Abweichung', default 50 %); computed when a
              track is loaded, stored in the new column activities.trackdistance, the list marks it in
              italics, the details show both values, the track header shows the track length
    - change: Garmin: the sync stores the values of the list entry as preliminary details (no
              'details' call needed for most values), the dialog offers 'Details nachladen' for the
              rest (min. HR, avg. temperature); 'make install' + a full
              sync ('alle') fill them for the existing activities
    - added:  config item type 'ComboChoice' (ctComboChoice): text input with the values stored before
              as suggestions (internal item '<name>History' with value and label, the label is resolved by
              the WEBIF, setup.js comboLabelResolvers), used for 'Windy App Spot ID' - the spot name comes
              from Windy's widget endpoint, spots are picked by name, a button forgets the entered value
    - change: the icon font (mdi, 400 kB) is preloaded with the page, the icons appeared late when the
              font was first requested by the first icon drawn (e.g. on the setup page)
    - change: web server: mime types for fonts (woff2, woff, ttf), json and ico (were text/plain)
    - bugfix: Garmin: the type filter's options followed the name / location filter only with the next
              list push and stayed restricted after the filter was cleared
    - bugfix: Garmin: details with an emoji in the description could not be stored (tables are utf8mb3),
              the JSON is written ASCII escaped now; the sync message counted all activities as new
    - bugfix: start: loadIoStates() reset the values received during the init to 0 (float read as int),
              the first store wrote them as 0 (since 0.1.17); invalid / script init data is not stored
    - change: code cleanup: the empty subclass HomeCtl (specific.*) is gone, Daemon is renamed to HomeCtl,
              the configuration items moved to the new config.c

2026-09-14: version 0.1.21
    - added: System Services: 'Options' in the burger menu opens /etc/default/<service> in the
             editor (only if the file exists), save or save & restart the service
    - change: make install: systemd units are only written when their content changed,
              'systemctl daemon-reload' only in this case (installUnit in Make.config)

2026-09-14: version 0.1.20
    - added: Garmin: location editable, bulk edit (check boxes in the list, group check box) of
             type / name / location via garmin.py settypes / renames / setlocations
    - added: Garmin: world map of the activities (start positions as points colored by type,
             legend filters, click opens the track), speed / time / altitude on hovering the track
    - added: 'Zentrieren' button on the maps (live map, Garmin track, activity map)
    - change: Garmin: water sports show km/h next to the knots everywhere (list, details, track, hover)
    - added: GPS tours: odometer at start / end and a comment (gpstours, editable with the name), events while
             recording (fuel stop with liters, price and odometer, toll, pause, night stop, note)
             with the current position in the new table 'gpstourevents'; costs and fuel consumption
             per stop (partial fills count to the next full tank, by odometer and by GPS distance) and average are calculated and
             shown in the list, the tour info and as markers on the map (edit / delete)
    - change: tour name dialog in the look of the other dialogs, field labels in dialogs
              in the dialog text color (the panel gray was too pale)

2026-09-13: version 0.1.19
    - bugfix: dialogs on phones: size and center by the visible area (on iOS Safari the OK
              button ended below the screen), buttons wrap, Garmin details in one column
    - added: scroll arrows at the button strips (main tabs, dashboards, setup menu) when
             there is more to scroll, a click scrolls, on touch devices swipe as before
    - change: loading: files with version tag are cached a year ('immutable'), others get
              Last-Modified / 304; 'make install' creates gzip twins (<file>.gz) which the
              daemon serves; CodeMirror (LUA editor) is loaded on demand; moment without
              the locale bundle (de only)

2026-09-13: version 0.1.18
    - added: Garmin table shows two dots per activity: details / track already loaded
    - added: GPS tour stop asks for the end: now or the arrival at the final position
             (config 'Tour: Toleranz Ankunft', default 200 m)
    - change: build: HISTORY.h is included by main.c only (a version bump rebuilds main.o instead
              of everything), dependency rules keep the lib/ paths (header changes there rebuild
              the objects now), -pipe, compile echo shows the directory

2026-09-13: version 0.1.17
    - change: SensorData got setters (setValue/setState/setText/touch) which maintain
              last, changedAt, valid and a 'dirty' flag; store() writes a sensor only with
              new data (dirty) instead of comparing time stamps (the 'time(0) - 1' hack is gone);
              last, changedAt, valid and dirty are private now (getters, named mutators)
    - added: The last raw message (JSON) of a sensor is kept and shown in the widget info
             dialog on a second tab 'JSON' (MQTT, Deconz, Arduino, W1, ...)

2026-09-12: version 0.1.16
    - added:  Garmin Connect activities fetched on demand via
              garmin/garmin.py into the new table 'activities', grouped by type, details,
              change of type and name, GPS track on a map colored by speed (see garmin/README.md)
    - change: Browser cache time of the WEBIF files reduced from 1 hour to 10 minutes;
              htdocs/index.html is generated from index.html.in with versioned asset
              URLs (?v=<content hash>), browsers load changed files right after 'make'
    - added:  'make config' edits Make.user interactively (dialog based), GARMIN switch
              in Make.user to include the Garmin script in 'make install'

2026-09-12: version 0.1.15
    - added:  WEBIF Setup -> Wifi: 'Forget' button deletes stored connection profiles,
              list of all stored profiles with the bound device, the present wifi
              device (vendor, product, MAC) is shown; the last profile of a SSID is
              renamed to the SSID when duplicates ('SSID 1', ...) are deleted
    - bugfix: Wifi connect failed for stored networks after a change of the USB wifi
              stick (profile bound to the old interface name), the profile is unbound now

2026-09-12: version 0.1.14
    - added:  WEBIF widgets can show the peak min value in addition to the peak max
              (Value, Chart, MeterLevel, Gauge); the Meter widget shows min/max
              above the gauge
    - bugfix: The first store after a daemon start wrote a sample with value 0 for
              every sensor which had not reported yet (poisoned charts and peak min)

2026-09-12: version 0.1.13
    - added: Support of the INA219 current/power monitor in the i2cmqtt service
             (option --ina), publishes bus voltage, current and power; the shunt
             resistor is configurable and defaults to 0.1 Ohm

2026-09-12: version 0.1.12
    - added: ESP32 sketch kenwood/ to control a Kenwood car radio via the steering wheel
             remote wire (MQTT -> NEC codes), power state read from P.CONT

2026-09-12: version 0.1.11
    - added: Recording of GPS tours (WEBIF: Map -> Touren), points are stored on movement,
             auto pause/resume, survives a restart; new table 'gpstours'

2026-09-08: version 0.1.10
    - added: Support of openHASP touch panels (e.g. Elecrow CrowPanel) as display.
             Pages and widgets are configured in the WEBIF (Setup -> HASP Panel),
             the daemon sends them via MQTT, shows live values and handles touches
             on switch widgets. Widget types: Symbol, SymbolValue, Value, Text,
             Meter, MeterLevel, Time and Level (symbol filled by percentage).
    - added: Help buttons in the WEBIF open the README at the related section

2026-01-27: version 0.1.9
    - added: Support of LTE modem (Alfa Tube-U4G LTE USB Modem)

2025-12-21: version 0.1.8
    - change: Improvemets regarding http and websocket handling

2025-08-05: version 0.1.7
    - added: TASMOTA interface

2025-02-23: version 0.1.6
    - change: Update of README

2024-12-18: version 0.1.5
    - change: Improved deconz websocket reconnect

2024-05-07: version 0.1.4
   - change: Style improvements
   - change: Added color condition for meters bar color
   - added:  script update.sh to update homectld via WEBIF

2024-05-05: version 0.1.3
   - change: improved info dialog

2024-05-05: version 0.1.2
   - fixed: Pool pump steering

2024-05-01: version 0.1.1
   - added:  raspberry pi porting
   - change: style adjustments

2024-03-03: version 0.1.0
   - added:  i2c bus components
   - added:  victron protocol
   - change: many improvements
   - added:  configurable widget color condition
   - added:  condition for automatic outputs
   - added:  demo code for systemd interface

2023-01-07: version 0.0.7
   - change: minor improvements

2022-10-24: version 0.0.6
   - change: minor improvements like vdr url

2022-07-19: version 0.0.5
   - change: git merge

2022-07-19: version 0.0.4
   - change: development

2022-01-31: version 0.0.3
   - change: first release of common homectld version

2021-05-25: version 0.0.2
   - change: changes and fixed

2020-04-16: version 0.0.1
   - start of implementation

 * ------------------------------------
 */
