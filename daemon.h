//***************************************************************************
// Automation Control
// File daemon.h
// This code is distributed under the terms and conditions of the
// GNU GENERAL PUBLIC LICENSE. See the file LICENSE for details.
// Date 2010-2026 Jörg Wendel
//***************************************************************************

#pragma once

#include <queue>
#include <set>
#include <jansson.h>

#include "lib/common.h"
#include "lib/db.h"
#include "lib/mqtt.h"
#include "lib/lua.h"

#include "HISTORY.h"

#include "service.h"

#include "websock.h"
#include "deconz.h"
#include "lmccom.h"
#include "gpio.h"

#define confDirDefault "/etc/" TARGET

extern char dbHost[];
extern int  dbPort;
extern char dbName[];
extern char dbUser[];
extern char dbPass[];

extern char* confDir;

//***************************************************************************
// Class Daemon
//***************************************************************************

class Daemon : public cWebInterface, public Service
{
   public:

      Daemon();
      virtual ~Daemon();

      int loop();
      virtual int init();

      const char* myName() override  { return TARGET; }
      virtual const char* myTitle()  { return "Daemon"; }
      static void downF(int aSignal) { shutdown = true; }

      int addValueFact(int addr, const char* type, int factor, const char* name, const char* unit = "",
                       const char* title = nullptr, int rights = urNone, const char* choices = nullptr, SensorOptions options = soNone, const char* parameter = nullptr);

   protected:

      static const char* widgetTypes[];
      static const char* toName(WidgetType type);
      static WidgetType toType(const char* name);

      enum IoType
      {
         iotSensor = 0,
         iotLight
      };

      enum OutputMode
      {
         omAuto = 0,
         omManual
      };

      enum OutputOptions
      {
         ooUser = 0x01,        // Output can contolled by user
         ooAuto = 0x02         // Output automatic contolled
      };

      enum Direction
      {
         dirOpen  = 1,
         dirClose = 2
      };

      struct SensorData        // Sensor Data
      {
         bool record {false};
         bool active {false};

         std::string kind {"value"};       //    { value | status | text | trigger }
                                           // ->   value   state    text   state
         double value {0.0};
         bool state {false};
         std::string text;

         time_t last {0};                  // last info about value/state/..
         time_t changedAt {0};             // last change of value/state/..
         uint64_t lastInterruptMs {0};

         bool working {false};             // actually working/moving (eg for blinds or script running)
         Direction lastDir {dirOpen};
         std::string image;
         bool disabled {false};
         bool valid {false};               // set if the value, text or state is valid
         int battery {na};
         std::string color;
         int hue {0};                      // 0-360° hue
         int sat {0};                      // 0-100% saturation

         // fact

         std::string type;
         uint address {0};
         std::string name;
         std::string title;
         std::string unit;
         int factor {1};
         int rights {cWebService::urView};
         int group {0};

         // specials

         bool invert {false};
         Gpio::PullUpDown pull {Gpio::pudOff};
         bool impulse {false};        // change output only for a short impulse
         bool interruptSet {false};
         std::string fct;             // GPIO pin functionm: 'off', 'in', 'out', ...
         std::string script;          // CV,DO  - LUA script
                                      // SC     - parameter for script (JSON object)
         std::string parameter;       // parameter for widged look
         std::string choices;
         OutputMode mode {omAuto};    // eingestellter output mode
         uint outputModes {ooUser};   // bitmask of available output modes (User,Auto,...)
         std::string feedbackInType;  // feedback input for impuls out
         uint feedbackInAddress {0};  // feedback input for impuls out
      };

      struct Range
      {
         uint from {};
         uint to {};
         bool inRange(uint t) const {
            if (from < to)
               return t >= from && t < to;
            if (from > to)
               return t >= from || t < to;
            return false;
         }
      };

      enum LogicalOperator
      {
         loAnd,
         loOr,
         loAndNot,
         loOrNot
      };

      enum ConfigItemType
      {
         ctInteger = 0,
         ctNum,
         ctString,
         ctBool,
         ctRange,
         ctChoice,
         ctMultiSelect,
         ctBitSelect,
         ctText
      };

      struct ConfigItemDef
      {
         std::string name;
         ConfigItemType type;
         std::string def {};
         const char* kind {};
         bool internal {false};
         const char* category {};
         std::string title {};
         std::string description {};
      };

