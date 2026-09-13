//***************************************************************************
// Automation Control
// File specific.c
// This code is distributed under the terms and conditions of the
// GNU GENERAL PUBLIC LICENSE. See the file LICENSE for details.
// Date 16.04.2021 - Jörg Wendel
//***************************************************************************

#include <dirent.h>
#include <inttypes.h>

#include "lib/json.h"
#include "specific.h"

volatile int showerSwitch {0};

//***************************************************************************
// Configuration Items
//***************************************************************************

std::list<Daemon::ConfigItemDef> HomeCtl::configuration
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

// #ifdef _POOL
//    { "tPoolMax",                  ctNum,     "28.0",         "N", false, "Pool", "Pool max Temperatur", "" },
//    { "tSolarOff",                 ctNum,     "2.0",          "N", false, "Pool", "Ausschalt-Delta Solarpumpe [°C]", "" },
//    { "tSolarOn",                  ctNum,     "7.0",          "N", false, "Pool", "Einschalt-Delta der Solarpumpe [°C]", "" },

//    // PH stuff

//    { "phReference",               ctNum,     "7.2",          "N", false, "Pool", "PH Sollwert", "Sollwert [PH] (default 7,2)" },
//    { "phMinusDensity",            ctNum,     "1.4",          "N", false, "Pool", "Dichte PH Minus [kg/l]", "Wie viel kg wiegt ein Liter PH Minus (default 1,4)" },
//    { "phMinusDemand01",           ctInteger, "85",           "N", false, "Pool", "Menge zum Senken um 0,1 [g]", "Wie viel Gramm PH Minus wird zum Senken des PH Wertes um 0,1 für das vorhandene Pool Volumen benötigt (default 60g)" },
//    { "phMinusDayLimit",           ctInteger, "100",          "N", false, "Pool", "Obergrenze PH Minus/Tag [ml]", "Wie viel PH Minus wird pro Tag maximal zugegeben [ml] (default 100ml)" },
//    { "phPumpDurationPer100",      ctInteger, "1000",         "N", false, "Pool", "Laufzeit Dosierpumpe/100ml [ms]", "Welche Zeit in Millisekunden benötigt die Dosierpumpe um 100ml zu fördern (default 1000ms)" },
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
   { "windyAppSpotID",            ctString,  "5247411",      "N", false, "WEB Interface", "Windy App Spot ID", "Anleitung zum ermitteln der Spot-ID, siehe README" },
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

//***************************************************************************
// Home Control Daemon
//***************************************************************************

HomeCtl::HomeCtl()
   : Daemon()
{
}

HomeCtl::~HomeCtl()
{
}

// //***************************************************************************
// // Init/Exit
// //***************************************************************************

// int HomeCtl::init()
// {
//    int status = Daemon::init();

//    return status;
// }

// //***************************************************************************
// // Init/Exit Database
// //***************************************************************************

// int HomeCtl::initDb()
// {
//    int status = Daemon::initDb();

// // #ifdef _POOL
// //    // ------------------
// //    // select solar work per day
// //    //    select date(time), max(value)
// //    //      from samples
// //    //      where type = 'SP' and address = 6 group by date(time);

// //    selectSolarWorkPerDay = new cDbStatement(tableSamples);
// //    selectSolarWorkPerDay->build("select ");
// //    selectSolarWorkPerDay->bindTextFree("date(time)", tableSamples->getValue("time"), "", cDBS::bndOut);
// //    selectSolarWorkPerDay->bindTextFree("max(value)", tableSamples->getValue("value"), ", ", cDBS::bndOut);
// //    selectSolarWorkPerDay->build(" from %s where ", tableSamples->TableName());
// //    selectSolarWorkPerDay->build(" TYPE = '%s' and ADDRESS = %d", "SP", spSolarWork);
// //    selectSolarWorkPerDay->build(" group by date(time) order by time desc");
// //    status += selectSolarWorkPerDay->prepare();
// // #endif

