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

   { "instanceName",              ctString,  "Home Control", "N", false, "Daemon", "Titel", "Page Titel / Instanz" },
   { "instanceIcon",              ctChoice,  "home.png",     "N", false, "Daemon", "Application Icon", "" },
   { "latitude",                  ctNum,     "50.3",         "N", false, "Daemon", "Breitengrad", "" },
   { "longitude",                 ctNum,     "8.79",         "N", false, "Daemon", "Längengrad", "" },

   { "interval",                  ctInteger, "60",           "N", false, "Daemon", "Intervall der Aufzeichung", "Datenbank Aufzeichung [s]" },

   { "aggregateHistory",          ctInteger, "365",          "N", false, "Daemon", "Historie [Tage]", "history for aggregation [days] (default 0 days -> aggegation turned OFF)" },
   { "aggregateInterval",         ctInteger, "15",           "N", false, "Daemon", "Aggregate Interval der historisierten Daten [m]", "aggregation interval in minutes - 'one sample per interval will be build'" },
   { "peakResetAt",               ctString,  "",             "N", true,  "Daemon", "", "" },

   { "openWeatherApiKey",         ctString,  "",             "N", false, "Daemon", "Openweathermap API Key", "" },
   { "weatherInterval",           ctInteger, "15",           "N", false, "Daemon", "Weather Refresh Interval [m]", "" },
   { "toggleWeatherView",         ctBool,    "1",            "N", false, "Daemon", "Toggle Weather Widget", "" },

   { "eloquence",                 ctBitSelect, "1",          "N", false, "Daemon", "Log Eloquence", "" },

   // GPS tour recording

   { "gpsTourMinDistance",        ctInteger, "25",           "N", false, "GPS", "Tour: Mindestbewegung [m]", "Ein Punkt wird aufgezeichnet wenn die Position sich mindestens um diese Distanz geändert hat" },
   { "gpsTourPauseAfter",         ctInteger, "5",            "N", false, "GPS", "Tour: Pause nach Stillstand [min]", "Ohne Bewegung für diese Zeit gilt die Tour als pausiert, bei erneuter Bewegung wird automatisch fortgesetzt" },
   { "gpsTourEndTolerance",       ctInteger, "200",          "N", false, "GPS", "Tour: Toleranz Ankunft [m]", "Beim Beenden einer Tour wird als Ende der Zeitpunkt vorgeschlagen, ab dem die Position innerhalb dieser Distanz um den Endpunkt blieb (z.B. Anmeldung am Campingplatz)" },

   // Garmin activities

   { "garminTrackDeviation",      ctInteger, "50",           "N", false, "Garmin", "Distanz aus dem Track ab Abweichung [%]", "Weicht die von Garmin gemeldete Distanz um mehr als diesen Prozentsatz von der aus dem geladenen GPS-Track berechneten ab, wird in Liste und Details die Track-Distanz verwendet (die Uhr summiert bei manchen Wassersport-Aktivitäten kaum Strecke auf). 0 = immer Garmins Wert" },