      struct DefaultWidgetProperty
      {
         std::string type;
         int address {na};
         std::string unit;

         WidgetType widgetType {wtMeter};
         int minScale {0};
         int maxScale {100};
         int scaleStep {0};
         bool showPeak {false};
      };

      static DefaultWidgetProperty defaultWidgetProperties[];
      static DefaultWidgetProperty* getDefalutProperty(const char* type, const char* unit, int address = 0);
      int widgetDefaults2Json(json_t* jDefaults, std::string type, std::string unit, const char* usrTitle, int address = 0);

      struct ValueTypes
      {
         std::string typeExpression;
         std::string title;
      };

      static ValueTypes defaultValueTypes[];
      static const char* getTitleOfType(const char* type);

      virtual int exit();
      virtual int initLocale();
      virtual int initDb();
      virtual int exitDb();
      virtual int readConfiguration(bool initial);
      virtual int applyConfigurationSpecials();

      int initSensorByFact(myString type, uint address);
      int initGpioLine(uint physPin, const PinInfo& pinInfo);
      int initOutput(uint pin, int outputModes, OutputMode mode, const char* name, uint rights = urControl);
      int initInput(uint pin, const char* name);
      int cfgOutput(myString type, uint pin, json_t* jCal = nullptr);
      int cfgInput(myString type, uint pin, json_t* jCal = nullptr);

      int initScripts();

      void doSleep(int t);
      int standby(int t);
      virtual int standbyUntil();
      int meanwhile();
      virtual int atMeanwhile() { return done; }

      virtual int updateInputs(bool check = true);
      virtual int updateSensors() { return done; }
      int storeSamples();
      void updateScriptSensors();
      virtual int doLoop()     { return done; }
      virtual void afterUpdate();

      void sensorAlertCheck(time_t now);
      int performAlertCheck(cDbRow* alertRow, time_t now, int recurse = 0, int force = no);
      int add2AlertMail(cDbRow* alertRow, const char* title, double value, const char* unit);
      int sendAlertMail(const char* to);

      virtual int process(bool force = false, bool signal = false);
      virtual int processLua(bool force = false, bool signal = false);
      virtual int performJobs() { return done; }           // called every loop (1 second)
      int dispatchClientRequest();
      virtual int dispatchSpecialRequest(Event event, json_t* oObject, long client) { return ignore; }
      virtual int dispatchMqttHaCommandRequest(json_t* jData, const char* topic);
      virtual int dispatchNodeRedCommands(const char* topic, json_t* jObject);
      virtual int dispatchNodeRedCommand(json_t* jObject);
      virtual int dispatchDeconz();
      virtual int dispatchHomematicRpcResult(const char* message);
      virtual int dispatchHomematicEvents(const char* message);
      virtual int dispatchGrowattEvents(const char* message);
      virtual int dispatchRtl433(const char* message);
      virtual int dispatchOther(const char* topic, const char* message);
      const char* lookupCommandTopic(const char* type, int address);
      bool checkRights(long client, Event event, json_t* oObject);
      virtual bool onCheckRights(long client, Event event, uint rights) { return false; }
      int callScript(int addr, const char* command);
      int switchCommand(std::string type, int addr, std::string action, const char* topic, const char* value = nullptr);

      bool isInTimeRange(const std::vector<Range>* ranges, time_t t);

      int updateWeather();
      int weather2json(json_t* jWeather, json_t* owmWeather);

      int store(time_t now, const SensorData* sensor);

      cDbRow* valueFactRowOf(std::string type, uint addr);
      SensorData* getSensor(const char* type, int addr);
      void setSpecialValue(uint addr, double value, const std::string& text = "");

      int performMqttRequests();
      int mqttCheckConnection();
      int mqttDisconnect();
      int mqttHaPublish(SensorData& sensor, bool forceConfig = false);
      int mqttHaPublishSensor(SensorData& sensor, bool forceConfig = false);
      int mqttNodeRedPublishSensor(SensorData& sensor);
      int mqttNodeRedPublishAction(SensorData& sensor, double value, bool publishOnly = false);
      int mqttHaWrite(json_t* obj, uint groupid);
      int jsonAddValue(json_t* obj, SensorData& sensor, bool forceConfig = false);
      int updateSchemaConfTable();

      int scheduleAggregate();
      int aggregate();

      int loadHtmlHeader();
      int sendMail(const char* receiver, const char* subject, const char* body, const char* mimeType);

