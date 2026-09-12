/*
 * -----------------------------------
 * Daemon /  Revision History
 * -----------------------------------
 *
 */

#define _VERSION     "0.1.13"
#define VERSION_DATE "12.09.2026"

#ifdef GIT_REV
#  define VERSION _VERSION "-GIT" GIT_REV
#else
#  define VERSION _VERSION
#endif

/*
 * ------------------------------------

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
