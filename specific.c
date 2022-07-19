//***************************************************************************
// Automation Control
// File specific.c
// This code is distributed under the terms and conditions of the
// GNU GENERAL PUBLIC LICENSE. See the file LICENSE for details.
// Date 16.04.2021 - Jörg Wendel
//***************************************************************************

#include <dirent.h>
#include <inttypes.h>

#ifndef _NO_RASPBERRY_PI_
#  include <wiringPi.h>
#else
#  include "gpio.h"
#endif

#include "lib/json.h"
#include "specific.h"

volatile int showerSwitch {0};

//***************************************************************************
// Configuration Items
//***************************************************************************

std::list<Daemon::ConfigItemDef> HomeCtl::configuration
{
   // daemon

   { "instanceName",              ctString,  "Home Control", false, "Daemon", "Titel", "Page Titel / Instanz" },
   { "latitude",                  ctNum,     "50.3",         false, "Daemon", "Breitengrad", "" },
   { "longitude",                 ctNum,     "8.79",         false, "Daemon", "Längengrad", "" },

   { "interval",                  ctInteger, "60",           false, "Daemon", "Intervall der Aufzeichung", "Datenbank Aufzeichung [s]" },
   { "webPort",                   ctInteger, "61109",        false, "Daemon", "Port des Web Interfaces", "" },
   { "eloquence",                 ctBitSelect, "1",          false, "Daemon", "Log Eloquence", "" },

#ifdef _POOL
   { "filterPumpTimes",           ctRange,   "10:00-17:00",  false, "Daemon", "Zeiten Filter Pumpe", "[hh:mm] - [hh:mm]" },
   { "uvcLightTimes",             ctRange,   "",             false, "Daemon", "Zeiten UV-C Licht", "[hh:mm] - [hh:mm], wird nur angeschaltet wenn auch die Filterpumpe läuft!" },
   { "poolLightTimes",            ctRange,   "",             false, "Daemon", "Zeiten Pool Licht", "[hh:mm] - [hh:mm] (ansonsten manuell schalten)" },
   { "poolLightColorToggle",      ctBool,    "0",            false, "Daemon", "Pool Licht Farb-Toggel", "" },

   { "alertSwitchOffPressure",    ctInteger, "0",            false, "Daemon", "Trockenlaufschutz unter x bar", "Deaktiviert Pumpen nach 5 Minuten (0 deaktiviert)" },

   { "tPoolMax",                  ctNum,     "28.0",         false, "Daemon", "Pool max Temperatur", "" },
   { "tSolarDelta",               ctNum,     "5.0",          false, "Daemon", "Einschaltdifferenz Solarpumpe", "" },
   { "showerDuration",            ctInteger, "20",           false, "Daemon", "Laufzeit der Dusche", "Laufzeit [s]" },
   { "minSolarPumpDuration",      ctInteger, "10",           false, "Daemon", "Mindestlaufzeit der Solarpumpe [m]", "" },
   { "deactivatePumpsAtLowWater", ctBool,    "0",            false, "Daemon", "Pumpen bei geringem Wasserstand deaktivieren", "" },
   { "w1AddrPool",                ctString,  "",             false, "Daemon", "Adresse Fühler Temperatur Pool", "" },
   { "w1AddrSolar",               ctString,  "",             false, "Daemon", "Adresse Fühler Temperatur Kollektor", "" },
   { "massPerSecond",             ctNum,     "11.0",         false, "Daemon", "Durchfluss Solar", "[Liter/min]" },
#endif

   { "aggregateHistory",          ctInteger, "365",          false, "Daemon", "Historie [Tage]", "history for aggregation in days (default 0 days -> aggegation turned OFF)" },
   { "aggregateInterval",         ctInteger, "15",           false, "Daemon", "Intervall [m]", "aggregation interval in minutes - 'one sample per interval will be build'" },
   { "peakResetAt",               ctString,  "",             true,  "Daemon", "", "" },

   { "openWeatherApiKey",         ctString,  "",             false, "Daemon", "Openweathermap API Key", "" },
   { "toggleWeatherView",         ctBool,    "1",            false, "Daemon", "Toggle Weather Widget", "" },

#ifdef _POOL
   // PH stuff

   { "phReference",               ctNum,     "7.2",          false, "PH", "PH Sollwert", "Sollwert [PH] (default 7,2)" },
   { "phMinusDensity",            ctNum,     "1.4",          false, "PH", "Dichte PH Minus [kg/l]", "Wie viel kg wiegt ein Liter PH Minus (default 1,4)" },
   { "phMinusDemand01",           ctInteger, "85",           false, "PH", "Menge zum Senken um 0,1 [g]", "Wie viel Gramm PH Minus wird zum Senken des PH Wertes um 0,1 für das vorhandene Pool Volumen benötigt (default 60g)" },
   { "phMinusDayLimit",           ctInteger, "100",          false, "PH", "Obergrenze PH Minus/Tag [ml]", "Wie viel PH Minus wird pro Tag maximal zugegeben [ml] (default 100ml)" },
   { "phPumpDurationPer100",      ctInteger, "1000",         false, "PH", "Laufzeit Dosierpumpe/100ml [ms]", "Welche Zeit in Millisekunden benötigt die Dosierpumpe um 100ml zu fördern (default 1000ms)" },
#endif

#ifdef _WOMO
   { "batteryCapacity",           ctInteger, "180",          false, "1 Daemon", "Kapazität der Batterie", "" },
#endif

   // web

   { "webSSL",                    ctBool,    "",             false, "WEB Interface", "Use SSL for WebInterface" },
   { "style",                     ctChoice,  "dark",         false, "WEB Interface", "Farbschema", "" },
   { "iconSet",                   ctChoice,  "light",        true,  "WEB Interface", "Status Icon Set", "" },
   { "background",                ctChoice,  "",             false, "WEB Interface", "Background image", "" },
   { "schema",                    ctChoice,  "schema.jpg",   false, "WEB Interface", "Schematische Darstellung", "" },
   { "vdr",                       ctBool,    "0",            false, "WEB Interface", "VDR (Video Disk Recorder) OSD verfügbar", "" },
   { "chartRange",                ctNum,     "1.5",          true,  "WEB Interface", "Chart Range", "" },
   { "chartSensors",              ctNum,     "VA:0x0",       true,  "WEB Interface", "Chart Sensors", "" },

   // MQTT interface

   { "mqttUrl",                   ctString,  "tcp://localhost:1883", false, "MQTT Interface", "MQTT Broker Url", "URL der MQTT Instanz Beispiel: 'tcp://127.0.0.1:1883'" },
   { "mqttUser",                  ctString,  "",                     false, "MQTT Interface", "User", "" },
   { "mqttPassword",              ctString,  "",                     false, "MQTT Interface", "Password", "" },
   { "mqttSensorTopics",          ctText,    TARGET "2mqtt/w1/#",    false, "MQTT Interface", "Zusätzliche sensor Topics", "Diese Topics werden gelesen und als Sensoren Daten verwendet" },
   { "arduinoInterval",           ctInteger, "10",                   false, "MQTT Interface", "Intervall der Arduino Messungen", "[s]" },
   { "arduinoTopic",              ctString,  TARGET "2mqtt/arduino", false, "MQTT Interface", "MQTT Topic des Arduino Interface", "" },

   // Home Automation MQTT interface

   { "mqttDataTopic",             ctString,  "",  false, "Home Automation Interface (like Home-Assistant, ...)", "Data Topic Name", "&lt;NAME&gt; wird gegen den Messwertnamen und &lt;GROUP&gt; gegen den Namen der Gruppe ersetzt. Beispiel: p4d2mqtt/sensor/&lt;NAME&gt;/state" },
   { "mqttSendWithKeyPrefix",     ctString,  "",  false, "Home Automation Interface (like Home-Assistant, ...)", "Adresse übertragen", "Wenn hier ein Präfix konfiguriert ist wird die Adresse der Sensoren nebst Präfix übertragen" },
   { "mqttHaveConfigTopic",       ctBool,    "0", false, "Home Automation Interface (like Home-Assistant, ...)", "Config Topic", "Speziell für HomeAssistant" },

   // mail

   { "mail",                      ctBool,    "0",                  false, "Mail", "Mail Benachrichtigung", "Mail Benachrichtigungen aktivieren/deaktivieren" },
   { "mailScript",                ctString,  BIN_PATH "/mail.sh",  false, "Mail", "poold sendet Mails über das Skript", "" },
   { "stateMailTo",               ctString,  "",                   false, "Mail", "Status Mail Empfänger", "Komma getrennte Empfängerliste" },
   { "errorMailTo",               ctString,  "",                   false, "Mail", "Fehler Mail Empfänger", "Komma getrennte Empfängerliste" },
   { "webUrl",                    ctString,  "",                   false, "Mail", "URL der Visualisierung", "kann mit %weburl% in die Mails eingefügt werden" },

   { "deconzHttpUrl",             ctString,  "",                   false, "DECONZ", "deCONZ HTTP URL", "" },
   { "deconzApiKey",              ctString,  "",                   false, "DECONZ", "deCONZ API key", "" },

   { "homeMaticInterface",        ctBool,    "false",              false, "HomeMatic CCU", "HomeMatic Interface", "" },
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
#ifdef _POOL
   free(w1AddrPool);
   free(w1AddrSolar);
#endif
}