      int getConfigItem(const char* name, std::string& value, const char* def = 0);
      int setConfigItem(const char* name, const char* value, const char* kind = "N");
      int getConfigItem(const char* name, int& value, int def = na);
      int getConfigItem(const char* name, long& value, long def = na);
      int setConfigItem(const char* name, long value, const char* kind = "N");
      int getConfigItem(const char* name, double& value, double def = na);
      int setConfigItem(const char* name, double value, const char* kind = "N");
      int getConfigItem(const char* name, bool& value, bool def = false);
      int setConfigItem(const char* name, bool value, const char* kind = "N");

      int getConfigTimeRangeItem(const char* name, std::vector<Range>& ranges);

      bool doShutDown() { return shutdown; }

      void publishVictronInit(const char* type);
      void requestAlpicoolInit(const char* type);
      void publishAlpicoolInit(const char* type, const char* name);
      void publishI2CSensorConfig(const char* type, uint pin, json_t* jParameters);
      void publishPin(const char* type, uint pin);
      void gpioWrite(uint pin, bool state, bool saveIoState = true);
      bool gpioRead(uint pin, bool check = false);
      virtual void logReport() { return ; }

      // web

      int pushOutMessage(json_t* obj, const char* event, long client = 0, bool keepJson = false);
      int pushDataUpdate(const char* event, long client);

      int pushInMessage(const char* data) override;
      std::queue<std::string> messagesIn;
      cMyMutex messagesInMutex;

      int replyResult(int status, const char* message, long client);
      virtual int performLogin(json_t* oObject);
      int performLogout(json_t* oObject);
      int performTokenRequest(json_t* oObject, long client);
      int performPageChange(json_t* oObject, long client);
      int performToggleIo(json_t* oObject, long client);
      int performSystem(json_t* oObject, long client);
      int performGpioData(json_t* oObject, long client);
      int performDatabaseStatistic(json_t* oObject, long client);
      int performWifi(json_t* oObject, long client);
      int performWifiCommand(json_t* oObject, long client);
      int performSyslog(json_t* oObject, long client);
      int performConfigDetails(long client);
      int performGroups(long client);
      int performTestMail(json_t* oObject, long client);
      int storeAlerts(json_t* oObject, long client);
      int performAlerts(json_t* oObject, long client);
      int performAlertTestMail(int id, long client);

      int performData(long client, const char* event = nullptr);
      int performChartData(json_t* oObject, long client);
      int performUserDetails(long client);
      int storeUserConfig(json_t* oObject, long client);
      int performPasswChange(json_t* oObject, long client);
      int performForceRefresh(json_t* obj, long client);
      int storeConfig(json_t* obj, long client);
      int storeDashboards(json_t* obj, long client);
      virtual int storeIoSetup(json_t* array, long client);
      int storeGroups(json_t* array, long client);
      int performChartbookmarks(long client);
      int storeChartbookmarks(json_t* array, long client);
      int performImageConfig(json_t* obj, long client);
      int performSchema(json_t* oObject, long client);
      int storeSchema(json_t* oObject, long client);
      int deleteValueFact(const char* type, long address);
      int storeSensorSetup(json_t* oObject, long client);
      int checkLuaScript(json_t* obj, long client);
      int gpsLive(json_t* obj, long client);
      int storeCvSettings(json_t* oObject, long client);
      int storeAiSettings(json_t* oObject, long client);
      int storeIoSettings(json_t* oObject, long client);
      int performLmcAction(json_t* oObject, long client);
      virtual int performCommand(json_t* obj, long client);
      virtual const char* getTextImage(const char* key, const char* text) { return nullptr; }

      int widgetTypes2Json(json_t* obj);
      int config2Json(json_t* obj);
      int environment2Json(json_t* obj);
      int configDetails2Json(json_t* obj);
      int userDetails2Json(json_t* obj);
      virtual int configChoice2json(json_t* obj, const char* name);

      int valueTypes2Json(json_t* obj);
      int valueFacts2Json(json_t* obj, bool filterActive);
      int dashboards2Json(json_t* obj);
      int groups2Json(json_t* obj);
      virtual int commands2Json(json_t* obj);
      int syslogs2Json(json_t* obj);
      int daemonState2Json(json_t* obj);
      int sensor2Json(json_t* obj, const char* type, uint address);
      int systemServices2Json(json_t* obj);
      int images2Json(json_t* obj);
      void pin2Json(json_t* ojData, const char* type, uint pin);
      void publishSpecialValue(int addr);
      bool webFileExists(const char* file, const char* base = nullptr);

