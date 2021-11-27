//***************************************************************************
// Automation Control
// File specific.c
// This code is distributed under the terms and conditions of the
// GNU GENERAL PUBLIC LICENSE. See the file LICENSE for details.
// Date 16.04.2021 - Jörg Wendel
//***************************************************************************

#ifndef _NO_RASPBERRY_PI_
#  include <wiringPi.h>
#endif

#include "specific.h"

volatile int showerSwitch {0};

//***************************************************************************
// Configuration Items
//***************************************************************************

std::list<Daemon::ConfigItemDef> Pool::configuration
{
   // daemon

   { "interval",                  ctInteger, "60",           false, "1 Daemon", "Intervall der Aufzeichung", "Datenbank Aufzeichung [s]" },
   { "arduinoInterval",           ctInteger, "10",           false, "1 Daemon", "Intervall der Arduino Messungen", "[s]" },
   { "webPort",                   ctInteger, "61109",        false, "1 Daemon", "Port des Web Interfaces", "" },
   { "loglevel",                  ctInteger, "1",            false, "1 Daemon", "Log level", "" },
   { "filterPumpTimes",           ctRange,   "10:00-17:00",  false, "1 Daemon", "Zeiten Filter Pumpe", "[hh:mm] - [hh:mm]" },
   { "uvcLightTimes",             ctRange,   "",             false, "1 Daemon", "Zeiten UV-C Licht", "[hh:mm] - [hh:mm], wird nur angeschaltet wenn auch die Filterpumpe läuft!" },
   { "poolLightTimes",            ctRange,   "",             false, "1 Daemon", "Zeiten Pool Licht", "[hh:mm] - [hh:mm] (ansonsten manuell schalten)" },
   { "poolLightColorToggle",      ctBool,    "0",            false, "1 Daemon", "Pool Licht Farb-Toggel", "" },

   { "alertSwitchOffPressure",    ctInteger, "0",            false, "1 Daemon", "Trockenlaufschutz unter x bar", "Deaktiviert Pumpen nach 5 Minuten (0 deaktiviert)" },

   { "tPoolMax",                  ctNum,     "28.0",         false, "1 Daemon", "Pool max Temperatur", "" },
   { "tSolarDelta",               ctNum,     "5.0",          false, "1 Daemon", "Einschaltdifferenz Solarpumpe", "" },
   { "showerDuration",            ctInteger, "20",           false, "1 Daemon", "Laufzeit der Dusche", "Laufzeit [s]" },
   { "minSolarPumpDuration",      ctInteger, "10",           false, "1 Daemon", "Mindestlaufzeit der Solarpumpe [m]", "" },
   { "deactivatePumpsAtLowWater", ctBool,    "0",            false, "1 Daemon", "Pumpen bei geringem Wasserstand deaktivieren", "" },

   { "invertDO",                  ctBool,    "1",            false, "1 Daemon", "Digitalaugänge invertieren", "" },
   { "w1AddrAir",                 ctString,  "",             false, "1 Daemon", "Adresse Fühler Temperatur Luft", "" },
   { "w1AddrPool",                ctString,  "",             false, "1 Daemon", "Adresse Fühler Temperatur Pool", "" },
   { "w1AddrSolar",               ctString,  "",             false, "1 Daemon", "Adresse Fühler Temperatur Kollektor", "" },
   { "w1AddrSuctionTube",         ctString,  "",             false, "1 Daemon", "Adresse Fühler Temperatur Saugleitung", "" },
   { "mqttUrl",                   ctString,  "tcp://localhost:1883",  false, "1 Daemon", "Url des MQTT Message Broker für den Daemon", "Wird zur Kommunikation mit dem one-wire Interface Service (w1mqtt) sowie dem Arduino verwendet. Beispiel: 'tcp://localhost:1883'" },

   { "aggregateHistory",          ctInteger, "0",            false, "1 Daemon", "Historie [Tage]", "history for aggregation in days (default 0 days -> aggegation turned OFF)" },
   { "aggregateInterval",         ctInteger, "15",           false, "1 Daemon", "Intervall [m]", "aggregation interval in minutes - 'one sample per interval will be build'" },
   { "peakResetAt",               ctString,  "",             true,  "1 Daemon", "", "" },

   { "massPerSecond",             ctNum,     "11.0",         false, "1 Daemon", "Durchfluss Solar", "[Liter/min]" },

   // PH stuff

   { "phReference",               ctNum,     "7.2",          false, "2 PH", "PH Sollwert", "Sollwert [PH] (default 7,2)" },
   { "phMinusDensity",            ctNum,     "1.4",          false, "2 PH", "Dichte PH Minus [kg/l]", "Wie viel kg wiegt ein Liter PH Minus (default 1,4)" },
   { "phMinusDemand01",           ctInteger, "85",           false, "2 PH", "Menge zum Senken um 0,1 [g]", "Wie viel Gramm PH Minus wird zum Senken des PH Wertes um 0,1 für das vorhandene Pool Volumen benötigt (default 60g)" },
   { "phMinusDayLimit",           ctInteger, "100",          false, "2 PH", "Obergrenze PH Minus/Tag [ml]", "Wie viel PH Minus wird pro Tag maximal zugegeben [ml] (default 100ml)" },
   { "phPumpDurationPer100",      ctInteger, "1000",         false, "2 PH", "Laufzeit Dosierpumpe/100ml [ms]", "Welche Zeit in Millisekunden benötigt die Dosierpumpe um 100ml zu fördern (default 1000ms)" },

   // web

   { "style",                     ctChoice,  "dark",         false, "3 WEB Interface", "Farbschema", "" },
   { "vdr",                       ctBool,    "0",            false, "3 WEB Interface", "VDR (Video Disk Recorder) OSD verfügbar", "" },

   // mqtt interface

   { "hassMqttUrl",                   ctString,  "",             false, "4 MQTT Interface", "Url des MQTT Message Broker", "Deiser kann z.B. zur Kommunikation mit Hausautomatisierungen verwendet werden. Beispiel: 'tcp://localhost:1883'" },
   { "hassMqttUser",                  ctString,  "",             false, "4 MQTT Interface", "User", "" },
   { "hassMqttPassword",              ctString,  "",             false, "4 MQTT Interface", "Password", "" },

   // mail

   { "mail",                      ctBool,    "0",                       false, "5 Mail", "Mail Benachrichtigung", "Mail Benachrichtigungen aktivieren/deaktivieren" },
   { "mailScript",                ctString,  BIN_PATH "/poold-mail.sh", false, "5 Mail", "poold sendet Mails über das Skript", "" },
   { "stateMailTo",               ctString,  "",                        false, "5 Mail", "Status Mail Empfänger", "Komma getrennte Empfängerliste" },
   { "errorMailTo",               ctString,  "",                        false, "5 Mail", "Fehler Mail Empfänger", "Komma getrennte Empfängerliste" },
   { "webUrl",                    ctString,  "",                        false, "5 Mail", "URL der Visualisierung", "kann mit %weburl% in die Mails eingefügt werden" },
};