//***************************************************************************
// Init/Exit
//***************************************************************************

int HomeCtl::init()
{
   int status = Daemon::init();

#ifdef _POOL
   sensors["SP"][spWaterLevel].value = 50;  // start with 50% until we get first signal
#endif

   return status;
}

//***************************************************************************
// Init/Exit Database
//***************************************************************************

int HomeCtl::initDb()
{
   int status = Daemon::initDb();

#ifdef _POOL
   // ------------------
   // select solar work per day
   //    select date(time), max(value)
   //      from samples
   //      where type = 'SP' and address = 6 group by date(time);

   selectSolarWorkPerDay = new cDbStatement(tableSamples);
   selectSolarWorkPerDay->build("select ");
   selectSolarWorkPerDay->bindTextFree("date(time)", tableSamples->getValue("time"), "", cDBS::bndOut);
   selectSolarWorkPerDay->bindTextFree("max(value)", tableSamples->getValue("value"), ", ", cDBS::bndOut);
   selectSolarWorkPerDay->build(" from %s where ", tableSamples->TableName());
   selectSolarWorkPerDay->build(" TYPE = '%s' and ADDRESS = %d", "SP", spSolarWork);
   selectSolarWorkPerDay->build(" group by date(time) order by time desc");
   status += selectSolarWorkPerDay->prepare();
#endif

#ifdef _WOMO
   // ------------------
   // select solar Ah per day
   //    select date(time), max(value)
   //      from samples
   //      where type = 'SP' and address = 6 group by date(time);

   selectSolarAhPerDay = new cDbStatement(tableSamples);
   selectSolarAhPerDay->build("select ");
   selectSolarAhPerDay->bindTextFree("date(time)", tableSamples->getValue("time"), "", cDBS::bndOut);
   selectSolarAhPerDay->bindTextFree("max(value)", tableSamples->getValue("value"), ", ", cDBS::bndOut);
   selectSolarAhPerDay->build(" from %s where ", tableSamples->TableName());
   selectSolarAhPerDay->build(" TYPE = '%s' and ADDRESS = %d", "CV", 0x01);
   selectSolarAhPerDay->build(" and time >= curdate() - INTERVAL DAYOFWEEK(curdate())+14 DAY");
   selectSolarAhPerDay->build(" group by date(time)");
   status += selectSolarAhPerDay->prepare();
#endif

   return status;

}

