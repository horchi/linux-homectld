//***************************************************************************
// poold / Linux - Pool Steering
// File poold.c
// This code is distributed under the terms and conditions of the
// GNU GENERAL PUBLIC LICENSE. See the file LICENSE for details.
// Date 16.04.2021 - Jörg Wendel
//***************************************************************************

#include <stdio.h>
#include <unistd.h>
#include <dirent.h>
#include <libxml/parser.h>
#include <algorithm>
#include <cmath>

#include <wiringPi.h>

#include "lib/json.h"
#include "poold.h"

bool Poold::shutdown {false};

//***************************************************************************
// Configuration Items
//***************************************************************************

std::list<Poold::ConfigItemDef> Poold::configuration
{
   // poold

   { "interval",                  ctInteger, "60",           false, "1 Pool Daemon", "Intervall der Aufzeichung", "Datenbank Aufzeichung [s]" },
   { "webPort",                   ctInteger, "61109",        false, "1 Pool Daemon", "Port des Web Interfaces", "" },
   { "loglevel",                  ctInteger, "1",            false, "1 Pool Daemon", "Log level", "" },
   { "filterPumpTimes",           ctRange,   "10:00-17:00",  false, "1 Pool Daemon", "Zeiten Filter Pumpe", "[hh:mm] - [hh:mm]" },
   { "uvcLightTimes",             ctRange,   "",             false, "1 Pool Daemon", "Zeiten UV-C Licht", "[hh:mm] - [hh:mm], wird nur angeschaltet wenn auch die Filterpumpe läuft!" },
   { "poolLightTimes",            ctRange,   "",             false, "1 Pool Daemon", "Zeiten Pool Licht", "[hh:mm] - [hh:mm] (ansonsten manuell schalten)" },
   { "poolLightColorToggle",      ctBool,    "0",            false, "1 Pool Daemon", "Pool Licht Farb-Toggel", "" },

   { "alertSwitchOffPressure",    ctInteger, "0",            false, "1 Pool Daemon", "Trockenlaufschutz unter x bar", "Deaktiviert Pumpen nach 5 Minuten (0 deaktiviert)" },

   { "tPoolMax",                  ctNum,     "28.0",         false, "1 Pool Daemon", "Pool max Temperatur", "" },
   { "tSolarDelta",               ctNum,     "5.0",          false, "1 Pool Daemon", "Einschaltdifferenz Solarpumpe", "" },
   { "showerDuration",            ctInteger, "20",           false, "1 Pool Daemon", "Laufzeit der Dusche", "Laufzeit [s]" },
   { "minSolarPumpDuration",      ctInteger, "10",           false, "1 Pool Daemon", "Mindestlaufzeit der Solarpumpe [m]", "" },
   { "deactivatePumpsAtLowWater", ctBool,    "0",            false, "1 Pool Daemon", "Pumpen bei geringem Wasserstand deaktivieren", "" },

   { "invertDO",                  ctBool,    "1",            false, "1 Pool Daemon", "Digitalaugänge invertieren", "" },
   { "w1AddrAir",                 ctString,  "",             false, "1 Pool Daemon", "Adresse Fühler Temperatur Luft", "" },
   { "w1AddrPool",                ctString,  "",             false, "1 Pool Daemon", "Adresse Fühler Temperatur Pool", "" },
   { "w1AddrSolar",               ctString,  "",             false, "1 Pool Daemon", "Adresse Fühler Temperatur Kollektor", "" },
   { "w1AddrSuctionTube",         ctString,  "",             false, "1 Pool Daemon", "Adresse Fühler Temperatur Saugleitung", "" },
   { "poolMqttUrl",               ctString,  "tcp://localhost:1883",  false, "1 Pool Daemon", "Url des MQTT Message Broker für den Pool Daemon", "Wird zur Kommunikation mit dem one-wire Interface Service (w1mqtt) sowie dem Arduino verwendet. Beispiel: 'tcp://localhost:1883'" },

   { "aggregateHistory",          ctInteger, "0",            false, "1 Pool Daemon", "Historie [Tage]", "history for aggregation in days (default 0 days -> aggegation turned OFF)" },
   { "aggregateInterval",         ctInteger, "15",           false, "1 Pool Daemon", "Intervall [m]", "aggregation interval in minutes - 'one sample per interval will be build'" },
   { "peakResetAt",               ctString,  "",             true,  "1 Pool Daemon", "", "" },

   { "massPerSecond",             ctNum,     "11.0",         false, "1 Pool Daemon", "Durchfluss Solar", "[Liter/min]" },

   // PH stuff

   { "phInterval",                ctInteger, "15",           false, "2 PH", "Intervall der PH Messung", "[s]" },
   { "phReference",               ctNum,     "7.2",          false, "2 PH", "PH Sollwert", "Sollwert [PH] (default 7,2)" },
   { "phMinusDensity",            ctNum,     "1.4",          false, "2 PH", "Dichte PH Minus [kg/l]", "Wie viel kg wiegt ein Liter PH Minus (default 1,4)" },
   { "phMinusDemand01",           ctInteger, "85",           false, "2 PH", "Menge zum Senken um 0,1 [g]", "Wie viel Gramm PH Minus wird zum Senken des PH Wertes um 0,1 für das vorhandene Pool Volumen benötigt (default 60g)" },
   { "phMinusDayLimit",           ctInteger, "100",          false, "2 PH", "Obergrenze PH Minus/Tag [ml]", "Wie viel PH Minus wird pro Tag maximal zugegeben [ml] (default 100ml)" },
   { "phPumpDurationPer100",      ctInteger, "1000",         false, "2 PH", "Laufzeit Dosierpumpe/100ml [ms]", "Welche Zeit in Millisekunden benötigt die Dosierpumpe um 100ml zu fördern (default 1000ms)" },

   // { "phCalibratePointA",         ctNum,     "7.0",          false, "2 PH", "Kallibrier-Punkt A", "" },
   // { "phCalibratePointValueA",    ctInteger, "377",          false, "2 PH", "Wert (digits) am Kallibrier-Punkt A", "" },
   // { "phCalibratePointB",         ctNum,     "9.0",          false, "2 PH", "Kallibrier-Punkt B", "" },
   // { "phCalibratePointValueB",    ctInteger, "412",          false, "2 PH", "Wert (digits) am Kallibrier-Punkt B", "" },

   // web

   { "addrsDashboard",            ctString,  "",             false, "3 WEB Interface", "Sensoren 'Dashboard'", "Komma getrennte Liste aus ID:Typ siehe 'IO Setup'" },
   { "addrsList",                 ctString,  "",             false, "3 WEB Interface", "Sensoren 'Liste'", "Komma getrennte Liste aus ID:Typ siehe 'IO Setup'" },
   { "style",                     ctChoice,  "dark",         false, "3 WEB Interface", "Farbschema", "" },
   { "vdr",                       ctBool,    "0",            false, "3 WEB Interface", "VDR (Video Disk Recorder) OSD verfügbar", "" },

   // mqtt interface

   { "mqttUrl",                   ctString,  "",             false, "4 MQTT Interface", "Url des MQTT Message Broker", "Deiser kann z.B. zur Kommunikation mit Hausautomatisierungen verwendet werden. Beispiel: 'tcp://localhost:1883'" },
   { "mqttUser",                  ctString,  "",             false, "4 MQTT Interface", "User", "" },
   { "mqttPassword",              ctString,  "",             false, "4 MQTT Interface", "Password", "" },

   // mail

   { "mail",                      ctBool,    "0",                       false, "5 Mail", "Mail Benachrichtigung", "Mail Benachrichtigungen aktivieren/deaktivieren" },
   { "mailScript",                ctString,  BIN_PATH "/poold-mail.sh", false, "5 Mail", "poold sendet Mails über das Skript", "" },
   { "stateMailTo",               ctString,  "",                        false, "5 Mail", "Status Mail Empfänger", "Komma getrennte Empfängerliste" },
   { "errorMailTo",               ctString,  "",                        false, "5 Mail", "Fehler Mail Empfänger", "Komma getrennte Empfängerliste" },
   { "webUrl",                    ctString,  "",                        false, "5 Mail", "URL der Visualisierung", "kann mit %weburl% in die Mails eingefügt werden" },
};