//***************************************************************************
// Pool Daemon
//***************************************************************************

Pool::Pool()
   : Daemon()
{
   webPort = 61109;
}

Pool::~Pool()
{
   free(w1AddrPool);
   free(w1AddrSolar);
   free(w1AddrSuctionTube);
   free(w1AddrAir);
}

//***************************************************************************
// Init/Exit
//***************************************************************************

int Pool::init()
{
   int status = Daemon::init();

   spSensors[spWaterLevel].value = 50;  // start with 50% until we get first signal

   return status;
}

//***************************************************************************
// Init/Exit Database
//***************************************************************************

int Pool::initDb()
{
   int status = Daemon::initDb();

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

   return status;

}

int Pool::exitDb()
{
   delete selectSolarWorkPerDay;     selectSolarWorkPerDay = nullptr;

   return Daemon::exitDb();
}

int Pool::loadStates()
{
   Daemon::loadStates();

   // if filter pump is running assume its running at least 'minPumpTimeForPh'

   if (digitalOutputStates[pinFilterPump].state)
      digitalOutputStates[pinFilterPump].last = time(0)-minPumpTimeForPh;

   return done;
}

//***************************************************************************
// Read Configuration
//***************************************************************************

int Pool::readConfiguration(bool initial)
{
   Daemon::readConfiguration(initial);

   getConfigItem("poolLightColorToggle", poolLightColorToggle, no);

   getConfigItem("w1AddrPool", w1AddrPool, "");
   getConfigItem("w1AddrSolar", w1AddrSolar, "");
   getConfigItem("w1AddrSuctionTube", w1AddrSuctionTube, "");
   getConfigItem("w1AddrAir", w1AddrAir, "");

   getConfigItem("tPoolMax", tPoolMax, tPoolMax);
   getConfigItem("tSolarDelta", tSolarDelta, tSolarDelta);
   getConfigItem("lastSolarWork", spSensors[spSolarWork].value, 0);

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

   /*
   // config of GPIO pins

   getConfigItem("gpioFilterPump", gpioFilterPump, gpioFilterPump);
   getConfigItem("gpioSolarPump", gpioSolarPump, gpioSolarPump);
   */

   // Time ranges

   getConfigTimeRangeItem("filterPumpTimes", filterPumpTimes);
   getConfigTimeRangeItem("uvcLightTimes", uvcLightTimes);
   getConfigTimeRangeItem("poolLightTimes", poolLightTimes);

   return done;
}