int HomeCtl::exitDb()
{
#ifdef _POOL
   delete selectSolarWorkPerDay;   selectSolarWorkPerDay = nullptr;
#endif
#ifdef _WOMO
   delete selectSolarAhPerDay;     selectSolarAhPerDay = nullptr;
#endif

   return Daemon::exitDb();
}

int HomeCtl::loadStates()
{
   Daemon::loadStates();

#ifdef _POOL
   // if filter pump is running assume its running at least 'minPumpTimeForPh'

   if (sensors["DO"][pinFilterPump].state)
      sensors["DO"][pinFilterPump].last = time(0)-minPumpTimeForPh;
#endif

   return done;
}

//***************************************************************************
// Read Configuration
//***************************************************************************

int HomeCtl::readConfiguration(bool initial)
{
   Daemon::readConfiguration(initial);

#ifdef _POOL
   getConfigItem("poolLightColorToggle", poolLightColorToggle, no);

   getConfigItem("w1AddrPool", w1AddrPool, "");
   getConfigItem("w1AddrSolar", w1AddrSolar, "");

   getConfigItem("tPoolMax", tPoolMax, tPoolMax);
   getConfigItem("tSolarDelta", tSolarDelta, tSolarDelta);
   getConfigItem("lastSolarWork", sensors["SP"][spSolarWork].value, 0);

   getConfigItem("showerDuration", showerDuration, 20);
   getConfigItem("minSolarPumpDuration", minSolarPumpDuration, 10);
   getConfigItem("deactivatePumpsAtLowWater", deactivatePumpsAtLowWater, no);
   getConfigItem("alertSwitchOffPressure", alertSwitchOffPressure, 0);

   // Solar stuff

   getConfigItem("massPerSecond", massPerSecond, 11.0);                  // [Liter/min]
   massPerSecond /= 60.0;                                                // => [l/s]

   // PH stuff

   getConfigItem("phReference", phReference, 7.2);
   getConfigItem("phMinusDensity", phMinusDensity, 1.4);                  // [kg/l]
   getConfigItem("phMinusDemand01", phMinusDemand01, 85);                 // [ml]
   getConfigItem("phMinusDayLimit", phMinusDayLimit, 100);                // [ml]
   getConfigItem("phPumpDuration100", phPumpDuration100, 1000);           // [ms]

   // Time ranges

   getConfigTimeRangeItem("filterPumpTimes", filterPumpTimes);
   getConfigTimeRangeItem("uvcLightTimes", uvcLightTimes);
   getConfigTimeRangeItem("poolLightTimes", poolLightTimes);

#endif

   return done;
}