// // #ifdef _WOMO
// //    // ------------------
// //    // select solar Ah per day
// //    //    select date(time), max(value)
// //    //      from samples
// //    //      where type = 'SP' and address = 6 group by date(time);

// //    selectSolarAhPerDay = new cDbStatement(tableSamples);
// //    selectSolarAhPerDay->build("select ");
// //    selectSolarAhPerDay->bindTextFree("date(time)", tableSamples->getValue("time"), "", cDBS::bndOut);
// //    selectSolarAhPerDay->bindTextFree("max(value)", tableSamples->getValue("value"), ", ", cDBS::bndOut);
// //    selectSolarAhPerDay->build(" from %s where ", tableSamples->TableName());
// //    selectSolarAhPerDay->build(" TYPE = '%s' and ADDRESS = %d", "CV", 0x01);
// //    selectSolarAhPerDay->build(" and time >= curdate() - INTERVAL DAYOFWEEK(curdate())+14 DAY");
// //    selectSolarAhPerDay->build(" group by date(time)");
// //    status += selectSolarAhPerDay->prepare();
// // #endif

//    return status;
// }

// int HomeCtl::exitDb()
// {
// // #ifdef _POOL
// //    delete selectSolarWorkPerDay;   selectSolarWorkPerDay = nullptr;
// // #endif
// // #ifdef _WOMO
// //    delete selectSolarAhPerDay;     selectSolarAhPerDay = nullptr;
// // #endif

//    return Daemon::exitDb();
// }

// int HomeCtl::loadIoStates()
// {
//    Daemon::loadIoStates();

// // #ifdef _POOL
// //    // if filter pump is running assume its running at least 'minPumpTimeForPh'

// //    if (sensors["DO"][pinFilterPump].state)
// //       sensors["DO"][pinFilterPump].last = time(0)-minPumpTimeForPh;
// // #endif

//    return done;
// }

//***************************************************************************
// Read Configuration
//***************************************************************************

int HomeCtl::readConfiguration(bool initial)
{
   Daemon::readConfiguration(initial);

#ifdef _POOL
   // getConfigItem("lastSolarWork", sensors["SP"][spSolarWork].value, 0);
   // getConfigItem("showerDuration", showerDuration, 20);
   // getConfigItem("alertSwitchOffPressure", alertSwitchOffPressure, 0);

   // tell(eloAlways, "Pump 'alertSwitchOffPressure' is set to %.2f", alertSwitchOffPressure);

   // // Solar stuff

   // getConfigItem("massPerSecond", massPerSecond, 11.0);                  // [Liter/min]
   // massPerSecond /= 60.0;                                                // => [l/s]

   // PH stuff

   // getConfigItem("phReference", phReference, 7.2);
   // getConfigItem("phMinusDensity", phMinusDensity, 1.4);                  // [kg/l]
   // getConfigItem("phMinusDemand01", phMinusDemand01, 85);                 // [ml]
   // getConfigItem("phMinusDayLimit", phMinusDayLimit, 100);                // [ml]
   // getConfigItem("phPumpDuration100", phPumpDuration100, 1000);           // [ms]

   // Time ranges

   // getConfigTimeRangeItem("filterPumpTimes", filterPumpTimes);
   // getConfigTimeRangeItem("uvcLightTimes", uvcLightTimes);
   // getConfigTimeRangeItem("poolLightTimes", poolLightTimes);

#endif

   return done;
}

//***************************************************************************
// At Meanwhile
//***************************************************************************

// int HomeCtl::atMeanwhile()
// {
// #ifdef _POOL
//    if (showerSwitch > 0)
//    {
//       toggleIo(pinShower, "DO");
//       showerSwitch = 0;
//    }
// #endif

//    return done;
// }

//***************************************************************************
// On GPIO Change
//  !Attention!
//    calles in thred dont use database access functions like gpioWrite(), ...
//***************************************************************************

// void Daemon::onGpioChange(int physPin, bool value)
// {
//    tell(eloDebugGpio, "Debug: GPIO: Interrupt trigger for pin %d (%s)", physPin, value ? "ON" : "OFF");
//    triggerGpioPins.push(physPin);

