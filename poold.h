//***************************************************************************
// poold / Linux - Pool Steering
// File poold.h
// This code is distributed under the terms and conditions of the
// GNU GENERAL PUBLIC LICENSE. See the file LICENSE for details.
// Date 16.04.2020 - Jörg Wendel
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
extern char dbName[];
extern char dbUser[];
extern char dbPass[];

extern char* confDir;

//***************************************************************************
// Class Pool Daemon
//***************************************************************************

class Poold
{
   public:

      enum Pins  // we use the 'physical' PIN numbers here!
      {
         pinW1 = 7,              // GPIO4

         pinFilterPump = 11,     // GPIO17
         pinSolarPump  = 12,     // GPIO18
         pinPoolLight  = 13,     // GPIO27
         pinUVC        = 15,     // GPIO22
         pinUserOut1   = 16,     // GPIO23
         pinUserOut2   = 18,     // GPIO24
         pinUserOut3   = 22,     // GPIO25

         pinLevel1     = 31,     // GPIO6
         pinLevel2     = 32,     // GPIO12
         pinLevel3     = 33      // GPIO13
      };

      // object

      Poold();
      ~Poold();

      int init();
	   int loop();

      static void downF(int aSignal) { shutdown = yes; }

   protected:

      enum IoType
      {
         iotSensor,
         iotLight
      };

      // moved here for debugging !!

      enum OutputMode
      {
         omAuto,
         omManual
      };

      enum OutputOptions
      {
         ooUser = 0x01,        // Output can contolled by user
         ooAuto = 0x02         // Output can contolled by poold
      };

      struct OutputState
      {
         bool state {false};
         OutputMode mode {omAuto};
         uint opt {ooUser};
         const char* name;
      };

      struct Range
      {
         uint from;
         uint to;

         bool inRange(uint t)  const    { return (from <= t && to >= t); }
      };

      std::map<int,OutputState> digitalOutputStates;
      std::map<int,bool> digitalInputStates;

      int exit();
      int initDb();
      int exitDb();
      int readConfiguration();
      int applyConfigurationSpecials();

      int addValueFact(int addr, const char* type, const char* name, const char* unit);
      int initOutput(uint pin, int opt, OutputMode mode, const char* name);
      int initInput(uint pin, const char* name);
      int initW1();

      int standby(int t);
      int standbyUntil(time_t until);
      int meanwhile();

      int update();
      int process();

      bool isInTimeRange(const std::vector<Range>* ranges, time_t t);
      int store(time_t now, const char* name, const char* title, const char* unit, const char* type, int address, double value, const char* text = 0);

      int hassPush(IoType iot, const char* name, const char* title, const char* unit, double theValue, const char* text = 0, bool forceConfig = false);
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

      int getConfigTimeRangeItem(const char* name, std::vector<Range>& ranges);

      int doShutDown() { return shutdown; }

      int getWaterLevel();
      void gpioWrite(uint pin, bool state);
      bool gpioRead(uint pin);
      void logReport();

      // web

      int performWebifRequests();
      int cleanupWebifRequests();
      int toggleIo(uint pin);
      int toggleOutputMode(uint pin, OutputMode mode);

      // data

      bool initialized {false};
      cDbConnection* connection {nullptr};

      cDbTable* tableSamples {nullptr};
      cDbTable* tablePeaks {nullptr};
      cDbTable* tableValueFacts {nullptr};
      cDbTable* tableConfig {nullptr};
      cDbTable* tableJobs {nullptr};

      cDbStatement* selectActiveValueFacts {nullptr};
      cDbStatement* selectAllValueFacts {nullptr};
      cDbStatement* selectMaxTime {nullptr};
      cDbStatement* selectPendingJobs {nullptr};
      cDbStatement* cleanupJobs {nullptr};

      time_t nextAt {0};
      time_t startedAt {0};
      time_t nextAggregateAt {0};

      W1 w1;

      string mailBody;
      string mailBodyHtml;
      bool initialRun {true};

      // Home Assistant stuff

      MqTTPublishClient* mqttWriter {nullptr};
      MqTTSubscribeClient* mqttReader {nullptr};

      // config

      int interval {60};
      int aggregateInterval {15};         // aggregate interval in minutes
      int aggregateHistory {0};           // history in days
      char* hassMqttUrl {0};

      int mail {no};
      char* mailScript {nullptr};
      char* stateMailTo {nullptr};
      MemoryStruct htmlHeader;

      int invertDO {no};
      int poolLightColorToggle {no};
      char* w1AddrPool {nullptr};
      char* w1AddrSolar {nullptr};
      char* w1AddrSuctionTube {nullptr};
      char* w1AddrAir {nullptr};

      double tPoolMax {28.0};
      double tSolarDelta {5.0};

      std::vector<Range> filterPumpTimes;
      std::vector<Range> uvcLightTimes;
      std::vector<Range> poolLightTimes;

      // int gpioFilterPump {na};
      // int gpioSolarPump {na};

      // actual state and data

      double tPool {0.0};
      double tSolar {0.0};
      double tCurrentDelta {0.0};

      // statics

      static int shutdown;
};