int HomeCtl::atMeanwhile()
{
   if (showerSwitch > 0)
   {
      toggleIo(pinShower, "DO");
      showerSwitch = 0;
   }

   return done;
}

//***************************************************************************
// IO Interrupt Handler
//***************************************************************************

void ioInterrupt()
{
   static uint64_t lastShowerSwitch = cTimeMs::Now();

   // detect only once a second

   if (cTimeMs::Now() > lastShowerSwitch + 1000 && !digitalRead(HomeCtl::pinShowerSwitch))
   {
      tell(eloDebug, "Info: Shower key detected");
      showerSwitch = showerSwitch +1;
      lastShowerSwitch = cTimeMs::Now();
   }
}

//***************************************************************************
// Apply Configuration Specials
//***************************************************************************

int HomeCtl::applyConfigurationSpecials()
{
#ifndef _NO_RASPBERRY_PI_
   initOutput(pinW1Power, ooAuto, omAuto, "W1 Power", urFullControl);
   gpioWrite(pinW1Power, true, false);

   initOutput(pinUserOut1, ooUser, omManual, "User 1");
   initOutput(pinUserOut2, ooUser, omManual, "User 2");
   initOutput(pinUserOut3, ooUser, omManual, "User 3");
   initOutput(pinUserOut4, ooUser, omManual, "User 4");
#endif

#ifdef _POOL

#ifndef _NO_RASPBERRY_PI_
   initOutput(pinFilterPump, ooAuto|ooUser, omAuto, "Filter Pump", urFullControl);
   initOutput(pinSolarPump, ooAuto|ooUser, omAuto, "Solar Pump", urFullControl);
   initOutput(pinPoolLight, ooUser, omManual, "Pool Light");
   initOutput(pinUVC, ooAuto|ooUser, omAuto, "UV-C Light", urFullControl);
   initOutput(pinShower, ooAuto|ooUser, omAuto, "Shower");

   // init input IO

   initInput(pinLevel1, 0);
   initInput(pinLevel2, 0);
   initInput(pinLevel3, 0);
   initInput(pinShowerSwitch, 0);
   pullUpDnControl(pinShowerSwitch, PUD_UP);

   if (wiringPiISR(pinShowerSwitch, INT_EDGE_FALLING, &ioInterrupt) < 0)
      tell(eloAlways, "Error: Unable to setup ISR: %s", strerror(errno));

#endif // _NO_RASPBERRY_PI_

   // special values

   addValueFact(spWaterLevel, "SP", 1, "Water Level", "%");
   addValueFact(spSolarDelta, "SP", 1, "Solar Delta", "°C");
   addValueFact(spPhMinusDemand, "SP", 1, "PH Minus Bedarf", "ml");
   addValueFact(spSolarPower, "SP", 1, "Solar Leistung", "W");
   addValueFact(spSolarWork, "SP", 1, "Solar Energie (heute)", "kWh");

#ifndef _NO_RASPBERRY_PI_
   uint opt = ooUser;

   if (poolLightTimes.size() > 0)
      opt |= ooAuto;

   if (sensors["DO"][pinPoolLight].opt != opt)
   {
      sensors["DO"][pinPoolLight].opt = opt;
      sensors["DO"][pinPoolLight].mode = opt & ooAuto ? omAuto : omManual;
   }
#endif

#endif  // _POOL

   for (uint i = 0; i <= 7; i++)
   {
      std::string name = "Analog Input " + std::to_string(i);
      addValueFact(i, "AI", 1, name.c_str(), "%");
   }

   for (uint i = 0; i <= 10; i++)
   {
      std::string name = "Analog Input (Arduino) " + std::to_string(i);
      addValueFact(i + aiArduinoFirst, "AI", 1, name.c_str(), "mV");
   }

   return done;
}