// // #ifdef _POOL
// //    static uint64_t lastShowerSwitch {cTimeMs::Now()};     // detect only once a second to prevent bouncing

// //    if (cTimeMs::Now() > lastShowerSwitch + 1000 && !digitalRead(HomeCtl::pinShowerSwitch))
// //    {
// //       tell(eloDebug, "Info: Shower key detected");
// //       showerSwitch = showerSwitch +1;
// //       lastShowerSwitch = cTimeMs::Now();
// //    }
// // #endif
// }

//***************************************************************************
// Apply Configuration Specials
//***************************************************************************

int HomeCtl::applyConfigurationSpecials()
{
   Daemon::applyConfigurationSpecials();

   // initOutput(pinUserOut1, ooUser, omManual, "Digital Output");
   // initOutput(pinUserOut3, ooUser, omManual, "Digital Output");
   // initOutput(pinUserOut4, ooUser, omManual, "Digital Output");
   // initOutput(pinUserOut6, ooUser, omManual, "Digital Output");
   // initInput(pinUserInput3, "Digital Input");

#ifndef _POOL
//    initOutput(pinUserOut7, ooUser, omManual, "Digital Output");
//    initOutput(pinUserOut8, ooAuto, omAuto, "Digital Output");
//    initOutput(pinUserOut9, ooUser, omManual, "Digital Output");

//    initInput(pinUserInput6, "Digital Input");
#else
//    initOutput(pinFilterPump, ooAuto|ooUser, omAuto, "Filter Pump", urFullControl);
//    // initOutput(pinSolarPump, ooAuto|ooUser, omAuto, "Solar Pump", urFullControl);
//    // initOutput(pinPoolLight, ooUser, omManual, "Pool Light");
//    initOutput(pinUVC, ooAuto|ooUser, omAuto, "UV-C Light", urFullControl);
//    // initOutput(pinShower, ooAuto|ooUser, omAuto, "Shower");

//    // init input IO

//    initInput(pinShowerSwitch, "Shower");
//    pullUpDnControl(pinShowerSwitch, PUD_UP);

//   if (gpio->setIsr(pinShowerSwitch, Gpio::edgeBoth, std::bind(&Daemon::onGpioChange, this, std::placeholders::_1, std::placeholders::_2)) != success)
//      tell(eloAlways, "Error: Unable to setup ISR: %s", strerror(errno));

   // special values

   // addValueFact(spPhMinusDemand, "SP", 1, "PH Minus Bedarf", "ml");
   // addValueFact(spSolarPower, "SP", 1, "Solar Leistung", "W");
   // addValueFact(spSolarWork, "SP", 1, "Solar Energie (heute)", "kWh");

   // uint outputModes {ooUser};

   // if (poolLightTimes.size() > 0)
   //    outputModes |= ooAuto;

   // if (sensors["DO"][pinPoolLight].outputModes != outputModes)
   // {
   //    sensors["DO"][pinPoolLight].outputModes = outputModes;
   //    sensors["DO"][pinPoolLight].mode = (outputModes & ooAuto) ? omAuto : omManual;
   // }

#endif  // _POOL

   return done;
}

//***************************************************************************
// Perform Jobs
//***************************************************************************

// int HomeCtl::performJobs()
// {
// #ifdef _POOL
//    // check timed shower duration

//    if (sensors["DO"][pinShower].state && sensors["DO"][pinShower].mode == omAuto)
//    {
//       if (sensors["DO"][pinShower].last() < time(0) - showerDuration)
//       {
//          tell(eloDebug, "Shower of after %ld seconds", time(0)-sensors["DO"][pinShower].last());
//          // sensors["DO"][pinShower].next = 0;
//          gpioWrite(pinShower, false, true);
//       }
//       else
//       {
//          // sensors["DO"][pinShower].next = sensors["DO"][pinShower].last() + showerDuration;
//       }
//    }
// #endif
//    return done;
// }