int Pool::atMeanwhile()
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

   if (cTimeMs::Now() > lastShowerSwitch + 1000 && !digitalRead(Pool::pinShowerSwitch))
   {
      tell(2, "Info: Shower key detected");
      showerSwitch++;
      lastShowerSwitch = cTimeMs::Now();
   }
}

//***************************************************************************
// Apply Configuration Specials
//***************************************************************************

int Pool::applyConfigurationSpecials()
{
   initOutput(pinFilterPump, ooAuto|ooUser, omAuto, "Filter Pump", urFullControl);
   initOutput(pinSolarPump, ooAuto|ooUser, omAuto, "Solar Pump", urFullControl);
   initOutput(pinPoolLight, ooUser, omManual, "Pool Light");
   initOutput(pinUVC, ooAuto|ooUser, omAuto, "UV-C Light", urFullControl);
   initOutput(pinShower, ooAuto|ooUser, omAuto, "Shower");
   initOutput(pinUserOut1, ooUser, omManual, "User 1");
   initOutput(pinUserOut2, ooUser, omManual, "User 2");
   initOutput(pinUserOut3, ooUser, omManual, "User 3");
   initOutput(pinUserOut4, ooUser, omManual, "User 4");
   initOutput(pinW1Power, ooAuto, omAuto, "W1 Power", urFullControl);

   gpioWrite(pinW1Power, true, false);

   // init input IO

   initInput(pinLevel1, 0);
   initInput(pinLevel2, 0);
   initInput(pinLevel3, 0);
   initInput(pinShowerSwitch, 0);
   pullUpDnControl(pinShowerSwitch, PUD_UP);

   if (wiringPiISR(pinShowerSwitch, INT_EDGE_FALLING, &ioInterrupt) < 0)
      tell(0, "Error: Unable to setup ISR: %s", strerror(errno));

   // special values

   addValueFact(spLastUpdate, "SP", "Aktualisiert", "", wtText);
   addValueFact(spWaterLevel, "SP", "Water Level", "%", wtMeterLevel);
   addValueFact(spSolarDelta, "SP", "Solar Delta", "°C", wtMeter);
   addValueFact(spPhMinusDemand, "SP", "PH Minus Bedarf", "ml", wtMeter);
   addValueFact(spSolarPower, "SP", "Solar Leistung", "W", wtMeter);
   addValueFact(spSolarWork, "SP", "Solar Energie (heute)", "kWh", wtChart);

   // analog inputs

   addValueFact(aiPh, "AI", "PH", "", wtMeter);
   addValueFact(aiFilterPressure, "AI", "Druck", "bar", wtMeter);

   // ---------------------------------
   // move calibration to config!

   aiSensors[aiPh].calPointA = 7.0;
   aiSensors[aiPh].calPointB = 9.0;
   aiSensors[aiPh].calPointValueA = 2070;
   aiSensors[aiPh].calPointValueB = 3135;
   aiSensors[aiPh].round = 50.0;

   aiSensors[aiFilterPressure].calPointA = 0.0;
   aiSensors[aiFilterPressure].calPointB = 0.6;
   aiSensors[aiFilterPressure].calPointValueA = 463;
   aiSensors[aiFilterPressure].calPointValueB = 2290;
   aiSensors[aiFilterPressure].round = 20.0;

   uint opt = ooUser;

   if (poolLightTimes.size() > 0)
      opt |= ooAuto;

   if (digitalOutputStates[pinPoolLight].opt != opt)
   {
      digitalOutputStates[pinPoolLight].opt = opt;
      digitalOutputStates[pinPoolLight].mode = opt & ooAuto ? omAuto : omManual;
   }

   return done;
}