//***************************************************************************
// Service
//***************************************************************************

const char* Poold::widgetTypes[] =
{
   "Symbol",
   "Chart",
   "Text",
   "Value",
   "Gauge",
   "Meter",
   "MeterLevel",
   "PlainText",
   0
};

const char* Poold::toName(WidgetType type)
{
   if (type > wtUnknown && type < wtCount)
      return widgetTypes[type];

   return widgetTypes[wtText];
}

Poold::WidgetType Poold::toType(const char* name)
{
   if (!name)
      return wtUnknown;

   for (int t = wtUnknown+1; t < wtCount; t++)
      if (strcasecmp(name, widgetTypes[t]) == 0)
         return (WidgetType)t;

   return wtText;
}


//***************************************************************************
// Object
//***************************************************************************

Poold::Poold()
{
   nextRefreshAt = time(0) + 5;
   startedAt = time(0);

   cDbConnection::init();
   cDbConnection::setEncoding("utf8");
   cDbConnection::setHost(dbHost);
   cDbConnection::setPort(dbPort);
   cDbConnection::setName(dbName);
   cDbConnection::setUser(dbUser);
   cDbConnection::setPass(dbPass);

   webSock = new cWebSock(this, httpPath);
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

   //arduinoInterface.close();
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

   if (!p)
   {
      tell(0, "Error: Dumping json message failed");
      return fail;
   }

   webSock->pushOutMessage(p, (lws*)client);
   free(p);

   webSock->performData(cWebSock::mtData);

   return done;
}

int Poold::pushDataUpdate(const char* title, long client)
{
   // push all in the jsonSensorList to the 'interested' clients

   if (client)
   {
      auto cl = wsClients[(void*)client];
      json_t* oWsJson = json_array();

      if (cl.page == "dashboard")
      {
         if (addrsDashboard.size())
         {
            for (const auto sensor : addrsDashboard)
               json_array_append(oWsJson, jsonSensorList[sensor]);
         }
         else
            for (auto sj : jsonSensorList)
               json_array_append(oWsJson, sj.second);

         pushOutMessage(oWsJson, title, client);
      }
      else if (cl.page == "list")
      {
         if (addrsList.size())
            for (const auto sensor : addrsList)
               json_array_append(oWsJson, jsonSensorList[sensor]);
         else
            for (auto sj : jsonSensorList)
               json_array_append(oWsJson, sj.second);

         pushOutMessage(oWsJson, title, client);
      }
   }
   else
   {
      for (const auto cl : wsClients)
      {
         json_t* oWsJson = json_array();

         if (cl.second.page == "dashboard")
         {
            if (addrsDashboard.size())
            {
               for (const auto sensor : addrsDashboard)
                  json_array_append(oWsJson, jsonSensorList[sensor]);
            }
            else
               for (auto sj : jsonSensorList)
                  json_array_append(oWsJson, sj.second);
         }
         else if (cl.second.page == "list")
         {
            if (addrsList.size())
               for (const auto sensor : addrsList)
                  json_array_append(oWsJson, jsonSensorList[sensor]);
            else
               for (auto sj : jsonSensorList)
                  json_array_append(oWsJson, sj.second);
         }
         else
            continue;

         pushOutMessage(oWsJson, title, (long)cl.first);
      }
   }

   // cleanup

   for (auto sj : jsonSensorList)
      json_decref(sj.second);

   jsonSensorList.clear();

   return success;
}

volatile int showerSwitch {0};

//***************************************************************************
// IO Interrupt Handler
//***************************************************************************

void ioInterrupt()
{
   static uint64_t lastShowerSwitch = cTimeMs::Now();

   // detect only once a second

   if (cTimeMs::Now() > lastShowerSwitch + 1000 && !digitalRead(Poold::pinShowerSwitch))
   {
      tell(2, "Info: Shower key detected");
      showerSwitch++;
      lastShowerSwitch = cTimeMs::Now();
   }
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
   // check users - add default user if empty

   int userCount {0};
   tableUsers->countWhere("", userCount);

   if (userCount <= 0)
   {
      tell(0, "Initially adding default user (pool/pool)");

      md5Buf defaultPwd;
      createMd5("pool", defaultPwd);
      tableUsers->clear();
      tableUsers->setValue("USER", "pool");
      tableUsers->setValue("PASSWD", defaultPwd);
      tableUsers->setValue("TOKEN", "dein&&secret12login34token");
      tableUsers->setValue("RIGHTS", 0xff);  // all rights
      tableUsers->store();
   }

   // ---------------------------------
   // Update/Read configuration from config table

   for (const auto& it : configuration)
   {
      tableConfig->clear();
      tableConfig->setValue("OWNER", myName());
      tableConfig->setValue("NAME", it.name.c_str());

      if (!tableConfig->find())
      {
         tableConfig->setValue("VALUE", it.def);
         tableConfig->store();
      }
   }

   readConfiguration(true);
   initLocale();

   // ---------------------------------
   // setup GPIO

   wiringPiSetupPhys();     // we use the 'physical' PIN numbers
   // wiringPiSetup();      // to use the 'special' wiringPi PIN numbers
   // wiringPiSetupGpio();  // to use the 'GPIO' PIN numbers

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
   addValueFact(aiPh, "AI", "PH", "", wtMeter);
   addValueFact(aiFilterPressure, "AI", "Druck", "bar", wtMeter);

   // ---------------------------------
   // move calibration to config!

   aiSensors[aiPh].calPointA = 7.0;
   aiSensors[aiPh].calPointB = 9.0;
   aiSensors[aiPh].calPointValueA = 2070;
   aiSensors[aiPh].calPointValueB = 3135;

   aiSensors[aiFilterPressure].calPointA = 0.0;
   aiSensors[aiFilterPressure].calPointB = 0.6;
   aiSensors[aiFilterPressure].calPointValueA = 463;
   aiSensors[aiFilterPressure].calPointValueB = 2290;

   // ---------------------------------
   // apply some configuration specials

   applyConfigurationSpecials();

   // init web socket ...

   while (webSock->init(webPort, webSocketPingTime) != success)
   {
      tell(0, "Retrying in 2 seconds");
      sleep(2);
   }

   initScripts();
   loadStates();
   initArduino();

   initialized = true;

   return success;
}

