//***************************************************************************
// poold / Linux - Pool Steering
// File poold.c
// This code is distributed under the terms and conditions of the
// GNU GENERAL PUBLIC LICENSE. See the file LICENSE for details.
// Date 16.04.2020 - Jörg Wendel
//***************************************************************************

//***************************************************************************
// Include
//***************************************************************************

#include <stdio.h>
#include <unistd.h>
#include <dirent.h>
#include <libxml/parser.h>

#include <wiringPi.h>

#include "lib/json.h"
#include "poold.h"

int Poold::shutdown = no;
std::queue<std::string> Poold::messagesIn;

//***************************************************************************
// Web Service
//***************************************************************************

const char* cWebService::events[] =
{
   "unknown",
   "login",
   "logout",
   "toggleio",
   "toggleionext",
   "togglemode",

   0
};

const char* cWebService::toName(Event event)
{
   if (event >= evUnknown && event < evCount)
      return events[event];

   return events[evUnknown];
}

cWebService::Event cWebService::toEvent(const char* name)
{
   for (int e = evUnknown; e < evCount; e++)
      if (strcasecmp(name, events[e]) == 0)
         return (Event)e;

   return evUnknown;
}

//***************************************************************************
// Object
//***************************************************************************

Poold::Poold()
{
   nextAt = time(0);
   startedAt = time(0);

   cDbConnection::init();
   cDbConnection::setEncoding("utf8");
   cDbConnection::setHost(dbHost);
   cDbConnection::setPort(dbPort);
   cDbConnection::setName(dbName);
   cDbConnection::setUser(dbUser);
   cDbConnection::setPass(dbPass);

   webSock = new cWebSock("/var/lib/pool/");
}

Poold::~Poold()
{
   exit();

   delete mqttWriter;
   delete mqttReader;
   delete mqttCommandReader;
   delete webSock;

   free(mailScript);
   free(stateMailTo);
   free(w1AddrPool);
   free(w1AddrSolar);
   free(w1AddrSuctionTube);
   free(w1AddrAir);

   cDbConnection::exit();
}

//***************************************************************************
// Push Message
//***************************************************************************

int Poold::pushMessage(json_t* oContents, const char* title, long client)
{
   json_t* obj = json_object();

   addToJson(obj, "event", title);
   json_object_set_new(obj, "object", oContents);

   char* p = json_dumps(obj, JSON_PRESERVE_ORDER);
   json_decref(obj);

   cWebSock::pushMessage(p, (lws*)client);

   tell(4, "DEBUG: PushMessage [%s]", p);
   free(p);

   return done;
}

//***************************************************************************
// Dispatch Client Data
//***************************************************************************

int Poold::dispatchClientRequest()
{
   int status = fail;
   json_error_t error;
   json_t *oData, *oObject;
   Event event = evUnknown;

   // #TODO loop here while (!messagesIn.empty()) ?

   if (messagesIn.empty())
      return done;

   // { "event" : "toggleio", "object" : { "address" : "122", "type" : "DO" } }

   tell(1, "DEBUG: Got '%s'", messagesIn.front().c_str());
   oData = json_loads(messagesIn.front().c_str(), 0, &error);

   // get the request

   event = cWebService::toEvent(getStringFromJson(oData, "event", "<null>"));
   oObject = json_object_get(oData, "object");

   int addr = getIntFromJson(oObject, "address");
   // const char* type = getStringFromJson(oObject, "type");

   // dispatch client request

   switch (event)
   {
      // case evLogin:      status = performLogin(oObject);  break;
      case evToggleIo:      status = toggleIo(addr);            break;
      case evToggleIoNext:  status = toggleIoNext(addr);        break;
      case evToggleMode:    status = toggleOutputMode(addr);    break;

      default:
         tell(0, "Error: Received unexpected client request '%s' at [%s]",
              toName(event), messagesIn.front().c_str());
   }

   json_decref(oData);      // free the json object
   messagesIn.pop();

   return status;
}

//***************************************************************************
// Init / Exit
//***************************************************************************

int Poold::init()
{
   int status {success};
   char* dictPath = 0;

   // initialize the dictionary

   asprintf(&dictPath, "%s/poold.dat", confDir);

   if (dbDict.in(dictPath) != success)
   {
      tell(0, "Fatal: Dictionary not loaded, aborting!");
      return 1;
   }

   tell(0, "Dictionary '%s' loaded", dictPath);
   free(dictPath);

   if ((status = initDb()) != success)
   {
      exitDb();
      return status;
   }

   // ---------------------------------
   // Read configuration from config table

   readConfiguration();

   // ---------------------------------
   // setup GPIO

   wiringPiSetupPhys();     // use the 'physical' PIN numbers
   // wiringPiSetup();      // use the 'special' wiringPi PIN numbers
   // wiringPiSetupGpio();  // use the 'GPIO' PIN numbers

   initOutput(pinFilterPump, ooAuto|ooUser, omAuto, "Filter Pump");
   initOutput(pinSolarPump, ooAuto|ooUser, omAuto, "Solar Pump");
   initOutput(pinPoolLight, ooUser, omManual, "Pool Light");
   initOutput(pinUVC, ooAuto|ooUser, omAuto, "UV-C Light");
   initOutput(pinShower, ooAuto|ooUser, omAuto, "Shower");
   initOutput(pinUserOut1, ooUser, omManual, "User 1");
   initOutput(pinUserOut2, ooUser, omManual, "User 2");
   initOutput(pinUserOut3, ooUser, omManual, "User 3");

   // init water Level sensor

   initInput(pinLevel1, 0);
   initInput(pinLevel2, 0);
   initInput(pinLevel3, 0);

   // special values

   addValueFact(1, "SP", "Water Level", "%");
   addValueFact(2, "SP", "Solar Delta", "°C");

   //

   initW1();

   // ---------------------------------
   // apply some configuration specials

   applyConfigurationSpecials();

   // init web socket ...

   while (webSock->init(61109, webSocketPingTime) != success)
   {
      tell(0, "Retrying in 2 seconds");
      sleep(2);
   }

   initialized = true;

   return success;
}