      int lmcTrack2Json(json_t* obj, TrackInfo* track);
      int lmcPlayerState2Json(json_t* obj);
      int lmcPlayers2Json(json_t* obj);
      int lmcPlaylist2Json(json_t* obj);
      int lmcMainMenu2Json(json_t* obj);

      const char* getImageFor(std::string type, const char* usrTitle, std::string unit, int value);
      int toggleIo(uint addr, const char* type, int state = na, int bri = na, int transitiontime = na);
      int toggleColor(uint addr, const char* type, int color, int sat, int bri);
      int toggleIoNext(uint pin);
      int toggleOutputMode(json_t* oObject, long client);

      virtual int loadIoState(const char* type, uint address, bool& state, OutputMode& mode);
      virtual int loadIoStates();
      virtual int storeIoState(const char* type, uint address);

      // LMC

      int lmcInit(bool force = false);
      int lmcExit();
      int lmcUpdates(long client = 0);
      int performLmcUpdates();

      // arduino

      int dispatchArduinoMsg(const char* message);
      int initArduino();
      bool updateAnalogInput(uint addr, const char* type, double value, time_t stamp, const char* unit);

      // W1

      bool existW1(uint address);
      int dispatchW1Msg(const char* message);
      double valueOfW1(uint address, time_t& last);
      uint toW1Id(const char* name);
      bool updateW1(const char* id, double value, time_t stamp);
      void cleanupW1();

      // data

      bool initialized {false};
      cDbConnection* connection {};

      cDbTable* tableTableStatistics {};
      cDbTable* tableSamples {};
      cDbTable* tablePeaks {};
      cDbTable* tableValueFacts {};
      cDbTable* tableValueTypes {};
      cDbTable* tableConfig {};
      cDbTable* tableSensorAlert {};
      cDbTable* tableUsers {};
      cDbTable* tableGroups {};
      cDbTable* tableDashboards {};
      cDbTable* tableDashboardWidgets {};
      cDbTable* tableSchemaConf {};
      cDbTable* tableHomeMatic {};
      cDbTable* tableIoStates {};

      cDbStatement* selectTableStatistic {};
      cDbStatement* selectAllGroups {};
      cDbStatement* selectAllValueTypes {};
      cDbStatement* selectActiveValueFacts {};
      cDbStatement* selectValueFactsByType {};
      cDbStatement* selectMaxValueFactsByType {};
      cDbStatement* selectValueFactsByTypeAndName {};
      cDbStatement* selectAllValueFacts {};
      cDbStatement* selectAllConfig {};
      cDbStatement* selectAllUser {};
      cDbStatement* selectMaxTime {};
      cDbStatement* selectSamplesRange {};              // for chart
      cDbStatement* selectSamplesRange60 {};            // for chart
      cDbStatement* selectSamplesRange360 {};           // for chart
      cDbStatement* selectSamplesRangeMonth {};         // for chart
      cDbStatement* selectSamplesRangeMonthOfDayMax {}; // for chart
      cDbStatement* selectAllIoStates {};
      cDbStatement* selectSensorAlerts {};
      cDbStatement* selectAllSensorAlerts {};
      cDbStatement* selectSampleInRange {};        // for alert check

      cDbStatement* selectDashboards {};
      cDbStatement* selectDashboardById {};
      cDbStatement* selectDashboardWidgetsFor {};
      cDbStatement* selectSchemaConfByState {};
      cDbStatement* selectAllSchemaConf {};
      cDbStatement* selectHomeMaticByUuid {};

      cDbValue xmlTime;
      cDbValue rangeFrom;
      cDbValue rangeTo;
      cDbValue avgValue;
      cDbValue maxValue;
      cDbValue rangeEnd;

      time_t nextRefreshAt {0};
      time_t startedAt {0};
      time_t nextAggregateAt {0};
      time_t lastSampleTime {0};

      std::vector<std::string> addrsDashboard;
      std::vector<std::string> addrsList;

      bool initialRun {true};

      // WS Interface stuff

      cWebSock* webSock {};
      time_t nextWebSocketPing {0};
      int webSocketPingTime {60};
      const char* httpPath {"/var/lib/" TARGET};