int Poold::initLocale()
{
   // set a locale to "" means 'reset it to the environment'
   // as defined by the ISO-C standard the locales after start are C

   const char* locale {nullptr};

   setlocale(LC_ALL, "");
   locale = setlocale(LC_ALL, 0);  // 0 for query the setting

   if (!locale)
   {
      tell(0, "Info: Detecting locale setting for LC_CTYPE failed");
      return fail;
   }

   tell(1, "current locale is %s", locale);

   if ((strcasestr(locale, "UTF-8") != 0) || (strcasestr(locale, "UTF8") != 0))
      tell(1, "Detected UTF-8");

   return done;
}

int Poold::exit()
{
   for (auto it = digitalOutputStates.begin(); it != digitalOutputStates.end(); ++it)
      gpioWrite(it->first, false, false);

   exitDb();

   return success;
}

//***************************************************************************
// Init digital Output
//***************************************************************************

int Poold::initOutput(uint pin, int opt, OutputMode mode, const char* name, uint rights)
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
      tableValueFacts->setValue("RIGHTS", (int)rights);
   }

   tableValueFacts->reset();

   pinMode(pin, OUTPUT);
   gpioWrite(pin, false, false);
   addValueFact(pin, "DO", name, "", wtSymbol, 0, 0, urControl);

   return done;
}

//***************************************************************************
// Init digital Input
//***************************************************************************

int Poold::initInput(uint pin, const char* name)
{
   pinMode(pin, INPUT);

   if (!isEmpty(name))
      addValueFact(pin, "DI", name, "", wtSymbol, 0, 0);

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
         const char* unit {""};
         char* cmd {nullptr};

         asprintf(&scriptPath, "%s/%s", path, script.name.c_str());

         asprintf(&cmd, "%s status", scriptPath);
         std::string result = executeCommand(cmd);
         tell(5, "Calling '%s'", cmd);
         free(cmd);
         json_t* oData = json_loads(result.c_str(), 0, 0);

         if (oData)
            unit = getStringFromJson(oData, "unit");

         tableScripts->clear();
         tableScripts->setValue("PATH", scriptPath);

         if (!selectScriptByPath->find())
         {
            tableScripts->store();
            id = tableScripts->getLastInsertId();
         }
         else
            id = tableScripts->getIntValue("ID");

         selectScriptByPath->freeResult();

         addValueFact(id, "SC", script.name.c_str(), unit, wtSymbol, 0, 0, urControl);

         tell(0, "Found script '%s' id (%d), unit '%s'; result was [%s]", scriptPath, id, unit, result.c_str());
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
cDbFieldDef avgValueDef("AVG_VALUE", "avalue", cDBS::ffFloat, 122, cDBS::ftData);
cDbFieldDef maxValueDef("MAX_VALUE", "mvalue", cDBS::ffInt, 0, cDBS::ftData);

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

   tableUsers = new cDbTable(connection, "users");
   if (tableUsers->open() != success) return fail;

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
   // select all users

   selectAllUser = new cDbStatement(tableUsers);

   selectAllUser->build("select ");
   selectAllUser->bindAllOut();
   selectAllUser->build(" from %s", tableUsers->TableName());

   status += selectAllUser->prepare();

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
   avgValue.setField(&avgValueDef);
   maxValue.setField(&maxValueDef);
   selectSamplesRange = new cDbStatement(tableSamples);

   selectSamplesRange->build("select ");
   selectSamplesRange->bind("ADDRESS", cDBS::bndOut);
   selectSamplesRange->bind("TYPE", cDBS::bndOut, ", ");
   selectSamplesRange->bindTextFree("date_format(time, '%Y-%m-%dT%H:%i')", &xmlTime, ", ", cDBS::bndOut);
   selectSamplesRange->bindTextFree("avg(value)", &avgValue, ", ", cDBS::bndOut);
   selectSamplesRange->bindTextFree("max(value)", &maxValue, ", ", cDBS::bndOut);
   selectSamplesRange->build(" from %s where ", tableSamples->TableName());
   selectSamplesRange->bind("ADDRESS", cDBS::bndIn | cDBS::bndSet);
   selectSamplesRange->bind("TYPE", cDBS::bndIn | cDBS::bndSet, " and ");
   selectSamplesRange->bindCmp(0, "TIME", &rangeFrom, ">=", " and ");
   selectSamplesRange->bindCmp(0, "TIME", &rangeTo, "<=", " and ");
   selectSamplesRange->build(" group by date(time), ((60/5) * hour(time) + floor(minute(time)/5))");
   selectSamplesRange->build(" order by time");

   status += selectSamplesRange->prepare();

   selectSamplesRange60 = new cDbStatement(tableSamples);

   selectSamplesRange60->build("select ");
   selectSamplesRange60->bind("ADDRESS", cDBS::bndOut);
   selectSamplesRange60->bind("TYPE", cDBS::bndOut, ", ");
   selectSamplesRange60->bindTextFree("date_format(time, '%Y-%m-%dT%H:%i')", &xmlTime, ", ", cDBS::bndOut);
   selectSamplesRange60->bindTextFree("avg(value)", &avgValue, ", ", cDBS::bndOut);
   selectSamplesRange60->bindTextFree("max(value)", &maxValue, ", ", cDBS::bndOut);
   selectSamplesRange60->build(" from %s where ", tableSamples->TableName());
   selectSamplesRange60->bind("ADDRESS", cDBS::bndIn | cDBS::bndSet);
   selectSamplesRange60->bind("TYPE", cDBS::bndIn | cDBS::bndSet, " and ");
   selectSamplesRange60->bindCmp(0, "TIME", &rangeFrom, ">=", " and ");
   selectSamplesRange60->bindCmp(0, "TIME", &rangeTo, "<=", " and ");
   selectSamplesRange60->build(" group by date(time), ((60/60) * hour(time) + floor(minute(time)/60))");
   selectSamplesRange60->build(" order by time");

   status += selectSamplesRange60->prepare();

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
   selectSolarWorkPerDay->build(" group by date(time)");
   status += selectSolarWorkPerDay->prepare();

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
   delete tableSamples;            tableSamples = nullptr;
   delete tablePeaks;              tablePeaks = nullptr;
   delete tableValueFacts;         tableValueFacts = nullptr;
   delete tableConfig;             tableConfig = nullptr;
   delete tableUsers;              tableUsers = nullptr;
   delete selectActiveValueFacts;  selectActiveValueFacts = nullptr;
   delete selectAllValueFacts;     selectAllValueFacts = nullptr;
   delete selectAllConfig;         selectAllConfig = nullptr;
   delete selectAllUser;           selectAllUser = nullptr;
   delete selectMaxTime;           selectMaxTime = nullptr;
   delete selectSamplesRange;      selectSamplesRange = nullptr;
   delete selectSamplesRange60;    selectSamplesRange60 = nullptr;
   delete selectSolarWorkPerDay;   selectSolarWorkPerDay = nullptr;
   delete connection;              connection = nullptr;

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
      int res = addValueFact((int)toW1Id(it.first.c_str()), "W1", it.first.c_str(), "°C", wtMeter);

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

int Poold::readConfiguration(bool initial)
{
   // init configuration

   if (argLoglevel != na)
      loglevel = argLoglevel;
   else
      getConfigItem("loglevel", loglevel, 1);

   tell(0, "Info: LogLevel set to %d", loglevel);

   getConfigItem("interval", interval, interval);
   getConfigItem("webPort", webPort, 61109);

   getConfigItem("webUrl", webUrl);

   char* port {nullptr};
   asprintf(&port, "%d", webPort);
   if (isEmpty(webUrl) || !strstr(webUrl, port))
   {
      asprintf(&webUrl, "http://%s:%d", getFirstIp(), webPort);
      setConfigItem("webUrl", webUrl);
   }
   free(port);

   char* addrs {nullptr};
   getConfigItem("addrsDashboard", addrs, "");
   addrsDashboard = split(addrs, ',');
   getConfigItem("addrsList", addrs, "");
   addrsList = split(addrs, ',');
   free(addrs);

   getConfigItem("mail", mail, no);
   getConfigItem("mailScript", mailScript);
   getConfigItem("stateMailTo", stateMailTo);

   getConfigItem("aggregateInterval", aggregateInterval, 15);
   getConfigItem("aggregateHistory", aggregateHistory, 0);

   std::string url = poolMqttUrl ? poolMqttUrl : "";
   getConfigItem("poolMqttUrl", poolMqttUrl);

   if (url != poolMqttUrl)
      mqttDisconnect();

   url = mqttUrl ? mqttUrl : "";
   getConfigItem("mqttUrl", mqttUrl);

   if (url != mqttUrl)
      mqttDisconnect();

   getConfigItem("mqttUser", mqttUser, nullptr);
   getConfigItem("mqttPassword", mqttPassword, nullptr);

   // more special

   getConfigItem("poolLightColorToggle", poolLightColorToggle, no);

   getConfigItem("w1AddrPool", w1AddrPool, "");
   getConfigItem("w1AddrSolar", w1AddrSolar, "");
   getConfigItem("w1AddrSuctionTube", w1AddrSuctionTube, "");
   getConfigItem("w1AddrAir", w1AddrAir, "");

   getConfigItem("tPoolMax", tPoolMax, tPoolMax);
   getConfigItem("tSolarDelta", tSolarDelta, tSolarDelta);
   getConfigItem("lastSolarWork", solarWork, 0);

   getConfigItem("showerDuration", showerDuration, 20);
   getConfigItem("minSolarPumpDuration", minSolarPumpDuration, 10);
   getConfigItem("deactivatePumpsAtLowWater", deactivatePumpsAtLowWater, no);
   getConfigItem("alertSwitchOffPressure", alertSwitchOffPressure, 0);

   getConfigItem("invertDO", invertDO, yes);

   // Solar stuff

   getConfigItem("massPerSecond", massPerSecond, 11.0);                  // [Liter/min]
   massPerSecond /= 60.0;                                                // => [l/s]

   // PH stuff

   getConfigItem("phInterval", phInterval, 15);
   getConfigItem("phReference", phReference, 7.2);
   getConfigItem("phMinusDensity", phMinusDensity, 1.4);                  // [kg/l]
   getConfigItem("phMinusDemand01", phMinusDemand01, 85);                 // [ml]
   getConfigItem("phMinusDayLimit", phMinusDayLimit, 100);                // [ml]
   getConfigItem("phPumpDuration100", phPumpDuration100, 1000);           // [ms]

   // getConfigItem("phCalibratePointA", phCalibratePointA, 7.0);            // [ph]
   // getConfigItem("phCalibratePointValueA", phCalibratePointValueA, 377);  // [digits]
   // getConfigItem("phCalibratePointB", phCalibratePointB, 9.0);            // [ph]
   // getConfigItem("phCalibratePointValueB", phCalibratePointValueB, 412);  // [digits]

   /*
   // config of GPIO pins

   getConfigItem("gpioFilterPump", gpioFilterPump, gpioFilterPump);
   getConfigItem("gpioSolarPump", gpioSolarPump, gpioSolarPump);
   */

   // Time ranges

   getConfigTimeRangeItem("filterPumpTimes", filterPumpTimes);
   getConfigTimeRangeItem("uvcLightTimes", uvcLightTimes);
   getConfigTimeRangeItem("poolLightTimes", poolLightTimes);

   performMqttRequests();

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
      usleep(50000);
   }

   return done;
}

