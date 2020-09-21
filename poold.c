//***************************************************************************
// poold / Linux - Pool Steering
// File poold.c
// This code is distributed under the terms and conditions of the
// GNU GENERAL PUBLIC LICENSE. See the file LICENSE for details.
// Date 16.04.2020 - Jörg Wendel
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

int Poold::shutdown = no;

//***************************************************************************
// Configuration Items
//***************************************************************************

std::list<Poold::ConfigItemDef> Poold::configuration
{
   // web

   { "addrsDashboard",            ctString,  false, "3 WEB Interface", "Sensoren 'Dashboard'", "Komma getrennte Liste aus ID:Typ siehe 'IO Setup'" },
   { "addrsList",                 ctString,  false, "3 WEB Interface", "Sensoren 'Liste'", "Komma getrennte Liste aus ID:Typ siehe 'IO Setup'" },

   // { "chart",                  ctString,  false, "3 WEB Interface", "Charts", "Komma getrennte Liste aus ID:Typ siehe 'Aufzeichnung'" },
   { "chartStart",                ctInteger, false, "3 WEB Interface", "Chart Zeitraum (Tage)", "Standardzeitraum der Chartanzeige (seit x Tagen bis heute)" },
   { "style",                     ctChoice,  false, "3 WEB Interface", "Farbschema", "" },
   { "vdr",                       ctBool,    false, "3 WEB Interface", "VDR (Video Disk Recorder) Osd verfügbar", "" },

   // poold

   { "interval",                  ctInteger, false, "1 Pool Daemon", "Intervall der Aufzeichung", "Datenbank Aufzeichung [s]" },
   { "webPort",                   ctInteger, false, "1 Pool Daemon", "Port des Web Interfaces", "" },
   { "loglevel",                  ctInteger, false, "1 Pool Daemon", "Log level", "" },
   { "filterPumpTimes",           ctRange,   false, "1 Pool Daemon", "Zeiten Filter Pumpe", "[hh:mm] - [hh:mm]" },
   { "uvcLightTimes",             ctRange,   false, "1 Pool Daemon", "Zeiten UV-C Licht", "[hh:mm] - [hh:mm], wird nur angeschaltet wenn auch die Filterpumpe läuft!" },
   { "poolLightTimes",            ctRange,   false, "1 Pool Daemon", "Zeiten Pool Licht", "[hh:mm] - [hh:mm]" },
   { "poolLightColorToggle",      ctBool,    false, "1 Pool Daemon", "Pool Licht Farb-Toggel", "" },

   { "tPoolMax",                  ctNum,     false, "1 Pool Daemon", "Pool max Temperatur", "" },
   { "tSolarDelta",               ctNum,     false, "1 Pool Daemon", "Einschaltdifferenz Solarpumpe", "" },
   { "showerDuration",            ctInteger, false, "1 Pool Daemon", "Laufzeit der Dusche", "Laufzeit [s]" },
   { "minSolarPumpDuration",      ctInteger, false, "1 Pool Daemon", "Mindestlaufzeit der Solarpumpe [m]", "" },
   { "deactivatePumpsAtLowWater", ctBool,    false, "1 Pool Daemon", "Pumpen bei geringem Wasserstand deaktivieren", "" },

   { "invertDO",                  ctBool,    false, "1 Pool Daemon", "Digitalaugänge invertieren", "" },
   { "w1AddrAir",                 ctString,  false, "1 Pool Daemon", "Adresse Fühler Temperatur Luft", "" },
   { "w1AddrPool",                ctString,  false, "1 Pool Daemon", "Adresse Fühler Temperatur Pool", "" },
   { "w1AddrSolar",               ctString,  false, "1 Pool Daemon", "Adresse Fühler Temperatur Kollektor", "" },
   { "w1AddrSuctionTube",         ctString,  false, "1 Pool Daemon", "Adresse Fühler Temperatur Saugleitung", "" },

   { "aggregateHistory",          ctInteger, false, "1 Pool Daemon", "Historie [Tage]", "history for aggregation in days (default 0 days -> aggegation turned OFF)" },
   { "aggregateInterval",         ctInteger, false, "1 Pool Daemon", "Intervall [m]", "aggregation interval in minutes - 'one sample per interval will be build'" },
   { "peakResetAt",               ctString,  true,  "1 Pool Daemon", "", "" },

   { "mqttUrl",                   ctString,  false, "1 Pool Daemon", "Url des MQTT Message Broker", "Wird zur Kommunikation mit dem one-wire Interface und mit Hausautomatisierungen verwendet. Beispiel: 'tcp://localhost:1883'" },

   // PH stuff

   { "arduinoDevice",             ctString,  false, "1 Pool Daemon", "Arduino interface device", "Beispiel: '/dev/ttyS0'" },
   { "phReference",               ctNum,     false, "1 Pool Daemon", "PH Sollwert", "Sollwert [PH] (default 7,2)" },
   { "phMinusDensity",            ctNum,     false, "1 Pool Daemon", "Dichte PH Minus [kg/l]", "Wie viel kg wiegt ein Liter PH Minus (default 1,4)" },
   { "phMinusDemand01",           ctInteger, false, "1 Pool Daemon", "Menge zum Senken um 0,1 [g]", "Wie viel Gramm PH Minus wird zum Senken des PH Wertes um 0,1 für das vorhandene Pool Volumen benötigt (default 60g)" },
   { "phMinusDayLimit",           ctInteger, false, "1 Pool Daemon", "Obergrenze PH Minus/Tag [ml]", "Wie viel PH Minus wird pro Tag maximal zugegeben [ml] (default 100ml)" },
   { "phPumpDurationPer100",      ctInteger, false, "1 Pool Daemon", "Laufzeit Dosierpumpe/100ml [ms]", "Welche Zeit in Millisekunden benötigt die Dosierpumpe um 100ml zu fördern (default 1000ms)" },

   // mail

   { "mail",                      ctBool,    false, "4 Mail", "Mail Benachrichtigung", "Mail Benachrichtigungen aktivieren/deaktivieren" },
   { "mailScript",                ctString,  false, "4 Mail", "poold sendet Mails über das Skript", "" },
   { "stateMailTo",               ctString,  false, "4 Mail", "Status Mail Empfänger", "Komma getrennte Empfängerliste" },
   { "errorMailTo",               ctString,  false, "4 Mail", "Fehler Mail Empfänger", "Komma getrennte Empfängerliste" },
   { "webUrl",                    ctString,  false, "4 Mail", "URL der Visualisierung", "kann mit %weburl% in die Mails eingefügt werden" },
};

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

   arduinoInterface.close();
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
   tell(1, "DEBUG: PushMessage [%s]", p);
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
            for (const auto sensor : addrsDashboard)
               json_array_append(oWsJson, jsonSensorList[sensor]);
         else
            for (auto sj : jsonSensorList)
               json_array_append(oWsJson, sj.second);
      }
      else if (cl.page == "list")
      {
         if (addrsList.size())
            for (const auto sensor : addrsList)
               json_array_append(oWsJson, jsonSensorList[sensor]);
         else
            for (auto sj : jsonSensorList)
               json_array_append(oWsJson, sj.second);
      }
      else if (cl.page == "schema")
      {
         // #TODO - send visible instead of all

         for (auto sj : jsonSensorList)
            json_array_append(oWsJson, sj.second);
      }

      pushOutMessage(oWsJson, title, client);
   }
   else
   {
      for (const auto cl : wsClients)
      {
         json_t* oWsJson = json_array();

         if (cl.second.page == "dashboard")
         {
            if (addrsDashboard.size())
               for (const auto sensor : addrsDashboard)
                  json_array_append(oWsJson, jsonSensorList[sensor]);
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

         pushOutMessage(oWsJson, title, (long)cl.first);
      }
   }

   // cleanup

   for (auto sj : jsonSensorList)
      json_decref(sj.second);

   jsonSensorList.clear();

   return success;
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

   while (!messagesIn.empty())
   {
      // dispatch message like
      // { "event" : "toggleio", "object" : { "address" : "122", "type" : "DO" } }

      tell(2, "DEBUG: Got '%s'", messagesIn.front().c_str());
      oData = json_loads(messagesIn.front().c_str(), 0, &error);

      // get the request

      Event event = cWebService::toEvent(getStringFromJson(oData, "event", "<null>"));
      long client = getLongFromJson(oData, "client");
      oObject = json_object_get(oData, "object");
      int addr = getIntFromJson(oObject, "address");
      const char* type = getStringFromJson(oObject, "type");

      // rights ...

      if (checkRights(client, event, oObject))
      {
         // dispatch client request

         switch (event)
         {
            case evLogin:          status = performLogin(oObject);                  break;
            case evLogout:         status = performLogout(oObject);                 break;
            case evGetToken:       status = performTokenRequest(oObject, client);   break;
            case evToggleIo:       status = toggleIo(addr, type);                   break;
            case evToggleIoNext:   status = toggleIoNext(addr);                     break;
            case evToggleMode:     status = toggleOutputMode(addr);                 break;
            case evStoreConfig:    status = storeConfig(oObject, client);           break;
            case evIoSetup:        status = performIoSettings(oObject, client);     break;
            case evStoreIoSetup:   status = storeIoSetup(oObject, client);          break;
            case evChartData:      status = performChartData(oObject, client);      break;
            case evUserConfig:     status = performUserConfig(oObject, client);     break;
            case evChangePasswd:   status = performPasswChange(oObject, client);    break;
            case evResetPeaks:     status = resetPeaks(oObject, client);            break;
            case evPh:             status = performPh(client, false);               break;
            case evPhAll:          status = performPh(client, true);                break;
            case evPhCal:          status = performPhCal(oObject, client);          break;
            case evPhSetCal:       status = performPhSetCal(oObject, client);       break;
            case evSendMail:       status = performSendMail(oObject, client);          break;
            case evChartbookmarks: status = performChartbookmarks(client);             break;
            case evStoreChartbookmarks: status = storeChartbookmarks(oObject, client); break;

            default: tell(0, "Error: Received unexpected client request '%s' at [%s]",
                          toName(event), messagesIn.front().c_str());
         }
      }
      else
      {
         tell(0, "Insufficient rights to '%s' for user '%s'",
              getStringFromJson(oData, "event", "<null>"),
              wsClients[(void*)client].user.c_str());
      }

      json_decref(oData);      // free the json object
      messagesIn.pop();
   }

   return status;
}