int Poold::exit()
{
   for (auto it = digitalOutputStates.begin(); it != digitalOutputStates.end(); ++it)
      gpioWrite(it->first, false);

   exitDb();

   return success;
}

//***************************************************************************
// Init digital Output
//***************************************************************************

int Poold::initOutput(uint pin, int opt, OutputMode mode, const char* name)
{
   digitalOutputStates[pin].opt = opt;
   digitalOutputStates[pin].mode = mode;
   digitalOutputStates[pin].name = name;

   tableValueFacts->clear();

   tableValueFacts->setValue("ADDRESS", (int)pin);
   tableValueFacts->setValue("TYPE", "DO");

   if (tableValueFacts->find())
   {
      if (!tableValueFacts->getValue("USRTITLE")->isEmpty())
         digitalOutputStates[pin].title = tableValueFacts->getStrValue("USRTITLE");
      else
         digitalOutputStates[pin].title = tableValueFacts->getStrValue("TITLE");
   }
   else
   {
      digitalOutputStates[pin].title = name;
   }

   tableValueFacts->reset();

   pinMode(pin, OUTPUT);
   gpioWrite(pin, false);
   addValueFact(pin, "DO", name, "");

   return done;
}

//***************************************************************************
// Init digital Input
//***************************************************************************

int Poold::initInput(uint pin, const char* name)
{
   pinMode(pin, INPUT);

   if (!isEmpty(name))
      addValueFact(pin, "DI", name, "");

   digitalInputStates[pin] = gpioRead(pin);

   return done;
}

//***************************************************************************
// Init/Exit Database
//***************************************************************************

int Poold::initDb()
{
   static int initial = yes;
   int status {success};

   if (connection)
      exitDb();

   tell(eloAlways, "Try conneting to database");

   connection = new cDbConnection();

   if (initial)
   {
      // ------------------------------------------
      // initially create/alter tables and indices
      // ------------------------------------------

      tell(0, "Checking database connection ...");

      if (connection->attachConnection() != success)
      {
         tell(0, "Error: Initial database connect failed");
         return fail;
      }

      tell(0, "Checking table structure and indices ...");

      for (auto t = dbDict.getFirstTableIterator(); t != dbDict.getTableEndIterator(); t++)
      {
         cDbTable* table = new cDbTable(connection, t->first.c_str());

         tell(1, "Checking table '%s'", t->first.c_str());

         if (!table->exist())
         {
            if ((status += table->createTable()) != success)
               continue;
         }
         else
         {
            status += table->validateStructure();
         }

         status += table->createIndices();

         delete table;
      }

      connection->detachConnection();

      if (status != success)
         return abrt;

      tell(0, "Checking table structure and indices succeeded");
   }

   // ------------------------
   // create/open tables
   // ------------------------

   tableValueFacts = new cDbTable(connection, "valuefacts");
   if (tableValueFacts->open() != success) return fail;

   tableSamples = new cDbTable(connection, "samples");
   if (tableSamples->open() != success) return fail;

   tablePeaks = new cDbTable(connection, "peaks");
   if (tablePeaks->open() != success) return fail;

   tableConfig = new cDbTable(connection, "config");
   if (tableConfig->open() != success) return fail;

   tableJobs = new cDbTable(connection, "jobs");
   if (tableJobs->open() != success) return fail;

   // prepare statements

   selectActiveValueFacts = new cDbStatement(tableValueFacts);

   selectActiveValueFacts->build("select ");
   selectActiveValueFacts->bindAllOut();
   selectActiveValueFacts->build(" from %s where ", tableValueFacts->TableName());
   selectActiveValueFacts->bind("STATE", cDBS::bndIn | cDBS::bndSet);

   status += selectActiveValueFacts->prepare();

   // ------------------

   selectAllValueFacts = new cDbStatement(tableValueFacts);

   selectAllValueFacts->build("select ");
   selectAllValueFacts->bindAllOut();
   selectAllValueFacts->build(" from %s", tableValueFacts->TableName());

   status += selectAllValueFacts->prepare();

   // ------------------
   // select max(time) from samples

   selectMaxTime = new cDbStatement(tableSamples);

   selectMaxTime->build("select ");
   selectMaxTime->bind("TIME", cDBS::bndOut, "max(");
   selectMaxTime->build(") from %s", tableSamples->TableName());

   status += selectMaxTime->prepare();

   // ------------------

   selectPendingJobs = new cDbStatement(tableJobs);

   selectPendingJobs->build("select ");
   selectPendingJobs->bind("ID", cDBS::bndOut);
   selectPendingJobs->bind("REQAT", cDBS::bndOut, ", ");
   selectPendingJobs->bind("STATE", cDBS::bndOut, ", ");
   selectPendingJobs->bind("COMMAND", cDBS::bndOut, ", ");
   selectPendingJobs->bind("ADDRESS", cDBS::bndOut, ", ");
   selectPendingJobs->bind("DATA", cDBS::bndOut, ", ");
   selectPendingJobs->build(" from %s where state = 'P'", tableJobs->TableName());

   status += selectPendingJobs->prepare();

   // ------------------

   cleanupJobs = new cDbStatement(tableJobs);

   cleanupJobs->build("delete from %s where ", tableJobs->TableName());
   cleanupJobs->bindCmp(0, "REQAT", 0, "<");

   status += cleanupJobs->prepare();

   // ------------------

   if (status == success)
      tell(eloAlways, "Connection to database established");

   connection->query("%s", "truncate table jobs");

   return status;
}

