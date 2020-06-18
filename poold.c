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
cMyMutex Poold::messagesInMutex;

std::map<std::string, Poold::ConfigItemDef> Poold::configuration
{
   // web

   { "refreshWeb",            { ctInteger, false, "3 WEB Interface", "Seite aktualisieren", "" } },
   { "addrsDashboard",        { ctString,  false, "3 WEB Interface", "Sensoren Dashboard", "Komma getrennte Liste aus ID:Typ siehe 'Aufzeichnung'" } },
   { "addrsMain",             { ctString,  false, "3 WEB Interface", "Sensoren", "Komma getrennte Liste aus ID:Typ siehe 'Aufzeichnung'" } },
   { "addrsMainMobile",       { ctString,  false, "3 WEB Interface", "Sensoren Mobile Device", "Komma getrennte Liste aus ID:Typ siehe 'Aufzeichnung'" } },

   { "chart1",                { ctString,  false, "3 WEB Interface", "Chart 1", "Komma getrennte Liste aus ID:Typ siehe 'Aufzeichnung'" } },
   { "chart2",                { ctString,  false, "3 WEB Interface", "Chart 2", "Komma getrennte Liste aus ID:Typ siehe 'Aufzeichnung'" } },
   { "chartDiv",              { ctInteger, true,  "3 WEB Interface", "Linien-Abstand der Y-Achse", "klein:15 mittel:25 groß:45" } },
   { "chartStart",            { ctInteger, false, "3 WEB Interface", "Chart Zeitraum (Tage)", "Standardzeitraum der Chartanzeige (seit x Tagen bis heute)" } },

   { "user",                  { ctString,  false, "3 WEB Interface", "User", "" } },
   { "passwd",                { ctString,  true,  "3 WEB Interface", "Passwort", "" } },

   // poold

   { "interval",              { ctInteger, false, "1 Pool Daemon", "Intervall der Aufzeichung", "Datenbank Aufzeichung [s]" } },

   { "filterPumpTimes",       { ctRange,   false, "1 Pool Daemon", "Zeiten Filter Pumpe", "[hh:mm] - [hh:mm]" } },
   { "uvcLightTimes",         { ctRange,   false, "1 Pool Daemon", "Zeiten UV-C Licht", "[hh:mm] - [hh:mm], wird nur angeschaltet wenn auch die Filterpumpe läuft!" } },
   { "poolLightTimes",        { ctRange,   false, "1 Pool Daemon", "Zeiten Pool Licht", "[hh:mm] - [hh:mm]" } },
   { "poolLightColorToggle",  { ctBool,    false, "1 Pool Daemon", "Pool Licht Farb-Toggel", "" } },

   { "tPoolMax",              { ctNum,     false, "1 Pool Daemon", "Pool max Temperatur", "" } },
   { "tSolarDelta",           { ctNum,     false, "1 Pool Daemon", "Einschaltdifferenz Solarpumpe", "" } },
   { "showerDuration",        { ctInteger, false, "1 Pool Daemon", "Laufzeit der Dusche", "Laufzeit [s]" } },

   { "invertDO",              { ctBool,    false, "1 Pool Daemon", "Digitalaugänge invertieren", "" } },
   { "w1AddrAir",             { ctString,  false, "1 Pool Daemon", "Adresse Fühler Temperatur Luft", "" } },
   { "w1AddrPool",            { ctString,  false, "1 Pool Daemon", "Adresse Fühler Temperatur Pool", "" } },
   { "w1AddrSolar",           { ctString,  false, "1 Pool Daemon", "Adresse Fühler Temperatur Kollektor", "" } },
   { "w1AddrSuctionTube",     { ctString,  false, "1 Pool Daemon", "Adresse Fühler Temperatur Saugleitung", "" } },

   { "aggregateHistory",      { ctInteger, false, "1 Pool Daemon", "Historie [Tage]", "history for aggregation in days (default 0 days -> aggegation turned OFF)" } },
   { "aggregateInterval",     { ctInteger, false, "1 Pool Daemon", "Intervall [m]", "aggregation interval in minutes - 'one sample per interval will be build'" } },

   { "hassMqttUrl",           { ctString,  false, "1 Pool Daemon", "Home Assistant MQTT Url", "Optional. Beispiel: 'tcp://127.0.0.1:1883'" } },

   // mail

   { "mail",                  { ctBool,    false, "4 Mail", "Mail Benachrichtigung", "Mail Benachrichtigungen aktivieren/deaktivieren" } },
   { "mailScript",            { ctString,  false, "4 Mail", "poold sendet Mails über das Skript", "" } },
   { "stateMailTo",           { ctString,  false, "4 Mail", "Status Mail Empfänger", "Komma separierte Empfängerliste" } },
   { "errorMailTo",           { ctString,  false, "4 Mail", "Fehler Mail Empfänger", "Komma separierte Empfängerliste" } },
   { "webUrl",                { ctString,  false, "4 Mail", "URL der Visualisierung", "kann mit %weburl% in die Mails eingefügt werden" } },

   // sonstiges

   { "WSLoginToken",          { ctString,  true,  "2 Sonstiges", "", "" } }
};

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
   "storeconfig",
   "syslog",
   "configdetails",
   "gettoken",
   "iosettings",
   "chartdata",
   "storeiosetup",
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
   nextAt = time(0) + 5;
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
// Push In Message (from WS to poold)
//***************************************************************************

