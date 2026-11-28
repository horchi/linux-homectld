//***************************************************************************
// Automation Control
// File config.c
// This code is distributed under the terms and conditions of the
// GNU GENERAL PUBLIC LICENSE. See the file LICENSE for details.
// Date 16.04.2021 - Jörg Wendel
//***************************************************************************

#include "daemon.h"

//***************************************************************************
// Configuration Items
//   the source of the 'N' items of the config table (initConfigTable())
//***************************************************************************

std::list<HomeCtl::ConfigItemDef> HomeCtl::configuration
{
   // daemon

   { "instanceName",              ctString,  "Home Control", "N", false, "Daemon", "Titel", "Page Titel / Instanz", "doc/configuration.md#daemon" },
   { "instanceIcon",              ctChoice,  "home.png",     "N", false, "Daemon", "Application Icon", "", "doc/configuration.md#daemon" },
   { "latitude",                  ctNum,     "50.3",         "N", false, "Daemon", "Breitengrad", "", "doc/lights.md#sun-times" },
   { "longitude",                 ctNum,     "8.79",         "N", false, "Daemon", "Längengrad", "", "doc/lights.md#sun-times" },
   { "gpsLocation",               ctBool,    "",             "N", false, "Daemon", "Standort aus GPS", "Position für die Sonnenzeiten (Sensor 'Sonne', Zeitfenster der Ausgänge) und das Wetter vom GPS-Empfänger statt Breiten- / Längengrad der Konfiguration; liefert der Empfänger nichts mehr, gilt die letzte bekannte Position", "doc/lights.md#sun-times" },

   { "interval",                  ctInteger, "60",           "N", false, "Daemon", "Intervall der Aufzeichung", "Datenbank Aufzeichung [s]", "doc/configuration.md#daemon" },

   { "aggregateHistory",          ctInteger, "365",          "N", false, "Daemon", "Historie [Tage]", "history for aggregation [days] (default 0 days -> aggegation turned OFF)", "doc/configuration.md#daemon" },
   { "aggregateInterval",         ctInteger, "15",           "N", false, "Daemon", "Aggregate Interval der historisierten Daten [m]", "aggregation interval in minutes - 'one sample per interval will be build'", "doc/configuration.md#daemon" },
   { "peakResetAt",               ctString,  "",             "N", true,  "Daemon", "", "" },

   { "openWeatherApiKey",         ctString,  "",             "N", false, "Daemon", "Openweathermap API Key", "", "doc/configuration.md#daemon" },
   { "weatherInterval",           ctInteger, "15",           "N", false, "Daemon", "Weather Refresh Interval [m]", "", "doc/configuration.md#daemon" },

   { "eloquence",                 ctBitSelect, "1",          "N", false, "Daemon", "Log Eloquence", "", "doc/configuration.md#daemon" },
   { "hiddenCategories",          ctMultiSelect, "",         "N", false, "Daemon", "Ausgeblendete Kategorien", "Kategorien dieser Seite die nicht benötigt werden (z.B. GPS, Garmin), ihre Einstellungen bleiben erhalten", "doc/configuration.md#daemon" },

   // GPS tour recording

   { "gpsTourMinDistance",        ctInteger, "25",           "N", false, "GPS", "Tour: Mindestbewegung [m]", "Ein Punkt wird aufgezeichnet wenn die Position sich mindestens um diese Distanz geändert hat", "doc/gps.md#recording" },
   { "gpsTourPauseAfter",         ctInteger, "5",            "N", false, "GPS", "Tour: Pause nach Stillstand [min]", "Ohne Bewegung für diese Zeit gilt die Tour als pausiert, bei erneuter Bewegung wird automatisch fortgesetzt; die Pausen werden als Ereignis 'Stillstand' mit Beginn und Ende festgehalten", "doc/gps.md#recording" },
   { "gpsTourPauseRadius",        ctInteger, "200",          "N", false, "GPS", "Tour: Radius Stillstand [m]", "Als Bewegung für die Pause zählt erst das Verlassen dieses Radius um die letzte Position - im Stand streut das GPS um 100-200 m, diese Sprünge sollen die Pause nicht unterbrechen", "doc/gps.md#recording" },
   { "gpsTourEndTolerance",       ctInteger, "200",          "N", false, "GPS", "Tour: Toleranz Ankunft [m]", "Beim Beenden einer Tour wird als Ende der Zeitpunkt vorgeschlagen, ab dem die Position innerhalb dieser Distanz um den Endpunkt blieb (z.B. Anmeldung am Campingplatz)", "doc/gps.md#display" },
   { "gpsUbloxStaticHold",        ctInteger, "0",            "N", false, "GPS", "Empfänger: Static Hold [km/h]", "u-blox Empfänger (gpsmqtt.py): unter dieser Geschwindigkeit friert der Empfänger die Position ein, kein Drift im Stand (0 = aus, max 9 km/h, Empfehlung 2-3); wird beim Start und bei Änderung an den Empfänger gesendet", "doc/gps.md#recording" },
   { "gpsUbloxAutomotive",        ctBool,    "",             "N", false, "GPS", "Empfänger: Dynamikmodell Automotive", "u-blox Empfänger (gpsmqtt.py): Plattformmodell 'automotive' statt 'portable', die Positionsfilterung ist auf Fahrzeuge abgestimmt; aus = 'portable' (Werkseinstellung)", "doc/gps.md#recording" },
   { "gpsUbloxGalileo",           ctBool,    "",             "N", false, "GPS", "Empfänger: Galileo aktivieren", "u-blox 8 (M8) Empfänger: GPS + Galileo + GLONASS statt ab Werk GPS + GLONASS, mehr Satelliten und weniger Drift; der u-blox 7 kann kein Galileo (Log: 'CFG-GNSS not acknowledged')", "doc/gps.md#recording" },
   { "gpsSatLongitude",           ctNum,     "19.2",         "N", false, "GPS", "TV-Satellit: Orbitposition [°]", "Position des Satelliten auf dem geostationären Orbit, Ost positiv, West negativ (Astra 19.2, Hotbird 13.0, Astra 28.2, Eutelsat 9.0); Grundlage für Azimut, Elevation und Skew in der Ansicht 'Sat' der Karte", "doc/gps.md#dish-alignment" },
   { "gpsSatSignalSensor",        ctString,  "",             "N", false, "GPS", "TV-Satellit: Signal-Sensor", "Sensor des Signalmonitors für das Ausrichten der Schüssel, z.B. SC:0x30 (Script-Sensor dvb-signal.sh); ohne Angabe wird die Ansicht 'Sat' der Karte nicht angeboten", "doc/gps.md#dish-alignment" },
   { "gpsSatCrankDegrees",        ctNum,     "12",           "N", false, "GPS", "TV-Satellit: Kurbel [°/Umdrehung]", "Gelenkmast mit Kurbel für die Elevation (Kathrein HDM 140: 12° je Umdrehung, 1 Teilstrich = 1°, Flachantenne waagerecht bei 0): die Ansicht 'Sat' zeigt die Umdrehungen = (90° - Elevation) / Wert; 0 = keine Anzeige", "doc/gps.md#dish-alignment" },

   // Garmin activities

   { "garminTrackDeviation",      ctInteger, "50",           "N", false, "Garmin", "Distanz aus dem Track ab Abweichung [%]", "Weicht die von Garmin gemeldete Distanz um mehr als diesen Prozentsatz von der aus dem geladenen GPS-Track berechneten ab, wird in Liste und Details die Track-Distanz verwendet (die Uhr summiert bei manchen Wassersport-Aktivitäten kaum Strecke auf). 0 = immer Garmins Wert", "doc/garmin.md#garmin-activities" },

// #ifdef _POOL
//    { "phReference",               ctNum,     "7.2",       "N", false, "Pool", "PH Sollwert", "Sollwert [PH] (default 7,2)" },
//    { "phMinusDensity",            ctNum,     "1.4",       "N", false, "Pool", "Dichte PH Minus [kg/l]", "Wie viel kg wiegt ein Liter PH Minus (default 1,4)" },
//    { "phMinusDemand01",           ctInteger, "85",        "N", false, "Pool", "Menge zum Senken um 0,1 [g]", "Wie viel Gramm PH Minus wird zum Senken des PH Wertes um 0,1 für das vorhandene Pool Volumen benötigt (default 60g)" },
//    { "phMinusDayLimit",           ctInteger, "100",       "N", false, "Pool", "Obergrenze PH Minus/Tag [ml]", "Wie viel PH Minus wird pro Tag maximal zugegeben [ml] (default 100ml)" },
//    { "phPumpDurationPer100",      ctInteger, "1000",      "N", false, "Pool", "Laufzeit Dosierpumpe/100ml [ms]", "Welche Zeit in Millisekunden benötigt die Dosierpumpe um 100ml zu fördern (default 1000ms)" },
// #endif

   // web

   { "webPort",                   ctInteger, "61109",        "N", false, "WEB Interface", "Port des Web Interfaces", "", "doc/configuration.md#web-interface" },
   { "webSSL",                    ctBool,    "",             "N", false, "WEB Interface", "Use SSL for WebInterface", "", "doc/configuration.md#web-interface" },
   { "style",                     ctChoice,  "dark",         "N", false, "WEB Interface", "Farbschema", "", "doc/configuration.md#web-interface" },
   { "background",                ctChoice,  "",             "N", false, "WEB Interface", "Background image", "", "doc/configuration.md#web-interface" },
   { "schema",                    ctChoice,  "default.svg",  "N", false, "WEB Interface", "Schematische Darstellung", "Das Bild des Schemas (img/schema/schema-<Name>.png oder .svg)", "doc/configuration.md#web-interface" },
   { "schemaBackground",          ctString,  "",             "N", false, "WEB Interface", "Hintergrundfarbe des Schemas", "CSS Farbe hinter dem Bild des Schemas (wirkt bei transparenten Bildern wie den SVGs), leer = die des Farbschemas, auch im Schema-Setup über 'Hintergrund' einstellbar", "doc/configuration.md#web-interface" },
   { "chartRange",                ctNum,     "1.5",          "N", true,  "WEB Interface", "Chart Range", "" },
   { "chartSensors",              ctNum,     "VA:0x0",       "N", true,  "WEB Interface", "Chart Sensors", "" },
   { "showList",                  ctBool,    "0",            "N", false, "WEB Interface", "Liste anzeigen", "", "doc/configuration.md#web-interface" },
   { "bigDashSymbols",            ctBool,    "0",            "N", false, "WEB Interface", "Große Dashboard Symbole", "Dashboard-Menü mit großen Symbolen wie im Kiosk-Modus, der URL-Parameter 'kiosk' hat Vorrang", "doc/webif.md#url-parameters" },
   { "windyAppSpotID",            ctComboChoice,   "5247411",      "N", false, "WEB Interface", "Windy App Spot ID", "Spot-ID eingeben oder einen der bisher gespeicherten Spots aus den Vorschlägen wählen (die Namen holt die Oberfläche von Windy), Anleitung zum Ermitteln der Spot-ID siehe README", "doc/windy.md#how-to-find-a-specific-windsurf-spot-id" },
   { "windyAppID",                ctString,  "",             "N", false, "WEB Interface", "Windy App ID", "App-ID einrichten, siehe README", "doc/windy.md#how-to-get-your-windy-app-api-key" },

   // MQTT interface

   { "mqttUrl",                   ctString,  "tcp://localhost:1883",   "N", false, "MQTT", "MQTT Broker Url", "URL der MQTT Instanz Beispiel: 'tcp://127.0.0.1:1883'", "doc/configuration.md#mqtt" },
   { "mqttUser",                  ctString,  "",                       "N", false, "MQTT", "User", "", "doc/configuration.md#mqtt" },
   { "mqttPassword",              ctString,  "",                       "N", false, "MQTT", "Password", "", "doc/configuration.md#mqtt" },
   { "mqttSensorTopics",          ctStringSet, INSTANCE "2mqtt/w1/#",  "N", false, "MQTT", "Zusätzliche sensor Topics", "Diese Topics werden gelesen und als Sensor Daten verwendet", "doc/mqtt-converter.md#how-it-works" },
   { "arduinoTopic",              ctString,  INSTANCE "2mqtt/arduino", "N", false, "MQTT", "MQTT Topic des Arduino Interface", "", "doc/configuration.md#mqtt" },
   { "arduinoInterval",           ctInteger, "10",                     "N", false, "MQTT", "Intervall der Arduino Messungen", "[s]", "doc/configuration.md#mqtt" },

   // Home Automation MQTT interface

   { "mqttHaDataTopic",           ctString,  "",  "N", false, "Home Automation Interface", "MQTT Data Topic Name", "&lt;NAME&gt; wird gegen den Messwertnamen und &lt;GROUP&gt; gegen den Namen der Gruppe ersetzt. Beispiel: p4d2mqtt/sensor/&lt;NAME&gt;/state", "doc/configuration.md#home-automation-interface" },
   { "mqttHaHaveConfigTopic",     ctBool,    "0", "N", false, "Home Automation Interface", "Config Topic", "Speziell für HomeAssistant", "doc/configuration.md#home-automation-interface" },

   // openHASP panel

   { "haspMqttTopic",             ctString,  "hasp/plates", "N", false, "HASP Panel", "MQTT Basis-Topic des openHASP Panels", "Kommandos gehen an &lt;Topic&gt;/command/..., z.B. 'hasp/plates' (Gruppe) oder 'hasp/&lt;hostname&gt;'. Leer = Panel-Anbindung aus", "doc/hasp.md#prerequisites" },
   { "haspIdleShort",             ctInteger, "0", "N", false, "HASP Panel", "Leerlauf kurz [s]", "Nach dieser Zeit ohne Berührung dunkelt das Panel ab (Skript idle_short.cmd im Panel), 0 = aus. Wird mit den Seiten an das Panel gesendet (config/gui), beide Zeiten 0 = die Einstellung des Panels bleibt unverändert", "doc/hasp.md#pages-and-widgets" },
   { "haspFontCodepoints",        ctString,  "",             "N", true,  "HASP Panel", "", "" },   // MDI symbols of the last font upload (hint 'new symbols')
   { "haspIdleLong",              ctInteger, "0", "N", false, "HASP Panel", "Leerlauf lang [s]", "Nach dieser Zeit ohne Berührung schaltet das Panel das Display aus (Skript idle_long.cmd im Panel), 0 = aus", "doc/hasp.md#pages-and-widgets" },
   { "haspHomeAfter",             ctInteger, "0", "N", false, "HASP Panel", "Zurück zu Seite 1 [s]", "Nach dieser Zeit ohne Bedienung (Berührung eines Objekts, Seitenwechsel, Wecken) schaltet das Panel auf Seite 1 zurück, 0 = aus", "doc/hasp.md#pages-and-widgets" },

   // mail

   { "mail",                      ctBool,    "0",                  "N", false, "Mail", "Mail Benachrichtigung", "Schaltet alle Mails ein/aus (Alerts, Status- und Fehler-Mails der S 3200); die Test-Mails werden immer gesendet", "doc/configuration.md#mail" },
   { "mailScript",                ctString,  BIN_PATH "/mail.sh",  "N", false, "Mail", "Skript zum senden von Mails", "", "doc/configuration.md#mail" },
   { "stateMailTo",               ctString,  "",                   "N", false, "Mail", "Status Mail Empfänger", "Komma getrennte Empfängerliste", "doc/configuration.md#mail" },
   { "errorMailTo",               ctString,  "",                   "N", false, "Mail", "Fehler Mail Empfänger", "Komma getrennte Empfängerliste", "doc/configuration.md#mail" },
   { "webUrl",                    ctString,  "",                   "N", false, "Mail", "URL der Visualisierung", "kann mit %weburl% in die Mails eingefügt werden", "doc/configuration.md#mail" },

   // deconz

   { "deconzHttpUrl",             ctString,  "",                   "N", false, "DECONZ", "deCONZ HTTP URL", "", "doc/deconz.md#setup" },
   { "deconzApiKey",              ctString,  "",                   "N", false, "DECONZ", "deCONZ API key", "", "doc/deconz.md#setup" },

   // homematic

   { "homeMaticBridge",           ctChoice,  "Aus",                "N", false, "HomeMatic", "Anbindung", "Rollläden (HMB) über HomeMatic BidCos-RF – Aus; Node-RED: Brücke zur HomeMatic CCU; hmmqtt: eigener Dienst mit CUL-Funkstick, ohne CCU und Node-RED (Seite Setup -> HomeMatic)", "doc/homematic.md#homematic" },
   { "homeMaticTopic",            ctString,  "homematic",          "N", false, "HomeMatic", "MQTT Topic des Dienstes hmmqtt", "Basis-Topic (hmmqtt Option -T)", "doc/configuration.md#homematic" },

   // S 3200 (Fröling heating) via the service p4mqttd

   { "p4Enabled",                 ctBool,    "0",                  "N", false, "Heizung S 3200", "Anbindung über p4mqttd", "Aktiviert die Anbindung der Fröling S 3200 über den Dienst p4mqttd (p4mqttd/README.md). ACHTUNG: Der Dienst kann Parameter der Heizung schreiben, Nutzung auf eigenes Risiko, siehe Disclaimer in p4mqttd/README.md", "doc/p4.md#setup" },
   { "p4Mail",                    ctBool,    "0",                  "N", false, "Heizung S 3200", "Status- und Fehler-Mails", "Mail bei einer Störung (Fehler Mail Empfänger) und beim Wechsel des Betriebszustands (Status Mail Empfänger) der Heizung, braucht die Mail-Konfiguration der Gruppe 'Mail' und dort 'Mail Benachrichtigung'. Solange der alte p4d noch Mails sendet aus lassen!", "doc/p4.md#further-functions" },
   { "p4StateMailStates",         ctString,  "0,1,3,19",           "N", false, "Heizung S 3200", "Status Mail bei den Zuständen", "Nummern der Betriebszustände der Heizung, bei deren Erreichen eine Status Mail gesendet wird, Komma getrennt (0 Störung, 1 Brenner aus, 3 Heizen, 19 Betriebsbereit), leer = bei jedem Wechsel", "doc/configuration.md#heizung-s-3200" },
   { "p4ConsumptionPerHour",      ctNum,     "0",                  "N", false, "Heizung S 3200", "Pellet Verbrauch / Stoker Stunde [kg]", "Für die Schätzung des Verbrauchs seit der letzten Lieferung (Seite Pellets), 0 = der Wert aus den letzten beiden Lieferungen", "doc/configuration.md#heizung-s-3200" },
   { "p4Schema",                  ctChoice,  "p4-2hk-puffer.svg",  "N", false, "Heizung S 3200", "Bild des Schemas", "Das Anlagenbild des Tabs Schema (img/p4/schema/schema-<Name>.png oder .svg), die Elemente werden dort im Schema-Setup platziert", "doc/p4.md#the-page-s3200-main-menu" },
   { "p4SchemaBackground",        ctString,  "",                   "N", false, "Heizung S 3200", "Hintergrundfarbe des Schemas", "CSS Farbe hinter dem Anlagenbild (wirkt bei transparenten Bildern wie den SVGs), leer = die des Farbschemas, auch im Schema-Setup über 'Hintergrund' einstellbar", "doc/configuration.md#heizung-s-3200" },
   { "p4HeatingType",             ctChoice,  "P4",                 "N", false, "Heizung S 3200", "Typ der Heizung", "Das Bild des Heizungstyps (img/p4/type/heating-<Typ>.png) zeigt der Status der Heizung, wenn es für den Betriebszustand kein eigenes animiertes Bild gibt", "doc/configuration.md#heizung-s-3200" },
   { "p4PageTitle",               ctString,  "S3200",              "N", false, "Heizung S 3200", "Name der Seite im Hauptmenü", "Die Seite mit dem Service Menü der Heizung (nur sichtbar wenn die Anbindung aktiviert ist)", "doc/p4.md#the-page-s3200-main-menu" },
   { "p4Topic",                   ctString,  "p4",                 "N", false, "Heizung S 3200", "MQTT Topic des Dienstes p4mqttd", "Basis-Topic (p4mqttd Option -T)", "doc/configuration.md#heizung-s-3200" },
   { "p4PollInterval",            ctInteger, "60",                 "N", false, "Heizung S 3200", "Intervall der Messwerte [s]", "Die aktiven Sensoren der Heizung werden in diesem Intervall gelesen", "doc/configuration.md#heizung-s-3200" },
   { "p4StateInterval",           ctInteger, "10",                 "N", false, "Heizung S 3200", "Intervall der Status Prüfung [s]", "", "doc/configuration.md#heizung-s-3200" },
   { "p4TimeSync",                ctBool,    "0",                  "N", false, "Heizung S 3200", "Zeitsynchronisation", "Die Uhr der Heizung wird (frühestens um 3:00, höchstens einmal täglich) auf die Systemzeit gesetzt, wenn die Abweichung größer als die unten genannte ist. Nicht im Nur-Lesen-Modus von p4mqttd", "doc/p4.md#further-functions" },
   { "p4MaxTimeLeak",             ctInteger, "10",                 "N", false, "Heizung S 3200", "Zeitsynchronisation bei Abweichung über [s]", "Mindestabweichung für die Synchronisation in Sekunden", "doc/configuration.md#heizung-s-3200" },
   { "p4InitMenu",                ctButton,  R"({"event":"p4cmd","object":{"what":"initmenu","parent":0},"progress":900,"confirm":"Die Menüstruktur neu aus der Steuerung lesen? Das dauert einige Minuten."})",
                                                                   "N", false, "Heizung S 3200", "Menü neu lesen", "Liest die Menüstruktur des Service-Menüs und die Ein- / Ausgänge (Pumpen, Mischer, ...) neu aus der Steuerung, z. B. nach einem Firmware Update. Dauert einige Minuten, solange beantwortet p4mqttd keine Anfragen", "doc/configuration.md#heizung-s-3200" },
   { "p4InitValues",              ctButton,  R"({"event":"p4cmd","object":{"what":"initvalues","parent":0},"progress":300,"confirm":"Die Sensoren (Messwerte) neu aus der Steuerung lesen?"})",
                                                                   "N", false, "Heizung S 3200", "Sensoren neu lesen", "Liest die Liste der Messwerte (Temperaturen, Füllstand, ...) neu aus der Steuerung", "doc/configuration.md#heizung-s-3200" },
   { "p4InitTimes",               ctButton,  R"({"event":"p4cmd","object":{"what":"inittimes","parent":0},"progress":180})",
                                                                   "N", false, "Heizung S 3200", "Zeitprogramme neu lesen", "Übernimmt die Zeitprogramme aus der Steuerung (wenn sie dort geändert wurden)", "doc/configuration.md#heizung-s-3200" },

   { "p4FillEnabled",             ctBool,    "0",                  "N", false, "Heizung S 3200", "Dynamische Pelletbefüllung", "Die Zeiten der 1. und 2. Pelletsbefüllung werden automatisch gesetzt: täglich die Standardzeiten, bei Betriebsbereit und niedrigem Füllstand sofort befüllen, eine Befüllung während die Heizung heizt verschieben, nach N Tagen den Behälter leer fahren (Ablösung von DynamischePelletaustragung.py). Status und Änderungen auf der Seite Pellets", "doc/configuration.md#dynamic-pellet-filling" },
   { "p4FillTime1",               ctString,  "16:00",              "N", false, "Heizung S 3200", "Befüllung: Standardzeit 1. Befüllung", "HH:MM, wird täglich gesetzt, 00:00 = aus", "doc/configuration.md#dynamic-pellet-filling" },
   { "p4FillTime2",               ctString,  "00:00",              "N", false, "Heizung S 3200", "Befüllung: Standardzeit 2. Befüllung", "HH:MM, wird täglich gesetzt, 00:00 = aus", "doc/configuration.md#dynamic-pellet-filling" },
   { "p4FillMinLevel",            ctInteger, "20",                 "N", false, "Heizung S 3200", "Befüllung: sofort unter Füllstand [%]", "Ist die Heizung im Zustand Betriebsbereit und der Füllstand darunter, wird sofort befüllt (in 5 Minuten)", "doc/configuration.md#dynamic-pellet-filling" },
   { "p4FillHold",                ctInteger, "5",                  "N", false, "Heizung S 3200", "Befüllung: sofort nach Wartezeit [min]", "So lange muss Betriebsbereit und der niedrige Füllstand anstehen, bevor sofort befüllt wird", "doc/configuration.md#dynamic-pellet-filling" },
   { "p4FillDaysToZero",          ctInteger, "8",                  "N", false, "Heizung S 3200", "Befüllung: Leerfahrt nach [Tage]", "War der Behälter so lange nicht leer, werden beide Zeiten auf 00:00 gesetzt bis er leer ist (Feinanteil), 0 = nie", "doc/configuration.md#dynamic-pellet-filling" },
   { "p4FillDelayStates",         ctString,  "Vorbereitung,Vorwärmen,Zünden,Heizen", "N", false, "Heizung S 3200", "Befüllung: verschieben bei Zuständen", "Steht in diesen Zuständen eine Befüllung innerhalb von 15 Minuten an, wird sie um 30 Minuten verschoben (Komma getrennt, Text des Zustands)", "doc/configuration.md#dynamic-pellet-filling" },
   { "p4FillAddress1",            ctInteger, "60",                 "N", false, "Heizung S 3200", "Befüllung: Adresse 1. Befüllung", "Parameter 'Start der 1. Pelletsbefüllung' im Service Menü (dezimal)", "doc/configuration.md#dynamic-pellet-filling" },
   { "p4FillAddress2",            ctInteger, "516",                "N", false, "Heizung S 3200", "Befüllung: Adresse 2. Befüllung", "Parameter 'Start der 2. Pelletsbefüllung' im Service Menü (dezimal)", "doc/configuration.md#dynamic-pellet-filling" },
   { "p4FillLevelAddress",        ctInteger, "113",                "N", false, "Heizung S 3200", "Befüllung: Sensor Füllstand", "Adresse des P4VA Sensors 'Füllstand im Pelletsbehälter' (dezimal, muss aktiv sein)", "doc/configuration.md#dynamic-pellet-filling" },
   { "p4FillLastZero",            ctInteger, "0",                  "N", true,  "Heizung S 3200", "", "" },
   { "p4FillResetDay",            ctString,  "",                   "N", true,  "Heizung S 3200", "", "" },
   { "p4FillToZero",              ctBool,    "0",                  "N", true,  "Heizung S 3200", "", "" },
   { "p4FillLog",                 ctText,    "[]",                 "N", true,  "Heizung S 3200", "", "" },

   // VDR

   { "vdr",                       ctStringSet, "",                 "N", false, "VDR", "VDRs (Video Disk Recorder)", "Ein Eintrag je VDR: 'Name;Host:Port;MAC', Host:Port ist das osd2web Plugin, Beispiel: 'Wohnzimmer;vdr:4444;00:1e:06:43:48:8e'. Host als Name oder IP. Die MAC (für Wake-on-LAN) kann fehlen, sie wird beim Speichern ermittelt wenn der VDR läuft. Mit ';OFF' am Ende darf die FB den VDR ausschalten, sonst nur einschalten", "doc/configuration.md#vdr" },

   // LMC (Logitec Media Server)

   { "lmcHost",                   ctString,  "",                   "N", false, "Logitech Media Server", "LMC Host", "", "doc/configuration.md#logitech-media-server" },
   { "lmcPort",                   ctInteger, "9090",               "N", false, "Logitech Media Server", "LMC Port", "", "doc/configuration.md#logitech-media-server" },
   { "lmcPlayerMac",              ctString,  "",                   "N", false, "Logitech Media Server", "MAC of LMC Player ", "", "doc/configuration.md#logitech-media-server" },
};