//***************************************************************************
// Process
//***************************************************************************

int HomeCtl::process(bool force, bool signal)
{
   Daemon::process(force, signal);

#ifdef _POOL

   // static time_t lastDay {midnightOf(time(0))};

   // // tell(eloAlways, "Process ...");

   // if (lastDay != midnightOf(time(0)))
   // {
   //    lastDay = midnightOf(time(0));
   //    setSpecialValue(spSolarWork, 0.0);
   //    setConfigItem("lastSolarWork", sensors["SP"][spSolarWork].value);
   // }

   // -----------
   // PH

   // if (!isNan(sensors["AI"][aiPh].value) && sensors["AI"][aiPh].last() > time(0)-120) // not older than 2 minutes
   // {
   //    setSpecialValue(spPhMinusDemand, calcPhMinusVolume(sensors["AI"][aiPh].value));
   //    publishSpecialValue(spPhMinusDemand);
   // }

   // // -----------
   // // Filter Pump

   // if (sensors["DO"][pinFilterPump].mode == omAuto)
   // {
   //    bool activate = isInTimeRange(&filterPumpTimes, time(0));

   //    if (sensors["DO"][pinFilterPump].state != activate)
   //       gpioWrite(pinFilterPump, activate);
   // }

   // // -----------
   // // UV-C Light (only if Filter Pump is running)

   // if (sensors["DO"][pinUVC].mode == omAuto)
   // {
   //    bool activate = sensors["DO"][pinFilterPump].state && isInTimeRange(&uvcLightTimes, time(0));

   //    if (sensors["DO"][pinUVC].state != activate)
   //       gpioWrite(pinUVC, activate);
   // }

   // -----------
   // Pool Light

   // if (sensors["DO"][pinPoolLight].mode == omAuto)
   // {
   //    bool activate = isInTimeRange(&poolLightTimes, time(0));

   //    if (sensors["DO"][pinPoolLight].state != activate)
   //       gpioWrite(pinPoolLight, activate);
   // }

   // --------------------
   // check pump condition

   // if (alertSwitchOffPressure != 0)
   // {
   //    static time_t pressureAlarmDetectedAt {0};

   //    // tell(eloAlways, "aiFilterPressure %02.2f; alertSwitchOffPressure %02.2f", sensors["AI"][aiFilterPressure].value, alertSwitchOffPressure);

   //    if (sensors["AI"][aiFilterPressure].value < alertSwitchOffPressure)
   //    {
   //       // pressure is less than configured value

   //       if (sensors["DO"][pinFilterPump].state && pressureAlarmDetectedAt && pressureAlarmDetectedAt < time(0) - 5*tmeSecondsPerMinute)
   //       {
   //          // and pump is runnning longer than 5 minutes

   //          tell(eloAlways, "Filter pressure is %.2f bar, alarm detected at '%s', switching off now",
   //               sensors["AI"][aiFilterPressure].value, l2pTime(pressureAlarmDetectedAt).c_str());

   //          gpioWrite(pinFilterPump, false);
   //          gpioWrite(pinSolarPump, false);
   //          gpioWrite(pinUVC, false);
   //          sensors["DO"][pinFilterPump].mode = omManual;
   //          sensors["DO"][pinSolarPump].mode = omManual;
   //          sensors["DO"][pinUVC].mode = omManual;

   //          char* body {};
   //          asprintf(&body, "Filter pressure is %.2f bar and pump is running!\n Pumps switched off now!", sensors["AI"][aiFilterPressure].value);

   //          if (sendMail(stateMailTo.c_str(), "Pool pump alert", body, "text/plain") != success)
   //             tell(eloAlways, "Error: Sending alert mail failed");
   //          free(body);
   //       }
   //       else if (sensors["DO"][pinFilterPump].state && !pressureAlarmDetectedAt)
   //       {
   //          pressureAlarmDetectedAt = time(0);

   //          tell(eloAlways, "Filter pressure is %.2f bar, setting alarm detection time to '%s'",
   //               sensors["AI"][aiFilterPressure].value, l2pTime(pressureAlarmDetectedAt).c_str());
   //       }
   //    }
   //    else if (pressureAlarmDetectedAt)
   //    {
   //       // reset pressure alarm

   //       tell(eloAlways, "Pressure back to normal (%.2f), resetting alarm time", sensors["AI"][aiFilterPressure].value);
   //       pressureAlarmDetectedAt = 0;
   //    }
   // }

   // phMeasurementActive();

#endif // _POOL

   // logReport();

   return success;
}