int Poold::pushInMessage(const char* data)
{
   cMyMutexLock lock(&messagesInMutex);

   messagesIn.push(data);

   return success;
}

//***************************************************************************
// Push Out Message (from poold to WS)
//***************************************************************************

int Poold::pushOutMessage(json_t* oContents, const char* title, long client)
{
   json_t* obj = json_object();

   addToJson(obj, "event", title);
   json_object_set_new(obj, "object", oContents);

   char* p = json_dumps(obj, JSON_REAL_PRECISION(4));
   json_decref(obj);

   cWebSock::pushMessage(p, (lws*)client);

   tell(4, "DEBUG: PushMessage [%s]", p);
   free(p);

   webSock->performData(cWebSock::mtData);

   return done;
}

//***************************************************************************
// Dispatch Client Requests
//***************************************************************************

int Poold::dispatchClientRequest()
{
   int status = fail;
   json_error_t error;
   json_t *oData, *oObject;

   cMyMutexLock lock(&messagesInMutex);

   // #TODO loop here while (!messagesIn.empty()) ?

   if (messagesIn.empty())
      return done;

   // { "event" : "toggleio", "object" : { "address" : "122", "type" : "DO" } }

   tell(1, "DEBUG: Got '%s'", messagesIn.front().c_str());
   oData = json_loads(messagesIn.front().c_str(), 0, &error);

   // get the request

   Event event = cWebService::toEvent(getStringFromJson(oData, "event", "<null>"));
   long client = getLongFromJson(oData, "client");
   oObject = json_object_get(oData, "object");

   int addr = getIntFromJson(oObject, "address");
   const char* type = getStringFromJson(oObject, "type");

   // dispatch client request

   switch (event)
   {
      case evLogin:         status = performLogin(oObject);                  break;
      case evGetToken:      status = performTokenRequest(oObject, client);   break;
      case evToggleIo:      status = toggleIo(addr, type);                   break;
      case evToggleIoNext:  status = toggleIoNext(addr);                     break;
      case evToggleMode:    status = toggleOutputMode(addr);                 break;
      case evStoreConfig:   status = storeConfig(oObject, client);           break;
      case evSyslog:        status = performSyslog(oObject, client);         break;
      case evConfigDetails: status = performConfigDetails(oObject, client);  break;
      case evIoSettings:    status = performIoSettings(oObject, client);     break;
      case evChartData:     status = performChartData(oObject, client);      break;
      case evStoreIoSetup:  status = storeIoSetup(oObject, client);          break;

      default: tell(0, "Error: Received unexpected client request '%s' at [%s]",
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
   // Update/Read configuration from config table

   for (const auto& it : configuration)
   {
      tableConfig->clear();
      tableConfig->setValue("OWNER", "poold");
      tableConfig->setValue("NAME", it.first.c_str());
      tableConfig->find();
      tableConfig->store();
   }

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

   // ---------------------------------
   // apply some configuration specials

   applyConfigurationSpecials();

   // init web socket ...

   while (webSock->init(61109, webSocketPingTime) != success)
   {
      tell(0, "Retrying in 2 seconds");
      sleep(2);
   }

   initScripts();

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
// Init Scripts
//***************************************************************************

int Poold::initScripts()
{
   char* path {nullptr};
   int count {0};
   FileList scripts;

   asprintf(&path, "%s/scripts.d", confDir);
   int status = getFileList(path, DT_REG, "sh", false, &scripts, count);

   if (status == success)
   {
      for (const auto& script : scripts)
      {
         char* scriptPath {nullptr};
         uint id {0};

         asprintf(&scriptPath, "%s/%s", path, script.name.c_str());

         tableScripts->clear();
         tableScripts->setValue("PATH", scriptPath);

         if (!selectScriptByPath->find())
         {
            tableScripts->store();
            id = tableScripts->getLastInsertId();
         }
         else
         {
            id = tableScripts->getIntValue("ID");
         }

         selectScriptByPath->freeResult();

         addValueFact(id, "SC", script.name.c_str(), "");

         tell(0, "Found script '%s' id is (%d)", scriptPath, id);
         free(scriptPath);
      }
   }

   free(path);

   return status;
}

//***************************************************************************
// Call Script
//***************************************************************************

std::string Poold::callScript(int addr, const char* type, const char* command)
{
   char* cmd {nullptr};

   tableScripts->clear();
   tableScripts->setValue("ID", addr);

   if (!tableScripts->find())
   {
      tell(0, "Fatal: Script with id (%d) not found", addr);
      return "0";
   }

   asprintf(&cmd, "%s %s", tableScripts->getStrValue("PATH"), command);

   tableScripts->reset();

   tell(2, "Info: Calling '%s'", cmd);
   std::string result = executeCommand(cmd);
   tell(2, "Debug: Result of script '%s' was [%s]", cmd, result.c_str());
   free(cmd);

   if (!isEmpty(type))
      publishScriptResult(addr, type, result);

   return result;
}

//***************************************************************************
// Init/Exit Database
//***************************************************************************

cDbFieldDef xmlTimeDef("XML_TIME", "xmltime", cDBS::ffAscii, 20, cDBS::ftData);
cDbFieldDef rangeFromDef("RANGE_FROM", "rfrom", cDBS::ffDateTime, 0, cDBS::ftData);
cDbFieldDef rangeToDef("RANGE_TO", "rto", cDBS::ffDateTime, 0, cDBS::ftData);

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

   tableScripts = new cDbTable(connection, "scripts");
   if (tableScripts->open() != success) return fail;

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
   // select all config

   selectAllConfig = new cDbStatement(tableConfig);

   selectAllConfig->build("select ");
   selectAllConfig->bindAllOut();
   selectAllConfig->build(" from %s", tableConfig->TableName());
   // selectAllConfig->build(" order by ord");

   status += selectAllConfig->prepare();

   // ------------------
   // select max(time) from samples

   selectMaxTime = new cDbStatement(tableSamples);

   selectMaxTime->build("select ");
   selectMaxTime->bind("TIME", cDBS::bndOut, "max(");
   selectMaxTime->build(") from %s", tableSamples->TableName());

   status += selectMaxTime->prepare();

   // ------------------
   // select samples for chart data

   rangeFrom.setField(&rangeFromDef);
   rangeTo.setField(&rangeToDef);
   xmlTime.setField(&xmlTimeDef);

   selectSamplesRange = new cDbStatement(tableSamples);

   selectSamplesRange->build("select ");
   selectSamplesRange->bind("ADDRESS", cDBS::bndOut);
   selectSamplesRange->bind("TYPE", cDBS::bndOut, ", ");
   selectSamplesRange->bindTextFree("date_format(time, '%Y-%m-%dT%H:%i')", &xmlTime, ", ", cDBS::bndOut);
   selectSamplesRange->bind("VALUE", cDBS::bndOut, ", ");
   selectSamplesRange->build(" from %s where ", tableSamples->TableName());
   selectSamplesRange->bind("ADDRESS", cDBS::bndIn | cDBS::bndSet);
   selectSamplesRange->bind("TYPE", cDBS::bndIn | cDBS::bndSet, " and ");
   selectSamplesRange->bindCmp(0, "TIME", &rangeFrom, ">=", " and ");
   selectSamplesRange->bindCmp(0, "TIME", &rangeTo, "<=", " and ");
   selectSamplesRange->build(" group by date(time), ((60/15) * hour(time) + floor(minute(time)/15))");
   selectSamplesRange->build(" order by time");

   status += selectSamplesRange->prepare();

   // ------------------
   // select script by path

   selectScriptByPath = new cDbStatement(tableScripts);

   selectScriptByPath->build("select ");
   selectScriptByPath->bindAllOut();
   selectScriptByPath->build(" from %s where ", tableScripts->TableName());
   selectScriptByPath->bind("PATH", cDBS::bndIn | cDBS::bndSet);

   status += selectScriptByPath->prepare();


   // ------------------

   if (status == success)
      tell(eloAlways, "Connection to database established");

   return status;
}

int Poold::exitDb()
{
   delete tableSamples;            tableSamples = 0;
   delete tablePeaks;              tablePeaks = 0;
   delete tableValueFacts;         tableValueFacts = 0;
   delete tableConfig;             tableConfig = 0;

   delete selectActiveValueFacts;  selectActiveValueFacts = 0;
   delete selectAllValueFacts;     selectAllValueFacts = 0;
   delete selectAllConfig;         selectAllConfig = 0;
   delete selectMaxTime;           selectMaxTime = 0;
   delete selectSamplesRange;      selectSamplesRange = 0;
   delete connection;              connection = 0;

   return done;
}

//***************************************************************************
// Init one wire sensors
//***************************************************************************

int Poold::initW1()
{
   int count {0};
   int added {0};
   int modified {0};

   for (const auto& it : w1Sensors)
   {
      int res = addValueFact((int)toW1Id(it.first.c_str()), "W1", it.first.c_str(), "°C");

      if (res == 1)
         added++;
      else if (res == 2)
         modified++;

      count++;
   }

   tell(eloAlways, "Found %d one wire sensors, added %d, modified %d", count, added, modified);

   return success;
}

//***************************************************************************
// Read Configuration
//***************************************************************************

int Poold::readConfiguration()
{
   // init default web user and password

   getConfigItem("WSLoginToken", wsLoginToken, "dein secret login token");
   webSock->setLoginToken(wsLoginToken);

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
   getConfigItem("chart1", chart1, "");

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
   if (!initialized)
      return done;

   if (!connection || !connection->isConnected())
      return fail;

   if (mqttReader && mqttReader->isConnected()) mqttReader->yield();
   if (mqttWriter && mqttWriter->isConnected()) mqttWriter->yield();
   if (mqttW1Reader && mqttW1Reader->isConnected()) mqttW1Reader->yield();
   if (mqttCommandReader && mqttCommandReader->isConnected()) mqttCommandReader->yield();

   tell(2, "loop ...");
   webSock->service(10);
   dispatchClientRequest();
   webSock->performData(cWebSock::mtData);
   performWebSocketPing();
   performHassRequests();
   performJobs();

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
      // check db connection

      while (!doShutDown() && (!connection || !connection->isConnected()))
      {
         if (initDb() == success)
            break;

         exitDb();
         tell(eloAlways, "Retrying in 10 seconds");
         standby(10);
      }

      standbyUntil(nextAt);

      if (doShutDown())
         break;

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

      // mail

      if (mail)
         sendStateMail();

      initialRun = false;
   }

   return success;
}

//***************************************************************************
// Update
//***************************************************************************

int Poold::update(bool webOnly, long client)
{
   static size_t w1Count = w1Sensors.size();
   time_t now = time(0);
   int count {0};

   if (!webOnly)
   {
      if (w1Count < w1Sensors.size())
         initW1();
   }

   tell(eloDetail, "Update ...");

   tableValueFacts->clear();
   tableValueFacts->setValue("STATE", "A");

   if (!webOnly)
      connection->startTransaction();

   json_t* oJson = json_array();

   for (int f = selectActiveValueFacts->find(); f; f = selectActiveValueFacts->fetch())
   {
      char text[1000];

      int addr = tableValueFacts->getIntValue("ADDRESS");
      const char* type = tableValueFacts->getStrValue("TYPE");
      const char* title = tableValueFacts->getStrValue("TITLE");
      const char* usrtitle = tableValueFacts->getStrValue("USRTITLE");
      const char* unit = tableValueFacts->getStrValue("UNIT");
      const char* name = tableValueFacts->getStrValue("NAME");

      if (!isEmpty(usrtitle))
         title = usrtitle;

      json_t* ojData = json_object();
      json_array_append_new(oJson, ojData);

      sensor2Json(ojData, tableValueFacts);

      if (tableValueFacts->hasValue("TYPE", "W1"))
      {
         json_object_set_new(ojData, "value", json_real(valueOfW1(name)));
         json_object_set_new(ojData, "widgettype", json_integer(wtGauge));

         if (!webOnly)
            store(now, name, title, unit, type, addr, valueOfW1(name));
      }
      else if (tableValueFacts->hasValue("TYPE", "DO"))
      {
         json_object_set_new(ojData, "mode", json_string(digitalOutputStates[addr].mode == omManual ? "manual" : "auto"));
         json_object_set_new(ojData, "options", json_integer(digitalOutputStates[addr].opt));
         json_object_set_new(ojData, "image", json_string(getImageOf(title, digitalOutputStates[addr].state)));
         json_object_set_new(ojData, "value", json_integer(digitalOutputStates[addr].state));
         json_object_set_new(ojData, "last", json_integer(digitalOutputStates[addr].last));
         json_object_set_new(ojData, "next", json_integer(digitalOutputStates[addr].next));
         json_object_set_new(ojData, "widgettype", json_integer(wtSymbol));

         if (!webOnly)
            store(now, name, title, unit, type, addr, digitalOutputStates[addr].state);
      }
      else if (tableValueFacts->hasValue("TYPE", "SC"))
      {
         std::string result = callScript(addr, 0, "status").c_str();

         auto pos = result.find(':');

         if (pos == std::string::npos)
         {
            tell(0, "Error: Failed to parse result of script '%s' [%s]", title, result.c_str());
            continue;
         }

         std::string kind = result.substr(0, pos);
         std::string value = result.substr(pos+1);

         if (kind == "status")
         {
            bool state = atoi(value.c_str());
            json_object_set_new(ojData, "value", json_integer(state));
            json_object_set_new(ojData, "image", json_string(getImageOf(title, state)));
            json_object_set_new(ojData, "widgettype", json_integer(wtSymbol));
         }
         else if (kind == "value")
         {
            json_object_set_new(ojData, "value", json_real(strtod(value.c_str(), nullptr)));
            json_object_set_new(ojData, "widgettype", json_integer(wtGauge));
         }
         else
            tell(0, "Got unexpected script kind '%s' in '%s'", kind.c_str(), result.c_str());

         if (!webOnly)
            store(now, name, title, unit, type, addr, strtod(value.c_str(), nullptr));
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

               if (!webOnly)
                  store(now, name, title, unit, type, addr, waterLevel, text);

               json_object_set_new(ojData, "text", json_string(text));
               json_object_set_new(ojData, "widgettype", json_integer(wtText));
            }
            else
            {
               json_object_set_new(ojData, "value", json_integer(waterLevel));
               json_object_set_new(ojData, "widgettype", json_integer(wtGauge));

               if (!webOnly)
                  store(now, name, title, unit, type, addr, waterLevel);
            }
         }
         else if (addr == 2)
         {
            tPool = valueOfW1(w1AddrPool);
            tSolar = valueOfW1(w1AddrSolar);
            tCurrentDelta = tSolar - tPool;

            json_object_set_new(ojData, "value", json_real(tCurrentDelta));
            json_object_set_new(ojData, "widgettype", json_integer(wtGauge));

            if (!webOnly)
               store(now, name, title, unit, type, addr, tCurrentDelta);
         }
      }

      count++;
   }

   selectActiveValueFacts->freeResult();

   if (!webOnly)
      connection->commit();

   // send result to all connected WEBIF clients

   pushOutMessage(oJson, webOnly ? "init" : "all", client);

   // ?? json_decref(oJson);

   if (!webOnly)
      tell(eloAlways, "Updated %d samples", count);

   return success;
}