// #ifdef _POOL
//    { "phReference",               ctNum,     "7.2",       "N", false, "Pool", "PH Sollwert", "Sollwert [PH] (default 7,2)" },
//    { "phMinusDensity",            ctNum,     "1.4",       "N", false, "Pool", "Dichte PH Minus [kg/l]", "Wie viel kg wiegt ein Liter PH Minus (default 1,4)" },
//    { "phMinusDemand01",           ctInteger, "85",        "N", false, "Pool", "Menge zum Senken um 0,1 [g]", "Wie viel Gramm PH Minus wird zum Senken des PH Wertes um 0,1 für das vorhandene Pool Volumen benötigt (default 60g)" },
//    { "phMinusDayLimit",           ctInteger, "100",       "N", false, "Pool", "Obergrenze PH Minus/Tag [ml]", "Wie viel PH Minus wird pro Tag maximal zugegeben [ml] (default 100ml)" },
//    { "phPumpDurationPer100",      ctInteger, "1000",      "N", false, "Pool", "Laufzeit Dosierpumpe/100ml [ms]", "Welche Zeit in Millisekunden benötigt die Dosierpumpe um 100ml zu fördern (default 1000ms)" },
// #endif

   // web

   { "webPort",                   ctInteger, "61109",        "N", false, "WEB Interface", "Port des Web Interfaces", "" },
   { "webSSL",                    ctBool,    "",             "N", false, "WEB Interface", "Use SSL for WebInterface", "" },
   { "style",                     ctChoice,  "dark",         "N", false, "WEB Interface", "Farbschema", "" },
   { "iconSet",                   ctChoice,  "light",        "N", true,  "WEB Interface", "Status Icon Set", "" },
   { "background",                ctChoice,  "",             "N", false, "WEB Interface", "Background image", "" },
   { "schema",                    ctChoice,  "schema.jpg",   "N", false, "WEB Interface", "Schematische Darstellung", "" },
   { "chartRange",                ctNum,     "1.5",          "N", true,  "WEB Interface", "Chart Range", "" },
   { "chartSensors",              ctNum,     "VA:0x0",       "N", true,  "WEB Interface", "Chart Sensors", "" },
   { "showList",                  ctBool,    "0",            "N", false, "WEB Interface", "Liste anzeigen", "" },
   { "windyAppSpotID",            ctComboChoice,   "5247411",      "N", false, "WEB Interface", "Windy App Spot ID", "Spot-ID eingeben oder einen der bisher gespeicherten Spots aus den Vorschlägen wählen (die Namen holt die Oberfläche von Windy), Anleitung zum Ermitteln der Spot-ID siehe README" },
   { "windyAppID",                ctString,  "",             "N", false, "WEB Interface", "Windy App ID", "App-ID einrichten, siehe README" },

   // MQTT interface

   { "mqttUrl",                   ctString,  "tcp://localhost:1883",   "N", false, "MQTT", "MQTT Broker Url", "URL der MQTT Instanz Beispiel: 'tcp://127.0.0.1:1883'" },
   { "mqttUser",                  ctString,  "",                       "N", false, "MQTT", "User", "" },
   { "mqttPassword",              ctString,  "",                       "N", false, "MQTT", "Password", "" },
   { "mqttSensorTopics",          ctText,    INSTANCE "2mqtt/w1/#",    "N", false, "MQTT", "Zusätzliche sensor Topics", "Diese Topics werden gelesen und als Sensor Daten verwendet (Komma getrennte Liste)" },
   { "arduinoTopic",              ctString,  INSTANCE "2mqtt/arduino", "N", false, "MQTT", "MQTT Topic des Arduino Interface", "" },
   { "arduinoInterval",           ctInteger, "10",                     "N", false, "MQTT", "Intervall der Arduino Messungen", "[s]" },

   // Home Automation MQTT interface

   { "mqttHaDataTopic",           ctString,  "",  "N", false, "Home Automation Interface", "MQTT Data Topic Name", "&lt;NAME&gt; wird gegen den Messwertnamen und &lt;GROUP&gt; gegen den Namen der Gruppe ersetzt. Beispiel: p4d2mqtt/sensor/&lt;NAME&gt;/state" },
   { "mqttHaSendWithKeyPrefix",   ctString,  "",  "N", false, "Home Automation Interface", "Adresse übertragen", "Wenn hier ein Präfix konfiguriert ist wird die Adresse der Sensoren nebst Präfix übertragen" },
   { "mqttHaHaveConfigTopic",     ctBool,    "0", "N", false, "Home Automation Interface", "Config Topic", "Speziell für HomeAssistant" },

   // openHASP panel

   { "haspMqttTopic",             ctString,  "hasp/plates", "N", false, "HASP Panel", "MQTT Basis-Topic des openHASP Panels", "Kommandos gehen an &lt;Topic&gt;/command/..., z.B. 'hasp/plates' (Gruppe) oder 'hasp/&lt;hostname&gt;'. Leer = Panel-Anbindung aus" },

   // mail

   { "mail",                      ctBool,    "0",                  "N", false, "Mail", "Mail Benachrichtigung", "Mail Benachrichtigungen aktivieren/deaktivieren" },
   { "mailScript",                ctString,  BIN_PATH "/mail.sh",  "N", false, "Mail", "Skript zum senden von Mails", "" },
   { "stateMailTo",               ctString,  "",                   "N", false, "Mail", "Status Mail Empfänger", "Komma getrennte Empfängerliste" },
   { "errorMailTo",               ctString,  "",                   "N", false, "Mail", "Fehler Mail Empfänger", "Komma getrennte Empfängerliste" },
   { "webUrl",                    ctString,  "",                   "N", false, "Mail", "URL der Visualisierung", "kann mit %weburl% in die Mails eingefügt werden" },

   // deconz

   { "deconzHttpUrl",             ctString,  "",                   "N", false, "DECONZ", "deCONZ HTTP URL", "" },
   { "deconzApiKey",              ctString,  "",                   "N", false, "DECONZ", "deCONZ API key", "" },

   // homematic

   { "homeMaticInterface",        ctBool,    "false",              "N", false, "HomeMatic CCU", "HomeMatic Interface", "NodeRed wird als Brücke zur HomeMatic CCU benötigt" },

   // VDR

   { "vdr",                       ctString,  "",                   "N", false, "VDR", "URL des VDR WEB Interfaces (Video Disk Recorder)", "Beispiel: vdr:4444" },

   // LMC (Logitec Media Server)

   { "lmcHost",                   ctString,  "",                   "N", false, "Logitech Media Server", "LMC Host", "" },
   { "lmcPort",                   ctInteger, "9090",               "N", false, "Logitech Media Server", "LMC Port", "" },
   { "lmcPlayerMac",              ctString,  "",                   "N", false, "Logitech Media Server", "MAC of LMC Player ", "" },
};
