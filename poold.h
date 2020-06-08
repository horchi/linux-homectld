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
#include <libwebsockets.h>

#include "lib/db.h"
#include "lib/mqtt.h"
#include "lib/thread.h"

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
// Class Web Service
//***************************************************************************

class cWebService
{
   public:

      enum Event
      {
         evUnknown,
         evLogin,
         evLogout,
         evToggleIo,
         evToggleIoNext,
         evToggleMode,
         evStoreConfig,

         evCount
      };

      enum ClientType
      {
         ctWithLogin = 0,
         ctActive
      };

      static const char* toName(Event event);
      static Event toEvent(const char* name);

      static const char* events[];
};

//***************************************************************************
// Class cWebSock
//***************************************************************************

class cWebSock : public cWebService
{
   public:

      enum MsgType
      {
         mtNone = na,
         mtPing,    // 0
         mtData     // 1
      };

      enum Protokoll
      {
         sizeLwsPreFrame  = LWS_SEND_BUFFER_PRE_PADDING,
         sizeLwsPostFrame = LWS_SEND_BUFFER_POST_PADDING,
         sizeLwsFrame     = sizeLwsPreFrame + sizeLwsPostFrame
      };

      struct SessionData
      {
         char* buffer;
         int bufferSize;
         int payloadSize;
         int dataPending;
      };

      struct Client
      {
         ClientType type;
         int tftprio;
         std::queue<std::string> messagesOut;
         cMyMutex messagesOutMutex;
         void* wsi;

         void pushMessage(const char* p)
         {
            cMyMutexLock lock(&messagesOutMutex);
            messagesOut.push(p);
         }

         void cleanupMessageQueue()
         {
            cMyMutexLock lock(&messagesOutMutex);

            // just in case client is not connected and wasted messages are pending

            tell(0, "Info: Flushing (%d) old 'wasted' messages of client (%p)", messagesOut.size(), wsi);

            while (!messagesOut.empty())
               messagesOut.pop();
         }
      };

      cWebSock(const char* aHttpPath);
      virtual ~cWebSock();

      void setLoginToken(const char* aLoginToken) {free(loginToken); loginToken = strdup(aLoginToken);}
      int init(int aPort, int aTimeout);
      int exit();

      int service(int timeoutMs);
      int performData(MsgType type);

      // status callback methods

      static int wsLogLevel;
      static int callbackHttp(lws* wsi, lws_callback_reasons reason, void* user, void* in, size_t len);
      static int callbackPool(lws* wsi, lws_callback_reasons reason, void* user, void* in, size_t len);

      // static interface

      static void atLogin(lws* wsi, const char* message, const char* clientInfo);
      static void atLogout(lws* wsi, const char* message, const char* clientInfo);
      static int getClientCount();
      static void pushMessage(const char* p, lws* wsi = 0);
      static void writeLog(int level, const char* line);

   private:

      static int serveFile(lws* wsi, const char* path);
      static int dispatchDataRequest(lws* wsi, SessionData* sessionData, const char* url);

      static const char* methodOf(const char* url);
      static const char* getStrParameter(lws* wsi, const char* name, const char* def = 0);
      static int getIntParameter(lws* wsi, const char* name, int def = na);

      //

      int port {na};
      lws_protocols protocols[3];

      // statics

      static lws_context* context;

      static char* loginToken;
      static char* httpPath;
      static char* epgImagePath;
      static int timeout;
      static std::map<void*,Client> clients;
      static cMyMutex clientsMutex;
      static MsgType msgType;

      // only used in callback

      static char* msgBuffer;
      static int msgBufferSize;
};

//***************************************************************************
// Class Pool Daemon
//***************************************************************************

class Poold : public cWebService
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
         pinShower     = 22,     // GPIO25
         pinUserOut3   = 23,     // GPIO11

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

      // public static message interface to web thread

      int pushMessage(json_t* obj, const char* title, long client = 0);
      static std::queue<std::string> messagesIn;

   protected:

      enum WidgetType
      {
         wtSymbol,     // == 0
         wtGauge,      // == 1
         wtText,       // == 2
         wtValue       // == 3
      };

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
         const char* name;     // crash on init with nullptr :o
         std::string title;
         time_t last {0};
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
         ConfigItemType type;
         bool internal;
         std::string category;
         std::string title;
         std::string description;
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

      int update(bool webOnly = false, long client = 0);   // called each (at least) 'interval'
      int process();                                       // called each 'interval'
      int performJobs();                                   // called every loop (1 second)
      int performWebSocketPing();
      int dispatchClientRequest();

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
      void gpioWrite(uint pin, bool state);
      bool gpioRead(uint pin);
      void logReport();

      // web

      int performLogin(json_t* oObject);
      int config2Json(json_t* obj);
      int configDetails2Json(json_t* obj);
      int sensor2Json(json_t* obj, cDbTable* table);
      void pin2Json(json_t* ojData, int pin);
      int storeConfig(json_t* obj);

      const char* getImageOf(const char* title, int value);
      int performWebifRequests();
      int cleanupWebifRequests();
      int toggleIo(uint pin);
      int toggleIoNext(uint pin);
      int toggleOutputMode(uint pin);

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
      cDbStatement* selectAllConfig {nullptr};
      cDbStatement* selectMaxTime {nullptr};
      cDbStatement* selectPendingJobs {nullptr};
      cDbStatement* cleanupJobs {nullptr};

      time_t nextAt {0};
      time_t startedAt {0};
      time_t nextAggregateAt {0};

      W1* w1 {nullptr};

      string mailBody;
      string mailBodyHtml;
      bool initialRun {true};

      cWebSock* webSock {nullptr};
      time_t nextWebSocketPing {0};
      int webSocketPingTime {60};

      // Home Assistant stuff

      MqTTPublishClient* mqttWriter {nullptr};
      MqTTSubscribeClient* mqttReader {nullptr};
      MqTTSubscribeClient* mqttCommandReader {nullptr};

      // config

      int interval {60};
      int aggregateInterval {15};         // aggregate interval in minutes
      int aggregateHistory {0};           // history in days
      char* hassMqttUrl {0};
      char* wsLoginToken {nullptr};
      std::map<std::string,std::string> hassCmdTopicMap; // <topic,name>

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
      int showerDuration {20};

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

      static std::map<std::string, ConfigItemDef> configuration;

      static int shutdown;
};