//***************************************************************************
// Poold WS Ping
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
// Perform WS Client Login
//***************************************************************************

int Poold::performLogin(json_t* oObject)
{
   int type = getIntFromJson(oObject, "type", na);
   long client = getLongFromJson(oObject, "client");

   tell(0, "Login of client 0x%x; type is %d", (unsigned int)client, type);

   json_t* oJson = json_object();
   config2Json(oJson);
   pushOutMessage(oJson, "config", client);

   oJson = json_object();
   daemonState2Json(oJson);
   pushOutMessage(oJson, "daemonstate", client);

   update(true, client);

   return done;
}

//***************************************************************************
// Perform WS Token Request
//***************************************************************************

int Poold::performTokenRequest(json_t* oObject, long client)
{
   char* webUser {nullptr};
   char* webPass {nullptr};
   md5Buf defaultPwd;

   const char* user = getStringFromJson(oObject, "user", "");
   const char* passwd  = getStringFromJson(oObject, "password", "");

   tell(0, "Token request of client 0x%x for user '%s'", (unsigned int)client, user);

   createMd5("pool", defaultPwd);
   getConfigItem("user", webUser, "pool");
   getConfigItem("passwd", webPass, defaultPwd);

   json_t* oJson = json_object();

   if (strcmp(webUser, user) == 0 && strcmp(passwd, webPass) == 0)
   {
      tell(0, "Token request for user '%s' succeeded", user);
      json_object_set_new(oJson, "value", json_string(wsLoginToken));
      pushOutMessage(oJson, "token", client);
   }
   else
   {
      tell(0, "Token request for user '%s' failed, wrong user/password", user);
   }

   free(webPass);
   free(webUser);

   return done;
}