//***************************************************************************
// Perform Jobs
//***************************************************************************

int Pool::performJobs()
{
   // check timed shower duration

   if (digitalOutputStates[pinShower].state && digitalOutputStates[pinShower].mode == omAuto)
   {
      if (digitalOutputStates[pinShower].last < time(0) - showerDuration)
      {
         tell(eloAlways, "Shower of after %ld seconds", time(0)-digitalOutputStates[pinShower].last);
         digitalOutputStates[pinShower].next = 0;
         gpioWrite(pinShower, false, true);
      }
      else
      {
         digitalOutputStates[pinShower].next = digitalOutputStates[pinShower].last + showerDuration;
      }
   }

   return done;
}

//***************************************************************************
// Process
//***************************************************************************

int Pool::process()
{
   static time_t lastDay {midnightOf(time(0))};
   static time_t pSolarSince {0};

   tell(0, "Process ...");

   setSpecialValue(spLastUpdate, 0, l2pTime(time(0), "%A\n%d. %b %Y\n\n%T"));
   publishSpecialValue(spLastUpdate);

   if (lastDay != midnightOf(time(0)))
   {
      lastDay = midnightOf(time(0));
      setSpecialValue(spSolarWork, 0.0);
      setConfigItem("lastSolarWork", spSensors[spSolarWork].value);
   }

   time_t tPoolLast, tSolarLast;
   double tPool = valueOfW1(w1AddrPool, tPoolLast);
   double tSolar = valueOfW1(w1AddrSolar, tSolarLast);

   // use W1 values only if not older than 2 cycles

   bool w1Valid = tPoolLast > time(0) - 2*interval && tSolarLast > time(0) - 2*interval;

   // ------------
   // Solar State

   if (w1Valid)
   {
      //

      setSpecialValue(spSolarWork, spSensors[spSolarWork].value + (spSensors[spSolarPower].value * ((time(0)-pSolarSince) / 3600.0) / 1000.0));  // in kWh
      setConfigItem("lastSolarWork", spSensors[spSolarWork].value);
      setSpecialValue(spSolarDelta, tSolar - tPool);

      const double termalCapacity = 4183.0; // Wärmekapazität Wasser bei 20°C [kJ·kg-1·K-1]

      if (digitalOutputStates[pinSolarPump].state)
         setSpecialValue(spSolarPower, termalCapacity * massPerSecond * spSensors[spSolarDelta].value);
      else
         setSpecialValue(spSolarPower, 0.0);

      pSolarSince = time(0);

      // publish

      publishSpecialValue(spSolarDelta);
      publishSpecialValue(spSolarPower);
      publishSpecialValue(spSolarWork);
   }
   else
      tell(0, "W1 values NOT valid");

   // -----------
   // PH

   if (!isNan(aiSensors[aiPh].value) &&  aiSensors[aiPh].last > time(0)-120) // not older than 2 minutes
   {
      setSpecialValue(spPhMinusDemand, calcPhMinusVolume(aiSensors[aiPh].value));
      publishSpecialValue(spPhMinusDemand);
   }

   // -----------
   // Pumps Alert

   if (deactivatePumpsAtLowWater && spSensors[spWaterLevel].value == 0)   // || waterLevel == fail ??
   {
      if (digitalOutputStates[pinSolarPump].state || digitalOutputStates[pinFilterPump].state)
      {
         tell(0, "Warning: Deactivating pumps due to low water condition!");

         gpioWrite(pinFilterPump, false);
         gpioWrite(pinSolarPump, false);

         digitalOutputStates[pinFilterPump].mode = omManual;
         digitalOutputStates[pinSolarPump].mode = omManual;

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

      if (w1Valid && digitalOutputStates[pinSolarPump].mode == omAuto)
      {
         if (!isEmpty(w1AddrPool) && !isEmpty(w1AddrSolar) && existW1(w1AddrPool) && existW1(w1AddrSolar))
         {
            if (tPool > tPoolMax)
            {
               // switch OFF solar pump

               if (digitalOutputStates[pinSolarPump].state)
               {
                  tell(0, "Configured pool maximum of %.2f°C reached, pool has is %.2f°C, stopping solar pump!", tPoolMax, tPool);
                  gpioWrite(pinSolarPump, false);
               }
            }
            else if (spSensors[spSolarDelta].value > tSolarDelta)
            {
               // switch ON solar pump

               if (!digitalOutputStates[pinSolarPump].state)
               {
                  tell(0, "Solar delta of %.2f°C reached, pool has %.2f°C, starting solar pump", tSolarDelta, tPool);
                  gpioWrite(pinSolarPump, true);
               }
            }
            else
            {
               // switch OFF solar pump

               if (digitalOutputStates[pinSolarPump].state && digitalOutputStates[pinSolarPump].last < time(0) - minSolarPumpDuration*tmeSecondsPerMinute)
               {
                  tell(0, "Solar delta (%.2f°C) lower than %.2f°C, pool has %.2f°C, stopping solar pump", spSensors[spSolarDelta].value, tSolarDelta, tPool);
                  gpioWrite(pinSolarPump, false);
               }
            }
         }
         else
         {
            tell(0, "Warning: Missing at least one sensor, switching solar pump off!");
            gpioWrite(pinSolarPump, false);
         }
      }
      else if (!w1Valid)
      {
         gpioWrite(pinSolarPump, false);
         tell(0, "Warning: Solar pump switched OFF, sensor values older than %d seconds!", 2*interval);
      }

      // -----------
      // Filter Pump

      if (digitalOutputStates[pinFilterPump].mode == omAuto)
      {
         bool activate = isInTimeRange(&filterPumpTimes, time(0));

         if (digitalOutputStates[pinFilterPump].state != activate)
            gpioWrite(pinFilterPump, activate);
      }
   }

   // -----------
   // UV-C Light (only if Filter Pump is running)

   if (digitalOutputStates[pinUVC].mode == omAuto)
   {
      bool activate = digitalOutputStates[pinFilterPump].state && isInTimeRange(&uvcLightTimes, time(0));

      if (digitalOutputStates[pinUVC].state != activate)
         gpioWrite(pinUVC, activate);
   }

   // -----------
   // Pool Light

   if (digitalOutputStates[pinPoolLight].mode == omAuto)
   {
      bool activate = isInTimeRange(&poolLightTimes, time(0));

      if (digitalOutputStates[pinPoolLight].state != activate)
         gpioWrite(pinPoolLight, activate);
   }

   // --------------------
   // check pump condition

   if (alertSwitchOffPressure != 0 && aiSensors[aiFilterPressure].value < alertSwitchOffPressure)
   {
      // pressure is less than configured value

      if (digitalOutputStates[pinFilterPump].state &&
          digitalOutputStates[pinFilterPump].last < time(0) - 5*tmeSecondsPerMinute)
      {
         // and pump is runnning longer than 5 minutes

         gpioWrite(pinFilterPump, false);
         gpioWrite(pinSolarPump, false);
         digitalOutputStates[pinFilterPump].mode = omManual;
         digitalOutputStates[pinSolarPump].mode = omManual;

         char* body;
         asprintf(&body, "Filter pressure is %.2f bar and pump is running!\n Pumps switched off now!", aiSensors[aiFilterPressure].value);
         tell(0, "%s", body);
         if (sendMail(stateMailTo, "Pool pump alert", body, "text/plain") != success)
            tell(eloAlways, "Error: Sending alert mail failed");
         free(body);
      }
   }

   phMeasurementActive();
   calcWaterLevel();
   logReport();

   return success;
}

//***************************************************************************
// Report Actual State
//***************************************************************************

void Pool::logReport()
{
   static time_t nextLogAt {0};
   static time_t nextDetailLogAt {0};
   char buf[255+TB];

   time_t tPoolLast, tSolarLast;
   double tPool = valueOfW1(w1AddrPool, tPoolLast);
   double tSolar = valueOfW1(w1AddrSolar, tSolarLast);

   if (time(0) > nextLogAt)
   {
      nextLogAt = time(0) + 1 * tmeSecondsPerMinute;

      tell(0, "# ------------------------");

      tell(0, "# Pool has %.2f °C; Solar has %.2f °C; Current delta is %.2f° (%.2f° configured)",
           tPool, tSolar, spSensors[spSolarDelta].value, tSolarDelta);
      tell(0, "# Solar power is %0.2f Watt; Solar work (today) %0.2f kWh", spSensors[spSolarPower].value, spSensors[spSolarWork].value);
      tell(0, "# Solar pump is '%s/%s' since '%s'", digitalOutputStates[pinSolarPump].state ? "running" : "stopped",
           digitalOutputStates[pinSolarPump].mode == omAuto ? "auto" : "manual", toElapsed(time(0)-digitalOutputStates[pinSolarPump].last, buf));
      tell(0, "# Filter pump is '%s/%s' since '%s'", digitalOutputStates[pinFilterPump].state ? "running" : "stopped",
           digitalOutputStates[pinFilterPump].mode == omAuto ? "auto" : "manual", toElapsed(time(0)-digitalOutputStates[pinFilterPump].last, buf));
      tell(0, "# UV-C light is '%s/%s'", digitalOutputStates[pinUVC].state ? "on" : "off",
           digitalOutputStates[pinUVC].mode == omAuto ? "auto" : "manual");
      tell(0, "# Pool light is '%s/%s'", digitalOutputStates[pinPoolLight].state ? "on" : "off",
           digitalOutputStates[pinPoolLight].mode == omAuto ? "auto" : "manual");
      tell(0, "# PH Minus Demand %.2f", spSensors[spPhMinusDemand].value);

      tell(0, "# ------------------------");
   }

   if (time(0) > nextDetailLogAt)
   {
      nextDetailLogAt = time(0) + 5 * tmeSecondsPerMinute;

      tell(0, "# Solar Work");

      for (int i = 0, f = selectSolarWorkPerDay->find(); f && i++ < 5; f = selectSolarWorkPerDay->fetch())
         tell(0, "#   %s: %.2f kWh", l2pTime(tableSamples->getTimeValue("TIME"), "%d.%m.%Y").c_str(), tableSamples->getFloatValue("VALUE"));

      selectSolarWorkPerDay->freeResult();
      tell(0, "# ------------------------");
   }
}

//***************************************************************************
// Get Water Level [%]
//***************************************************************************

int Pool::calcWaterLevel()
{
   spSensors[spWaterLevel].text = "";
   spSensors[spWaterLevel].kind = "value";

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
      sprintf(text, "ERROR: Sensor Fehler <br/>(%d/%d/%d)", digitalInputStates[pinLevel1],
              digitalInputStates[pinLevel2], digitalInputStates[pinLevel3]);
      setSpecialValue(spWaterLevel, 0, text);
   }

   tell(eloDetail, "Water level is %.0f (%d/%d/%d)", spSensors[spWaterLevel].value, l1, l2, l3);

   return done;
}

//***************************************************************************
// PH Measurement Active
//***************************************************************************

void Pool::phMeasurementActive()
{
   if (digitalOutputStates[pinFilterPump].state &&
       digitalOutputStates[pinFilterPump].last < time(0)-minPumpTimeForPh)
   {
      aiSensors[aiPh].disabled = false;
      spSensors[spPhMinusDemand].disabled = false;
   }

   aiSensors[aiPh].disabled = true;
   spSensors[spPhMinusDemand].disabled = true;
}

//***************************************************************************
// Calc PH Minus Volume
//***************************************************************************

int Pool::calcPhMinusVolume(double ph)
{
   double phLack = ph - phReference;
   double mlPer01 = phMinusDemand01 * (1.0/phMinusDensity);

   // tell(0, "ph %0.2f; phLack %0.2f; mlPer01 %0.2f; phMinusDemand01 %d; phMinusDensity %0.2f -> %0.2f",
   //      ph, phLack, mlPer01, phMinusDemand01, phMinusDensity, (phLack/0.1) * mlPer01);

   return (phLack/0.1) * mlPer01;
}