int Poold::standbyUntil(time_t until)
{
   while (time(0) < until && !doShutDown())
   {
      meanwhile();
      usleep(50000);
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

   tell(3, "loop ...");

   if (showerSwitch > 0)
   {
      toggleIo(pinShower, "DO");
      showerSwitch = 0;
   }

   // webSock->service();       // takes around 1 second :o
   dispatchClientRequest();
   // webSock->performData(cWebSock::mtData);
   // performWebSocketPing();

   performMqttRequests();
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

      standbyUntil(nextRefreshAt);

      if (doShutDown())
         break;

      // aggregate

      if (aggregateHistory && nextAggregateAt <= time(0))
         aggregate();

      // refresh expected?

      if (time(0) >= nextRefreshAt)
      {
         // perform update

         nextRefreshAt = time(0) + interval;
         mailBody = "";
         mailBodyHtml = "";

         updateScriptSensors();
         update();
         process();

         // mail

         if (mail)
            sendStateMail();

         initialRun = false;
      }
   }

   return success;
}

//***************************************************************************
// Update
//***************************************************************************

int Poold::update(bool webOnly, long client)
{
   static size_t w1Count = 0;
   time_t now = time(0);
   int count {0};

   if (!webOnly)
   {
      if (w1Count < w1Sensors.size())
      {
         initW1();
         w1Count = w1Sensors.size();
      }
   }

   tell(eloDetail, "Update ...");

   tableValueFacts->clear();
   tableValueFacts->setValue("STATE", "A");

   if (!webOnly)
      connection->startTransaction();

   for (auto sj : jsonSensorList)
      json_decref(sj.second);

   jsonSensorList.clear();

   for (int f = selectActiveValueFacts->find(); f; f = selectActiveValueFacts->fetch())
   {
      char text[1000];

      uint addr = tableValueFacts->getIntValue("ADDRESS");
      const char* type = tableValueFacts->getStrValue("TYPE");
      const char* title = tableValueFacts->getStrValue("TITLE");
      const char* usrtitle = tableValueFacts->getStrValue("USRTITLE");
      const char* unit = tableValueFacts->getStrValue("UNIT");
      const char* name = tableValueFacts->getStrValue("NAME");

      if (!isEmpty(usrtitle))
         title = usrtitle;

      json_t* ojData = json_object();
      char* tupel {nullptr};
      asprintf(&tupel, "%s:0x%02x", type, addr);
      jsonSensorList[tupel] = ojData;
      free(tupel);

      sensor2Json(ojData, tableValueFacts);

      if (tableValueFacts->hasValue("TYPE", "W1"))       // One Wire Sensor
      {
         bool w1Exist = existW1(name);
         time_t w1Last {0};
         double w1Value {0};

         if (w1Exist)
            w1Value = valueOfW1(name, w1Last);

         // exist and updated at least once in last 5 minutes?

         if (w1Exist && w1Last > time(0)-300)
         {
            json_object_set_new(ojData, "value", json_real(w1Value));

            if (!webOnly)
               store(now, name, title, unit, type, addr, w1Value);
         }
         else
         {
            if (!w1Exist)
               tell(eloAlways, "Warning: W1 sensor '%s' missing", name);
            else
               tell(eloAlways, "Warning: Data of W1 sensor '%s' seems to be to old (%s)", name, l2pTime(w1Last).c_str());

            json_object_set_new(ojData, "text", json_string("missing sensor"));
         }
      }
      else if (tableValueFacts->hasValue("TYPE", "AI"))     // Analog Input
      {
         if (aiSensors[addr].value == aiSensors[addr].value &&  // check for NaN
             aiSensors[addr].last > time(0)-120)                // not older than 2 minutes
         {
            json_object_set_new(ojData, "value", json_real(aiSensors[addr].value));

            if (addr == aiPh && !phMeasurementActive())
               json_object_set_new(ojData, "disabled", json_boolean(true));
            else if (!webOnly)
               store(now, name, title, unit, type, addr, aiSensors[addr].value);
         }
         else
         {
            json_object_set_new(ojData, "text", json_string("missing sensor"));
         }
      }
      else if (tableValueFacts->hasValue("TYPE", "DO"))    // Digital Output
      {
         json_object_set_new(ojData, "mode", json_string(digitalOutputStates[addr].mode == omManual ? "manual" : "auto"));
         json_object_set_new(ojData, "options", json_integer(digitalOutputStates[addr].opt));
         json_object_set_new(ojData, "value", json_integer(digitalOutputStates[addr].state));
         json_object_set_new(ojData, "last", json_integer(digitalOutputStates[addr].last));
         json_object_set_new(ojData, "next", json_integer(digitalOutputStates[addr].next));

         if (!webOnly)
            store(now, name, title, unit, type, addr, digitalOutputStates[addr].state);
      }
      else if (tableValueFacts->hasValue("TYPE", "SC"))   // Script Sensor
      {
         if (scSensors[addr].kind == "status")
            json_object_set_new(ojData, "value", json_integer(scSensors[addr].state));
         else if (scSensors[addr].kind == "value")
            json_object_set_new(ojData, "value", json_real(scSensors[addr].value));

         if (!webOnly)
            store(now, name, title, unit, type, addr, scSensors[addr].kind == "value" ? scSensors[addr].value : scSensors[addr].state);
      }
      else if (tableValueFacts->hasValue("TYPE", "SP"))            // Special Values
      {
         if (addr == spSolarPower)
         {
            json_object_set_new(ojData, "value", json_real(pSolar));

            if (!webOnly)
               store(now, name, title, unit, type, addr, pSolar);
         }
         else if (addr == spSolarWork)
         {
            json_object_set_new(ojData, "value", json_real(solarWork));

            if (!webOnly)
               store(now, name, title, unit, type, addr, solarWork);
         }
         else if (addr == spLastUpdate)
         {
            json_object_set_new(ojData, "text", json_string(l2pTime(time(0), "%A\n\n%d. %b %Y\n%T").c_str()));
         }
         else if (addr == spWaterLevel)
         {
            getWaterLevel();

            if (waterLevel == fail)
            {
               sprintf(text, "ERROR:Sensor Fehler <br/>(%d/%d/%d)", digitalInputStates[pinLevel1],
                       digitalInputStates[pinLevel2], digitalInputStates[pinLevel3]);

               if (!webOnly)
                  store(now, name, title, unit, type, addr, waterLevel, text);

               json_object_set_new(ojData, "text", json_string(text));
            }
            else
            {
               json_object_set_new(ojData, "value", json_integer(waterLevel));

               if (!webOnly)
                  store(now, name, title, unit, type, addr, waterLevel);
            }
         }
         else if (addr == spSolarDelta)
         {
            json_object_set_new(ojData, "value", json_real(tCurrentDelta));

            if (!webOnly)
               store(now, name, title, unit, type, addr, tCurrentDelta);
         }
         else if (addr == spPhMinusDemand)
         {
            if (aiSensors[aiPh].value)
            {
               json_object_set_new(ojData, "value", json_real(phMinusVolume));

               if (!phMeasurementActive())
                  json_object_set_new(ojData, "disabled", json_boolean(true));
               else if (!webOnly)
                  store(now, name, title, unit, type, addr, phMinusVolume);
            }
         }
      }

      count++;
   }

   selectActiveValueFacts->freeResult();

   if (!webOnly)
      connection->commit();

   // send result to all connected WEBIF clients

   pushDataUpdate(webOnly ? "init" : "all", client);

   if (!webOnly)
      tell(eloAlways, "Updated %d samples", count);

   return success;
}