//***************************************************************************
// Perform WS Syslog Request
//***************************************************************************

int Poold::performSyslog(json_t* oObject, long client)
{
   if (client <= 0)
      return done;

   json_t* oJson = json_object();
   const char* name = "/var/log/syslog";
   std::vector<std::string> lines;
   std::string result;

   if (loadLinesFromFile(name, lines, false) == success)
   {
      const int maxLines {150};
      int count {0};

      for (auto it = lines.rbegin(); it != lines.rend(); ++it)
      {
         if (count++ >= maxLines)
         {
            result += "...\n...\n";
            break;
         }

         result += *it;
      }
   }

   json_object_set_new(oJson, "lines", json_string(result.c_str()));
   pushOutMessage(oJson, "syslog", client);

   return done;
}

//***************************************************************************
// Perform WS Config Data Request
//***************************************************************************

int Poold::performConfigDetails(json_t* oObject, long client)
{
   if (client <= 0)
      return done;

   json_t* oJson = json_array();
   configDetails2Json(oJson);
   pushOutMessage(oJson, "configdetails", client);

   return done;
}

//***************************************************************************
// Perform WS IO Setting Data Request
//***************************************************************************

int Poold::performIoSettings(json_t* oObject, long client)
{
   if (client <= 0)
      return done;

   json_t* oJson = json_array();
   valueFacts2Json(oJson);
   pushOutMessage(oJson, "valuefacts", client);

   return done;
}