//***************************************************************************
// Perform Jobs
//***************************************************************************

int HomeCtl::performJobs()
{
#ifdef _POOL
   // check timed shower duration

   if (sensors["DO"][pinShower].state && sensors["DO"][pinShower].mode == omAuto)
   {
      if (sensors["DO"][pinShower].last < time(0) - showerDuration)
      {
         tell(eloDebug, "Shower of after %ld seconds", time(0)-sensors["DO"][pinShower].last);
         sensors["DO"][pinShower].next = 0;
         gpioWrite(pinShower, false, true);
      }
      else
      {
         sensors["DO"][pinShower].next = sensors["DO"][pinShower].last + showerDuration;
      }
   }
#endif

   return done;
}

//***************************************************************************
// Process
//***************************************************************************

int HomeCtl::process()
{
   Daemon::process();

#ifdef _POOL

   static time_t lastDay {midnightOf(time(0))};
   static time_t pSolarSince {0};

   tell(eloAlways, "Process ...");

   if (lastDay != midnightOf(time(0)))
   {
      lastDay = midnightOf(time(0));
      setSpecialValue(spSolarWork, 0.0);
      setConfigItem("lastSolarWork", sensors["SP"][spSolarWork].value);
   }

   time_t tPoolLast, tSolarLast;
   double tPool = valueOfW1(toW1Id(w1AddrPool), tPoolLast);
   double tSolar = valueOfW1(toW1Id(w1AddrSolar), tSolarLast);

   // use W1 values only if not older than 2 cycles

   bool w1Valid = tPoolLast > time(0) - 2*interval && tSolarLast > time(0) - 2*interval;

   // ------------
   // Solar State

   if (w1Valid)
   {
      setSpecialValue(spSolarWork, sensors["SP"][spSolarWork].value + (sensors["SP"][spSolarPower].value * ((time(0)-pSolarSince) / 3600.0) / 1000.0));  // in kWh
      setConfigItem("lastSolarWork", sensors["SP"][spSolarWork].value);
      setSpecialValue(spSolarDelta, tSolar - tPool);

      const double termalCapacity = 4183.0; // Wärmekapazität Wasser bei 20°C [kJ·kg-1·K-1]

      if (sensors["DO"][pinSolarPump].state)
         setSpecialValue(spSolarPower, termalCapacity * massPerSecond * sensors["SP"][spSolarDelta].value);
      else
         setSpecialValue(spSolarPower, 0.0);

      pSolarSince = time(0);

      // publish

      publishSpecialValue(spSolarDelta);
      publishSpecialValue(spSolarPower);
      publishSpecialValue(spSolarWork);
   }
   else
      tell(eloAlways, "W1 values NOT valid");

   // -----------
   // PH

   if (!isNan(sensors["AI"][aiPh].value) && sensors["AI"][aiPh].last > time(0)-120) // not older than 2 minutes
   {
      setSpecialValue(spPhMinusDemand, calcPhMinusVolume(sensors["AI"][aiPh].value));
      publishSpecialValue(spPhMinusDemand);
   }

   // -----------
   // Pumps Alert

   if (deactivatePumpsAtLowWater && sensors["SP"][spWaterLevel].value == 0)   // || waterLevel == fail ??
   {
      if (sensors["DO"][pinSolarPump].state || sensors["DO"][pinFilterPump].state)
      {
         tell(eloAlways, "Warning: Deactivating pumps due to low water condition!");

         gpioWrite(pinFilterPump, false);
         gpioWrite(pinSolarPump, false);

         sensors["DO"][pinFilterPump].mode = omManual;
         sensors["DO"][pinSolarPump].mode = omManual;

         char* body;
         asprintf(&body, "Water level is 'low'\n Pumps switched off now!");
         if (sendMail(stateMailTo, "Pool pump alert", body, "text/plain") != success)
            tell(eloAlways, "Error: Sending alert mail failed");
         free(body);
      }
   }
   else
   {
      // -----------
      // Solar Pump

      if (w1Valid && sensors["DO"][pinSolarPump].mode == omAuto)
      {
         if (!isEmpty(w1AddrPool) && !isEmpty(w1AddrSolar) && existW1(toW1Id(w1AddrPool)) && existW1(toW1Id(w1AddrSolar)))
         {
            if (tPool > tPoolMax)
            {
               // switch OFF solar pump

               if (sensors["DO"][pinSolarPump].state)
               {
                  tell(eloAlways, "Configured pool maximum of %.2f°C reached, pool has is %.2f°C, stopping solar pump!", tPoolMax, tPool);
                  gpioWrite(pinSolarPump, false);
               }
            }
            else if (sensors["SP"][spSolarDelta].value > tSolarDelta)
            {
               // switch ON solar pump

               if (!sensors["DO"][pinSolarPump].state)
               {
                  tell(eloAlways, "Solar delta of %.2f°C reached, pool has %.2f°C, starting solar pump", tSolarDelta, tPool);
                  gpioWrite(pinSolarPump, true);
               }
            }
            else
            {
               // switch OFF solar pump

               if (sensors["DO"][pinSolarPump].state && sensors["DO"][pinSolarPump].last < time(0) - minSolarPumpDuration*tmeSecondsPerMinute)
               {
                  tell(eloAlways, "Solar delta (%.2f°C) lower than %.2f°C, pool has %.2f°C, stopping solar pump", sensors["SP"][spSolarDelta].value, tSolarDelta, tPool);
                  gpioWrite(pinSolarPump, false);
               }
            }
         }
         else
         {
            tell(eloAlways, "Warning: Missing at least one sensor, switching solar pump off!");
            gpioWrite(pinSolarPump, false);
         }
      }
      else if (!w1Valid && sensors["DO"][pinSolarPump].mode == omAuto)
      {
         gpioWrite(pinSolarPump, false);
         tell(eloAlways, "Warning: Solar pump switched OFF, sensor values older than %d seconds!", 2*interval);
      }

      // -----------
      // Filter Pump

      if (sensors["DO"][pinFilterPump].mode == omAuto)
      {
         bool activate = isInTimeRange(&filterPumpTimes, time(0));

         if (sensors["DO"][pinFilterPump].state != activate)
            gpioWrite(pinFilterPump, activate);
      }
   }

   // -----------
   // UV-C Light (only if Filter Pump is running)

   if (sensors["DO"][pinUVC].mode == omAuto)
   {
      bool activate = sensors["DO"][pinFilterPump].state && isInTimeRange(&uvcLightTimes, time(0));

      if (sensors["DO"][pinUVC].state != activate)
         gpioWrite(pinUVC, activate);
   }

   // -----------
   // Pool Light

   if (sensors["DO"][pinPoolLight].mode == omAuto)
   {
      bool activate = isInTimeRange(&poolLightTimes, time(0));

      if (sensors["DO"][pinPoolLight].state != activate)
         gpioWrite(pinPoolLight, activate);
   }

   // --------------------
   // check pump condition

   if (alertSwitchOffPressure != 0 && sensors["AI"][aiFilterPressure].value < alertSwitchOffPressure)
   {
      // pressure is less than configured value

      if (sensors["DO"][pinFilterPump].state &&
          sensors["DO"][pinFilterPump].last < time(0) - 5*tmeSecondsPerMinute)
      {
         // and pump is runnning longer than 5 minutes

         gpioWrite(pinFilterPump, false);
         gpioWrite(pinSolarPump, false);
         sensors["DO"][pinFilterPump].mode = omManual;
         sensors["DO"][pinSolarPump].mode = omManual;

         char* body;
         asprintf(&body, "Filter pressure is %.2f bar and pump is running!\n Pumps switched off now!", sensors["AI"][aiFilterPressure].value);
         tell(eloAlways, "%s", body);
         if (sendMail(stateMailTo, "Pool pump alert", body, "text/plain") != success)
            tell(eloAlways, "Error: Sending alert mail failed");
         free(body);
      }
   }

   phMeasurementActive();
   calcWaterLevel();

#endif // _POOL

   logReport();

   return success;
}