//***************************************************************************
// Report Actual State
//***************************************************************************

// void HomeCtl::logReport()
// {
//    Daemon::logReport();

// #ifdef _POOL
//    if (time(0) > nextDetailLogAt)
//    {
//       nextDetailLogAt = time(0) + 5 * tmeSecondsPerMinute;
//       tell(eloAlways, "# Solar Work");

//       for (int i = 0, f = selectSolarWorkPerDay->find(); f && i++ < 5; f = selectSolarWorkPerDay->fetch())
//       {
//          if (tableSamples->getFloatValue("VALUE"))
//             tell(eloAlways, "#   %s: %.2f kWh", l2pTime(tableSamples->getTimeValue("TIME"), "%d.%m.%Y").c_str(),
//                  tableSamples->getFloatValue("VALUE"));
//       }

//       selectSolarWorkPerDay->freeResult();
//       tell(eloAlways, "# ------------------------");
//    }
// #endif

// #ifdef _WOMO

//    // static time_t nextLogAt {0};
//    // if (time(0) > nextLogAt)
//    // {
//    //    nextLogAt = time(0) + 5 * tmeSecondsPerMinute;

//    //    tell(eloAlways, "# ------------------------");
//    //    tell(eloAlways, "Solar Strom: %0.2f A", sensors["AI"][aiUser4].value);
//    //    tell(eloAlways, "# ------------------------");
//    // }

//    static time_t nextDetailLogAt {0};

//    if (time(0) > nextDetailLogAt)
//    {
//       nextDetailLogAt = time(0) + 10 * tmeSecondsPerMinute;

//       tell(eloAlways, "# Solar Ladung/Tag");

//       for (int f = selectSolarAhPerDay->find(); f; f = selectSolarAhPerDay->fetch())
//       {
//          tell(eloAlways, "#   %s: %3.2f [Ah]",
//               l2pTime(tableSamples->getTimeValue("TIME"), "%d.%m.%Y").c_str(),
//               tableSamples->getFloatValue("VALUE"));
//       }

//       selectSolarAhPerDay->freeResult();
//       tell(eloAlways, "# ------------------------");
//    }

// #endif
// }

// #ifdef _POOL

// //***************************************************************************
// // PH Measurement Active
// //***************************************************************************

// void HomeCtl::phMeasurementActive()
// {
//    if (sensors["DO"][pinFilterPump].state && sensors["DO"][pinFilterPump].last() < time(0)-minPumpTimeForPh)
//    {
//       sensors["AI"][aiPh].disabled = false;
//       sensors["SP"][spPhMinusDemand].disabled = false;
//    }
//    else
//    {
//       sensors["AI"][aiPh].disabled = true;
//       // sensors["SP"][spPhMinusDemand].disabled = true;
//    }
// }

// //***************************************************************************
// // Calc PH Minus Volume
// //***************************************************************************

// int HomeCtl::calcPhMinusVolume(double ph)
// {
//    double phLack = ph - phReference;
//    double mlPer01 = phMinusDemand01 * (1.0/phMinusDensity);

//    tell(eloAlways, "ph %0.2f; phLack %0.2f; mlPer01 %0.2f; phMinusDemand01 %d; phMinusDensity %0.2f -> %0.2f",
//         ph, phLack, mlPer01, phMinusDemand01, phMinusDensity, (phLack/0.1) * mlPer01);

//    return (phLack/0.1) * mlPer01;
// }
// #endif