//***************************************************************************
// Process
//***************************************************************************

int Poold::process()
{
   static time_t lastDay {midnightOf(time(0))};

   if (lastDay != midnightOf(time(0)))
   {
      lastDay = midnightOf(time(0));
      solarWork = 0.0;
      setConfigItem("lastSolarWork", solarWork);
   }

   time_t tPoolLast, tSolarLast;
   tPool = valueOfW1(w1AddrPool, tPoolLast);
   tSolar = valueOfW1(w1AddrSolar, tSolarLast);

   // use W1 values only if not older than 2 cycles

   bool w1Valid = tPoolLast > time(0) - 2*interval && tSolarLast > time(0) - 2*interval;

   tell(0, "Process ...");

   // ------------
   // Solar State

   if (w1Valid)
   {
      //

      solarWork += pSolar * ((time(0)-pSolarSince) / 3600.0) / 1000.0;    // in kWh
      setConfigItem("lastSolarWork", solarWork);
      tCurrentDelta = tSolar - tPool;

      const double termalCapacity = 4183.0; // Wärmekapazität Wasser bei 20°C [kJ·kg-1·K-1]

      if (digitalOutputStates[pinSolarPump].state)
         pSolar = termalCapacity * massPerSecond * tCurrentDelta;
      else
         pSolar = 0.0;

      pSolarSince = time(0);

      // publish

      publishSpecialValue(spSolarDelta, tCurrentDelta);
      publishSpecialValue(spSolarPower, pSolar);
      publishSpecialValue(spSolarWork, solarWork);
   }
   else
      tell(0, "W1 values NOT valid");

   // -----------
   // PH

   if (aiSensors[aiPh].value == aiSensors[aiPh].value &&  // check for NaN
       aiSensors[aiPh].last > time(0)-120)                // not older than 2 minutes
   {
      phMinusVolume = calcPhMinusVolume(aiSensors[aiPh].value);
      publishSpecialValue(spPhMinusDemand, phMinusVolume);
   }

   // -----------
   // Pumps Alert

   if (deactivatePumpsAtLowWater && waterLevel == 0)   // || waterLevel == fail ??
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
            else if (tCurrentDelta > tSolarDelta)
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
                  tell(0, "Solar delta (%.2f°C) lower than %.2f°C, pool has %.2f°C, stopping solar pump", tCurrentDelta, tSolarDelta, tPool);
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
         tell(0, body);
         if (sendMail(stateMailTo, "Pool pump alert", body, "text/plain") != success)
            tell(eloAlways, "Error: Sending alert mail failed");
         free(body);
      }
   }

   logReport();

   return success;
}

//***************************************************************************
// Report Actual State
//***************************************************************************