//***************************************************************************
// Report Actual State
//***************************************************************************

void HomeCtl::logReport()
{
   Daemon::logReport();

#ifdef _POOL
   static time_t nextLogAt {0};
   static time_t nextDetailLogAt {0};
   char buf[255+TB];

   time_t tPoolLast, tSolarLast;
   double tPool = valueOfW1(toW1Id(w1AddrPool), tPoolLast);
   double tSolar = valueOfW1(toW1Id(w1AddrSolar), tSolarLast);

   if (time(0) > nextLogAt)
   {
      nextLogAt = time(0) + 1 * tmeSecondsPerMinute;

      tell(eloAlways, "# ------------------------");

      tell(eloAlways, "# Pool has %.2f °C; Solar has %.2f °C; Current delta is %.2f° (%.2f° configured)",
           tPool, tSolar, sensors["SP"][spSolarDelta].value, tSolarDelta);
      tell(eloAlways, "# Solar power is %0.2f Watt; Solar work (today) %0.2f kWh", sensors["SP"][spSolarPower].value, sensors["SP"][spSolarWork].value);
      tell(eloAlways, "# Solar pump is '%s/%s' since '%s'", sensors["DO"][pinSolarPump].state ? "running" : "stopped",
           sensors["DO"][pinSolarPump].mode == omAuto ? "auto" : "manual", toElapsed(time(0)-sensors["DO"][pinSolarPump].last, buf));
      tell(eloAlways, "# Filter pump is '%s/%s' since '%s'", sensors["DO"][pinFilterPump].state ? "running" : "stopped",
           sensors["DO"][pinFilterPump].mode == omAuto ? "auto" : "manual", toElapsed(time(0)-sensors["DO"][pinFilterPump].last, buf));
      tell(eloAlways, "# UV-C light is '%s/%s'", sensors["DO"][pinUVC].state ? "on" : "off",
           sensors["DO"][pinUVC].mode == omAuto ? "auto" : "manual");
      tell(eloAlways, "# Pool light is '%s/%s'", sensors["DO"][pinPoolLight].state ? "on" : "off",
           sensors["DO"][pinPoolLight].mode == omAuto ? "auto" : "manual");
      tell(eloAlways, "# PH Minus Demand %.2f", sensors["SP"][spPhMinusDemand].value);

      tell(eloAlways, "# ------------------------");
   }

   if (time(0) > nextDetailLogAt)
   {
      nextDetailLogAt = time(0) + 5 * tmeSecondsPerMinute;

      tell(eloAlways, "# Solar Work");

      for (int i = 0, f = selectSolarWorkPerDay->find(); f && i++ < 5; f = selectSolarWorkPerDay->fetch())
      {
         if (tableSamples->getFloatValue("VALUE"))
            tell(eloAlways, "#   %s: %.2f kWh", l2pTime(tableSamples->getTimeValue("TIME"), "%d.%m.%Y").c_str(),
                 tableSamples->getFloatValue("VALUE"));
      }

      selectSolarWorkPerDay->freeResult();
      tell(eloAlways, "# ------------------------");
   }
#endif

#ifdef _WOMO

   // static time_t nextLogAt {0};
   // if (time(0) > nextLogAt)
   // {
   //    nextLogAt = time(0) + 5 * tmeSecondsPerMinute;

   //    tell(eloAlways, "# ------------------------");
   //    tell(eloAlways, "Solar Strom: %0.2f A", sensors["AI"][aiUser4].value);
   //    tell(eloAlways, "# ------------------------");
   // }

   static time_t nextDetailLogAt {0};

   if (time(0) > nextDetailLogAt)
   {
      nextDetailLogAt = time(0) + 10 * tmeSecondsPerMinute;

      tell(eloAlways, "# Solar Ladung / Tag der letzen 14 Tage");

      for (int f = selectSolarAhPerDay->find(); f; f = selectSolarAhPerDay->fetch())
      {
         tell(eloAlways, "#   %s: %.2f [Ah]", l2pTime(tableSamples->getTimeValue("TIME"), "%d.%m.%Y").c_str(),
              tableSamples->getFloatValue("VALUE"));
      }

      selectSolarAhPerDay->freeResult();
      tell(eloAlways, "# ------------------------");
   }

#endif
}

