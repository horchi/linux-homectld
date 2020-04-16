//***************************************************************************
// poold / Linux - Pool Steering
// File poold.h
// This code is distributed under the terms and conditions of the
// GNU GENERAL PUBLIC LICENSE. See the file LICENSE for details.
// Date 16.04.2020  Jörg Wendel
//***************************************************************************

#pragma once

//***************************************************************************
// Includes
//***************************************************************************

#include "lib/db.h"
#include "lib/mqtt.h"

#include "w1.h"
#include "HISTORY.h"

#define confDirDefault "/etc/poold"

extern char dbHost[];
extern int  dbPort;
extern char hassMqttUrl[];
extern char dbName[];
extern char dbUser[];
extern char dbPass[];

extern int interval;
extern int aggregateInterval;        // aggregate interval in minutes
extern int aggregateHistory;         // history in days
extern char* confDir;

//***************************************************************************
// Class Pool Daemon
//***************************************************************************

class Poold
{
   public:

      // object

      Poold();
      ~Poold();

      int init();
	   int loop();

      static void downF(int aSignal) { shutdown = yes; }

   protected:

      int exit();
      int initDb();
      int exitDb();
      int readConfiguration();

      int standby(int t);
      int standbyUntil(time_t until);
      int meanwhile();

      int update();
      int process();

      int store(time_t now, const char* name, const char* title, const char* unit, const char* type, int address, double value,
                unsigned int factor, const char* text = 0);

      int hassPush(const char* name, const char* title, const char* unit, double theValue, const char* text = 0, bool forceConfig = false);
      int hassCheckConnection();

      int scheduleAggregate();
      int aggregate();

      void afterUpdate();
      int sendStateMail();
      int sendMail(const char* receiver, const char* subject, const char* body, const char* mimeType);

      int loadHtmlHeader();

      int getConfigItem(const char* name, char*& value, const char* def = "");
      int setConfigItem(const char* name, const char* value);
      int getConfigItem(const char* name, int& value, int def = na);
      int setConfigItem(const char* name, int value);

      int getConfigItem(const char* name, double& value, double def = na);
      int setConfigItem(const char* name, double value);

      int doShutDown() { return shutdown; }

      // data

      cDbConnection* connection {nullptr};

      cDbTable* tableSamples {nullptr};
      cDbTable* tablePeaks {nullptr};
      cDbTable* tableValueFacts {nullptr};
      cDbTable* tableConfig {nullptr};

      cDbStatement* selectActiveValueFacts {nullptr};
      cDbStatement* selectAllValueFacts {nullptr};
      cDbStatement* selectMaxTime {nullptr};

      time_t nextAt {0};
      time_t startedAt {0};
      time_t nextAggregateAt {0};

      W1 w1;                       // for one wire sensors

      string mailBody {""};
      string mailBodyHtml {""};
      bool initialRun {true};

      // Home Assistant stuff

      MqTTPublishClient* mqttWriter {nullptr};
      MqTTSubscribeClient* mqttReader {nullptr};

      // config

      int mail {no};
      int htmlMail {no};
      char* mailScript {nullptr};
      char* stateMailAtStates {nullptr};
      char* stateMailTo {nullptr};
      MemoryStruct htmlHeader;

      char* w1Pool {nullptr};
      char* w1Solar {nullptr};

      double tPoolMax {28.0};
      double tSolarDelta {5.0};

      int gpioFilterPump {na};
      int gpioSolarPump {na};

      // statics

      static int shutdown;
};