int Poold::exitDb()
{
   delete tableSamples;            tableSamples = 0;
   delete tablePeaks;              tablePeaks = 0;
   delete tableValueFacts;         tableValueFacts = 0;
   delete tableConfig;             tableConfig = 0;
   delete tableJobs;               tableJobs = 0;

   delete selectActiveValueFacts;  selectActiveValueFacts = 0;
   delete selectAllValueFacts;     selectAllValueFacts = 0;
   delete selectMaxTime;           selectMaxTime = 0;
   delete selectPendingJobs;       selectPendingJobs = 0;
   delete cleanupJobs;             cleanupJobs = 0;

   delete connection;              connection = 0;

   return done;
}

// ---------------------------------
// Init one wire sensors
// ---------------------------------

int Poold::initW1()
{
   if (w1.scan() == success)
   {
      const W1::SensorList* list = w1.getList();

      // yes, we have one-wire sensors

      int count {0};
      int added {0};
      int modified {0};

      for (auto it = list->begin(); it != list->end(); ++it)
      {
         int res = addValueFact((int)W1::toId(it->first.c_str()), "W1", it->first.c_str(), "°C");

         if (res == 1)
            added++;
         else if (res == 2)
            modified++;
         count++;
      }

      tell(eloAlways, "Found %d one wire sensors, added %d, modified %d", count, added, modified);
   }

   return success;
}

//***************************************************************************
// Read Configuration
//***************************************************************************