#ifdef _POOL

//***************************************************************************
// Get Water Level [%]
//***************************************************************************

int HomeCtl::calcWaterLevel()
{
   sensors["SP"][spWaterLevel].text = "";
   sensors["SP"][spWaterLevel].kind = "value";

   bool l1 = gpioRead(pinLevel1);
   bool l2 = gpioRead(pinLevel2);
   bool l3 = gpioRead(pinLevel3);

   if (l1 && l2 && l3)
      setSpecialValue(spWaterLevel, 100);
   else if (l1 && l2 && !l3)
      setSpecialValue(spWaterLevel, 66);
   else if (l1 && !l2 && !l3)
      setSpecialValue(spWaterLevel, 33);
   else if (!l1 && !l2 && !l3)
      setSpecialValue(spWaterLevel, 0);
   else
   {
      char text[255+TB];
      sprintf(text, "ERROR: Sensor Fehler <br/>(%d/%d/%d)", sensors["DI"][pinLevel1].state,
              sensors["DI"][pinLevel2].state, sensors["DI"][pinLevel3].state);
      setSpecialValue(spWaterLevel, 0, text);
   }

   tell(eloDetail, "Water level is %.0f (%d/%d/%d)", sensors["SP"][spWaterLevel].value, l1, l2, l3);

   return done;
}

//***************************************************************************
// PH Measurement Active
//***************************************************************************

void HomeCtl::phMeasurementActive()
{
   if (sensors["DO"][pinFilterPump].state && sensors["DO"][pinFilterPump].last < time(0)-minPumpTimeForPh)
   {
      sensors["AI"][aiPh].disabled = false;
      sensors["SP"][spPhMinusDemand].disabled = false;
   }
   else
   {
      sensors["AI"][aiPh].disabled = true;
      sensors["SP"][spPhMinusDemand].disabled = true;
   }
}

//***************************************************************************
// Calc PH Minus Volume
//***************************************************************************

int HomeCtl::calcPhMinusVolume(double ph)
{
   double phLack = ph - phReference;
   double mlPer01 = phMinusDemand01 * (1.0/phMinusDensity);

   // tell(eloAlways, "ph %0.2f; phLack %0.2f; mlPer01 %0.2f; phMinusDemand01 %d; phMinusDensity %0.2f -> %0.2f",
   //      ph, phLack, mlPer01, phMinusDemand01, phMinusDensity, (phLack/0.1) * mlPer01);

   return (phLack/0.1) * mlPer01;
}
#endif
