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
#include <lws_config.h>

#include "lib/db.h"
#include "lib/mqtt.h"
#include "lib/thread.h"

#include "HISTORY.h"
#include "ph.h"

#define confDirDefault "/etc/poold"

extern char dbHost[];
extern int  dbPort;
extern char dbName[];
extern char dbUser[];
extern char dbPass[];

extern char* confDir;

class Poold;
typedef Poold MainClass;

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
         evGetToken,
         evStoreIoSetup,
         evChartData,
         evLogMessage,
         evUserConfig,
         evChangePasswd,
         evResetPeaks,

         evCount
      };

      enum ClientType
      {
         ctWithLogin = 0,
         ctActive
      };

      enum UserRights
      {
         urView        = 0x01,
         urControl     = 0x02,
         urFullControl = 0x04,
         urSettings    = 0x08,
         urAdmin       = 0x10
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

         std::string buffer;   // for chunked messages
      };

      cWebSock(const char* aHttpPath);
      virtual ~cWebSock();

      int init(int aPort, int aTimeout);
      int exit();

      int service();
      int performData(MsgType type);

      // status callback methods

      static int wsLogLevel;
      static int callbackHttp(lws* wsi, lws_callback_reasons reason, void* user, void* in, size_t len);
      static int callbackWs(lws* wsi, lws_callback_reasons reason, void* user, void* in, size_t len);

      // static interface

      static void atLogin(lws* wsi, const char* message, const char* clientInfo, json_t* object);
      static void atLogout(lws* wsi, const char* message, const char* clientInfo);
      static int getClientCount();
      static void pushMessage(const char* p, lws* wsi = 0);
      static void writeLog(int level, const char* line);
      static void setClientType(lws* wsi, ClientType type);

   private:

      static int serveFile(lws* wsi, const char* path);
      static int dispatchDataRequest(lws* wsi, SessionData* sessionData, const char* url);

      static const char* methodOf(const char* url);
      static const char* getStrParameter(lws* wsi, const char* name, const char* def = 0);
      static int getIntParameter(lws* wsi, const char* name, int def = na);

      //

      int port {na};
      lws_protocols protocols[3];
      lws_http_mount mounts[1];
#if defined (LWS_LIBRARY_VERSION_MAJOR) && (LWS_LIBRARY_VERSION_MAJOR >= 4)
      lws_retry_bo_t retry;
#endif

      // statics

      static lws_context* context;

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
// Class
//***************************************************************************

class cInputThread : public cThread
{
   public:

};

//***************************************************************************
// Class Pool Daemon
//***************************************************************************

class Poold : public cWebService, public cPhBoardService
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
      ~Poold();

      int init();
      int loop();

      static const char* myName()    { return "poold"; }
      static void downF(int aSignal) { shutdown = yes; }

      // public static message interface to web thread

      static int pushInMessage(const char* data);
      static std::queue<std::string> messagesIn;
      static cMyMutex messagesInMutex;

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