//***************************************************************************
// Perform WS ChartData request
//***************************************************************************

int Poold::performChartData(json_t* oObject, long client)
{
   if (client <= 0)
      return done;

   int range = getIntFromJson(oObject, "range", 3);
   const char* sensors = getStringFromJson(oObject, "sensors");

   if (isEmpty(sensors))
      sensors = chart1;

   tell(eloAlways, "Selecting chats data");

   json_t* oJson = json_array();

   rangeFrom.setValue(time(0) - (range*tmeSecondsPerDay));
   rangeTo.setValue(time(0));

   tableValueFacts->clear();
   tableValueFacts->setValue("STATE", "A");

   for (int f = selectActiveValueFacts->find(); f; f = selectActiveValueFacts->fetch())
   {
      char* id {nullptr};
      asprintf(&id, "%s:0x%lx", tableValueFacts->getStrValue("TYPE"), tableValueFacts->getIntValue("ADDRESS"));

      if (!strstr(sensors, id))
      {
         free(id);
         continue;
      }

      tell(0, " ...collecting data for '%s'", id);
      free(id);

      const char* usrtitle = tableValueFacts->getStrValue("USRTITLE");
      const char* title = tableValueFacts->getStrValue("TITLE");

      if (!isEmpty(usrtitle))
         title = usrtitle;

      json_t* oSample = json_object();
      json_array_append_new(oJson, oSample);

      json_object_set_new(oSample, "title", json_string(title));
      json_t* oData = json_array();
      json_object_set_new(oSample, "data", oData);

      tableSamples->clear();
      tableSamples->setValue("TYPE", tableValueFacts->getStrValue("TYPE"));
      tableSamples->setValue("ADDRESS", tableValueFacts->getIntValue("ADDRESS"));

      for (int f = selectSamplesRange->find(); f; f = selectSamplesRange->fetch())
      {
         // tell(eloAlways, "0x%x: '%s' : %0.2f", (uint)tableSamples->getStrValue("ADDRESS"),
         //      xmlTime.getStrValue(), tableSamples->getFloatValue("VALUE"));

         json_t* oRow = json_object();
         json_array_append_new(oData, oRow);

         json_object_set_new(oRow, "x", json_string(xmlTime.getStrValue()));
         json_object_set_new(oRow, "y", json_real(tableSamples->getFloatValue("VALUE")));
      }

      selectSamplesRange->freeResult();
   }

   selectActiveValueFacts->freeResult();
   tell(eloAlways, "... done");
   pushOutMessage(oJson, "chartdata", client);

   return done;
}