      struct WsClient    // Web Socket Client
      {
         std::string user;
         uint rights;                  // rights mask
         std::string page;
         ClientType type {ctActive};
      };

      std::map<void*,WsClient> wsClients;

      // MQTT stuff

      enum MqttInterfaceStyle
      {
         misNone,
         misSingleTopic,     // one topic for all sensors
         misMultiTopic,      // separate topic for each sensors
         misGroupedTopic     // one topic for each group (Baugruppe)
      };

      struct Group
      {
         std::string name;
         json_t* oHaJson {};
      };

      std::string mqttUrl;          // like tcp://192.168.300.101:1883
      char* mqttUrlPlain {};        // like 192.168.300.101:1883
      std::string sensorTopics;
      std::string mqttUser;
      std::string mqttPassword;

      std::string mqttHaDataTopic;                       // for home automation interface
      std::string mqttHaSendWithKeyPrefix;               // for home automation interface
      bool mqttHaHaveConfigTopic {false};                // for home automation interface
      MqttInterfaceStyle mqttHaInterfaceStyle {misNone}; // for home automation interface

      Mqtt* mqttReader {};
      Mqtt* mqttWriter {};
      std::vector<std::string> mqttSensorTopics;
      std::vector<std::string> configTopicsFor;

      time_t lastMqttConnectAt {0};
      std::map<std::string,std::string> hassCmdTopicMap; // 'topic' to 'name' map

      json_t* oHaJson {};
      std::map<int,Group> groups;

      bool ioStatesLoaded {false};

      // config

      std::string instanceName;
      int interval {60};
      double latitude {50.30};
      double longitude {8.79};
      std::string openWeatherApiKey;
      std::string windyAppSpotID;
      std::string windyAppID;
      int weatherInterval {15};           // minutes
      time_t lastStore {0};
      int arduinoInterval {10};
      std::string arduinoTopic;

      std::map<std::string,std::string> commandTopicsMap; // like "VIC":"homectld2mqtt/victron/in"
      std::map<std::string,uint> mqttConverters;

      int webPort {61109};
      std::string webUrl;
      bool webSsl {false};
      std::string iconSet;
      int aggregateInterval {15};         // aggregate interval in minutes
      int aggregateHistory {0};           // history in days

      bool sendMails {false};
      std::string mailScript;
      std::string stateMailTo;
      std::string errorMailTo;
      std::string htmlHeader;

      bool triggerProcess {false};
      std::queue<int> triggerGpioPins;
      LmcCom* lmc {};

      Deconz deconz;
      bool homeMaticInterface {false};
      std::map<uint,std::string> homeMaticUuids;

      std::string lmcHost {};
      int lmcPort {9090};
      std::string lmcPlayerMac {};

      struct AiSensorConfig
      {
         double calPointA {0.0};
         double calPointB {10.0};
         double calPointValueA {0.0};
         double calPointValueB {10.0};
         double cutBelow {-10000.0};
         double round {20.0};                // e.g. 50.0 -> 0.02; 20.0 -> 0.05; 10.0 -> 0.1;
      };

      std::map<std::string,std::map<int,AiSensorConfig>> aiSensorConfig;
      std::map<std::string,std::map<int,SensorData>> sensors;

      struct GpsCoordinate
      {
         double latitude {};
         double longitude {};
      };

      GpsCoordinate gpsCoordinate;

      virtual std::list<ConfigItemDef>* getConfiguration() = 0;

      std::string alertMailBody;
      std::string alertMailSubject;

      std::map<std::string,json_t*> jsonSensorList;

      LuaSol lua;
      std::map<std::string, std::set<std::pair<std::string,int>>> luaDeps;
      std::string luaCurrentKey;

      // GPIO

      Gpio* gpio {};

      // scripts command thread handling

      struct ThreadControl
      {
         std::string command;
         std::string result;
         std::atomic<long> timeoutAt{0};
         std::atomic<bool> active{false};
         std::atomic<bool> cancel{false};
      };

      int connectWifi(const char* ssid, std::string& result, const char* pwd = nullptr);
      int disconnectWifi(const char* ssid, std::string& result);
      int executeNmcli(const std::vector<std::string>& cmdArgs, std::string& result);

      int executeCommandAsync(uint address, const char* cmd);
      std::map<uint,ThreadControl> commandThreads;
      virtual void onGpioChange(int physPin, bool value);

      // statics

      static void cmdThread(ThreadControl* threadCtl);
      static bool shutdown;
};
