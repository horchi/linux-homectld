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

#include "poold.h"

int Poold::shutdown = no;

//***************************************************************************
// Object
//***************************************************************************

Poold::Poold()
{
   nextAt = time(0);           // intervall for 'reading values'
   startedAt = time(0);

   cDbConnection::init();
   cDbConnection::setEncoding("utf8");
   cDbConnection::setHost(dbHost);
   cDbConnection::setPort(dbPort);
   cDbConnection::setName(dbName);
   cDbConnection::setUser(dbUser);
   cDbConnection::setPass(dbPass);
}

Poold::~Poold()
{
   exit();

   delete mqttWriter;
   delete mqttReader;

   free(mailScript);
   free(stateMailAtStates);
   free(stateMailTo);
   free(w1Pool);
   free(w1Solar);

   cDbConnection::exit();
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
   // prepare one wire sensors

   if (w1.scan() == success)
   {
      W1::SensorList* list = w1.getList();

      // yes, we have one-wire sensors

      int count {0};
      int added {0};
      int modified {0};

      for (auto it = list->begin(); it != list->end(); ++it)
      {
         // update table

         tableValueFacts->clear();
         tableValueFacts->setValue("ADDRESS", (int)W1::toId(it->first.c_str()));
         tableValueFacts->setValue("TYPE", "W1");

         if (!tableValueFacts->find())
         {
            tableValueFacts->setValue("NAME", it->first.c_str());
            tableValueFacts->setValue("STATE", "D");
            tableValueFacts->setValue("UNIT", "°C");
            tableValueFacts->setValue("TITLE", it->first.c_str());
            tableValueFacts->setValue("MAXSCALE", 300);

            tableValueFacts->store();
            added++;
         }
         else
         {
            tableValueFacts->clearChanged();

            if (tableValueFacts->getValue("MAXSCALE")->isNull())
               tableValueFacts->setValue("MAXSCALE", 300);

            if (tableValueFacts->getChanges())
            {
               tableValueFacts->store();
               modified++;
            }
         }

         count++;
      }

      tell(eloAlways, "Found %d one wire sensors, added %d, modified %d", count, added, modified);
   }

   // setup GPIO

   wiringPiSetupPhys();     // use the 'physical' PIN numbers
   // wiringPiSetup();      // use the 'special' wiringPi PIN numbers
   // wiringPiSetupGpio();  // use the 'GPIO' PIN numbers

   pinMode(pinFilterPump, OUTPUT);
   pinMode(pinSolarPump, OUTPUT);
   pinMode(pinPoolLight, OUTPUT);
   gpioWrite(pinFilterPump, stateFilterPump);
   gpioWrite(pinSolarPump, stateSolarPump);
   gpioWrite(pinPoolLight, statePoollight);

   return success;
}

int Poold::exit()
{
   gpioWrite(pinFilterPump, false);
   gpioWrite(pinSolarPump, false);
   gpioWrite(pinPoolLight, false);

   exitDb();

   return success;
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

   readConfiguration();

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

   getConfigItem("mail", mail, no);
   getConfigItem("htmlMail", htmlMail, no);
   getConfigItem("mailScript", mailScript, BIN_PATH "/poold-mail.sh");
   getConfigItem("stateMailStates", stateMailAtStates, "0,1,3,19");
   getConfigItem("stateMailTo", stateMailTo);

   // more special

   getConfigItem("w1AddrPool", w1Pool, "");
   getConfigItem("w1AddrSolar", w1Solar, "");

   getConfigItem("tPoolMax", tPoolMax, tPoolMax);
   getConfigItem("tSolarDelta", tSolarDelta, tSolarDelta);

   getConfigItem("gpioFilterPump", gpioFilterPump, gpioFilterPump);
   getConfigItem("gpioSolarPump", gpioSolarPump, gpioSolarPump);

   return done;
}

//***************************************************************************
// Store
//***************************************************************************