//***************************************************************************
// Store Configuration
//***************************************************************************

int Poold::storeConfig(json_t* obj, long client)
{
   const char* key;
   json_t* jValue;

   json_object_foreach(obj, key, jValue)
   {
      tell(3, "Debug: Storing config item '%s' with '%s'", key, json_string_value(jValue));
      setConfigItem(key, json_string_value(jValue));
   }

   readConfiguration();

   return done;
}

int Poold::storeIoSetup(json_t* array, long client)
{
   size_t index;
   json_t* jObj;

   json_array_foreach(array, index, jObj)
   {
      int addr = getIntFromJson(jObj, "address");
      const char* type = getStringFromJson(jObj, "type");
      int state = getIntFromJson(jObj, "state");
      const char* usrTitle = getStringFromJson(jObj, "usrtitle", "");
      int maxScale = getIntFromJson(jObj, "scalemax");

      tableValueFacts->clear();
      tableValueFacts->setValue("ADDRESS", addr);
      tableValueFacts->setValue("TYPE", type);

      if (!tableValueFacts->find())
         continue;

      tableValueFacts->clearChanged();
      tableValueFacts->setValue("STATE", state ? "A" : "D");
      tableValueFacts->setValue("USRTITLE", usrTitle);

      if (maxScale >= 0)
         tableValueFacts->setValue("MAXSCALE", maxScale);

      if (tableValueFacts->getChanges())
      {
         tableValueFacts->store();
         tell(2, "STORED %s:%d - usrtitle: '%s'; scalemax: %d; state: %d", type, addr, usrTitle, maxScale, state);
      }

      tell(3, "Debug: %s:%d - usrtitle: '%s'; scalemax: %d; state: %d", type, addr, usrTitle, maxScale, state);
   }

   return done;
}

//***************************************************************************
// Config 2 Json
//***************************************************************************

int Poold::config2Json(json_t* obj)
{
   for (int f = selectAllConfig->find(); f; f = selectAllConfig->fetch())
   {
      ConfigItemDef def = configuration[tableConfig->getStrValue("NAME")];

      if (def.internal)
         continue;

      json_object_set_new(obj, tableConfig->getStrValue("NAME"),
                          json_string(tableConfig->getStrValue("VALUE")));
   }

   selectAllConfig->freeResult();

   return done;
}

//***************************************************************************
// Config 2 Json
//***************************************************************************

int Poold::configDetails2Json(json_t* obj)
{
   for (int f = selectAllConfig->find(); f; f = selectAllConfig->fetch())
   {
      ConfigItemDef def = configuration[tableConfig->getStrValue("NAME")];

      if (def.internal)
         continue;

      json_t* oDetail = json_object();
      json_array_append_new(obj, oDetail);

      json_object_set_new(oDetail, "name", json_string(tableConfig->getStrValue("NAME")));
      json_object_set_new(oDetail, "type", json_integer(def.type));
      json_object_set_new(oDetail, "value", json_string(tableConfig->getStrValue("VALUE")));
      json_object_set_new(oDetail, "category", json_string(def.category));
      json_object_set_new(oDetail, "title", json_string(def.title));
      json_object_set_new(oDetail, "descrtiption", json_string(def.description));
   }

   selectAllConfig->freeResult();

   return done;
}

//***************************************************************************
// Value Facts 2 Json
//***************************************************************************

int Poold::valueFacts2Json(json_t* obj)
{
   tableValueFacts->clear();

   for (int f = selectAllValueFacts->find(); f; f = selectAllValueFacts->fetch())
   {
      json_t* oData = json_object();
      json_array_append_new(obj, oData);

      json_object_set_new(oData, "address", json_integer(tableValueFacts->getIntValue("ADDRESS")));
      json_object_set_new(oData, "type", json_string(tableValueFacts->getStrValue("TYPE")));
      json_object_set_new(oData, "state", json_integer(tableValueFacts->hasValue("STATE", "A")));
      json_object_set_new(oData, "name", json_string(tableValueFacts->getStrValue("NAME")));
      json_object_set_new(oData, "title", json_string(tableValueFacts->getStrValue("TITLE")));
      json_object_set_new(oData, "usrtitle", json_string(tableValueFacts->getStrValue("USRTITLE")));
      json_object_set_new(oData, "unit", json_string(tableValueFacts->getStrValue("UNIT")));
      json_object_set_new(oData, "scalemax", json_integer(tableValueFacts->getIntValue("MAXSCALE")));
   }

   selectAllValueFacts->freeResult();

   return done;
}