void Poold::logReport()
{
   static time_t nextLogAt {0};
   static time_t nextDetailLogAt {0};
   char buf[255+TB];

   if (time(0) > nextLogAt)
   {
      nextLogAt = time(0) + 5 * tmeSecondsPerMinute;

      tell(0, "# ------------------------");

      tell(0, "# Pool has %.2f °C; Solar has %.2f °C; Current delta is %.2f° (%.2f° configured)",
           tPool, tSolar, tCurrentDelta, tSolarDelta);
      tell(0, "# Solar power is %0.2f Watt; Solar work (today) %0.2f kWh", pSolar, solarWork);
      tell(0, "# Solar pump is '%s/%s' since '%s'", digitalOutputStates[pinSolarPump].state ? "running" : "stopped",
           digitalOutputStates[pinSolarPump].mode == omAuto ? "auto" : "manual", toElapsed(time(0)-digitalOutputStates[pinSolarPump].last, buf));
      tell(0, "# Filter pump is '%s/%s' since '%s'", digitalOutputStates[pinFilterPump].state ? "running" : "stopped",
           digitalOutputStates[pinFilterPump].mode == omAuto ? "auto" : "manual", toElapsed(time(0)-digitalOutputStates[pinFilterPump].last, buf));
      tell(0, "# UV-C light is '%s/%s'", digitalOutputStates[pinUVC].state ? "on" : "off",
           digitalOutputStates[pinUVC].mode == omAuto ? "auto" : "manual");
      tell(0, "# Pool light is '%s/%s'", digitalOutputStates[pinPoolLight].state ? "on" : "off",
           digitalOutputStates[pinPoolLight].mode == omAuto ? "auto" : "manual");

      tell(0, "# ------------------------");
   }

   if (time(0) > nextDetailLogAt)
   {
      nextDetailLogAt = time(0) + 1 * tmeSecondsPerMinute;

      tell(0, "# Solar Work");

      for (int f = selectSolarWorkPerDay->find(); f; f = selectSolarWorkPerDay->fetch())
         tell(0, "#   %s, %.2f", l2pTime(tableSamples->getTimeValue("TIME"), "%d.%m.%Y").c_str(), tableSamples->getFloatValue("VALUE"));

      selectSolarWorkPerDay->freeResult();
      tell(0, "# ------------------------");
   }
}

//***************************************************************************
// Update Script Sensors
//***************************************************************************

void Poold::updateScriptSensors()
{
   tableValueFacts->clear();
   tableValueFacts->setValue("STATE", "A");

   tell(1, "Update script sensors");

   for (int f = selectActiveValueFacts->find(); f; f = selectActiveValueFacts->fetch())
   {
      if (!tableValueFacts->hasValue("TYPE", "SC"))
         continue;

      uint addr = tableValueFacts->getIntValue("ADDRESS");
      std::string result = callScript(addr, 0, "status").c_str();
      json_error_t error;
      json_t* oData = json_loads(result.c_str(), 0, &error);

      if (!oData)
      {
         tell(0, "Error: Ignoring invalid script result [%s]", result.c_str());
         tell(0, "Error decoding json: %s (%s, line %d column %d, position %d)",
              error.text, error.source, error.line, error.column, error.position);
         return ;
      }

      std::string kind = getStringFromJson(oData, "kind");
      const char* unit = getStringFromJson(oData, "unit");
      double value = getDoubleFromJson(oData, "value");

      tell(1, "DEBUG: Got '%s' from script (kind:%s unit:%s value:%0.2f)", result.c_str(), kind.c_str(), unit, value);
      scSensors[addr].kind = kind;
      scSensors[addr].last = time(0);

      if (kind == "status")
         scSensors[addr].state = (bool)value;
      else if (kind == "value")
         scSensors[addr].value = value;
      else
         tell(0, "Got unexpected script kind '%s' in '%s'", kind.c_str(), result.c_str());
   }

   selectActiveValueFacts->freeResult();
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
   std::string subject = "Status: ";

   // check

   if (isEmpty(mailScript) || !mailBody.length() || isEmpty(stateMailTo))
      return done;

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
                     "  </head>\n\0");

   return success;
}

//***************************************************************************
// Add Value Fact
//***************************************************************************

int Poold::addValueFact(int addr, const char* type, const char* name, const char* unit,
                        WidgetType widgetType, int minScale, int maxScale, int rights)
{
   if (maxScale == na)
      maxScale = unit[0] == '%' ? 100 : 45;

   tableValueFacts->clear();
   tableValueFacts->setValue("ADDRESS", addr);
   tableValueFacts->setValue("TYPE", type);

   if (!tableValueFacts->find())
   {
      tell(0, "Add ValueFact '%ld' '%s'", tableValueFacts->getIntValue("ADDRESS"), tableValueFacts->getStrValue("TYPE"));

      tableValueFacts->setValue("NAME", name);
      tableValueFacts->setValue("TITLE", name);
      tableValueFacts->setValue("RIGHTS", rights);
      tableValueFacts->setValue("STATE", "D");
      tableValueFacts->setValue("UNIT", unit);

      char* opt {nullptr};
      asprintf(&opt, "{\"unit\": \"%s\", \"scalemax\": %d, \"scalemin\": %d, \"scalestep\": %.2f, \"imgon\": \"%s\", \"imgoff\": \"%s\", \"widgettype\": %d}}",
               unit, maxScale, minScale, 0.0, getImageFor(name, true), getImageFor(name, false), widgetType);
      tableValueFacts->setValue("WIDGETOPT", opt);
      free(opt);


      tableValueFacts->store();
      return 1;                               // 1 for 'added'
   }

   tableValueFacts->clearChanged();

   tableValueFacts->setValue("NAME", name);
   tableValueFacts->setValue("TITLE", name);

   if (tableValueFacts->getValue("RIGHTS")->isNull())
      tableValueFacts->setValue("RIGHTS", rights);

   if (tableValueFacts->getChanges())
   {
      tableValueFacts->store();
      return 2;                                // 2 for 'modified'
   }

   return done;
}

//***************************************************************************
// Config Data
//***************************************************************************

int Poold::getConfigItem(const char* name, char*& value, const char* def)
{
   free(value);
   value = nullptr;

   tableConfig->clear();
   tableConfig->setValue("OWNER", myName());
   tableConfig->setValue("NAME", name);

   if (tableConfig->find())
   {
      value = strdup(tableConfig->getStrValue("VALUE"));
   }
   else if (def)  // only if not a nullptr
   {
      value = strdup(def);
      setConfigItem(name, value);  // store the default
   }

   tableConfig->reset();

   return success;
}

int Poold::setConfigItem(const char* name, const char* value)
{
   tell(2, "Debug: Storing '%s' with value '%s'", name, value);
   tableConfig->clear();
   tableConfig->setValue("OWNER", myName());
   tableConfig->setValue("NAME", name);
   tableConfig->setValue("VALUE", value);

   return tableConfig->store();
}

int Poold::getConfigItem(const char* name, int& value, int def)
{
   return getConfigItem(name, (long&)value, (long)def);
}

int Poold::getConfigItem(const char* name, long& value, long def)
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