int Poold::store(time_t now, const char* name, const char* title, const char* unit,
               const char* type, int address, double value,
               unsigned int factor, const char* text)
{
   double theValue = value / (double)factor;

   tableSamples->clear();

   tableSamples->setValue("TIME", now);
   tableSamples->setValue("ADDRESS", address);
   tableSamples->setValue("TYPE", type);
   tableSamples->setValue("AGGREGATE", "S");

   tableSamples->setValue("VALUE", theValue);
   tableSamples->setValue("TEXT", text);
   tableSamples->setValue("SAMPLES", 1);

   tableSamples->store();

   // peaks

   tablePeaks->clear();

   tablePeaks->setValue("ADDRESS", address);
   tablePeaks->setValue("TYPE", type);

   if (!tablePeaks->find())
   {
      tablePeaks->setValue("MIN", theValue);
      tablePeaks->setValue("MAX", theValue);
      tablePeaks->store();
   }
   else
   {
      if (theValue > tablePeaks->getFloatValue("MAX"))
         tablePeaks->setValue("MAX", theValue);

      if (theValue < tablePeaks->getFloatValue("MIN"))
         tablePeaks->setValue("MIN", theValue);

      tablePeaks->store();
   }

   // Home Assistant

   if (!isEmpty(hassMqttUrl))
      hassPush(name, title, unit, theValue, text, initialRun /*forceConfig*/);

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
   static time_t lastCleanup = time(0);

   if (!connection || !connection->isConnected())
      return fail;

   if (mqttReader && mqttReader->isConnected()) mqttReader->yield();
   if (mqttWriter && mqttWriter->isConnected()) mqttWriter->yield();

      performWebifRequests();

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
   time_t nextStateAt {0};

   // info

   if (mail && !isEmpty(stateMailTo))
      tell(eloAlways, "Mail at states '%s' to '%s'", stateMailAtStates, stateMailTo);

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

      meanwhile();
      standbyUntil(min(nextStateAt, nextAt));

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
   time_t now = time(0);
   int count {0};

   w1.update();

   tell(eloDetail, "Update ...");

   connection->startTransaction();

   for (int f = selectActiveValueFacts->find(); f; f = selectActiveValueFacts->fetch())
   {
      int addr = tableValueFacts->getIntValue("ADDRESS");
      const char* title = tableValueFacts->getStrValue("TITLE");
      const char* type = tableValueFacts->getStrValue("TYPE");
      const char* unit = tableValueFacts->getStrValue("UNIT");
      const char* name = tableValueFacts->getStrValue("NAME");

      if (tableValueFacts->hasValue("TYPE", "W1"))
      {
         store(now, name, title, unit, type, addr, w1.valueOf(name), 1);
      }

      count++;
   }

   connection->commit();
   tell(eloAlways, "Updated %d samples", count);

   return success;
}

//***************************************************************************
// Process
//***************************************************************************

int Poold::process()
{
   tPool = w1.valueOf(w1Pool);
   tSolar = w1.valueOf(w1Solar);
   tCurrentDelta = tSolar - tPool;

   tell(0, "Process ...");
   logReport();

   if (isEmpty(w1Pool) || isEmpty(w1Solar))
   {
      tell(0, "Fatal: Missing configuration of 'w1Pool' and/or 'w1Solar'");
      return done;
   }

   if (tPool > tPoolMax)
   {
      // switch OFF solar pump

      if (stateSolarPump)
      {
         tell(0, "Configured pool maximum of %.2f°C reached, pool has is %.2f°C, stopping solar pump!", tPoolMax, tPool);
         gpioWrite(pinSolarPump, stateSolarPump = false);
      }
   }
   else if (tCurrentDelta > tSolarDelta)
   {
      // switch ON solar pump

      if (!stateSolarPump)
      {
         tell(0, "Solar delta of %.2f°C reached, pool has %.2f°C, starting solar pump!", tSolarDelta, tPool);
         gpioWrite(pinSolarPump, stateSolarPump = true);
      }
   }
   else
   {
      // switch OFF solar pump

      if (stateSolarPump)
      {
         tell(0, "Solar delta (%.2f°C) lower than %.2f°C, pool has %.2f°C, stopping solar pump!", tCurrentDelta, tSolarDelta, tPool);
         gpioWrite(pinSolarPump, stateSolarPump = false);
      }
   }

   logReport();

   return success;
}

//***************************************************************************
// Report to syslog
//***************************************************************************

void Poold::logReport()
{
   tell(0, "------------------------");
   tell(0, "Pool has %.2f °C; Solar has %.2f °C; Current delta is %.2f°", tPool, tSolar, tCurrentDelta);
   tell(0, "Solar pump is '%s'", stateSolarPump ? "running" : "stopped");
   tell(0, "Filter pump is '%s'", stateFilterPump ? "running" : "stopped");
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

   // send mail ...

   if (!htmlMail)
      return sendMail(stateMailTo, subject.c_str(), mailBody.c_str(), "text/plain");

   // HTML mail

   char* html = 0;

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
// Stored Parameters
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

void Poold::gpioWrite(uint pin, bool state)
{
   // negate state until the relai board is active at 'false'

   digitalWrite(pin, !state);
}
