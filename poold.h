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

#include <queue>
#include <jansson.h>

#include "lib/common.h"
#include "lib/db.h"
#include "lib/mqtt.h"

#include "HISTORY.h"

#include "websock.h"
#include "ph.h"

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

class Poold : public cWebInterface
{
   public:

      enum Pins       // we use the 'physical' PIN numbers here!
      {
         pinW1           = 7,      // GPIO4
         pinSerialTx     = 6,      // GPIO14
         pinSerialRx     = 8,      // GPIO15

         pinFilterPump   = 11,     // GPIO17
         pinSolarPump    = 12,     // GPIO18
         pinPoolLight    = 13,     // GPIO27
         pinUVC          = 15,     // GPIO22
         pinUserOut1     = 16,     // GPIO23
         pinUserOut2     = 18,     // GPIO24
         pinW1Power      = 19,     // GPIO10
         pinShower       = 22,     // GPIO25
         pinUserOut3     = 23,     // GPIO11

         pinLevel1       = 31,     // GPIO6
         pinLevel2       = 32,     // GPIO12
         pinLevel3       = 33,     // GPIO13
         pinShowerSwitch = 35      // GPIO19
      };

      // object

      Poold();
      virtual ~Poold();

      int init();
      int loop();

      const char* myName() override  { return "poold"; }
      static void downF(int aSignal) { shutdown = yes; }

   protected:

      enum WidgetType
      {
         wtSymbol = 0,  // == 0
         wtGauge,       // == 1
         wtText,        // == 2
         wtValue        // == 3
      };

      enum IoType
      {
         iotSensor = 0,
         iotLight
      };

      // moved here for debugging !!

      enum OutputMode
      {
         omAuto = 0,
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
         const char* name;     // crash on init with nullptr :o
         std::string title;
         time_t last {0};      // last switch time
         time_t next {0};      // calculated next switch time
      };

      struct Range
      {
         uint from;
         uint to;

         bool inRange(uint t)  const    { return (from <= t && to >= t); }
      };

      enum ConfigItemType
      {
         ctInteger = 0,
         ctNum,
         ctString,
         ctBool,
         ctRange
      };

      struct ConfigItemDef
      {
         std::string name;
         ConfigItemType type;
         bool internal;
         const char* category;
         const char* title;
         const char* description;
      };

      std::map<uint,OutputState> digitalOutputStates;
      std::map<int,bool> digitalInputStates;

      int exit();
      int initDb();
      int exitDb();
      int readConfiguration();
      int applyConfigurationSpecials();

      int addValueFact(int addr, const char* type, const char* name, const char* unit);
      int initOutput(uint pin, int opt, OutputMode mode, const char* name, uint rights = urControl);
      int initInput(uint pin, const char* name);
      int initScripts();

      int standby(int t);
      int standbyUntil(time_t until);
      int meanwhile();

      int update(bool webOnly = false, long client = 0);   // called each (at least) 'interval'
      int process();                                       // called each 'interval'
      int performJobs();                                   // called every loop (1 second)
      int performWebSocketPing();
      int dispatchClientRequest();
      bool checkRights(long client, Event event, json_t* oObject);
      std::string callScript(int addr, const char* type, const char* command);
      int publishScriptResult(ulong addr, const char* type, std::string result);
      bool isInTimeRange(const std::vector<Range>* ranges, time_t t);
      int store(time_t now, const char* name, const char* title, const char* unit, const char* type, int address, double value, const char* text = 0);

      int performHassRequests();
      int hassPush(IoType iot, const char* name, const char* title, const char* unit, double theValue, const char* text = 0, bool forceConfig = false);
      int hassCheckConnection();

      int scheduleAggregate();
      int aggregate();

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
      void gpioWrite(uint pin, bool state, bool callJobs = true);
      bool gpioRead(uint pin);
      void logReport();

      // web

      int pushOutMessage(json_t* obj, const char* title, long client = 0);

      int pushInMessage(const char* data) override;
      std::queue<std::string> messagesIn;
      cMyMutex messagesInMutex;

