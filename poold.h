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
//#include "arduinoif.h"

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
         // 1  - 3.3 V
         // 2  - 5 V
         // 3  - GPIO2 (SDA)
         // 4  - 5 V
         // 5  - GPIO2 (SCL)
         // 6  - GND
         pinW1           = 7,      // GPIO4
         pinSerialTx     = 8,      // GPIO14 (TX)
         // 9  - GND
         pinSerialRx     = 10,     // GPIO15 (RX)
         pinFilterPump   = 11,     // GPIO17
         pinSolarPump    = 12,     // GPIO18
         pinPoolLight    = 13,     // GPIO27
         // 14  - GND
         pinUVC          = 15,     // GPIO22
         pinUserOut1     = 16,     // GPIO23
         // 17  - 3.3 V
         pinUserOut2     = 18,     // GPIO24
         pinW1Power      = 19,     // GPIO10
         // 20  - GND
         pinUserOut4     = 21,     // GPIO9
         pinShower       = 22,     // GPIO25
         pinUserOut3     = 23,     // GPIO11
         // 24  - GPIO8 (SPI)
         // 25  - GND
         // 26  - GPIO7 (ID EEPROM)
         // 27  - ID_SD
         // 28  - ID_SC
         pinFree1        = 29,     // GPIO5
         // 30  - GND
         pinLevel1       = 31,     // GPIO6
         pinLevel2       = 32,     // GPIO12
         pinLevel3       = 33,     // GPIO13
         // 34  - GND
         pinShowerSwitch = 35,     // GPIO19
         pinFree2        = 36,     // GPIO16
         pinFree3        = 37,     // GPIO26
         pinFree4        = 38,     // GPIO20
         // 39  - GND
         pinFree5        = 40      // GPIO21
      };

      enum AnalogInputs
      {
         aiPh = 0,         // addr 0x00
         aiFilterPressure  // addr 0x01
      };

      enum SpecialValues  // 'SP'
      {
         spWaterLevel = 1,
         spSolarDelta,
         spPhMinusDemand,
         spLastUpdate,
         spSolarPower,
         spSolarWork
      };

      // object

      Poold();
      virtual ~Poold();

      int init();
      int loop();

      const char* myName() override  { return "poold"; }
      static void downF(int aSignal) { shutdown = true; }

   protected:

      enum WidgetType
      {
         wtDefault = -1,
         wtSymbol  = 0,  // == 0
         wtChart,        // == 1
         wtText,         // == 2
         wtValue,        // == 3
         wtGauge         // == 4
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
         const char* name {nullptr};     // crash on init with nullptr :o
         std::string title;
         time_t last {0};      // last switch time
         time_t next {0};      // calculated next switch time
      };

      struct Range
      {
         uint from;
         uint to;

         bool inRange(uint t) const { return (from <= t && to >= t); }
      };

      enum ConfigItemType
      {
         ctInteger = 0,
         ctNum,
         ctString,
         ctBool,
         ctRange,
         ctChoice
      };

      struct ConfigItemDef
      {
         std::string name;
         ConfigItemType type;
         const char* def {nullptr};
         bool internal {false};
         const char* category {nullptr};
         const char* title {nullptr};
         const char* description {nullptr};
      };

      std::map<uint,OutputState> digitalOutputStates;
      std::map<int,bool> digitalInputStates;

      int exit();
      int initDb();
      int exitDb();
      int readConfiguration();
      int applyConfigurationSpecials();

      int addValueFact(int addr, const char* type, const char* name, const char* unit, int rights = 0);
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

      int performMqttRequests();
      int hassPush(IoType iot, const char* name, const char* title, const char* unit, double theValue, const char* text = 0, bool forceConfig = false);
      int mqttCheckConnection();
      int mqttDisconnect();

      int scheduleAggregate();
      int aggregate();

      int sendStateMail();
      int sendMail(const char* receiver, const char* subject, const char* body, const char* mimeType);

      int loadHtmlHeader();

      int getConfigItem(const char* name, char*& value, const char* def = "");
      int setConfigItem(const char* name, const char* value);
      int getConfigItem(const char* name, int& value, int def = na);
      int getConfigItem(const char* name, long& value, long def = na);
      int setConfigItem(const char* name, long value);
      int getConfigItem(const char* name, double& value, double def = na);
      int setConfigItem(const char* name, double value);

      int getConfigTimeRangeItem(const char* name, std::vector<Range>& ranges);

      bool doShutDown() { return shutdown; }

      int getWaterLevel();
      bool phMeasurementActive();
      int calcPhMinusVolume(double ph);
      void gpioWrite(uint pin, bool state, bool store = true);
      bool gpioRead(uint pin);
      void logReport();

      // web

      int pushOutMessage(json_t* obj, const char* title, long client = 0);
      int pushDataUpdate(const char* title, long client);

      int pushInMessage(const char* data) override;
      std::queue<std::string> messagesIn;
      cMyMutex messagesInMutex;

      int replyResult(int status, const char* message, long client);
      int performLogin(json_t* oObject);
      int performLogout(json_t* oObject);
      int performTokenRequest(json_t* oObject, long client);
      int performPageChange(json_t* oObject, long client);
      int performSyslog(json_t* oObject, long client);
      int performSendMail(json_t* oObject, long client);
      int performConfigDetails(long client);
      int performUserDetails(long client);
      int performIoSettings(json_t* oObject, long client);
      int performChartData(json_t* oObject, long client);
      int storeUserConfig(json_t* oObject, long client);
      int performPasswChange(json_t* oObject, long client);
      int performPh(long client, bool all);
      int performPhCal(json_t* obj, long client);
      int performPhSetCal(json_t* oObject, long client);
      int storeConfig(json_t* obj, long client);
      int storeIoSetup(json_t* array, long client);
      int resetPeaks(json_t* obj, long client);
      int performChartbookmarks(long client);
      int storeChartbookmarks(json_t* array, long client);

      int config2Json(json_t* obj);
      int configDetails2Json(json_t* obj);
      int userDetails2Json(json_t* obj);
      int configChoice2json(json_t* obj, const char* name);
      int valueFacts2Json(json_t* obj, bool filterActive);
      int daemonState2Json(json_t* obj);
      int sensor2Json(json_t* obj, cDbTable* table);
      void pin2Json(json_t* ojData, int pin);
      void publishSpecialValue(int sp, double value);

      const char* getImageOf(const char* title, int value);
      int toggleIo(uint addr, const char* type);
      int toggleIoNext(uint pin);
      int toggleOutputMode(uint pin);

      int storeStates();
      int loadStates();

      // arduino

      int dispatchArduinoMsg(const char* message);
      int dispatchW1Msg(const char* message);
      int initArduino();
      void updateAnalogInput(const char* id, int value, time_t stamp);

      // W1

      int initW1();
      bool existW1(const char* id);
      double valueOfW1(const char* id, time_t& last);
      uint toW1Id(const char* name);
      void updateW1(const char* id, double value, time_t stamp);
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
      cDbStatement* selectSolarWorkPerDay {nullptr};

      cDbValue xmlTime;
      cDbValue rangeFrom;
      cDbValue rangeTo;
      cDbValue avgValue;
      cDbValue maxValue;

      time_t nextRefreshAt {0};
      time_t startedAt {0};
      time_t nextAggregateAt {0};

      std::vector<std::string> addrsDashboard;
      std::vector<std::string> addrsList;
      std::string mailBody;
      std::string mailBodyHtml;
      bool initialRun {true};

      cWebSock* webSock {nullptr};
      time_t nextWebSocketPing {0};
      int webSocketPingTime {60};
      const char* httpPath {"/var/lib/pool"};

      struct WsClient    // Web Socket Client
      {
         std::string user;
         uint rights;                  // rights mask
         std::string page;
         ClientType type {ctActive};
      };

      std::map<void*,WsClient> wsClients;

      // MQTT stuff

      Mqtt* mqttWriter {nullptr};
      Mqtt* mqttReader {nullptr};
      Mqtt* mqttCommandReader {nullptr};
      Mqtt* mqttPoolReader {nullptr};

      // config

      int interval {60};
      int webPort {61109};
      char* webUrl {nullptr};
      int aggregateInterval {15};         // aggregate interval in minutes
      int aggregateHistory {0};           // history in days
      char* poolMqttUrl {nullptr};
      char* mqttUrl {nullptr};
      char* mqttUser {nullptr};
      char* mqttPassword {nullptr};

      time_t lastMqttConnectAt {0};
      std::map<std::string,std::string> hassCmdTopicMap; // 'topic' to 'name' map
      char* chartSensors {nullptr};

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
      int alertSwitchOffPressure {0};

      // PH stuff

      int phMinusVolume {0};           // aktull errechnete Menge um phReference zu erreichen
      int phInterval {0};
      double phMinusDensity {0.0};
      int phMinusDemand01 {0};         // Menge zum Senken um 0,1 [g]
      int phMinusDayLimit {0};
      int phPumpDuration100 {0};
      double phReference {0.0};        // PG Referenzwert (sollwert)

      int minPumpTimeForPh { 10 * tmeSecondsPerMinute }; // [s] #TODO -> add to config?
      double phCalibratePointA {0.0};
      int phCalibratePointValueA {0};
      double phCalibratePointB {0.0};
      int phCalibratePointValueB {0};

      double pressCalibratePointA {0.0};
      int pressCalibratePointValueA {400};
      double pressCalibratePointB {0.6};
      int pressCalibratePointValueB {2600};

      std::vector<Range> filterPumpTimes;
      std::vector<Range> uvcLightTimes;
      std::vector<Range> poolLightTimes;

      // actual state and data

      double tPool {0.0};
      double tSolar {0.0};
      double tCurrentDelta {0.0};
      double pSolar {0.0};           // solar power [W]
      time_t pSolarSince {0};
      double solarWork {0.0};        // [kWh]
      double massPerSecond {0.0};    // Fördermeneg der Solarpumpe [kg·s-1] bzw. [l/s]

      struct SensorData
      {
         time_t last;
         double value;
      };

      std::map<int, SensorData> aiSensors;
      std::map<std::string, SensorData> w1Sensors;
      std::map<std::string, json_t*> jsonSensorList;

      // statics

      static std::list<ConfigItemDef> configuration;
      static bool shutdown;
};