bool Poold::checkRights(long client, Event event, json_t* oObject)
{
   uint rights = wsClients[(void*)client].rights;

   switch (event)
   {
      case evLogin:               return true;
      case evLogout:              return true;
      case evGetToken:            return true;
      case evToggleIoNext:        return rights & urControl;
      case evToggleMode:          return rights & urFullControl;
      case evStoreConfig:         return rights & urSettings;
      case evIoSetup:             return rights & urView;
      case evStoreIoSetup:        return rights & urSettings;
      case evChartData:           return rights & urView;
      case evUserConfig:          return rights & urAdmin;
      case evChangePasswd:        return true;   // check will done in performPasswChange()
      case evResetPeaks:          return rights & urAdmin;
      case evSendMail:            return rights & urSettings;

      case evPh:
      case evPhAll:
      case evPhCal:
      case evPhSetCal:            return rights & urAdmin;

      case evChartbookmarks:      return rights & urView;
      case evStoreChartbookmarks: return rights & urSettings;

      default: break;
   }

   if (event == evToggleIo && rights & urControl)
   {
      int addr = getIntFromJson(oObject, "address");
      const char* type = getStringFromJson(oObject, "type");

      tableValueFacts->clear();
      tableValueFacts->setValue("ADDRESS", addr);
      tableValueFacts->setValue("TYPE", type);

      if (tableValueFacts->find())
         return rights & tableValueFacts->getIntValue("RIGHTS");
   }

   return false;
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
      tableConfig->find();
      tableConfig->store();
   }

   readConfiguration();

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
   initOutput(pinW1Power, ooAuto|ooUser, omAuto, "W1 Power", urFullControl);

   gpioWrite(pinW1Power, true);

   // init input IO

   initInput(pinLevel1, 0);
   initInput(pinLevel2, 0);
   initInput(pinLevel3, 0);
   initInput(pinShowerSwitch, 0);
   pullUpDnControl(pinShowerSwitch, PUD_UP);

   if (wiringPiISR(pinShowerSwitch, INT_EDGE_FALLING, &ioInterrupt) < 0)
      tell(0, "Error: Unable to setup ISR: %s", strerror(errno));

   // special values

   addValueFact(spWaterLevel, "SP", "Water Level", "%");
   addValueFact(spSolarDelta, "SP", "Solar Delta", "°C");
   addValueFact(spPhMinusDemand, "SP", "PH Minus Bedarf", "ml");
   addValueFact(spLastUpdate, "SP", "Aktualisiert", "");
   addValueFact(aiPh, "AI", "PH", "");
   addValueFact(aiFilterPressure, "AI", "Druck", "bar");

   // ---------------------------------
   // apply some configuration specials

   applyConfigurationSpecials();

   // init web socket ...

   while (webSock->init(webPort, webSocketPingTime) != success)
   {
      tell(0, "Retrying in 2 seconds");
      sleep(2);
   }

   // init PH interface

   if (!isEmpty(arduinoDevice))
      arduinoInterface.open(arduinoDevice);

   //

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
   gpioWrite(pin, false);
   addValueFact(pin, "DO", name, "", urControl);

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

         addValueFact(id, "SC", script.name.c_str(), "", urControl);

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
   delete tableUsers;              tableUsers = 0;
   delete selectActiveValueFacts;  selectActiveValueFacts = 0;
   delete selectAllValueFacts;     selectAllValueFacts = 0;
   delete selectAllConfig;         selectAllConfig = 0;
   delete selectAllUser;           selectAllUser = 0;
   delete selectMaxTime;           selectMaxTime = 0;
   delete selectSamplesRange;      selectSamplesRange = 0;
   delete selectSamplesRange60;    selectSamplesRange60 = 0;
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
   // init configuration

   getConfigItem("loglevel", loglevel, 1);
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
   getConfigItem("mailScript", mailScript, BIN_PATH "/poold-mail.sh");
   getConfigItem("stateMailTo", stateMailTo);

   getConfigItem("aggregateInterval", aggregateInterval, 15);
   getConfigItem("aggregateHistory", aggregateHistory, 0);
   getConfigItem("mqttUrl", mqttUrl, "tcp://localhost:1883");

   // more special

   getConfigItem("poolLightColorToggle", poolLightColorToggle, no);

   getConfigItem("w1AddrPool", w1AddrPool, "");
   getConfigItem("w1AddrSolar", w1AddrSolar, "");
   getConfigItem("w1AddrSuctionTube", w1AddrSuctionTube, "");
   getConfigItem("w1AddrAir", w1AddrAir, "");

   getConfigItem("tPoolMax", tPoolMax, tPoolMax);
   getConfigItem("tSolarDelta", tSolarDelta, tSolarDelta);
   getConfigItem("showerDuration", showerDuration, 20);
   getConfigItem("minSolarPumpDuration", minSolarPumpDuration, 10);
   getConfigItem("deactivatePumpsAtLowWater", deactivatePumpsAtLowWater, no);

   getConfigItem("invertDO", invertDO, no);
   getConfigItem("chart", chartSensors, "");

   // PH stuff

   getConfigItem("arduinoDevice", arduinoDevice);
   getConfigItem("phReference", phReference, 7.2);
   getConfigItem("phMinusDensity", phMinusDensity, 1.4);         // [kg/l]
   getConfigItem("phMinusDemand01", phMinusDemand01, 85);        // [ml]
   getConfigItem("phMinusDayLimit", phMinusDayLimit, 100);       // [ml]
   getConfigItem("phPumpDuration100", phPumpDuration100, 1000);  // [ms]

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

   if (!isEmpty(mqttUrl))
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

   tell(3, "loop ...");

   if (showerSwitch > 0)
   {
      toggleIo(pinShower, "DO");
      showerSwitch = 0;
   }

   webSock->service();       // takes around 1 second :o
   dispatchClientRequest();
   webSock->performData(cWebSock::mtData);
   performWebSocketPing();

   if (!isEmpty(mqttUrl))
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

      if (tableValueFacts->hasValue("TYPE", "W1"))
      {
         if (existW1(name))
         {
            time_t w1Last;
            double w1Value = valueOfW1(name, w1Last);

            json_object_set_new(ojData, "value", json_real(w1Value));
            json_object_set_new(ojData, "widgettype", json_integer(wtChart));

            if (!webOnly)
               store(now, name, title, unit, type, addr, w1Value);
         }
         else
         {
            json_object_set_new(ojData, "text", json_string("missing sensor"));
            json_object_set_new(ojData, "widgettype", json_integer(wtText));
         }
      }
      else if (tableValueFacts->hasValue("TYPE", "AI"))
      {
         cArduinoInterface::AnalogValue aiValue;

         if (!isEmpty(arduinoDevice) && arduinoInterface.requestAi(aiValue, addr) == success)
         {
            aiSensors[addr].value = round2(aiValue.value); // std::ceil(aiValue.value*100.0)/100.0;
            aiSensors[addr].last = time(0);

            // to debug DEBUG
            //if (addr == aiFilterPressure)
            //  aiSensors[addr].value = 0.3;

            json_object_set_new(ojData, "value", json_real(aiSensors[addr].value));
            json_object_set_new(ojData, "widgettype", json_integer(wtChart));

            if (!webOnly)
               store(now, name, title, unit, type, addr, aiSensors[addr].value);
         }
         else
         {
            json_object_set_new(ojData, "text", json_string("missing sensor"));
            json_object_set_new(ojData, "widgettype", json_integer(wtText));
         }
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
            json_object_set_new(ojData, "widgettype", json_integer(wtChart));
         }
         else
            tell(0, "Got unexpected script kind '%s' in '%s'", kind.c_str(), result.c_str());

         if (!webOnly)
            store(now, name, title, unit, type, addr, strtod(value.c_str(), nullptr));
      }
      else if (tableValueFacts->hasValue("TYPE", "SP"))
      {
         if (addr == spLastUpdate)
         {
            json_object_set_new(ojData, "text", json_string(l2pTime(time(0), "%T").c_str()));
            json_object_set_new(ojData, "widgettype", json_integer(wtText));
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
               json_object_set_new(ojData, "widgettype", json_integer(wtText));
            }
            else
            {
               json_object_set_new(ojData, "value", json_integer(waterLevel));
               json_object_set_new(ojData, "widgettype", json_integer(wtChart));

               if (!webOnly)
                  store(now, name, title, unit, type, addr, waterLevel);
            }
         }
         else if (addr == spSolarDelta)
         {
            time_t tPoolLast, tSolarLast;
            tPool = valueOfW1(w1AddrPool, tPoolLast);
            tSolar = valueOfW1(w1AddrSolar, tSolarLast);

            // use value only if not older than 2 cycles

            if (tPoolLast > time(0) - 2*interval &&
                tSolarLast > time(0) - 2*interval)
            {
               tCurrentDelta = tSolar - tPool;

               json_object_set_new(ojData, "value", json_real(tCurrentDelta));
               json_object_set_new(ojData, "widgettype", json_integer(wtChart));

               if (!webOnly)
                  store(now, name, title, unit, type, addr, tCurrentDelta);
            }
         }
         else if (addr == spPhMinusDemand && !isEmpty(arduinoDevice))
         {
            if (aiSensors[aiPh].value)
            {
               int ml = calcPhMinusVolume(aiSensors[aiPh].value);

               json_object_set_new(ojData, "value", json_real(ml));
               json_object_set_new(ojData, "widgettype", json_integer(wtChart));

               if (!webOnly)
                  store(now, name, title, unit, type, addr, ml);
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
// WS Ping
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
// Reply Result
//***************************************************************************

int Poold::replyResult(int status, const char* message, long client)
{
   if (status != success)
      tell(0, "Error: Web request failed with '%s' (%d)", message, status);

   json_t* oJson = json_object();
   json_object_set_new(oJson, "status", json_integer(status));
   json_object_set_new(oJson, "message", json_string(message));
   pushOutMessage(oJson, "result", client);

   return status;
}

//***************************************************************************
// Perform WS Client Login / Logout
//***************************************************************************

int Poold::performLogin(json_t* oObject)
{
   long client = getLongFromJson(oObject, "client");
   const char* user = getStringFromJson(oObject, "user", "");
   const char* token = getStringFromJson(oObject, "token", "");
   const char* page = getStringFromJson(oObject, "page", "");
   json_t* aRequests = json_object_get(oObject, "requests");

   tableUsers->clear();
   tableUsers->setValue("USER", user);

   wsClients[(void*)client].user = user;
   wsClients[(void*)client].page = page;

   if (tableUsers->find() && tableUsers->hasValue("TOKEN", token))
   {
      wsClients[(void*)client].type = ctWithLogin;
      wsClients[(void*)client].rights = tableUsers->getIntValue("RIGHTS");
   }
   else
   {
      wsClients[(void*)client].type = ctActive;
      wsClients[(void*)client].rights = urView;  // allow view without login
      tell(0, "Warning: Unknown user '%s' or token mismatch connected!", user);

      json_t* oJson = json_object();
      json_object_set_new(oJson, "user", json_string(user));
      json_object_set_new(oJson, "state", json_string("reject"));
      json_object_set_new(oJson, "value", json_string(""));
      pushOutMessage(oJson, "token", client);
   }

   tell(0, "Login of client 0x%x; user '%s'; type is %d", (unsigned int)client, user, wsClients[(void*)client].type);
   webSock->setClientType((lws*)client, wsClients[(void*)client].type);

   //

   json_t* oJson = json_object();
   config2Json(oJson);
   pushOutMessage(oJson, "config", client);

   oJson = json_object();
   daemonState2Json(oJson);
   pushOutMessage(oJson, "daemonstate", client);

   // perform requests

   size_t index {0};
   json_t* oRequest {nullptr};

   json_array_foreach(aRequests, index, oRequest)
   {
      const char* name = getStringFromJson(oRequest, "name");

      // #TODO add this 'services' to the events and use it here

      if (isEmpty(name))
         continue;

      tell(0, "Got request '%s'", name);

      if (strcmp(name, "data") == 0)
         update(true, client);     // push the data ('init')
      else if (wsClients[(void*)client].rights & urAdmin && strcmp(name, "syslog") == 0)
         performSyslog(client);
      else if (wsClients[(void*)client].rights & urSettings && strcmp(name, "configdetails") == 0)
         performConfigDetails(client);
      else if (wsClients[(void*)client].rights & urAdmin && strcmp(name, "userdetails") == 0)
         performUserDetails(client);
      else if (wsClients[(void*)client].rights & urAdmin && strcmp(name, "iosettings") == 0)
         performIoSettings(nullptr, client);
      else if (wsClients[(void*)client].rights & urAdmin && strcmp(name, "phall") == 0)
         performPh(client, true);

      else if (strcmp(name, "chartdata") == 0)
         performChartData(oRequest, client);
   }

   return done;
}

int Poold::performLogout(json_t* oObject)
{
   long client = getLongFromJson(oObject, "client");
   tell(0, "Logout of client 0x%x", (unsigned int)client);
   wsClients.erase((void*)client);
   return done;
}

//***************************************************************************
// Perform WS Token Request
//***************************************************************************

int Poold::performTokenRequest(json_t* oObject, long client)
{
   json_t* oJson = json_object();
   const char* user = getStringFromJson(oObject, "user", "");
   const char* passwd  = getStringFromJson(oObject, "password", "");

   tell(0, "Token request of client 0x%x for user '%s'", (unsigned int)client, user);

   tableUsers->clear();
   tableUsers->setValue("USER", user);

   if (tableUsers->find())
   {
      if (tableUsers->hasValue("PASSWD", passwd))
      {
         tell(0, "Token request for user '%s' succeeded", user);
         json_object_set_new(oJson, "user", json_string(user));
         json_object_set_new(oJson, "rights", json_integer(tableUsers->getIntValue("RIGHTS")));
         json_object_set_new(oJson, "state", json_string("confirm"));
         json_object_set_new(oJson, "value", json_string(tableUsers->getStrValue("TOKEN")));
         pushOutMessage(oJson, "token", client);
      }
      else
      {
         tell(0, "Token request for user '%s' failed, wrong password", user);
         json_object_set_new(oJson, "user", json_string(user));
         json_object_set_new(oJson, "rights", json_integer(0));
         json_object_set_new(oJson, "state", json_string("reject"));
         json_object_set_new(oJson, "value", json_string(""));
         pushOutMessage(oJson, "token", client);
      }
   }
   else
   {
      tell(0, "Token request for user '%s' failed, unknown user", user);
      json_object_set_new(oJson, "user", json_string(user));
      json_object_set_new(oJson, "rights", json_integer(0));
      json_object_set_new(oJson, "state", json_string("reject"));
      json_object_set_new(oJson, "value", json_string(""));

      pushOutMessage(oJson, "token", client);
   }

   tableUsers->reset();

   return done;
}

//***************************************************************************
// Perform WS Syslog Request
//***************************************************************************

int Poold::performSyslog(long client)
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
// Perform Send Mail
//***************************************************************************

int Poold::performSendMail(json_t* oObject, long client)
{
/*   int alertid = getIntFromJson(oObject, "alertid", na);

   if (alertid != na)
      return performAlertTestMail(alertid, client);
*/

   const char* subject = getStringFromJson(oObject, "subject");
   const char* body = getStringFromJson(oObject, "body");

   tell(eloDetail, "Test mail requested with: '%s/%s'", subject, body);

   if (isEmpty(mailScript))
      return replyResult(fail, "missing mail script", client);

   if (!fileExists(mailScript))
      return replyResult(fail, "mail script not found", client);

   if (isEmpty(stateMailTo))
      return replyResult(fail, "missing receiver", client);

   if (sendMail(stateMailTo, subject, body, "text/plain") != success)
      return replyResult(fail, "send failed", client);

   return replyResult(success, "mail sended", client);
}

//***************************************************************************
// Perform WS Config Data Request
//***************************************************************************

int Poold::performConfigDetails(long client)
{
   if (client <= 0)
      return done;

   json_t* oJson = json_array();
   configDetails2Json(oJson);
   pushOutMessage(oJson, "configdetails", client);

   return done;
}

//***************************************************************************
// Perform WS User Data Request
//***************************************************************************

int Poold::performUserDetails(long client)
{
   if (client <= 0)
      return done;

   json_t* oJson = json_array();
   userDetails2Json(oJson);
   pushOutMessage(oJson, "userdetails", client);

   return done;
}

//***************************************************************************
// Perform WS IO Setting Data Request
//***************************************************************************

int Poold::performIoSettings(json_t* oObject, long client)
{
   if (client <= 0)
      return done;

   bool filterActive = false;

   if (oObject)
      filterActive = getBoolFromJson(oObject, "filter", false);

   json_t* oJson = json_array();
   valueFacts2Json(oJson, filterActive);
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

   int range = getIntFromJson(oObject, "range", 3);                // Anzahl der Tage
   time_t rangeStart = getLongFromJson(oObject, "start", 0);       // Start Datum (unix timestamp)
   const char* sensors = getStringFromJson(oObject, "sensors");    // Kommata getrennte Liste der Sensoren
   const char* id = getStringFromJson(oObject, "id", "");

   // the id is one of {"chart" "chartwidget" "chartdialog"}

   bool widget = strcmp(id, "chart") != 0;
   cDbStatement* select = widget ? selectSamplesRange60 : selectSamplesRange;

   if (!widget)
      performChartbookmarks(client);

   if (strcmp(id, "chart") == 0 && !isEmpty(sensors))
   {
      tell(0, "storing sensors '%s' for chart", sensors);
      setConfigItem("chart", sensors);
      getConfigItem("chart", chartSensors);
   }
   else if (isEmpty(sensors))
      sensors = chartSensors;

   tell(eloDebug, "Selecting chats data for '%s' ..", sensors);

   auto sList = split(sensors, ',');

   json_t* oMain = json_object();
   json_t* oJson = json_array();

   if (!rangeStart)
      rangeStart = time(0) - (range*tmeSecondsPerDay);

   rangeFrom.setValue(rangeStart);
   rangeTo.setValue(rangeStart + (range*tmeSecondsPerDay));

   tableValueFacts->clear();
   tableValueFacts->setValue("STATE", "A");

   json_t* aAvailableSensors {nullptr};

   if (!widget)
      aAvailableSensors = json_array();

   for (int f = selectActiveValueFacts->find(); f; f = selectActiveValueFacts->fetch())
   {
      char* id {nullptr};
      asprintf(&id, "%s:0x%lx", tableValueFacts->getStrValue("TYPE"), tableValueFacts->getIntValue("ADDRESS"));

      bool active = std::find(sList.begin(), sList.end(), id) != sList.end();  // #PORT
      const char* usrtitle = tableValueFacts->getStrValue("USRTITLE");
      const char* title = tableValueFacts->getStrValue("TITLE");

      if (!isEmpty(usrtitle))
         title = usrtitle;

      if (!widget)
      {
         json_t* oSensor = json_object();
         json_object_set_new(oSensor, "id", json_string(id));
         json_object_set_new(oSensor, "title", json_string(title));
         json_object_set_new(oSensor, "active", json_integer(active));
         json_array_append_new(aAvailableSensors, oSensor);
      }

      free(id);

      if (!active)
         continue;

      json_t* oSample = json_object();
      json_array_append_new(oJson, oSample);

      char* sensor {nullptr};
      asprintf(&sensor, "%s%lu", tableValueFacts->getStrValue("TYPE"), tableValueFacts->getIntValue("ADDRESS"));
      json_object_set_new(oSample, "title", json_string(title));
      json_object_set_new(oSample, "sensor", json_string(sensor));
      json_t* oData = json_array();
      json_object_set_new(oSample, "data", oData);
      free(sensor);

      tableSamples->clear();
      tableSamples->setValue("TYPE", tableValueFacts->getStrValue("TYPE"));
      tableSamples->setValue("ADDRESS", tableValueFacts->getIntValue("ADDRESS"));

      for (int f = select->find(); f; f = select->fetch())
      {
         // tell(eloAlways, "0x%x: '%s' : %0.2f", (uint)tableSamples->getStrValue("ADDRESS"),
         //      xmlTime.getStrValue(), tableSamples->getFloatValue("VALUE"));

         json_t* oRow = json_object();
         json_array_append_new(oData, oRow);

         json_object_set_new(oRow, "x", json_string(xmlTime.getStrValue()));

         if (tableValueFacts->hasValue("TYPE", "DO"))
            json_object_set_new(oRow, "y", json_integer(maxValue.getIntValue()*10));
         else
            json_object_set_new(oRow, "y", json_real(avgValue.getFloatValue()));
      }

      select->freeResult();
   }

   if (!widget)
      json_object_set_new(oMain, "sensors", aAvailableSensors);

   json_object_set_new(oMain, "rows", oJson);
   json_object_set_new(oMain, "id", json_string(id));
   selectActiveValueFacts->freeResult();
   tell(eloDebug, ".. done");
   pushOutMessage(oMain, "chartdata", client);

   return done;
}

//***************************************************************************
// Chart Bookmarks
//***************************************************************************

int Poold::storeChartbookmarks(json_t* array, long client)
{
   char* bookmarks = json_dumps(array, JSON_REAL_PRECISION(4));
   setConfigItem("chartBookmarks", bookmarks);
   free(bookmarks);

   performChartbookmarks(client);

   return done; // replyResult(success, "Bookmarks gespeichert", client);
}

int Poold::performChartbookmarks(long client)
{
   char* bookmarks {nullptr};
   getConfigItem("chartBookmarks", bookmarks, "{[]}");
   json_error_t error;
   json_t* oJson = json_loads(bookmarks, 0, &error);
   pushOutMessage(oJson, "chartbookmarks", client);
   free(bookmarks);

   return done;
}

//***************************************************************************
// Store User Configuration
//***************************************************************************

int Poold::performUserConfig(json_t* oObject, long client)
{
   if (client <= 0)
      return done;

   int rights = getIntFromJson(oObject, "rights", na);
   const char* user = getStringFromJson(oObject, "user");
   const char* passwd = getStringFromJson(oObject, "passwd");
   const char* action = getStringFromJson(oObject, "action");

   tableUsers->clear();
   tableUsers->setValue("USER", user);
   int exists = tableUsers->find();

   if (strcmp(action, "add") == 0)
   {
      if (exists)
         tell(0, "User alredy exists, ignoring 'add' request");
      else
      {
         char* token {nullptr};
         asprintf(&token, "%s_%s_%s", getUniqueId(), l2pTime(time(0)).c_str(), user);
         tell(0, "Add user '%s' with token [%s]", user, token);
         tableUsers->setValue("PASSWD", passwd);
         tableUsers->setValue("TOKEN", token);
         tableUsers->setValue("RIGHTS", urView);
         tableUsers->store();
         free(token);
      }
   }
   else if (strcmp(action, "store") == 0)
   {
      if (!exists)
         tell(0, "User not exists, ignoring 'store' request");
      else
      {
         tell(0, "Store settings for user '%s'", user);
         tableUsers->setValue("RIGHTS", rights);
         tableUsers->store();
      }
   }
   else if (strcmp(action, "del") == 0)
   {
      if (!exists)
         tell(0, "User not exists, ignoring 'del' request");
      else
      {
         tell(0, "Delete user '%s'", user);
         tableUsers->deleteWhere(" user = '%s'", user);
      }
   }
   else if (strcmp(action, "resetpwd") == 0)
   {
      if (!exists)
         tell(0, "User not exists, ignoring 'resetpwd' request");
      else
      {
         tell(0, "Reset password of user '%s'", user);
         tableUsers->setValue("PASSWD", passwd);
         tableUsers->store();
      }
   }
   else if (strcmp(action, "resettoken") == 0)
   {
      if (!exists)
         tell(0, "User not exists, ignoring 'resettoken' request");
      else
      {
         char* token {nullptr};
         asprintf(&token, "%s_%s_%s", getUniqueId(), l2pTime(time(0)).c_str(), user);
         tell(0, "Reset token of user '%s' to '%s'", user, token);
         tableUsers->setValue("TOKEN", token);
         tableUsers->store();
         free(token);
      }
   }

   tableUsers->reset();

   json_t* oJson = json_array();
   userDetails2Json(oJson);
   pushOutMessage(oJson, "userdetails", client);

   return done;
}

//***************************************************************************
// Perform PH Request
//***************************************************************************

int Poold::performPh(long client, bool all)
{
   if (client <= 0)
      return done;

   json_t* oJson = json_object();
   cArduinoInterface::AnalogValue phValue;
   cArduinoInterface::CalSettings calSettings;

   if (all)
   {
      if (arduinoInterface.requestCalGet(calSettings, 0) == success)
      {
         json_object_set_new(oJson, "currentPhA", json_real(calSettings.valueA));
         json_object_set_new(oJson, "currentPhB", json_real(calSettings.valueB));
         json_object_set_new(oJson, "currentCalA", json_real(calSettings.digitsA));
         json_object_set_new(oJson, "currentCalB", json_real(calSettings.digitsB));
      }
      else
      {
         json_object_set_new(oJson, "currentPhA", json_string("--"));
         json_object_set_new(oJson, "currentPhB", json_string("--"));
         json_object_set_new(oJson, "currentCalA", json_string("--"));
         json_object_set_new(oJson, "currentCalB", json_string("--"));
      }
   }

   if (arduinoInterface.requestAi(phValue, 0) == success)
   {
      json_object_set_new(oJson, "currentPh", json_real(phValue.value));
      json_object_set_new(oJson, "currentPhValue", json_integer(phValue.digits));
      json_object_set_new(oJson, "currentPhMinusDemand", json_integer(calcPhMinusVolume(phValue.value)));
   }
   else
   {
      json_object_set_new(oJson, "currentPh", json_string("--"));
      json_object_set_new(oJson, "currentPhValue", json_string("--"));
      json_object_set_new(oJson, "currentPhMinusDemand", json_string("--"));
   }

   pushOutMessage(oJson, "phdata", client);

   return done;
}

//***************************************************************************
// Perform PH Calibration Request
//***************************************************************************

int Poold::performPhCal(json_t* oObject, long client)
{
   if (client <= 0)
      return done;

   json_t* oJson = json_object();
   int duration = getIntFromJson(oObject, "duration");
   cArduinoInterface::CalResponse calResp;

   if (duration > 30)
   {
      tell(0, "Limit duration to 30, %d was requested", duration);
      duration = 30;
   }

   if (arduinoInterface.requestCalibration(calResp, 0, duration) == success)
      json_object_set_new(oJson, "calValue", json_integer(calResp.digits));
   else
      json_object_set_new(oJson, "calValue", json_string("request failed"));

   json_object_set_new(oJson, "duration", json_integer(duration));
   pushOutMessage(oJson, "phcal", client);

   return done;
}

//***************************************************************************
// Perform Request for setting PH Calibration Data
//***************************************************************************

int Poold::performPhSetCal(json_t* oObject, long client)
{
   if (client <= 0)
      return done;

   cArduinoInterface::CalSettings calSettings;

   // first get the actual settings

   if (arduinoInterface.requestCalGet(calSettings, 0) == success)
   {
      // now update what we get from the WS client

      if (getIntFromJson(oObject, "currentPhA", na) != na)
      {
         calSettings.valueA = getIntFromJson(oObject, "currentPhA");
         calSettings.digitsA = getIntFromJson(oObject, "currentCalA");
      }

      if (getIntFromJson(oObject, "currentPhB", na) != na)
      {
         calSettings.valueB = getIntFromJson(oObject, "currentPhB");
         calSettings.digitsB = getIntFromJson(oObject, "currentCalB");
      }

      // and store

      if (arduinoInterface.requestCalSet(calSettings, 0) != success)
         replyResult(fail, "Speichern fehlgeschlagen", client);
      else
         replyResult(success, "gespeichert", client);
   }

   return performPh(client, true);
}

//***************************************************************************
// Perform password Change
//***************************************************************************

int Poold::performPasswChange(json_t* oObject, long client)
{
   if (client <= 0)
      return done;

   const char* user = getStringFromJson(oObject, "user");
   const char* passwd = getStringFromJson(oObject, "passwd");

   if (strcmp(wsClients[(void*)client].user.c_str(), user) != 0)
   {
      tell(0, "Warning: User '%s' tried to change password of '%s'",
           wsClients[(void*)client].user.c_str(), user);
      return done;
   }

   tableUsers->clear();
   tableUsers->setValue("USER", user);

   if (tableUsers->find())
   {
      tell(0, "User '%s' changed password", user);
      tableUsers->setValue("PASSWD", passwd);
      tableUsers->store();
      replyResult(success, "Passwort gespeichert", client);
   }

   tableUsers->reset();

   return done;
}

//***************************************************************************
// Reset Peaks
//***************************************************************************

int Poold::resetPeaks(json_t* obj, long client)
{
   tablePeaks->truncate();
   setConfigItem("peakResetAt", l2pTime(time(0)).c_str());

   json_t* oJson = json_object();
   config2Json(oJson);
   pushOutMessage(oJson, "config", client);

   return done;
}

//***************************************************************************
// Store Configuration
//***************************************************************************

int Poold::storeConfig(json_t* obj, long client)
{
   const char* key {nullptr};
   json_t* jValue {nullptr};
   int oldWebPort = webPort;

   json_object_foreach(obj, key, jValue)
   {
      tell(3, "Debug: Storing config item '%s' with '%s'", key, json_string_value(jValue));
      setConfigItem(key, json_string_value(jValue));
   }

   // create link for the stylesheet

   const char* name = getStringFromJson(obj, "style");

   if (!isEmpty(name))
   {
      tell(1, "Info: Creating link 'stylesheet.css' to '%s'", name);
      char* link {nullptr};
      char* target {nullptr};
      asprintf(&link, "%s/stylesheet.css", httpPath);
      asprintf(&target, "%s/stylesheet-%s.css", httpPath, name);
      createLink(link, target, true);
      free(link);
      free(target);
   }

   // reload configuration

   readConfiguration();

   json_t* oJson = json_object();
   config2Json(oJson);
   pushOutMessage(oJson, "config", client);

   if (oldWebPort != webPort)
      replyResult(success, "Konfiguration gespeichert. Web Port geändert, bitte poold neu Starten!", client);
   else
      replyResult(success, "Konfiguration gespeichert", client);

   return done;
}

int Poold::storeIoSetup(json_t* array, long client)
{
   size_t index {0};
   json_t* jObj {nullptr};

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

   return replyResult(success, "Konfiguration gespeichert", client);
}

//***************************************************************************
// Config 2 Json
//***************************************************************************

int Poold::config2Json(json_t* obj)
{
   for (const auto& it : configuration)
   {
      tableConfig->clear();
      tableConfig->setValue("OWNER", myName());
      tableConfig->setValue("NAME", it.name.c_str());

      if (tableConfig->find())
         json_object_set_new(obj, tableConfig->getStrValue("NAME"), json_string(tableConfig->getStrValue("VALUE")));

      tableConfig->reset();
   }

   return done;
}

//***************************************************************************
// Config Details 2 Json
//***************************************************************************

int Poold::configDetails2Json(json_t* obj)
{
   for (const auto& it : configuration)
   {
      if (it.internal)
         continue;

      json_t* oDetail = json_object();
      json_array_append_new(obj, oDetail);

      json_object_set_new(oDetail, "name", json_string(it.name.c_str()));
      json_object_set_new(oDetail, "type", json_integer(it.type));
      json_object_set_new(oDetail, "category", json_string(it.category));
      json_object_set_new(oDetail, "title", json_string(it.title));
      json_object_set_new(oDetail, "descrtiption", json_string(it.description));

      if (it.type == ctChoice)
         configChoice2json(oDetail, it.name.c_str());

      tableConfig->clear();
      tableConfig->setValue("OWNER", myName());
      tableConfig->setValue("NAME", it.name.c_str());

      if (tableConfig->find())
         json_object_set_new(oDetail, "value", json_string(tableConfig->getStrValue("VALUE")));

      tableConfig->reset();
   }

   return done;
}

int Poold::configChoice2json(json_t* obj, const char* name)
{
   if (strcmp(name, "style") == 0)
   {
      FileList options;
      int count {0};

      if (getFileList(httpPath, DT_REG, "css", false, &options, count) == success)
      {
         json_t* oArray = json_array();

         for (const auto& opt : options)
         {
            if (strncmp(opt.name.c_str(), "stylesheet-", strlen("stylesheet-")) != 0)
               continue;

            char* p = strdup(srrchr(opt.name.c_str(), '-'));
            *(strrchr(p, '.')) = '\0';
            json_array_append_new(oArray, json_string(p+1));
         }

         json_object_set_new(obj, "options", oArray);
      }
   }

   return done;
}

//***************************************************************************
// User Details 2 Json
//***************************************************************************

int Poold::userDetails2Json(json_t* obj)
{
   for (int f = selectAllUser->find(); f; f = selectAllUser->fetch())
   {
      json_t* oDetail = json_object();
      json_array_append_new(obj, oDetail);

      json_object_set_new(oDetail, "user", json_string(tableUsers->getStrValue("USER")));
      json_object_set_new(oDetail, "rights", json_integer(tableUsers->getIntValue("RIGHTS")));
   }

   selectAllUser->freeResult();

   return done;
}

//***************************************************************************
// Value Facts 2 Json
//***************************************************************************

int Poold::valueFacts2Json(json_t* obj, bool filterActive)
{
   tableValueFacts->clear();

   for (int f = selectAllValueFacts->find(); f; f = selectAllValueFacts->fetch())
   {
      if (filterActive && !tableValueFacts->hasValue("STATE", "A"))
         continue;

      json_t* oData = json_object();
      json_array_append_new(obj, oData);

      json_object_set_new(oData, "address", json_integer((ulong)tableValueFacts->getIntValue("ADDRESS")));
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

   json_object_set_new(obj, "address", json_integer((ulong)table->getIntValue("ADDRESS")));
   json_object_set_new(obj, "type", json_string(table->getStrValue("TYPE")));
   json_object_set_new(obj, "name", json_string(table->getStrValue("NAME")));

   if (!table->getValue("USRTITLE")->isEmpty())
      json_object_set_new(obj, "title", json_string(table->getStrValue("USRTITLE")));
   else
      json_object_set_new(obj, "title", json_string(table->getStrValue("TITLE")));

   json_object_set_new(obj, "unit", json_string(table->getStrValue("UNIT")));
   json_object_set_new(obj, "scalemax", json_integer(table->getIntValue("MAXSCALE")));
   json_object_set_new(obj, "rights", json_integer(table->getIntValue("RIGHTS")));

   json_object_set_new(obj, "peak", json_real(peak));

   return done;
}

//***************************************************************************
// Process
//***************************************************************************

int Poold::process()
{
   time_t tPoolLast, tSolarLast;
   tPool = valueOfW1(w1AddrPool, tPoolLast);
   tSolar = valueOfW1(w1AddrSolar, tSolarLast);
   tCurrentDelta = tSolar - tPool;

   // use W1 values only if not older than 2 cycles

   bool w1Valid = tPoolLast > time(0) - 2*interval && tSolarLast > time(0) - 2*interval;

   tell(0, "Process ...");

   // -----------
   // Solar Pumps

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

   if (aiSensors[aiFilterPressure].value < 0.4)
   {
      // pressure is less than 0.4 bar

      if (digitalOutputStates[pinFilterPump].state &&
          digitalOutputStates[pinFilterPump].last < time(0) - 5*tmeSecondsPerMinute)
      {
         // and pump is runnning longer than 5 minutes

         gpioWrite(pinFilterPump, false);
         gpioWrite(pinSolarPump, false);
         digitalOutputStates[pinFilterPump].mode = omManual;
         digitalOutputStates[pinSolarPump].mode = omManual;

         char* body;
         asprintf(&body, "Filter pressure is %.2f bar and pump is running!\n Pumps switched off now!",
                  aiSensors[aiFilterPressure].value);
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

int Poold::addValueFact(int addr, const char* type, const char* name, const char* unit, int rights)
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
      tableValueFacts->setValue("RIGHTS", rights);
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

      if (tableValueFacts->getValue("RIGHTS")->isNull())
         tableValueFacts->setValue("RIGHTS", rights);

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
   value = nullptr;

   tableConfig->clear();
   tableConfig->setValue("OWNER", myName());
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
   tableConfig->setValue("OWNER", myName());
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
// Calc PH Minus Volume
//***************************************************************************

int Poold::calcPhMinusVolume(double ph)
{
   double phLack = ph - phReference;
   double mlPer01 = phMinusDemand01 * (1.0/phMinusDensity);

   return (phLack/0.1) * mlPer01;
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
   auto pos = result.find(':');

   if (pos == std::string::npos)
   {
      tell(0, "Error: Failed to parse result of script (%ld) [%s]", addr, result.c_str());
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

      json_object_set_new(ojData, "address", json_integer((ulong)addr));
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
         json_object_set_new(ojData, "widgettype", json_integer(wtChart));
      }

      pushOutMessage(oJson, "update");
   }

   if (!isEmpty(mqttUrl))
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

   if (!isEmpty(mqttUrl))
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

   w1Sensors[id].value = value;
   w1Sensors[id].last = time(0);

   json_t* ojData = json_object();

   tableValueFacts->clear();
   tableValueFacts->setValue("ADDRESS", (int)toW1Id(id));
   tableValueFacts->setValue("TYPE", "W1");

   if (tableValueFacts->find())
   {
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
      if (it->second.last < time(0) - 30)
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