//***************************************************************************
// Daemon Status 2 Json
//***************************************************************************

int Poold::daemonState2Json(json_t* obj)
{
   double averages[3] {0.0, 0.0, 0.0};
   char d[100];

   toElapsed(time(0)-startedAt, d);
   getloadavg(averages, 3);

   json_object_set_new(obj, "state", json_integer(success));
   json_object_set_new(obj, "version", json_string(VERSION));
   json_object_set_new(obj, "runningsince", json_string(d));
   json_object_set_new(obj, "average0", json_real(averages[0]));
   json_object_set_new(obj, "average1", json_real(averages[1]));
   json_object_set_new(obj, "average2", json_real(averages[2]));

   return done;
}

//***************************************************************************
// Sensor 2 Json
//***************************************************************************

int Poold::sensor2Json(json_t* obj, cDbTable* table)
{
   double peak {0.0};

   tablePeaks->clear();
   tablePeaks->setValue("ADDRESS", table->getIntValue("ADDRESS"));
   tablePeaks->setValue("TYPE", table->getStrValue("TYPE"));

   if (tablePeaks->find())
      peak = tablePeaks->getFloatValue("MAX");

   tablePeaks->reset();

   json_object_set_new(obj, "address", json_integer(table->getIntValue("ADDRESS")));
   json_object_set_new(obj, "type", json_string(table->getStrValue("TYPE")));
   json_object_set_new(obj, "name", json_string(table->getStrValue("NAME")));

   if (!table->getValue("USRTITLE")->isEmpty())
      json_object_set_new(obj, "title", json_string(table->getStrValue("USRTITLE")));
   else
      json_object_set_new(obj, "title", json_string(table->getStrValue("TITLE")));

   json_object_set_new(obj, "unit", json_string(table->getStrValue("UNIT")));
   json_object_set_new(obj, "scalemax", json_integer(table->getIntValue("MAXSCALE")));
   json_object_set_new(obj, "peak", json_real(peak));

   return done;
}

//***************************************************************************
// Process
//***************************************************************************

int Poold::process()
{
   tPool = valueOfW1(w1AddrPool);
   tSolar = valueOfW1(w1AddrSolar);
   tCurrentDelta = tSolar - tPool;

   tell(0, "Process ...");

   // -----------
   // Solar Pump

   if (digitalOutputStates[pinSolarPump].mode == omAuto)
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
      tell(0, "Add ValueFact '%ld' '%s'",
           tableValueFacts->getIntValue("ADDRESS"), tableValueFacts->getStrValue("TYPE"));

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
// getImageOf
//***************************************************************************

const char* Poold::getImageOf(const char* title, int value)
{
   const char* imagePath = "unknown.jpg";

   if (strcasestr(title, "Pump"))
      imagePath = value ? "img/icon/pump-on.gif" : "img/icon/pump-off.png";
   else if (strcasestr(title, "Steckdose"))
      imagePath = value ? "img/icon/plug-on.png" : "img/icon/plug-off.png";
   else if (strcasestr(title, "UV-C"))
      imagePath = value ? "img/icon/uvc-on.png" : "img/icon/uvc-off.png";
   else if (strcasestr(title, "Licht"))
      imagePath = value ? "img/icon/light-on.png" : "img/icon/light-off.png";
   else if (strcasestr(title, "Shower") || strcasestr(title, "Dusche"))
      imagePath = value ? "img/icon/shower-on.png" : "img/icon/shower-off.png";
   else
      imagePath = value ? "img/icon/boolean-on.png" : "img/icon/boolean-off.png";

   return imagePath;
}

//***************************************************************************
// Digital IO Stuff
//***************************************************************************

int Poold::toggleIo(uint addr, const char* type)
{
   if (strcmp(type, "DO") == 0)
      gpioWrite(addr, !digitalOutputStates[addr].state);
   else if (strcmp(type, "SC") == 0)
      callScript(addr, type, "toggle");

   return success;
}

int Poold::publishScriptResult(uint addr, const char* type, std::string result)
{
   auto pos = result.find(':');

   if (pos == std::string::npos)
   {
      tell(0, "Error: Failed to parse result of script (%d) [%s]", addr, result.c_str());
      return fail;
   }

   std::string kind = result.substr(0, pos);
   std::string value = result.substr(pos+1);

   tableValueFacts->clear();
   tableValueFacts->setValue("ADDRESS", (int)addr);
   tableValueFacts->setValue("TYPE", type);

   if (!tableValueFacts->find())
      return fail;

   const char* name = tableValueFacts->getStrValue("NAME");
   const char* title = tableValueFacts->getStrValue("TITLE");
   const char* usrtitle = tableValueFacts->getStrValue("USRTITLE");

   if (!isEmpty(usrtitle))
      title = usrtitle;

   // update WS
   {
      json_t* oJson = json_array();
      json_t* ojData = json_object();
      json_array_append_new(oJson, ojData);

      json_object_set_new(ojData, "address", json_integer(addr));
      json_object_set_new(ojData, "type", json_string(type));
      json_object_set_new(ojData, "name", json_string(name));
      json_object_set_new(ojData, "title", json_string(title));

      if (kind == "status")
      {
         bool state = atoi(value.c_str());
         json_object_set_new(ojData, "value", json_integer(state));
         json_object_set_new(ojData, "image", json_string(getImageOf(title, state)));
         json_object_set_new(ojData, "widgettype", json_integer(wtSymbol));
      }
      else if (kind == "value")
      {
         json_object_set_new(ojData, "value", json_real(strtod(value.c_str(), nullptr)));
         json_object_set_new(ojData, "widgettype", json_integer(wtGauge));
      }

      pushOutMessage(oJson, "update");
   }

   if (!isEmpty(hassMqttUrl))
      hassPush(iotLight, name, "", "", strtod(value.c_str(), nullptr), "", false /*forceConfig*/);

   tableValueFacts->reset();

   return success;
}

int Poold::toggleIoNext(uint pin)
{
   if (digitalOutputStates[pin].state)
   {
      toggleIo(pin, "DO");
      usleep(300000);
      toggleIo(pin, "DO");
      return success;
   }

   gpioWrite(pin, true);

   return success;
}

void Poold::pin2Json(json_t* ojData, int pin)
{
   json_object_set_new(ojData, "address", json_integer(pin));
   json_object_set_new(ojData, "type", json_string("DO"));
   json_object_set_new(ojData, "name", json_string(digitalOutputStates[pin].name));
   json_object_set_new(ojData, "title", json_string(digitalOutputStates[pin].title.c_str()));
   // json_object_set_new(ojData, "unit", json_string(""));
   json_object_set_new(ojData, "mode", json_string(digitalOutputStates[pin].mode == omManual ? "manual" : "auto"));
   json_object_set_new(ojData, "options", json_integer(digitalOutputStates[pin].opt));
   json_object_set_new(ojData, "value", json_integer(digitalOutputStates[pin].state));
   json_object_set_new(ojData, "last", json_integer(digitalOutputStates[pin].last));
   json_object_set_new(ojData, "next", json_integer(digitalOutputStates[pin].next));
   json_object_set_new(ojData, "image", json_string(getImageOf(digitalOutputStates[pin].title.c_str(), digitalOutputStates[pin].state)));
   json_object_set_new(ojData, "widgettype", json_integer(wtSymbol));
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
      pin2Json(ojData, pin);

      pushOutMessage(oJson, "update");
   }

   return success;
}