int Poold::readConfiguration()
{
   char* webUser {nullptr};
   char* webPass {nullptr};
   md5Buf defaultPwd;

   // init default web user and password

   createMd5("pool", defaultPwd);
   getConfigItem("user", webUser, "pool");
   getConfigItem("passwd", webPass, defaultPwd);

   free(webUser);
   free(webPass);

   // init configuration

   getConfigItem("interval", interval, interval);

   getConfigItem("mail", mail, no);
   getConfigItem("mailScript", mailScript, BIN_PATH "/poold-mail.sh");
   getConfigItem("stateMailTo", stateMailTo);

   getConfigItem("aggregateInterval", aggregateInterval, 15);
   getConfigItem("aggregateHistory", aggregateHistory, 0);
   getConfigItem("hassMqttUrl", hassMqttUrl, "");

   // more special

   getConfigItem("poolLightColorToggle", poolLightColorToggle, no);

   getConfigItem("w1AddrPool", w1AddrPool, "");
   getConfigItem("w1AddrSolar", w1AddrSolar, "");
   getConfigItem("w1AddrSuctionTube", w1AddrSuctionTube, "");
   getConfigItem("w1AddrAir", w1AddrAir, "");

   getConfigItem("tPoolMax", tPoolMax, tPoolMax);
   getConfigItem("tSolarDelta", tSolarDelta, tSolarDelta);
   getConfigItem("showerDuration", showerDuration, 20);

   getConfigItem("invertDO", invertDO, no);

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

int Poold::applyConfigurationSpecials()
{
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
// Store
//***************************************************************************

int Poold::store(time_t now, const char* name, const char* title, const char* unit,
                 const char* type, int address, double value, const char* text)
{
   tableSamples->clear();

   tableSamples->setValue("TIME", now);
   tableSamples->setValue("ADDRESS", address);
   tableSamples->setValue("TYPE", type);
   tableSamples->setValue("AGGREGATE", "S");

   tableSamples->setValue("VALUE", value);
   tableSamples->setValue("TEXT", text);
   tableSamples->setValue("SAMPLES", 1);

   tableSamples->store();

   // peaks

   tablePeaks->clear();

   tablePeaks->setValue("ADDRESS", address);
   tablePeaks->setValue("TYPE", type);

   if (!tablePeaks->find())
   {
      tablePeaks->setValue("MIN", value);
      tablePeaks->setValue("MAX", value);
      tablePeaks->store();
   }
   else
   {
      if (value > tablePeaks->getFloatValue("MAX"))
         tablePeaks->setValue("MAX", value);

      if (value < tablePeaks->getFloatValue("MIN"))
         tablePeaks->setValue("MIN", value);

      tablePeaks->store();
   }

   // Home Assistant

   IoType iot = strcmp(type, "DO") == 0 ? iotLight : iotSensor;

   if (!isEmpty(hassMqttUrl))
      hassPush(iot, name, title, unit, value, text, initialRun /*forceConfig*/);

   return success;
}

//***************************************************************************
// standby
//***************************************************************************

int Poold::standby(int t)
{
   time_t end = time(0) + t;

   while (time(0) < end && !doShutDown())
   {
      meanwhile();
      // usleep(50000);  sleep is don in meanwhile by mqttCommandReader
   }

   return done;
}

int Poold::standbyUntil(time_t until)
{
   while (time(0) < until && !doShutDown())
   {
      meanwhile();
      // tell(eloAlways, "standbyUntil: loop ...");
      // usleep(50000);  sleep is don in meanwhile by mqttCommandReader
   }

   return done;
}

//***************************************************************************
// Meanwhile
//***************************************************************************

int Poold::meanwhile()
{
   static time_t lastCleanup = time(0);

   if (!initialized)
      return done;

   if (!connection || !connection->isConnected())
      return fail;

   if (mqttReader && mqttReader->isConnected()) mqttReader->yield();
   if (mqttWriter && mqttWriter->isConnected()) mqttWriter->yield();
   if (mqttCommandReader && mqttCommandReader->isConnected()) mqttCommandReader->yield();

   webSock->service(100);
   dispatchClientRequest();
   webSock->performData(cWebSock::mtData);
   performWebSocketPing();

   performHassRequests();
   performWebifRequests();
   performJobs();

   if (lastCleanup < time(0) - 6*tmeSecondsPerHour)
   {
      cleanupWebifRequests();
      lastCleanup = time(0);
   }

   return done;
}

//***************************************************************************
// Loop
//***************************************************************************

int Poold::loop()
{
   // init

   scheduleAggregate();

   while (!doShutDown())
   {
      int stateChanged = no;

      // check db connection

      while (!doShutDown() && (!connection || !connection->isConnected()))
      {
         if (initDb() == success)
            break;

         exitDb();
         tell(eloAlways, "Retrying in 10 seconds");
         standby(10);
      }

      if (doShutDown())
         break;

      standbyUntil(nextAt);

      // aggregate

      if (aggregateHistory && nextAggregateAt <= time(0))
         aggregate();

      // work expected?

      if (time(0) < nextAt)
         continue;

      // perform update

      nextAt = time(0) + interval;
      mailBody = "";
      mailBodyHtml = "";

      update();
      process();

      afterUpdate();

      // mail

      if (mail && stateChanged)
         sendStateMail();

      initialRun = false;
   }

   return success;
}

//***************************************************************************
// Update
//***************************************************************************

int Poold::update()
{
   size_t w1Count = w1.getCount();
   time_t now = time(0);
   int count {0};

   w1.update();

   if (w1Count < w1.getCount())
   {
      tell(eloAlways, "New W1 sensor attached, inserting");
      initW1();
   }

   tell(eloDetail, "Update ...");

   tableValueFacts->clear();
   tableValueFacts->setValue("STATE", "A");

   connection->startTransaction();

   json_t* oJson = json_array();

   for (int f = selectActiveValueFacts->find(); f; f = selectActiveValueFacts->fetch())
   {
      char text[1000];
      double peak {0.0};

      int addr = tableValueFacts->getIntValue("ADDRESS");
      const char* type = tableValueFacts->getStrValue("TYPE");
      const char* title = tableValueFacts->getStrValue("TITLE");
      const char* usrtitle = tableValueFacts->getStrValue("USRTITLE");
      const char* unit = tableValueFacts->getStrValue("UNIT");
      const char* name = tableValueFacts->getStrValue("NAME");
      int scaleMax = tableValueFacts->getIntValue("MAXSCALE");

      tablePeaks->clear();
      tablePeaks->setValue("ADDRESS", addr);
      tablePeaks->setValue("TYPE", type);
      if (tablePeaks->find())
         peak = tablePeaks->getFloatValue("MAX");
      tablePeaks->reset();

      if (!isEmpty(usrtitle))
         title = usrtitle;

      json_t* ojData = json_object();
      json_array_append_new(oJson, ojData);

      json_object_set_new(ojData, "address", json_integer(addr));
      json_object_set_new(ojData, "type", json_string(type));
      json_object_set_new(ojData, "name", json_string(name));
      json_object_set_new(ojData, "title", json_string(title));
      json_object_set_new(ojData, "unit", json_string(unit));

      if (tableValueFacts->hasValue("TYPE", "W1"))
      {
         if (!w1.exist(name))
            continue;

         store(now, name, title, unit, type, addr, w1.valueOf(name));
         json_object_set_new(ojData, "value", json_real((w1.valueOf(name))));
         json_object_set_new(ojData, "scalemax", json_integer(scaleMax));
         json_object_set_new(ojData, "peak", json_real(peak));
         json_object_set_new(ojData, "widgettype", json_integer(wtGauge));
      }
      else if (tableValueFacts->hasValue("TYPE", "DO"))
      {
         store(now, name, title, unit, type, addr, digitalOutputStates[addr].state);
         json_object_set_new(ojData, "mode", json_string(digitalOutputStates[addr].mode == omManual ? "manual" : "auto"));
                  json_object_set_new(ojData, "options", json_integer(digitalOutputStates[addr].opt));
         json_object_set_new(ojData, "image", json_string(getImageOf(title, digitalOutputStates[addr].state)));
         json_object_set_new(ojData, "widgettype", json_integer(wtSymbol));
      }
      else if (tableValueFacts->hasValue("TYPE", "SP"))
      {
         if (addr == 1)
         {
            getWaterLevel();

            if (waterLevel == fail)
            {
               sprintf(text, "ERROR:Sensor Fehler <br/>(%d/%d/%d)", digitalInputStates[pinLevel1],
                       digitalInputStates[pinLevel2], digitalInputStates[pinLevel3]);

               store(now, name, title, unit, type, addr, waterLevel, text);
               json_object_set_new(ojData, "text", json_string(text));
               json_object_set_new(ojData, "widgettype", json_integer(wtText));
            }
            else
            {
               store(now, name, title, unit, type, addr, waterLevel);
               json_object_set_new(ojData, "value", json_integer(waterLevel));
               json_object_set_new(ojData, "scalemax", json_integer(scaleMax));
               json_object_set_new(ojData, "peak", json_real(peak));
               json_object_set_new(ojData, "widgettype", json_integer(wtGauge));
            }
         }
         else if (addr == 2)
         {
            tPool = w1.valueOf(w1AddrPool);
            tSolar = w1.valueOf(w1AddrSolar);
            tCurrentDelta = tSolar - tPool;
            store(now, name, title, unit, type, addr, tCurrentDelta);

            json_object_set_new(ojData, "value", json_real(tCurrentDelta));
            json_object_set_new(ojData, "scalemax", json_integer(scaleMax));
            json_object_set_new(ojData, "peak", json_real(peak));
            json_object_set_new(ojData, "widgettype", json_integer(wtGauge));
         }
      }

      count++;
   }

   connection->commit();

   // send result to all connected WEBIF clients

   Poold::pushMessage(oJson, "all");
   webSock->performData(cWebSock::mtData);
   // ?? json_decref(oJson);

   tell(eloAlways, "Updated %d samples", count);

   return success;
}

//***************************************************************************
// Process
//***************************************************************************

int Poold::process()
{
   tPool = w1.valueOf(w1AddrPool);
   tSolar = w1.valueOf(w1AddrSolar);
   tCurrentDelta = tSolar - tPool;

   tell(0, "Process ...");

   // -----------
   // Solar Pump

   if (digitalOutputStates[pinSolarPump].mode == omAuto)
   {
      if (!isEmpty(w1AddrPool) && !isEmpty(w1AddrSolar) && w1.exist(w1AddrPool) && w1.exist(w1AddrSolar))
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
         else if (tCurrentDelta > tSolarDelta)
         {
            // switch ON solar pump

            if (!digitalOutputStates[pinSolarPump].state)
            {
               tell(0, "Solar delta of %.2f°C reached, pool has %.2f°C, starting solar pump!", tSolarDelta, tPool);
               gpioWrite(pinSolarPump, true);
            }
         }
         else
         {
            // switch OFF solar pump

            if (digitalOutputStates[pinSolarPump].state)
            {
               tell(0, "Solar delta (%.2f°C) lower than %.2f°C, pool has %.2f°C, stopping solar pump!", tCurrentDelta, tSolarDelta, tPool);
               gpioWrite(pinSolarPump, false);
            }
         }
      }
      else
      {
         tell(0, "Missing at least one sensor, switching solar pump off");
         gpioWrite(pinSolarPump, false);
      }
   }

   // -----------
   // Filter Pump

   if (digitalOutputStates[pinFilterPump].mode == omAuto)
   {
      bool activate = isInTimeRange(&filterPumpTimes, time(0));

      if (digitalOutputStates[pinFilterPump].state != activate)
         gpioWrite(pinFilterPump, activate);
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

   logReport();

   return success;
}

//***************************************************************************
// Poold Ping
//***************************************************************************

int Poold::performWebSocketPing()
{
   if (nextWebSocketPing < time(0))
   {
      webSock->performData(cWebSock::mtPing);
      nextWebSocketPing = time(0) + webSocketPingTime-5;
   }

   return done;
}

//***************************************************************************
// Perform Jobs
//***************************************************************************

int Poold::performJobs()
{
   // check timed shower duration

   if (digitalOutputStates[pinShower].state && digitalOutputStates[pinShower].mode == omAuto)
   {
      if (digitalOutputStates[pinShower].last < time(0) - showerDuration)
      {
         tell(eloAlways, "Shower of after %ld seconds", time(0)-digitalOutputStates[pinShower].last);
         gpioWrite(pinShower, false);
      }
   }

   return done;
}

//***************************************************************************
// Is In Time Range
//***************************************************************************

bool Poold::isInTimeRange(const std::vector<Range>* ranges, time_t t)
{
   for (auto it = ranges->begin(); it != ranges->end(); ++it)
   {
      if (it->inRange(l2hhmm(t)))
      {
         tell(eloDebug, "Debug: %d in range '%d-%d'", l2hhmm(t), it->from, it->to);
         return true;
      }
   }

   return false;
}

//***************************************************************************
// Report to syslog
//***************************************************************************

void Poold::logReport()
{
   tell(0, "------------------------");

   tell(0, "Pool has %.2f °C; Solar has %.2f °C; Current delta is %.2f° (%.2f° configured)",
        tPool, tSolar, tCurrentDelta, tSolarDelta);
   tell(0, "Solar pump is '%s/%s'", digitalOutputStates[pinSolarPump].state ? "running" : "stopped",
        digitalOutputStates[pinSolarPump].mode == omAuto ? "auto" : "manual");
   tell(0, "Filter pump is '%s/%s'", digitalOutputStates[pinFilterPump].state ? "running" : "stopped",
        digitalOutputStates[pinFilterPump].mode == omAuto ? "auto" : "manual");
   tell(0, "UV-C light is '%s/%s'", digitalOutputStates[pinUVC].state ? "on" : "off",
        digitalOutputStates[pinUVC].mode == omAuto ? "auto" : "manual");
   tell(0, "Pool light is '%s/%s'", digitalOutputStates[pinPoolLight].state ? "on" : "off",
        digitalOutputStates[pinPoolLight].mode == omAuto ? "auto" : "manual");
}

//***************************************************************************
// After Update
//***************************************************************************

void Poold::afterUpdate()
{
   char* path {nullptr};

   asprintf(&path, "%s/after-update.sh", confDir);

   if (fileExists(path))
   {
      tell(0, "Calling '%s'", path);
      system(path);
   }

   free(path);
}

//***************************************************************************
// Schedule Aggregate
//***************************************************************************

int Poold::scheduleAggregate()
{
   struct tm tm = {0};
   time_t now {0};

   if (!aggregateHistory)
      return done;

   // calc today at 01:00:00

   now = time(0);
   localtime_r(&now, &tm);

   tm.tm_sec = 0;
   tm.tm_min = 0;
   tm.tm_hour = 1;
   tm.tm_isdst = -1;               // force DST auto detect

   nextAggregateAt = mktime(&tm);

   // if in the past ... skip to next day ...

   if (nextAggregateAt <= time(0))
      nextAggregateAt += tmeSecondsPerDay;

   tell(eloAlways, "Scheduled aggregation for '%s' with interval of %d minutes",
        l2pTime(nextAggregateAt).c_str(), aggregateInterval);

   return success;
}

//***************************************************************************
// Aggregate
//***************************************************************************

int Poold::aggregate()
{
   char* stmt {nullptr};
   time_t history = time(0) - (aggregateHistory * tmeSecondsPerDay);
   int aggCount {0};

   asprintf(&stmt,
            "replace into samples "
            "  select address, type, 'A' as aggregate, "
            "    CONCAT(DATE(time), ' ', SEC_TO_TIME((TIME_TO_SEC(time) DIV %d) * %d)) + INTERVAL %d MINUTE time, "
            "    unix_timestamp(sysdate()) as inssp, unix_timestamp(sysdate()) as updsp, "
            "    round(sum(value)/count(*), 2) as value, text, count(*) samples "
            "  from "
            "    samples "
            "  where "
            "    aggregate != 'A' and "
            "    time <= from_unixtime(%ld) "
            "  group by "
            "    CONCAT(DATE(time), ' ', SEC_TO_TIME((TIME_TO_SEC(time) DIV %d) * %d)) + INTERVAL %d MINUTE, address, type;",
            aggregateInterval * tmeSecondsPerMinute, aggregateInterval * tmeSecondsPerMinute, aggregateInterval,
            history,
            aggregateInterval * tmeSecondsPerMinute, aggregateInterval * tmeSecondsPerMinute, aggregateInterval);

   tell(eloAlways, "Starting aggregation ...");

   if (connection->query(aggCount, "%s", stmt) == success)
   {
      int delCount = 0;

      tell(eloDebug, "Aggregation: [%s]", stmt);
      free(stmt);

      // Einzelmesspunkte löschen ...

      asprintf(&stmt, "aggregate != 'A' and time <= from_unixtime(%ld)", history);

      if (tableSamples->deleteWhere(stmt, delCount) == success)
      {
         tell(eloAlways, "Aggregation with interval of %d minutes done; "
              "Created %d aggregation rows, deleted %d sample rows",
              aggregateInterval, aggCount, delCount);
      }
   }

   free(stmt);

   // schedule even in case of error!

   scheduleAggregate();

   return success;
}

//***************************************************************************
// Send State Mail
//***************************************************************************

int Poold::sendStateMail()
{
   char* webUrl {nullptr};
   string subject = "Status: ";

   // check

   if (isEmpty(mailScript) || !mailBody.length() || isEmpty(stateMailTo))
      return done;

   // get web url ..

   getConfigItem("webUrl", webUrl, "http://to-be-configured");

   // HTML mail

   char* html {nullptr};

   loadHtmlHeader();

   asprintf(&html,
            "<html>\n"
            " %s\n"
            "  <body>\n"
            "   <font face=\"Arial\"><br/>WEB Interface: <a href=\"%s\">S 3200</a><br/></font>\n"
            "   <br/>\n"
            "   <table>\n"
            "     <thead>\n"
            "       <tr class=\"head\">\n"
            "         <th><font>Parameter</font></th>\n"
            "         <th><font>Wert</font></th>\n"
            "       </tr>\n"
            "     </thead>\n"
            "     <tbody>\n"
            "%s"
            "     </tbody>\n"
            "   </table>\n"
            "   <br/>\n"
            "  </body>\n"
            "</html>\n",
            htmlHeader.memory, webUrl, mailBodyHtml.c_str());

   int result = sendMail(stateMailTo, subject.c_str(), html, "text/html");

   free(html);

   return result;
}

int Poold::sendMail(const char* receiver, const char* subject, const char* body, const char* mimeType)
{
   char* command {nullptr};
   int result {0};

   asprintf(&command, "%s '%s' '%s' '%s' %s", mailScript, subject, body, mimeType, receiver);
   result = system(command);
   free(command);

   tell(eloAlways, "Send mail '%s' with [%s] to '%s'", subject, body, receiver);

   return result;
}

//***************************************************************************
// Load Html Header
//***************************************************************************

int Poold::loadHtmlHeader()
{
   char* file {nullptr};

   // load only once at first call

   if (!htmlHeader.isEmpty())
      return done;

   asprintf(&file, "%s/mail-head.html", confDir);

   if (fileExists(file))
      if (loadFromFile(file, &htmlHeader) == success)
         htmlHeader.append("\0");

   free(file);

   if (!htmlHeader.isEmpty())
      return success;

   htmlHeader.clear();

   htmlHeader.append("  <head>\n"
                     "    <style type=\"text/css\">\n"
                     "      table {"
                     "        border: 1px solid #d2d2d2;\n"
                     "        border-collapse: collapse;\n"
                     "      }\n"
                     "      table tr.head {\n"
                     "        background-color: #004d8f;\n"
                     "        color: #fff;\n"
                     "        font-weight: bold;\n"
                     "        font-family: Helvetica, Arial, sans-serif;\n"
                     "        font-size: 12px;\n"
                     "      }\n"
                     "      table tr th,\n"
                     "      table tr td {\n"
                     "        padding: 4px;\n"
                     "        text-align: left;\n"
                     "      }\n"
                     "      table tr:nth-child(1n) td {\n"
                     "        background-color: #fff;\n"
                     "      }\n"
                     "      table tr:nth-child(2n+2) td {\n"
                     "        background-color: #eee;\n"
                     "      }\n"
                     "      td {\n"
                     "        color: #333;\n"
                     "        font-family: Helvetica, Arial, sans-serif;\n"
                     "        font-size: 12px;\n"
                     "        border: 1px solid #D2D2D2;\n"
                     "      }\n"
                     "      </style>\n"
                     "  </head>"
                     "\0");

   return success;
}

//***************************************************************************
// Add Value Fact
//***************************************************************************

int Poold::addValueFact(int addr, const char* type, const char* name, const char* unit)
{
   int maxScale = unit[0] == '%' ? 100 : 45;

   tableValueFacts->clear();
   tableValueFacts->setValue("ADDRESS", addr);
   tableValueFacts->setValue("TYPE", type);

   if (!tableValueFacts->find())
   {
      tableValueFacts->setValue("NAME", name);
      tableValueFacts->setValue("STATE", "D");
      tableValueFacts->setValue("UNIT", unit);
      tableValueFacts->setValue("TITLE", name);
      tableValueFacts->setValue("MAXSCALE", maxScale);

      tableValueFacts->store();
      return 1;    // 1 for 'added'
   }
   else
   {
      tableValueFacts->clearChanged();

      tableValueFacts->setValue("NAME", name);
      tableValueFacts->setValue("UNIT", unit);
      tableValueFacts->setValue("TITLE", name);

      if (tableValueFacts->getValue("MAXSCALE")->isNull())
         tableValueFacts->setValue("MAXSCALE", maxScale);

      if (tableValueFacts->getChanges())
      {
         tableValueFacts->store();
         return 2;  // 2 for 'modified'
      }
   }

   return fail;
}

//***************************************************************************
// Config Data
//***************************************************************************

int Poold::getConfigItem(const char* name, char*& value, const char* def)
{
   free(value);
   value = 0;

   tableConfig->clear();
   tableConfig->setValue("OWNER", "poold");
   tableConfig->setValue("NAME", name);

   if (tableConfig->find())
      value = strdup(tableConfig->getStrValue("VALUE"));
   else
   {
      value = strdup(def);
      setConfigItem(name, value);  // store the default
   }

   tableConfig->reset();

   return success;
}

int Poold::setConfigItem(const char* name, const char* value)
{
   tell(eloAlways, "Storing '%s' with value '%s'", name, value);
   tableConfig->clear();
   tableConfig->setValue("OWNER", "poold");
   tableConfig->setValue("NAME", name);
   tableConfig->setValue("VALUE", value);

   return tableConfig->store();
}

int Poold::getConfigItem(const char* name, int& value, int def)
{
   char* txt {nullptr};

   getConfigItem(name, txt);

   if (!isEmpty(txt))
      value = atoi(txt);
   else if (isEmpty(txt) && def != na)
   {
      value = def;
      setConfigItem(name, value);
   }
   else
      value = 0;

   free(txt);

   return success;
}

int Poold::setConfigItem(const char* name, int value)
{
   char txt[16];

   snprintf(txt, sizeof(txt), "%d", value);

   return setConfigItem(name, txt);
}

int Poold::getConfigItem(const char* name, double& value, double def)
{
   char* txt {nullptr};

   getConfigItem(name, txt);

   if (!isEmpty(txt))
      value = strtod(txt, nullptr);
   else if (isEmpty(txt) && def != na)
   {
      value = def;
      setConfigItem(name, value);
   }
   else
      value = 0.0;

   free(txt);

   return success;
}

int Poold::setConfigItem(const char* name, double value)
{
   char txt[16+TB];

   snprintf(txt, sizeof(txt), "%.2f", value);

   return setConfigItem(name, txt);
}

//***************************************************************************
// Get Config Time Range Item
//***************************************************************************

int Poold::getConfigTimeRangeItem(const char* name, std::vector<Range>& ranges)
{
   char* tmp {nullptr};

   getConfigItem(name, tmp, "");
   ranges.clear();

   for (const char* r = strtok(tmp, ","); r; r = strtok(0, ","))
   {
      uint fromHH, fromMM, toHH, toMM;

      if (sscanf(r, "%u:%u-%u:%u", &fromHH, &fromMM, &toHH, &toMM) == 4)
      {
         uint from = fromHH*100 + fromMM;
         uint to = toHH*100 + toMM;

         ranges.push_back({from, to});
         tell(3, "range: %d - %d", from, to);
      }
      else
      {
         tell(0, "Error: Unexpected range '%s' for '%s'", r, name);
      }
   }

   free(tmp);

   return success;
}

//***************************************************************************
// Get Water Level [%]
//***************************************************************************

int Poold::getWaterLevel()
{
   bool l1 = gpioRead(pinLevel1);
   bool l2 = gpioRead(pinLevel2);
   bool l3 = gpioRead(pinLevel3);

   if (l1 && l2 && l3)
      waterLevel = 100;
   else if (l1 && l2 && !l3)
      waterLevel = 66;
   else if (l1 && !l2 && !l3)
      waterLevel = 33;
   else if (!l1 && !l2 && !l3)
      waterLevel = 0;
   else
      waterLevel = fail;

   tell(eloDetail, "Water level is %d (%d/%d/%d)", waterLevel, l1, l2, l3);

   return waterLevel;
}

//***************************************************************************
// Digital IO Stuff
//***************************************************************************

int Poold::toggleIo(uint pin)
{
   gpioWrite(pin, !digitalOutputStates[pin].state);
   tell(eloDebug, "Debug: Set %d to %d", pin, digitalOutputStates[pin].state);

   return success;
}

int Poold::toggleIoNext(uint pin)
{
   if (digitalOutputStates[pin].state)
   {
      toggleIo(pin);
      usleep(300000);
      toggleIo(pin);
      return success;
   }

   gpioWrite(pin, true);

   return success;
}

int Poold::toggleOutputMode(uint pin)
{
   // allow mode toggle only if more than one option is given

   if (digitalOutputStates[pin].opt & ooAuto && digitalOutputStates[pin].opt & ooUser)
   {
      OutputMode mode = digitalOutputStates[pin].mode == omAuto ? omManual : omAuto;
      digitalOutputStates[pin].mode = mode;

      json_t* oJson = json_array();
      json_t* ojData = json_object();
      json_array_append_new(oJson, ojData);

      json_object_set_new(ojData, "address", json_integer(pin));
      json_object_set_new(ojData, "type", json_string("DO"));
      json_object_set_new(ojData, "name", json_string(digitalOutputStates[pin].name));
      json_object_set_new(ojData, "title", json_string(digitalOutputStates[pin].title.c_str()));
      // json_object_set_new(ojData, "unit", json_string(""));
      json_object_set_new(ojData, "mode", json_string(digitalOutputStates[pin].mode == omManual ? "manual" : "auto"));
      json_object_set_new(ojData, "options", json_integer(digitalOutputStates[pin].opt));
      json_object_set_new(ojData, "image", json_string(getImageOf(digitalOutputStates[pin].title.c_str(), digitalOutputStates[pin].state)));
      json_object_set_new(ojData, "widgettype", json_integer(wtSymbol));

      Poold::pushMessage(oJson, "update");
      webSock->performData(cWebSock::mtData);
   }

   return success;
}

void Poold::gpioWrite(uint pin, bool state)
{
   digitalOutputStates[pin].state = state;
   digitalOutputStates[pin].last = time(0);

   // negate state until the relay board is active at 'false'

   digitalWrite(pin, invertDO ? !state : state);

   if (!isEmpty(hassMqttUrl))
      hassPush(iotLight, digitalOutputStates[pin].name, "", "", digitalOutputStates[pin].state, "", false /*forceConfig*/);

   json_t* oJson = json_array();
   json_t* ojData = json_object();
   json_array_append_new(oJson, ojData);

   json_object_set_new(ojData, "address", json_integer(pin));
   json_object_set_new(ojData, "type", json_string("DO"));
   json_object_set_new(ojData, "name", json_string(digitalOutputStates[pin].name));
   json_object_set_new(ojData, "title", json_string(digitalOutputStates[pin].title.c_str()));
   // json_object_set_new(ojData, "unit", json_string(""));
   json_object_set_new(ojData, "mode", json_string(digitalOutputStates[pin].mode == omManual ? "manual" : "auto"));
   json_object_set_new(ojData, "options", json_integer(digitalOutputStates[pin].opt));
   json_object_set_new(ojData, "image", json_string(getImageOf(digitalOutputStates[pin].title.c_str(), digitalOutputStates[pin].state)));
   json_object_set_new(ojData, "widgettype", json_integer(wtSymbol));

   Poold::pushMessage(oJson, "update");
   webSock->performData(cWebSock::mtData);
}

bool Poold::gpioRead(uint pin)
{
   digitalInputStates[pin] = digitalRead(pin);
   return digitalInputStates[pin];
}