int Poold::setConfigItem(const char* name, long value)
{
   char txt[16];

   snprintf(txt, sizeof(txt), "%ld", value);

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
// PH Measurement Active
//***************************************************************************

bool Poold::phMeasurementActive()
{
   if (digitalOutputStates[pinFilterPump].state &&
       digitalOutputStates[pinFilterPump].last < time(0)-minPumpTimeForPh)
   {
      return true;
   }

   return false;
}

//***************************************************************************
// Calc PH Minus Volume
//***************************************************************************

int Poold::calcPhMinusVolume(double ph)
{
   double phLack = ph - phReference;
   double mlPer01 = phMinusDemand01 * (1.0/phMinusDensity);

   return (phLack/0.1) * mlPer01;
}

//***************************************************************************
// Get Image For
//***************************************************************************

const char* Poold::getImageFor(const char* title, int value)
{
   const char* imagePath = "img/icon/unknown.png";

   if (strcasestr(title, "Pump"))
      imagePath = value ? "img/icon/pump-on.gif" : "img/icon/pump-off.png";
   else if (strcasestr(title, "Steckdose"))
      imagePath = value ? "img/icon/plug-on.png" : "img/icon/plug-off.png";
   else if (strcasestr(title, "UV-C"))
      imagePath = value ? "img/icon/uvc-on.png" : "img/icon/uvc-off.png";
   else if (strcasestr(title, "Licht") || strcasestr(title, "Light"))
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

int Poold::publishScriptResult(ulong addr, const char* type, std::string result)
{
   json_error_t error;
   json_t* oData = json_loads(result.c_str(), 0, &error);

   if (!oData)
   {
      tell(0, "Error: Ignoring invalid script result [%s]", result.c_str());
      tell(0, "Error decoding json: %s (%s, line %d column %d, position %d)",
           error.text, error.source, error.line, error.column, error.position);
      return fail;
   }

   tell(1, "DEBUG: Got '%s' from script", result.c_str());

   std::string kind = getStringFromJson(oData, "kind");
   // const char* unit = getStringFromJson(oData, "unit");
   double value = getDoubleFromJson(oData, "value");

   tableValueFacts->clear();
   tableValueFacts->setValue("ADDRESS", (int)addr);
   tableValueFacts->setValue("TYPE", type);

   if (!tableValueFacts->find())
      return fail;

   const char* name = tableValueFacts->getStrValue("NAME");
   const char* title = tableValueFacts->getStrValue("TITLE");
   const char* usrtitle = tableValueFacts->getStrValue("USRTITLE");
   // WidgetType widgetType = toType(tableValueFacts->getStrValue("WIDGET"));

   if (!isEmpty(usrtitle))
      title = usrtitle;

   // update WS
   {
      json_t* oJson = json_array();
      json_t* ojData = json_object();
      json_array_append_new(oJson, ojData);

      json_object_set_new(ojData, "address", json_integer((ulong)addr));
      json_object_set_new(ojData, "type", json_string(type));
      json_object_set_new(ojData, "name", json_string(name));
      json_object_set_new(ojData, "title", json_string(title));
      // json_object_set_new(ojData, "widgettype", json_integer(widgetType));

      if (kind == "status")
         json_object_set_new(ojData, "value", json_integer((bool)value));
      else if (kind == "value")
         json_object_set_new(ojData, "value", json_real(value));

      char* tuple {nullptr};
      asprintf(&tuple, "%s:0x%02x", type, (int)addr);
      jsonSensorList[tuple] = ojData;
      free(tuple);

      pushDataUpdate("update", 0L);
   }

   hassPush(iotLight, name, "", "", value, "", false /*forceConfig*/);

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
   json_object_set_new(ojData, "mode", json_string(digitalOutputStates[pin].mode == omManual ? "manual" : "auto"));
   json_object_set_new(ojData, "options", json_integer(digitalOutputStates[pin].opt));
   json_object_set_new(ojData, "value", json_integer(digitalOutputStates[pin].state));
   json_object_set_new(ojData, "last", json_integer(digitalOutputStates[pin].last));
   json_object_set_new(ojData, "next", json_integer(digitalOutputStates[pin].next));
   json_object_set_new(ojData, "image", json_string(getImageFor(digitalOutputStates[pin].title.c_str(), digitalOutputStates[pin].state)));
   json_object_set_new(ojData, "widgettype", json_integer(wtSymbol));  // #TODO get type from valuefacts
}

int Poold::toggleOutputMode(uint pin)
{
   // allow mode toggle only if more than one option is given

   if (digitalOutputStates[pin].opt & ooAuto && digitalOutputStates[pin].opt & ooUser)
   {
      OutputMode mode = digitalOutputStates[pin].mode == omAuto ? omManual : omAuto;
      digitalOutputStates[pin].mode = mode;

      storeStates();

      json_t* oJson = json_array();
      json_t* ojData = json_object();
      json_array_append_new(oJson, ojData);
      pin2Json(ojData, pin);

      pushOutMessage(oJson, "update");
   }

   return success;
}

void Poold::gpioWrite(uint pin, bool state, bool store)
{
   digitalOutputStates[pin].state = state;
   digitalOutputStates[pin].last = time(0);

   if (!state)
      digitalOutputStates[pin].next = 0;

   // invert the state on 'invertDO' - some relay board are active at 'false'

   digitalWrite(pin, invertDO ? !state : state);

   if (store)
      storeStates();

   performJobs();

   // send update to WS
   {
      json_t* oJson = json_array();
      json_t* ojData = json_object();
      json_array_append_new(oJson, ojData);
      pin2Json(ojData, pin);

      pushOutMessage(oJson, "update");
   }

   hassPush(iotLight, digitalOutputStates[pin].name, "", "", digitalOutputStates[pin].state, "", false /*forceConfig*/);
}

bool Poold::gpioRead(uint pin)
{
   digitalInputStates[pin] = digitalRead(pin);
   return digitalInputStates[pin];
}

//***************************************************************************
// Set Special Value
//***************************************************************************

void Poold::publishSpecialValue(int sp, double value)
{
   // send update to WS

   tableValueFacts->clear();
   tableValueFacts->setValue("ADDRESS", sp);
   tableValueFacts->setValue("TYPE", "SP");

   if (tableValueFacts->find())
   {
      json_t* ojData = json_object();

      sensor2Json(ojData, tableValueFacts);
      json_object_set_new(ojData, "value", json_real(value));

      if (!phMeasurementActive() && sp == spPhMinusDemand)
         json_object_set_new(ojData, "disabled", json_boolean(true));

      char* tupel {nullptr};
      asprintf(&tupel, "SP:0x%02x", sp);
      jsonSensorList[tupel] = ojData;
      free(tupel);

      pushDataUpdate("update", 0L);
   }

   tableValueFacts->reset();
}

//***************************************************************************
// Store/Load States to DB
//  used to recover at restart
//***************************************************************************

int Poold::storeStates()
{
   long value {0};
   long mode {0};

   for (const auto& output : digitalOutputStates)
   {
      if (output.second.state)
         value += pow(2, output.first);

      if (output.second.mode == omManual)
         mode += pow(2, output.first);

      setConfigItem("ioStates", value);
      setConfigItem("ioModes", mode);

      // tell(0, "state bit (%d): %s: %d [%ld]", output.first, output.second.name, output.second.state, value);
   }

   return done;
}

int Poold::loadStates()
{
   long value {0};
   long mode {0};

   getConfigItem("ioStates", value, 0);
   getConfigItem("ioModes", mode, 0);

   tell(2, "Debug: Loaded iostates: %ld", value);

   for (const auto& output : digitalOutputStates)
   {
      if (digitalOutputStates[output.first].opt & ooUser)
      {
         gpioWrite(output.first, value & (long)pow(2, output.first), false);
         tell(0, "Info: IO %s/%d recovered to %d", output.second.name, output.first, output.second.state);
      }
   }

   if (mode)
   {
      for (const auto& output : digitalOutputStates)
      {
         if (digitalOutputStates[output.first].opt & ooAuto && digitalOutputStates[output.first].opt & ooUser)
         {
            OutputMode m = mode & (long)pow(2, output.first) ? omManual : omAuto;
            digitalOutputStates[output.first].mode = m;
         }
      }
   }

   // if filter pump is running assume its running at least 'minPumpTimeForPh'

   if (digitalOutputStates[pinFilterPump].state)
      digitalOutputStates[pinFilterPump].last = time(0)-minPumpTimeForPh;

   return done;
}

//***************************************************************************
// Arduino Stuff ...
//***************************************************************************

int Poold::dispatchArduinoMsg(const char* message)
{
   json_error_t error;
   json_t* jObject = json_loads(message, 0, &error);

   if (!jObject)
   {
      tell(0, "Error: Can't parse json in '%s'", message);
      return fail;
   }

   const char* event = getStringFromJson(jObject, "event", "<null>");

   if (strcmp(event, "update") == 0)
   {
      json_t* jArray = getObjectFromJson(jObject, "analog");
      size_t index {0};
      json_t* jValue {nullptr};

      json_array_foreach(jArray, index, jValue)
      {
         const char* name = getStringFromJson(jValue, "name");
         double value = getDoubleFromJson(jValue, "value");
         time_t stamp = getIntFromJson(jValue, "time");

         if (stamp < time(0)-300)
         {
            tell(eloAlways, "Skipping old (%ld seconds) arduino value", time(0)-stamp);
            continue;
         }

         updateAnalogInput(name, value, stamp);
      }

      process();
   }
   else if (strcmp(event, "init") == 0)
   {
      initArduino();
   }
   else
   {
      tell(eloAlways, "Got unexpected event '%s' from arduino", event);
   }

   return success;
}

int Poold::initArduino()
{
   mqttCheckConnection();

   if (isEmpty(poolMqttUrl) || !mqttPoolReader->isConnected())
      return fail;

   json_t* oJson = json_object();

   json_object_set_new(oJson, "event", json_string("setUpdateInterval"));
   json_object_set_new(oJson, "parameter", json_integer(phInterval));

   char* p = json_dumps(oJson, JSON_REAL_PRECISION(4));
   json_decref(oJson);

   if (!p)
   {
      tell(0, "Error: Dumping json message failed");
      return fail;
   }

   mqttPoolReader->write("poold2mqtt/arduino/in", p);
   tell(1, "DEBUG: PushMessage to arduino [%s]", p);
   free(p);

   return success;
}

void Poold::updateAnalogInput(const char* id, int value, time_t stamp)
{
   uint input = atoi(id+1);

   tableValueFacts->clear();
   tableValueFacts->setValue("ADDRESS", (int)input);
   tableValueFacts->setValue("TYPE", "AI");

   if (!tableValueFacts->find() || !tableValueFacts->hasValue("STATE", "A"))
   {
      tableValueFacts->reset();
      return ;
   }

   double m = (aiSensors[input].calPointB - aiSensors[input].calPointA) / (aiSensors[input].calPointValueB - aiSensors[input].calPointValueA);
   double b = aiSensors[input].calPointB - m * aiSensors[input].calPointValueB;
   double dValue = m * value + b;

   if (input == aiPh)
      dValue = std::llround(dValue*50) / 50.0;  // round to .02
   else
      dValue = std::llround(dValue*20) / 20.0;  // round to .05

   tell(2, "Rouned %.2f to %.2f", m * value + b, dValue);
   aiSensors[input].value = dValue;
   aiSensors[input].last = stamp;

   tell(3, "Debug: Input A%d: %.3f", input, aiSensors[input].value);

   // ----------------------------------

   json_t* ojData = json_object();

   sensor2Json(ojData, tableValueFacts);
   json_object_set_new(ojData, "value", json_real(aiSensors[input].value));

   char* tuple {nullptr};
   asprintf(&tuple, "AI:0x%02x", input);
   jsonSensorList[tuple] = ojData;
   free(tuple);

   pushDataUpdate("update", 0L);

   tableValueFacts->reset();
}

//***************************************************************************
// W1 Stuff ...
//***************************************************************************

int Poold::dispatchW1Msg(const char* message)
{
   json_error_t error;
   json_t* jArray = json_loads(message, 0, &error);

   if (!jArray)
   {
      tell(0, "Error: Can't parse json in '%s'", message);
      return fail;
   }

   size_t index {0};
   json_t* jValue {nullptr};

   json_array_foreach(jArray, index, jValue)
   {
      const char* name = getStringFromJson(jValue, "name");
      double value = getDoubleFromJson(jValue, "value");
      time_t stamp = getIntFromJson(jValue, "time");

      if (stamp < time(0)-300)
      {
         tell(eloAlways, "Skipping old (%ld seconds) w1 value", time(0)-stamp);
         continue;
      }

      updateW1(name, value, stamp);
   }

   cleanupW1();
   process();

   return success;
}

void Poold::updateW1(const char* id, double value, time_t stamp)
{
   tell(2, "w1: %s : %0.2f", id, value);

   w1Sensors[id].value = value;
   w1Sensors[id].last = stamp;

   tableValueFacts->clear();
   tableValueFacts->setValue("ADDRESS", (int)toW1Id(id));
   tableValueFacts->setValue("TYPE", "W1");

   if (tableValueFacts->find())
   {
      json_t* ojData = json_object();

      sensor2Json(ojData, tableValueFacts);
      json_object_set_new(ojData, "value", json_real(value));
      json_object_set_new(ojData, "widgettype", json_integer(wtChart));

      char* tupel {nullptr};
      asprintf(&tupel, "W1:0x%02x", toW1Id(id));
      jsonSensorList[tupel] = ojData;
      free(tupel);

      pushDataUpdate("update", 0L);
   }

   tableValueFacts->reset();
}

void Poold::cleanupW1()
{
   uint detached {0};

   for (auto it = w1Sensors.begin(); it != w1Sensors.end(); it++)
   {
      if (it->second.last < time(0) - 5*tmeSecondsPerMinute)
      {
         tell(0, "Info: Missing sensor '%s', removing it from list", it->first.c_str());
         detached++;
         w1Sensors.erase(it--);
      }
   }

   if (detached)
   {
      tell(0, "Info: %d w1 sensors detached, reseting power line to force a re-initialization", detached);
      gpioWrite(pinW1Power, false);
      sleep(2);
      gpioWrite(pinW1Power, true);
   }
}

bool Poold::existW1(const char* id)
{
   if (isEmpty(id))
      return false;

   auto it = w1Sensors.find(id);

   return it != w1Sensors.end();
}

double Poold::valueOfW1(const char* id, time_t& last)
{
   last = 0;

   if (isEmpty(id))
      return 0;

   auto it = w1Sensors.find(id);

   if (it == w1Sensors.end())
      return 0;

   last = w1Sensors[id].last;

   return w1Sensors[id].value;
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