void Poold::gpioWrite(uint pin, bool state, bool callJobs)
{
   digitalOutputStates[pin].state = state;
   digitalOutputStates[pin].last = time(0);

   if (!state)
      digitalOutputStates[pin].next = 0;

   // invert the state on 'invertDO' - some relay board are active at 'false'

   digitalWrite(pin, invertDO ? !state : state);
   performJobs();

   // update WS
   {
      json_t* oJson = json_array();
      json_t* ojData = json_object();
      json_array_append_new(oJson, ojData);
      pin2Json(ojData, pin);

      pushOutMessage(oJson, "update");
   }

   if (!isEmpty(hassMqttUrl))
      hassPush(iotLight, digitalOutputStates[pin].name, "", "", digitalOutputStates[pin].state, "", false /*forceConfig*/);
}

bool Poold::gpioRead(uint pin)
{
   digitalInputStates[pin] = digitalRead(pin);
   return digitalInputStates[pin];
}

//***************************************************************************
// W1 Sensor Stuff ...
//***************************************************************************

void Poold::updateW1(const char* id, double value)
{
   tell(2, "w1: %s : %0.2f", id, value);

   w1Sensors[id] = value;

   json_t* oJson = json_array();
   json_t* ojData = json_object();
   json_array_append_new(oJson, ojData);

   tableValueFacts->clear();
   tableValueFacts->setValue("ADDRESS", (int)toW1Id(id));
   tableValueFacts->setValue("TYPE", "W1");

   if (tableValueFacts->find())
   {
      sensor2Json(ojData, tableValueFacts);
      json_object_set_new(ojData, "value", json_real(value));
      json_object_set_new(ojData, "widgettype", json_integer(wtGauge));

      pushOutMessage(oJson, "update");
   }

   tableValueFacts->reset();
}

bool Poold::existW1(const char* id)
{
   if (isEmpty(id))
      return false;

   auto it = w1Sensors.find(id);

   return it != w1Sensors.end();
}

double Poold::valueOfW1(const char* id)
{
   if (isEmpty(id))
      return 0;

   auto it = w1Sensors.find(id);

   if (it == w1Sensors.end())
      return 0;

   return w1Sensors[id];
}

uint Poold::toW1Id(const char* name)
{
   const char* p;
   int len = strlen(name);

   // use 4 minor bytes as id

   if (len <= 2)
      return na;

   if (len <= 8)
      p = name;
   else
      p = name + (len - 8);

   return strtoull(p, 0, 16);
}