      int performLogin(json_t* oObject);
      int performLogout(json_t* oObject);
      int performTokenRequest(json_t* oObject, long client);
      int performSyslog(long client);
      int performConfigDetails(long client);
      int performUserDetails(long client);
      int performIoSettings(long client);
      int performChartData(json_t* oObject, long client);
      int performUserConfig(json_t* oObject, long client);
      int performPasswChange(json_t* oObject, long client);
      int performPh(long client, bool all);
      int performPhCal(json_t* obj, long client);
      int performPhSetCal(json_t* oObject, long client);
      int storeConfig(json_t* obj, long client);
      int storeIoSetup(json_t* array, long client);
      int resetPeaks(json_t* obj, long client);

      int config2Json(json_t* obj);
      int configDetails2Json(json_t* obj);
      int userDetails2Json(json_t* obj);
      int valueFacts2Json(json_t* obj);
      int daemonState2Json(json_t* obj);
      int sensor2Json(json_t* obj, cDbTable* table);
      void pin2Json(json_t* ojData, int pin);

      const char* getImageOf(const char* title, int value);
      int toggleIo(uint addr, const char* type);
      int toggleIoNext(uint pin);
      int toggleOutputMode(uint pin);

      // W1

      int initW1();
      bool existW1(const char* id);
      double valueOfW1(const char* id, time_t& last);
      uint toW1Id(const char* name);
      void updateW1(const char* id, double value);
      void cleanupW1();

      // data

      bool initialized {false};
      cDbConnection* connection {nullptr};

      cDbTable* tableSamples {nullptr};
      cDbTable* tablePeaks {nullptr};
      cDbTable* tableValueFacts {nullptr};
      cDbTable* tableConfig {nullptr};
      cDbTable* tableScripts {nullptr};
      cDbTable* tableUsers {nullptr};

      cDbStatement* selectActiveValueFacts {nullptr};
      cDbStatement* selectAllValueFacts {nullptr};
      cDbStatement* selectAllConfig {nullptr};
      cDbStatement* selectAllUser {nullptr};
      cDbStatement* selectMaxTime {nullptr};
      cDbStatement* selectSamplesRange {nullptr};
      cDbStatement* selectSamplesRange60 {nullptr};
      cDbStatement* selectScriptByPath {nullptr};

      cDbValue xmlTime;
      cDbValue rangeFrom;
      cDbValue rangeTo;
      cDbValue avgValue;
      cDbValue maxValue;

      time_t nextAt {0};
      time_t startedAt {0};
      time_t nextAggregateAt {0};

      std::string mailBody;
      std::string mailBodyHtml;
      bool initialRun {true};

      cWebSock* webSock {nullptr};
      time_t nextWebSocketPing {0};
      int webSocketPingTime {60};

      struct WsClient    // Web Socket Client
      {
         std::string user;
         uint rights;                  // rights mask
         bool dataUpdates {false};     // true if interested on data update
         ClientType type {ctActive};
      };

      std::map<void*,WsClient> wsClients;

      // Home Assistant stuff

      Mqtt* mqttWriter {nullptr};
      Mqtt* mqttReader {nullptr};
      Mqtt* mqttCommandReader {nullptr};
      Mqtt* mqttW1Reader {nullptr};

      // config

      int interval {60};
      int aggregateInterval {15};         // aggregate interval in minutes
      int aggregateHistory {0};           // history in days
      char* hassMqttUrl {nullptr};
      std::map<std::string,std::string> hassCmdTopicMap; // 'topic' to 'name' map

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
      int waterLevel {0};
      int showerDuration {20};         // seconds
      int minSolarPumpDuration {10};   // minutes
      int deactivatePumpsAtLowWater {no};

      char* chart1 {nullptr};

      std::vector<Range> filterPumpTimes;
      std::vector<Range> uvcLightTimes;
      std::vector<Range> poolLightTimes;

      // serial interface to arduino for PH stuff

      cPhInterface phInterface;
      char* phDevice {nullptr};

      // actual state and data

      double tPool {0.0};
      double tSolar {0.0};
      double tCurrentDelta {0.0};

      struct W1Data
      {
         time_t last;
         double value;
      };

      std::map<std::string, W1Data> w1Sensors;

      // statics

      static std::list<ConfigItemDef> configuration;
      static int shutdown;
};
