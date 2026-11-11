//***************************************************************************
// p4mqttd - Fröling S 3200 to MQTT
// File p4mqttd.c
// This code is distributed under the terms and conditions of the
// GNU GENERAL PUBLIC LICENSE. See the file LICENSE for details.
// Date 11.11.2026 - Jörg Wendel
//
// Communication daemon for the Fröling S 3200 boiler control, the counterpart of
// the homectld. No database, no WEBIF, only the serial line and MQTT.
// See README.md and p4d-integration-plan.md.
//
// Phase 1: state, measured values, digital in/outputs and analog outputs.
// Phase 2: configuration from the homectld, only the active sensors are polled,
// time synchronization of the control.
// Phase 3: the service menu of the control, commands and replies:
//
//    <base>/cmd      (to the service, not retained) {"cmd":"menu","rid":"7f3a","parent":1}
//    <base>/reply    (not retained)  {"rid":"7f3a","cmd":"menu","status":"ok","parent":1,"last":0,"title":"",
//                     "items":[{"id":12,"parent":1,"child":5,"type":42,"address":0,"title":"Kessel",
//                     "unit":"","digits":0,"value":"3,0","editable":false}, ...]}
//                    status: ok | notready | error, with "message"
//
//    cmd 'menu'       the items of one level of the service menu with their current values
//                     (the structure is cached in the cache file, the values are read from the control)
//    cmd 'initmenu'   (alias 'initsensors') read the menu and the sensor definitions from the control again,
//                     takes a while, the reply comes when it is done: {"menuItems":1234,"sensors":87}
//
// Phase 4 (this state): writing, never in the read only mode (-r, -P) and only for the parameters
// and time ranges of the service menu:
//
//    cmd 'parget'     {"address":480}  -> value, unit, digits, min, max, default, type
//    cmd 'parset'     {"address":480,"value":"72","user":"joerg"}  -> status ok | nochange | range | format |
//                     readonly | error | comm, with "value" (read back) and "old"
//    cmd 'timeset'    {"address":3,"range":1,"value":"06:00 - 22:00","user":"joerg"}   ('nn:nn - nn:nn' deletes)
//    cmd 'inittimes'  read the time programs from the control again (the p4d does the same on request, the
//                     control can't be asked for a single program, it takes some seconds)
//    the weekday lists of the time programs ('Zeiten') are part of the answer of 'menu'
//    every write is logged with the user, the old and the new value
//
// Phase 5 (this state): the error list of the control
//
//    <base>/errors   (retained) {"errors":[{"number":16,"info":163,"state":1,"time":1788000000,"text":"Raumaustragung kontrollieren"},...],
//                     "read":1788000100}   the list as the control keeps it (oldest first); state 1 came, 4 gone, 2 acknowledged.
//                     The homectld merges it into its table (the three entries of one error become one row) and sends
//                     the mails. It is read at the start, if the state changes from or to an error state, every
//                     10 minutes and on the command 'errors' (the answer carries the list, too)
//    <base>/state    has "error":true while the control is in an error state
//
// Widget defaults (0.6.0): every sensor message carries the object "parameter" with the default options of a
// dashboard widget of this sensor, the homectld takes them when the widget is created (the existing mechanism
// of the sensor 'parameter'), e.g.
//    "parameter":{"widgettype":6,"scalemin":0,"scalemax":100,"scalestep":20,"showpeak":true}
//    "parameter":{"widgettype":0,"imgon":"img/icon/pump-on.gif","imgoff":"img/icon/pump-off.png"}   (the images of the p4d)
//
// The state of the control (P4UD:1) carries the image of the p4d for it ("image":"img/state/light/state-fire.gif",
// the icon set 'light' or 'dark' comes with the config of the homectld).
//
//  service -> clients (retained, base topic default 'p4')
//    <base>/status   {"online":true,"comm":"ok|lost|closed","tty":"/dev/ttyUSB0","lastOk":1788000000,
//                     "firmware":"50.04.xx.xx","version":"p4mqttd 0.2.0","readonly":false,"parallel":false}
//                    (the last will of the connection sets "online":false)
//    <base>/state    {"state":3,"stateinfo":"Heizen","mode":1,"modeinfo":"Automatik","time":1788000000}
//
//  homectld -> service (retained)
//    <base>/config   {"pollInterval":60,"stateInterval":10,"tsync":true,"maxTimeLeak":10,
//                     "active":{"P4VA":[0,1,4],"P4DO":[1],"P4DI":[],"P4AO":[]}}
//                    The values of the command line are the defaults until the config is received.
//                    If 'active' is missing, all sensors are polled. The first sweep after the start and
//                    after reading the sensor definitions always covers all sensors, so the homectld
//                    gets to know them (and the user can activate them).
//
//  service -> homectld (not retained, topic <base>/sensor or the one given by -D),
//  the format of HomeCtl::dispatchOther:
//    {"type":"P4VA","address":1,"title":"Abgas","unit":"°C","value":77.5,"maxage":180}
//    {"type":"P4DO","address":12,"title":"Saugzug","kind":"status","state":1,"text":"A","maxage":180}
//    {"type":"P4AO","address":3,"title":"Saugzug Leistung","unit":"%","value":62,"text":"A","maxage":180}
//    {"type":"P4UD","address":1,"title":"Heizungsstatus","unit":"zst","kind":"text","value":3,"text":"Heizen"}
//    {"type":"P4VA","address":1,"title":"Abgas","unit":"°C","valid":false}              (not readable)
//
// The sensors (value specs and the in/outputs of the service menu) are read once from the
// control and kept in a cache file (sensors.json in the cache directory), until the firmware
// changes or SIGUSR1 is received. Their values are published every poll interval.
//
// The serial line is protected by the same SysV semaphore (0x3da00001) as in the p4d and
// in the tool 'p4', so p4mqttd can run beside them (see README.md, 'Parallel operation').
//***************************************************************************

#include <signal.h>
#include <sys/stat.h>
#include <set>
#include <deque>
#include <jansson.h>

#include "lib/common.h"
#include "lib/serial.h"
#include "lib/mqtt.h"

#include "p4io.h"

#define P4MQTTD_VERSION "0.10.2"

#define semaphoreKey 0x3da00001

//***************************************************************************
// Class P4Mqttd
//***************************************************************************

class P4Mqttd
{
   public:

      struct Options
      {
         std::string tty {"/dev/ttyUSB0"};
         std::string mqttUrl {"tcp://localhost:1883"};
         std::string topic {"p4"};
         std::string dataTopic;                 // default <topic>/sensor
         std::string cacheDir {"/var/cache/p4mqttd"};
         int stateInterval {10};                // [s]
         int pollInterval {60};                 // [s]
         bool readonly {false};
         bool parallel {false};
      };

      P4Mqttd(const Options& aOptions);
      ~P4Mqttd();

      int init();
      int loop();

      static void downF(int aSignal)        { shutdown = true; }
      static void rediscoverF(int aSignal)  { rediscover = true; }
      static bool doShutDown()              { return shutdown; }

   protected:

      struct Sensor
      {
         std::string type;                      // P4VA, P4DI, P4DO, P4AO
         uint address {0};
         std::string title;
         std::string unit;
         int factor {1};                        // P4VA
         int dataType {0};                      // P4VA: 1 -> unsigned, else signed
      };

      struct MenuEntry
      {
         uint id {0};                           // position in the menu list (stable as long as the cache is)
         int type {0};
         uint parent {0};                       // the level this item is part of
         uint child {0};                        // the level this item opens (0 -> none)
         uint address {0};
         std::string title;
         std::string unit;
      };

      // serial line

      int serialConnection();
      int busFailed(const char* what);
      void busOk()  { failures = 0; }
      void lockBus();
      void unlockBus()  { sem->v(); }

      // holds the bus for a whole walk through a list of the control (value list, menu list, time programs):
      //  the control keeps a position for such a list, requests of other processes in between (the
      //  old p4d polls the control, too) throw it off and the walk never reaches the end, the tool 'p4'
      //  holds the semaphore for its whole run for the same reason

      class BusGuard
      {
         public:

            BusGuard(P4Mqttd* aDaemon) : daemon(aDaemon)  { daemon->lockBus(); }
            ~BusGuard()                                   { daemon->unlockBus(); }

         private:

            P4Mqttd* daemon;
      };

      // sensors

      int discover();
      int discoverValues();
      int discoverIo();
      int loadCache();
      int saveCache();
      int pollOne(size_t index);
      int publishSensorInvalid(const Sensor& s);

      // commands (service menu)

      struct ErrorEntry
      {
         int number {0};
         int info {0};
         int state {0};
         time_t time {0};
         std::string text;
      };

      int readErrors(std::vector<ErrorEntry>& list);
      json_t* errors2Json(const std::vector<ErrorEntry>& list);
      int updateErrors();
      int cmdErrors(json_t* jReply);

      int dispatchCommands();
      int cmdMenu(json_t* jCmd, json_t* jReply);
      int cmdInitMenu(json_t* jReply);
      int menuItemValue(const MenuEntry& m, json_t* jItem, std::string& value);
      int cmdParGet(json_t* jCmd, json_t* jReply);
      int cmdParSet(json_t* jCmd, json_t* jReply);
      int cmdTimeSet(json_t* jCmd, json_t* jReply);
      int cmdInitTimes(json_t* jReply);
      int writableType(uint address);
      int addTimeGroup(const MenuEntry& m, json_t* jItems);

      // time programs

      int ensureTimes();
      int readTimes();
      int saveTimes();
      int loadTimes();
      int reply(json_t* jReply, const char* status, const char* message = nullptr);

      // state

      int updateState();
      int checkTimeSync();
      void scheduleTimeSyncIn(int offset);

      // configuration of the homectld

      int mqttRead();
      int applyConfig(const char* message);
      bool isActive(const Sensor& s);

      // MQTT

      int mqttConnection();
      int mqttDisconnect();
      int publish(const char* t, json_t* obj, bool retained);
      int publishStatus(bool online);
      int publishState();
      int publishUserDefined();
      std::string statusJson(bool online);

      Options opt;
      std::string dataTopic;
      std::string sensorsFile;

      Sem* sem {};
      Serial* serial {};
      P4Request* request {};

      Fs::Status currentState;
      int lastStateCode {-1};
      bool errorsDue {true};                    // read the error list soon
      time_t nextErrorsAt {0};
      bool haveState {false};
      bool commOk {false};
      int failures {0};                         // consecutive failed transactions
      time_t lastOk {0};
      time_t serialRetryAt {0};

      uint64_t nextStateAt {0};                 // [ms]
      uint64_t nextSweepAt {0};                 // [ms]
      uint64_t sweepStartedAt {0};
      bool sweeping {false};
      size_t pollIndex {0};
      bool firstSweepLogged {false};

      std::vector<Sensor> sensors;
      std::vector<MenuEntry> menu;
      std::deque<std::string> commands;         // received, not yet executed
      std::map<int,Fs::TimeRanges> times;       // the time programs by address (0 - 0xdf)
      bool timesLoaded {false};
      bool sensorsReady {false};
      std::string cacheFirmware;

      Mqtt* mqttWriter {};
      Mqtt* mqttReader {};
      time_t mqttRetryAt {0};

      // configuration (command line, overridden by <base>/config)

      bool configReceived {false};
      bool haveActive {false};                  // 'active' is part of the config
      std::set<std::string> activeSensors;      // "P4VA:5"
      bool announceSweep {true};                // the next sweep covers all sensors
      std::vector<size_t> pollList;
      bool tsync {false};
      int maxTimeLeak {10};
      std::string iconSet {"light"};            // of the WEBIF, for the images of the state
      std::string heatingType {"P4"};           // the picture of the heating if there is no image for the state
      time_t nextTimeSyncAt {0};
      time_t nextDriftReportAt {0};

      static bool shutdown;
      static volatile bool rediscover;
};

bool P4Mqttd::shutdown {false};
volatile bool P4Mqttd::rediscover {false};

//***************************************************************************
// Widget Defaults
//   the rules (first match wins) for the default options of the dashboard widget of a sensor:
//   type of the sensor, unit, a part of the title (case insensitive) -> kind of widget, scale and symbol
//***************************************************************************

// the kinds of widgets of the homectld (enum WidgetType in its service.h, the WEBIF takes the number)

enum WidgetKind
{
   wkSymbol      = 0,
   wkText        = 2,
   wkValue       = 3,
   wkSymbolValue = 9,
   wkMeter       = 5,
   wkMeterLevel  = 6,
   wkSymbolText  = 12,
   wkSpecialSymbol = 14,
   wkLevel       = 15
};

struct WidgetRule
{
   const char* type;
   const char* unit;          // "*" -> any
   const char* title;         // "" -> any
   WidgetKind kind;
   int min;
   int max;
   int step;
   bool peak;
   const char* symbol;        // MDI symbol of a switch, "" -> the default of the homectld
   const char* imgOn;         // or an image (the animated images of the p4d), when the switch is on
   const char* imgOff;        // ... and off
   bool spin;                 // the MDI symbol turns while the switch is on
   const char* color;         // color of the widget, empty -> the default of the homectld
   int barWidth;              // special symbol: width of the tank
};

static const WidgetRule widgetRules[] =
{
   // values

   { "P4VA", "*",   "Kesselanforderung", wkSymbol, 0,    0,    0, false, "" },       // 0 / 1: a request of a circuit or the boiler
   { "P4VA", "°C",  "Abgas",        wkMeterLevel,  0,  300,   50, true,  "" },
   { "P4VA", "°C",  "Zündrohr",     wkMeterLevel,  0,  600,  100, true,  "" },
   { "P4VA", "°C",  "Außentemp",    wkMeterLevel,-20,   40,   10, true,  "" },
   { "P4VA", "°C",  "Puffer",       wkMeterLevel,  0,  100,   20, true,  "" },
   { "P4VA", "°C",  "Vorlauf",      wkMeterLevel,  0,   80,   10, true,  "" },
   { "P4VA", "°C",  "Rücklauf",     wkMeterLevel,  0,   80,   10, true,  "" },
   { "P4VA", "°C",  "Kessel",       wkMeterLevel,  0,  100,   20, true,  "" },
   { "P4VA", "°C",  "",             wkMeterLevel,  0,  100,   20, true,  "" },
   { "P4VA", "%",   "Füllstand",    wkSpecialSymbol, 0, 100,   0, false, "", nullptr, nullptr, false, "rgb(245, 214, 70)", 100 },   // the pellet tank
   { "P4VA", "%",   "pump",         wkSymbolValue, 0,    0,    0, false, "", "img/icon/pump-on.gif", "img/icon/pump-off.png" },   // pumps with 0 - 100 %
   { "P4VA", "%",   "",             wkMeterLevel,  0,  100,   20, true,  "" },
   { "P4VA", "U",   "",             wkMeter,       0, 5000, 1000, true,  "" },
   { "P4VA", "bar", "",             wkMeter,       0,    4,    1, false, "" },
   { "P4VA", "*",   "",             wkValue,       0,    0,    0, false, "" },

   // outputs: analog outputs are percent values

   { "P4AO", "*",   "Stokerantrieb", wkValue,      0,    0,    0, false, "" },

   { "P4AO", "%",   "",             wkMeterLevel,  0,  100,   20, true,  "" },
   { "P4AO", "*",   "",             wkValue,       0,    0,    0, false, "" },

   // switches (digital in- and outputs), the symbol by the title

   { "P4DO", "*", "pumpe",          wkSymbol, 0, 0, 0, false, "", "img/icon/pump-on.gif", "img/icon/pump-off.png" },
   { "P4DO", "*", "mischer",        wkSymbol, 0, 0, 0, false, "mdi:mdi-valve" },
   { "P4DO", "*", "saugzug",        wkSymbol, 0, 0, 0, false, "mdi:mdi-fan", nullptr, nullptr, true },
   { "P4DO", "*", "gebläse",        wkSymbol, 0, 0, 0, false, "mdi:mdi-fan", nullptr, nullptr, true },
   { "P4DO", "*", "lüfter",         wkSymbol, 0, 0, 0, false, "mdi:mdi-fan", nullptr, nullptr, true },
   { "P4DO", "*", "zünd",           wkSymbol, 0, 0, 0, false, "mdi:mdi-fire" },
   { "P4DO", "*", "brenner",        wkSymbol, 0, 0, 0, false, "mdi:mdi-gas-burner" },
   { "P4DO", "*", "schnecke",       wkSymbol, 0, 0, 0, false, "mdi:mdi-screw-lag" },
   { "P4DO", "*", "",               wkSymbol, 0, 0, 0, false, "" },
   { "P4DI", "*", "tür",            wkSymbol, 0, 0, 0, false, "mdi:mdi-door-open" },
   { "P4DI", "*", "freigabe",       wkSymbol, 0, 0, 0, false, "mdi:mdi-lock-open-variant" },
   { "P4DI", "*", "",               wkSymbol, 0, 0, 0, false, "" },

   // state, mode and time of the control

   { "P4UD", "zst", "",             wkSymbolText, 0, 0, 0, false, "" },
   { "P4UD", "*",   "",             wkText,       0, 0, 0, false, "" },

   { nullptr }
};

// the "parameter" object of a sensor message, nullptr if no rule matches

static json_t* widgetDefaults(const std::string& type, const std::string& unit, const std::string& title)
{
   for (const WidgetRule* r = widgetRules; r->type; r++)
   {
      if (type != r->type)
         continue;

      if (strcmp(r->unit, "*") != 0 && unit != r->unit)
         continue;

      if (!isEmpty(r->title) && !strcasestr(title.c_str(), r->title))
         continue;

      json_t* jParam {json_object()};

      json_object_set_new(jParam, "widgettype", json_integer(r->kind));

      if (r->kind == wkMeter || r->kind == wkMeterLevel || r->kind == wkLevel || r->kind == wkSpecialSymbol)
      {
         json_object_set_new(jParam, "scalemin", json_integer(r->min));
         json_object_set_new(jParam, "scalemax", json_integer(r->max));
         json_object_set_new(jParam, "scalestep", json_integer(r->step));
         json_object_set_new(jParam, "showpeak", json_boolean(r->peak));
      }

      if (!isEmpty(r->color))
         json_object_set_new(jParam, "color", json_string(r->color));

      if (r->barWidth)
         json_object_set_new(jParam, "barwidth", json_integer(r->barWidth));

      if (!isEmpty(r->symbol))
      {
         json_object_set_new(jParam, "symbol", json_string(r->symbol));
         json_object_set_new(jParam, "symbolOn", json_string(r->symbol));

         if (r->spin)
            json_object_set_new(jParam, "spin", json_true());
      }
      else if (!isEmpty(r->imgOn) && !isEmpty(r->imgOff))
      {
         // the homectld sets an MDI symbol by default which wins over the images, so it has to be emptied

         json_object_set_new(jParam, "symbol", json_string(""));
         json_object_set_new(jParam, "symbolOn", json_string(""));
         json_object_set_new(jParam, "imgon", json_string(r->imgOn));
         json_object_set_new(jParam, "imgoff", json_string(r->imgOff));
      }

      return jParam;
   }

   return nullptr;
}

// the image of the p4d for the state of the control (animated, in the icon sets 'light' and 'dark')

static std::string stateImage(int state, const std::string& iconSet)
{
   const char* image {};

   if (state == 0)
      image = "state-error.gif";
   else if (state == 1 || state == 5)
      image = "state-fireoff.gif";
   else if (state == 2 || state == 71 || state == 9)
      image = "state-heatup.gif";
   else if (state == 3)
      image = "state-fire.gif";
   else if (state == 4)
      image = "state-firehold.gif";
   else if (state == 6)
      image = "state-dooropen.gif";
   else if (state == 7)
      image = "state-preparation.gif";
   else if (state == 8)
      image = "state-warmup.gif";
   else if (state == 15 || state == 70 || state == 69)
      image = "state-clean.gif";
   else if ((state >= 10 && state <= 14) || state == 35 || state == 16)
      image = "state-wait.gif";
   else if (state == 60 || state == 61 || state == 72)
      image = "state-shfire.gif";

   return image ? "img/state/" + iconSet + "/" + image : "";
}

static constexpr int maxFailures {5};          // failed transactions in a row -> reopen the line
static constexpr int maxListItems {20000};     // a list of the control which doesn't end (a menu has about 1300 entries)
static constexpr int busGapMs {30};            // pause between two transactions (the others get their turn)

//***************************************************************************
// Object
//***************************************************************************

P4Mqttd::P4Mqttd(const Options& aOptions)
   : opt(aOptions)
{
   // in the parallel mode we never write, whatever is configured and we are not faster
   //  than the old p4d (default interval of the p4d is 60 seconds)

   if (opt.parallel)
   {
      opt.readonly = true;
      opt.pollInterval = std::max(opt.pollInterval, 60);
   }

   dataTopic = opt.dataTopic.empty() ? opt.topic + "/sensor" : opt.dataTopic;
   sensorsFile = opt.cacheDir + "/sensors.json";

   sem = new Sem(semaphoreKey);
   serial = new Serial(B57600);
   request = new P4Request(serial);
}

P4Mqttd::~P4Mqttd()
{
   publishStatus(false);
   mqttDisconnect();

   serial->close();

   delete request;
   delete serial;
   delete sem;
}

//***************************************************************************
// Init
//***************************************************************************

int P4Mqttd::init()
{
   tell(eloAlways, "p4mqttd %s starting, tty '%s', mqtt '%s', base topic '%s', data topic '%s'%s%s",
        P4MQTTD_VERSION, opt.tty.c_str(), opt.mqttUrl.c_str(), opt.topic.c_str(), dataTopic.c_str(),
        opt.readonly ? ", READONLY" : "", opt.parallel ? ", PARALLEL" : "");

   tell(eloAlways, "Intervals (until the config of the homectld arrives): state %d s, values %d s, cache '%s'",
        opt.stateInterval, opt.pollInterval, opt.cacheDir.c_str());

   if (!opt.readonly)
      tell(eloAlways, "Attention: Writing of parameters is enabled. Read the disclaimer in README.md, use at your own risk!");

   return success;
}

//***************************************************************************
// Loop
//***************************************************************************

int P4Mqttd::loop()
{
   while (!doShutDown())
   {
      mqttConnection();
      mqttRead();

      if (serialConnection() != success)
      {
         sleep(1);
         continue;
      }

      uint64_t now {cTimeMs::Now()};

      // the first state tells the firmware, needed for the cache

      if (now >= nextStateAt)
      {
         updateState();
         nextStateAt = cTimeMs::Now() + opt.stateInterval * 1000;
         usleep(busGapMs * 1000);
         continue;
      }

      if (haveState && (!sensorsReady || rediscover))
      {
         // reading the lists of the control (one request after the other, without polling
         //  in between) takes a while, but only at the first start or after a firmware change

         if (rediscover || loadCache() != success)
            discover();

         rediscover = false;
         sensorsReady = true;
         announceSweep = true;
         nextSweepAt = 0;
         continue;
      }

      if (!commands.empty())
      {
         dispatchCommands();
         continue;
      }

      if (sensorsReady && haveState && (errorsDue || time(0) >= nextErrorsAt))
      {
         updateErrors();
         continue;
      }

      if (sensorsReady && !sweeping && now >= nextSweepAt)
      {
         sweeping = true;
         sweepStartedAt = now;
         pollIndex = 0;
         pollList.clear();

         for (size_t i = 0; i < sensors.size(); i++)
            if (announceSweep || !haveActive || isActive(sensors[i]))
               pollList.push_back(i);
      }

      if (sweeping)
      {
         if (pollIndex < pollList.size())
         {
            pollOne(pollList[pollIndex]);
            pollIndex++;
            usleep(busGapMs * 1000);
         }

         if (pollIndex >= pollList.size())
         {
            uint64_t duration {cTimeMs::Now() - sweepStartedAt};

            sweeping = false;
            nextSweepAt = std::max(cTimeMs::Now(), sweepStartedAt + opt.pollInterval * 1000);

            if (!firstSweepLogged || duration > (uint64_t)opt.pollInterval * 1000)
               tell(eloAlways, "Info: Polled %zu of %zu values in %.1f s%s", pollList.size(), sensors.size(), duration / 1000.0,
                    duration > (uint64_t)opt.pollInterval * 1000 ? " (longer than the interval!)" : "");

            firstSweepLogged = true;
            announceSweep = false;
         }

         continue;
      }

      usleep(50 * 1000);
   }

   return done;
}

//***************************************************************************
// Bus Lock
//   one request/response cycle at a time - for all processes using the line,
//   what is left in the input buffer (e.g. a late byte of a request of another
//   process) is dropped before we send
//***************************************************************************

void P4Mqttd::lockBus()
{
   byte b {0};

   sem->p();

   while (serial->look(b, 10) == success)
      tell(eloDebug, "-> 0x%2.2x", b);
}

//***************************************************************************
// Bus Failed
//   a number of failed transactions in a row means the line is gone
//***************************************************************************

int P4Mqttd::busFailed(const char* what)
{
   if (++failures < maxFailures)
      return fail;

   tell(eloAlways, "Error: %d transactions failed in a row (last: %s), reopen the line", failures, what);

   failures = 0;
   commOk = false;
   serial->close();
   publishStatus(true);

   return fail;
}

//***************************************************************************
// Serial Connection
//   (re)open the line and check the communication
//***************************************************************************

int P4Mqttd::serialConnection()
{
   if (commOk)
      return success;

   time_t now {time(0)};

   if (now < serialRetryAt)
      return fail;

   serialRetryAt = now + 5;

   sem->p();
   serial->close();

   if (serial->open(opt.tty.c_str()) != success)
   {
      sem->v();
      tell(eloAlways, "Error: Opening '%s' failed", opt.tty.c_str());
      publishStatus(true);
      return fail;
   }

   sem->v();

   lockBus();
   int status {request->check()};
   unlockBus();

   if (status != success)
   {
      tell(eloAlways, "Error: No answer from the S 3200 at '%s'", opt.tty.c_str());
      serial->close();
      publishStatus(true);
      return fail;
   }

   tell(eloAlways, "Communication with the S 3200 at '%s' established", opt.tty.c_str());
   commOk = true;
   failures = 0;
   lastOk = now;
   nextStateAt = 0;
   publishStatus(true);

   return success;
}

//***************************************************************************
// Update State
//***************************************************************************

int P4Mqttd::updateState()
{
   lockBus();
   int status {request->getStatus(&currentState)};
   unlockBus();

   if (status != success)
   {
      tell(eloAlways, "Error: Reading the state failed (%d)", status);
      return busFailed("state");
   }

   busOk();
   lastOk = time(0);
   haveState = true;

   // the error list changes with the error states, otherwise it is read every 10 minutes

   if (currentState.state != lastStateCode)
   {
      if (lastStateCode != -1 && (Fs::isError(lastStateCode) || Fs::isError(currentState.state)))
         errorsDue = true;

      lastStateCode = currentState.state;
   }

   tell(eloDetail, "State (%d) '%s', mode (%d) '%s', firmware '%s'",
        currentState.state, currentState.stateinfo, currentState.mode, currentState.modeinfo,
        currentState.version);

   publishStatus(true);
   publishState();
   publishUserDefined();
   checkTimeSync();

   return success;
}

//***************************************************************************
// Time Synchronization of the control (like the p4d: at most once a day, not before 3:00)
//   a write to the control, so never in the read only mode
//***************************************************************************

void P4Mqttd::scheduleTimeSyncIn(int offset)
{
   struct tm tm {};
   time_t now {time(0)};

   localtime_r(&now, &tm);

   tm.tm_sec = 0;
   tm.tm_min = 0;
   tm.tm_hour = 3;
   tm.tm_isdst = -1;               // force DST auto detect

   nextTimeSyncAt = mktime(&tm) + offset;
}

int P4Mqttd::checkTimeSync()
{
   if (!tsync || !maxTimeLeak || opt.readonly)
      return done;

   time_t now {time(0)};

   if (!nextTimeSyncAt)
      scheduleTimeSyncIn(0);

   if (labs(currentState.time - now) <= maxTimeLeak)
      return done;

   if (now > nextDriftReportAt)
   {
      tell(eloAlways, "Time drift is %lld seconds", (long long)(currentState.time - now));
      nextDriftReportAt = now + 2 * tmeSecondsPerMinute;
   }

   if (now <= nextTimeSyncAt)
      return done;

   scheduleTimeSyncIn(tmeSecondsPerDay);

   tell(eloAlways, "Time drift is %lld seconds, syncing now", (long long)(currentState.time - now));

   lockBus();
   int status {request->syncTime()};
   unlockBus();

   tell(eloAlways, status == success ? "Time sync succeeded" : "Time sync failed");

   sleep(2);                       // the S 3200 needs some seconds to store the time

   lockBus();
   status = request->getStatus(&currentState);
   unlockBus();

   if (status == success)
      tell(eloAlways, "Time drift now %lld seconds", (long long)(currentState.time - time(0)));

   return done;
}

//***************************************************************************
// Sensors - Discovery
//   the value specs and the in/outputs of the menu, the same as p4d does
//***************************************************************************

int P4Mqttd::discover()
{
   tell(eloAlways, "Reading the sensor definitions from the S 3200 ...");

   uint64_t start {cTimeMs::Now()};
   sensors.clear();
   menu.clear();

   if (discoverValues() != success || discoverIo() != success)
   {
      tell(eloAlways, "Error: Reading the sensor definitions failed, retry later");
      sensors.clear();
      menu.clear();
      sensorsReady = false;
      cacheFirmware.clear();
      return fail;
   }

   tell(eloAlways, "Read %zu sensor definitions and %zu menu items in %.1f s", sensors.size(), menu.size(), (cTimeMs::Now() - start) / 1000.0);

   cacheFirmware = currentState.version;
   saveCache();

   return success;
}

int P4Mqttd::discoverValues()
{
   Fs::ValueSpec v;
   int fails {0};
   int count {0};
   int status {success};
   BusGuard guard(this);

   for (bool first {true}; !doShutDown(); first = false)
   {
      status = first ? request->getFirstValueSpec(&v) : request->getNextValueSpec(&v);

      if (status == Fs::wrnLast)
         break;

      if (status != success)
      {
         if (status == Fs::wrnEmpty)
            continue;

         if (++fails >= maxFailures)
            return fail;

         // the control keeps its position in the list, a failed step is repeated by 'next'

         continue;
      }

      fails = 0;

      if (isEmpty(v.description))
         continue;

      Sensor s;
      s.type = "P4VA";
      s.address = v.address;
      s.title = v.description;
      s.unit = strcmp(v.unit, "°") == 0 ? "°C" : v.unit;
      s.factor = v.factor ? v.factor : 1;
      s.dataType = v.type;
      sensors.push_back(s);
      count++;

      tell(eloDebug, "Value 0x%04x '%s' factor %d unit '%s' type %d", v.address, v.description, v.factor, v.unit, v.type);

      if (count > maxListItems)
      {
         tell(eloAlways, "Error: The value list doesn't end after %d entries, giving up", count);
         return fail;
      }
   }

   tell(eloAlways, "Found %d values", count);

   return doShutDown() ? fail : success;
}

int P4Mqttd::discoverIo()
{
   Fs::MenuItem m;
   int fails {0};
   int count {0};
   int items {0};
   int status {success};
   BusGuard guard(this);

   for (bool first {true}; !doShutDown(); first = false)
   {
      status = first ? request->getFirstMenuItem(&m) : request->getNextMenuItem(&m);

      if (status == Fs::wrnLast)
         break;

      if (status == Fs::wrnSkip)
         continue;

      if (status != success)
      {
         if (++fails >= maxFailures)
            return fail;

         continue;
      }

      fails = 0;
      items++;

      if (items % 250 == 0)
         tell(eloAlways, "Info: %d menu items read ...", items);

      if (items > maxListItems)
      {
         tell(eloAlways, "Error: The menu list doesn't end after %d entries, giving up", items);
         return fail;
      }

      MenuEntry e;
      e.id = menu.size() + 1;
      e.type = m.type;
      e.parent = m.parent;
      e.child = m.child;
      e.address = m.address;
      e.title = m.description ? m.description : "";
      e.unit = m.unit ? m.unit : "";
      menu.push_back(e);

      const char* type {};

      switch (m.type)
      {
         case Fs::mstDigOut: type = "P4DO"; break;
         case Fs::mstDigIn:  type = "P4DI"; break;
         case Fs::mstAnlOut: type = "P4AO"; break;
      }

      if (!type || isEmpty(m.description))
         continue;

      bool known {false};

      for (const auto& s : sensors)
         if (s.type == type && s.address == m.address)
            known = true;

      if (known)
         continue;

      Sensor s;
      s.type = type;
      s.address = m.address;
      s.title = m.description;
      s.unit = m.unit ? m.unit : "";

      if (s.unit.empty() && s.type == "P4AO")
         s.unit = "%";

      sensors.push_back(s);
      count++;
   }

   tell(eloAlways, "Found %d in/outputs in %d menu items", count, items);

   return doShutDown() ? fail : success;
}

//***************************************************************************
// Cache of the Sensors
//   only valid for the firmware it was read with
//***************************************************************************

int P4Mqttd::saveCache()
{
   json_t* jRoot {json_object()};
   json_t* jSensors {json_array()};

   json_object_set_new(jRoot, "version", json_integer(1));
   json_object_set_new(jRoot, "firmware", json_string(cacheFirmware.c_str()));
   json_object_set_new(jRoot, "sensors", jSensors);

   json_t* jMenu {json_array()};
   json_object_set_new(jRoot, "menu", jMenu);

   for (const auto& m : menu)
   {
      json_t* jM {json_object()};

      json_object_set_new(jM, "type", json_integer(m.type));
      json_object_set_new(jM, "parent", json_integer(m.parent));
      json_object_set_new(jM, "child", json_integer(m.child));
      json_object_set_new(jM, "address", json_integer(m.address));
      json_object_set_new(jM, "title", json_string(m.title.c_str()));

      if (!m.unit.empty())
         json_object_set_new(jM, "unit", json_string(m.unit.c_str()));

      json_array_append_new(jMenu, jM);
   }

   for (const auto& s : sensors)
   {
      json_t* jS {json_object()};

      json_object_set_new(jS, "type", json_string(s.type.c_str()));
      json_object_set_new(jS, "address", json_integer(s.address));
      json_object_set_new(jS, "title", json_string(s.title.c_str()));
      json_object_set_new(jS, "unit", json_string(s.unit.c_str()));

      if (s.type == "P4VA")
      {
         json_object_set_new(jS, "factor", json_integer(s.factor));
         json_object_set_new(jS, "dataType", json_integer(s.dataType));
      }

      json_array_append_new(jSensors, jS);
   }

   mkdir(opt.cacheDir.c_str(), 0755);

   std::string tmp {sensorsFile + ".tmp"};

   if (json_dump_file(jRoot, tmp.c_str(), JSON_INDENT(1)) != 0 || rename(tmp.c_str(), sensorsFile.c_str()) != 0)
      tell(eloAlways, "Warning: Can't write the cache '%s', %s", sensorsFile.c_str(), strerror(errno));
   else
      tell(eloAlways, "Stored %zu sensors and %zu menu items in '%s'", sensors.size(), menu.size(), sensorsFile.c_str());

   json_decref(jRoot);

   return done;
}

int P4Mqttd::loadCache()
{
   json_error_t error;
   json_t* jRoot {json_load_file(sensorsFile.c_str(), 0, &error)};

   if (!jRoot)
      return fail;

   std::string firmware {getStringFromJson(jRoot, "firmware", "")};
   json_t* jSensors {getObjectFromJson(jRoot, "sensors")};

   json_t* jMenu {getObjectFromJson(jRoot, "menu")};

   if (firmware != currentState.version || !json_is_array(jSensors) || !json_array_size(jSensors) ||
       !json_is_array(jMenu) || !json_array_size(jMenu))
   {
      tell(eloAlways, "Cache '%s' is for firmware '%s', now '%s', read the definitions again",
           sensorsFile.c_str(), firmware.c_str(), currentState.version);
      json_decref(jRoot);
      return fail;
   }

   sensors.clear();

   size_t index {};
   json_t* jS {};

   json_array_foreach(jSensors, index, jS)
   {
      Sensor s;
      s.type = getStringFromJson(jS, "type", "");
      s.address = getIntFromJson(jS, "address", 0);
      s.title = getStringFromJson(jS, "title", "");
      s.unit = getStringFromJson(jS, "unit", "");
      s.factor = getIntFromJson(jS, "factor", 1);
      s.dataType = getIntFromJson(jS, "dataType", 0);

      if (!s.type.empty() && !s.title.empty())
         sensors.push_back(s);
   }

   menu.clear();

   json_array_foreach(jMenu, index, jS)
   {
      MenuEntry e;
      e.id = menu.size() + 1;
      e.type = getIntFromJson(jS, "type", 0);
      e.parent = getIntFromJson(jS, "parent", 0);
      e.child = getIntFromJson(jS, "child", 0);
      e.address = getIntFromJson(jS, "address", 0);
      e.title = getStringFromJson(jS, "title", "");
      e.unit = getStringFromJson(jS, "unit", "");
      menu.push_back(e);
   }

   json_decref(jRoot);
   cacheFirmware = firmware;

   tell(eloAlways, "Loaded %zu sensors and %zu menu items from '%s' (firmware %s)", sensors.size(), menu.size(),
        sensorsFile.c_str(), firmware.c_str());

   return sensors.empty() ? fail : success;
}

//***************************************************************************
// Configuration from the homectld
//***************************************************************************

bool P4Mqttd::isActive(const Sensor& s)
{
   return activeSensors.count(s.type + ":" + std::to_string(s.address)) > 0;
}

int P4Mqttd::mqttRead()
{
   if (!mqttReader || !mqttReader->isConnected())
      return done;

   MemoryStruct message;

   while (mqttReader->read(&message, 10) == success)
   {
      std::string t {mqttReader->getLastReadTopic()};

      if (t == opt.topic + "/config")
         applyConfig(message.memory ? message.memory : "");
      else if (t == opt.topic + "/cmd" && !mqttReader->isRetained())    // no old commands
      {
         if (commands.size() < 20)
            commands.push_back(message.memory ? message.memory : "");
         else
            tell(eloAlways, "Warning: Too many pending commands, ignoring [%s]", message.memory);
      }
   }

   return done;
}

int P4Mqttd::applyConfig(const char* message)
{
   json_t* jConfig {jsonLoad(message, 0, true)};

   if (!jConfig)
   {
      tell(eloAlways, "Error: Can't parse the config [%s]", message);
      return fail;
   }

   int poll {std::max(5, getIntFromJson(jConfig, "pollInterval", opt.pollInterval))};
   int state {std::max(1, getIntFromJson(jConfig, "stateInterval", opt.stateInterval))};

   if (opt.parallel)
      poll = std::max(poll, 60);

   bool pollChanged {poll != opt.pollInterval};
   opt.pollInterval = poll;
   opt.stateInterval = state;
   // an empty value is no value (the config of the homectld may not have set the icon set)

   if (std::string v {getStringFromJson(jConfig, "iconSet", "")}; !v.empty())
      iconSet = v;

   if (std::string v {getStringFromJson(jConfig, "heatingType", "")}; !v.empty())
      heatingType = v;
   tsync = getBoolFromJson(jConfig, "tsync", false);
   maxTimeLeak = getIntFromJson(jConfig, "maxTimeLeak", 10);

   std::set<std::string> active;
   json_t* jActive {getObjectFromJson(jConfig, "active")};
   bool haveList {json_is_object(jActive)};

   if (haveList)
   {
      const char* type {};
      json_t* jList {};

      json_object_foreach(jActive, type, jList)
      {
         size_t i {};
         json_t* jAddress {};

         json_array_foreach(jList, i, jAddress)
            active.insert(std::string(type) + ":" + std::to_string(json_integer_value(jAddress)));
      }
   }

   // a newly activated sensor gets its value without waiting for the end of the interval

   bool added {false};

   for (const auto& a : active)
      if (!activeSensors.count(a))
         added = true;

   bool changed {!configReceived || active != activeSensors || haveList != haveActive || pollChanged};

   activeSensors = active;
   haveActive = haveList;
   configReceived = true;

   if (added && !sweeping)
      nextSweepAt = 0;

   if (changed)
      tell(eloAlways, "Config received: values every %d s, state every %d s, time sync %s (max. drift %d s), %s%zu active sensors",
           opt.pollInterval, opt.stateInterval, tsync ? "on" : "off", maxTimeLeak,
           haveActive ? "" : "all sensors polled, ", activeSensors.size());

   json_decref(jConfig);

   return success;
}

//***************************************************************************
// Commands
//***************************************************************************

int P4Mqttd::reply(json_t* jReply, const char* status, const char* message)
{
   json_object_set_new(jReply, "status", json_string(status));

   if (message)
      json_object_set_new(jReply, "message", json_string(message));

   return publish((opt.topic + "/reply").c_str(), jReply, false);
}

int P4Mqttd::dispatchCommands()
{
   std::string message {commands.front()};
   commands.pop_front();

   json_t* jCmd {jsonLoad(message.c_str(), 0, true)};

   if (!jCmd)
   {
      tell(eloAlways, "Error: Can't parse the command [%s]", message.c_str());
      return fail;
   }

   std::string cmd {getStringFromJson(jCmd, "cmd", "")};
   json_t* jReply {json_object()};

   json_object_set_new(jReply, "rid", json_string(getStringFromJson(jCmd, "rid", "")));
   json_object_set_new(jReply, "cmd", json_string(cmd.c_str()));

   tell(eloAlways, "Command '%s' [%s]", cmd.c_str(), message.c_str());

   if (!commOk || !sensorsReady)
      reply(jReply, "notready", "Keine Verbindung zur Steuerung bzw. die Menüstruktur ist noch nicht gelesen");
   else if (cmd == "menu")
      cmdMenu(jCmd, jReply);
   else if (cmd == "initmenu" || cmd == "initsensors")
      cmdInitMenu(jReply);
   else if (cmd == "inittimes")
      cmdInitTimes(jReply);
   else if (cmd == "errors")
      cmdErrors(jReply);
   else if (cmd == "parget")
      cmdParGet(jCmd, jReply);
   else if (cmd == "parset")
      cmdParSet(jCmd, jReply);
   else if (cmd == "timeset")
      cmdTimeSet(jCmd, jReply);
   else
      reply(jReply, "error", "unknown command");

   json_decref(jCmd);

   return done;
}

int P4Mqttd::cmdInitMenu(json_t* jReply)
{
   if (discover() != success)
   {
      sensorsReady = menu.size() > 0;
      return reply(jReply, "error", "Lesen der Menüstruktur fehlgeschlagen");
   }

   announceSweep = true;
   nextSweepAt = 0;
   sweeping = false;
   timesLoaded = false;                // the time programs belong to the firmware, too

   json_object_set_new(jReply, "menuItems", json_integer(menu.size()));
   json_object_set_new(jReply, "sensors", json_integer(sensors.size()));

   return reply(jReply, "ok");
}

//***************************************************************************
// Menu Item Value
//   the value of an item of the menu as the p4d shows it, returns 'success' if the item
//   has a value, 'ignore' if it hasn't (groups and so on) or fail on a communication error
//***************************************************************************

int P4Mqttd::menuItemValue(const MenuEntry& m, json_t* jItem, std::string& value)
{
   char buf[100] {};
   int status {success};
   int paddr {(int)m.address};
   std::string unit {m.unit};
   bool editable {false};

   if (m.type == Fs::mstFirmware)
   {
      value = haveState ? currentState.version : "";
      unit.clear();
   }

   else if (m.type == Fs::mstDigOut || m.type == Fs::mstDigIn || m.type == Fs::mstAnlOut)
   {
      Fs::IoValue v(paddr);

      lockBus();

      if (m.type == Fs::mstDigOut)
         status = request->getDigitalOut(&v);
      else if (m.type == Fs::mstDigIn)
         status = request->getDigitalIn(&v);
      else
         status = request->getAnalogOut(&v);

      unlockBus();

      if (status != success)
         return fail;

      if (m.type == Fs::mstAnlOut)
      {
         if (v.mode == 0xff)
            snprintf(buf, sizeof(buf), "%d (A)", v.state);
         else
            snprintf(buf, sizeof(buf), "%d (%d)", v.state, v.mode);

         if (unit.empty())
            unit = "%";
      }
      else
         snprintf(buf, sizeof(buf), "%s (%c)", v.state ? "on" : "off", isprint(v.mode) ? v.mode : '?');

      value = buf;
      unit.clear();
   }

   else if (m.type == Fs::mstMesswert || m.type == Fs::mstMesswert1)
   {
      const Sensor* sensor {};

      for (const auto& s : sensors)
         if (s.type == "P4VA" && s.address == m.address)
            sensor = &s;

      if (!sensor)
         return ignore;

      Fs::Value v(paddr);

      lockBus();
      status = request->getValue(&v);
      unlockBus();

      if (status != success)
         return fail;

      int raw {sensor->dataType == 1 ? (int)(word)v.value : (int)(sword)v.value};
      snprintf(buf, sizeof(buf), "%.2f", raw / (double)sensor->factor);
      value = buf;
      unit = sensor->unit;
   }

   else if (Fs::isGroup(m.type) || m.type == Fs::mstBusValues || m.type == Fs::mstReset || m.type == Fs::mstEmpty)
      return ignore;

   else if (m.child)
      return ignore;                         // I have childs -> I have no value

   else if (paddr == 0 && m.type != Fs::mstPar)
      return ignore;                         // address 0 only for type mstPar

   else if (paddr == 9997 || paddr == 9998 || paddr == 9999)
      return ignore;                         // takes a long while and delivers nothing useful

   else
   {
      Fs::ConfigParameter p(paddr);

      lockBus();
      status = request->getParameter(&p);
      unlockBus();

      if (status != success)
         return fail;

      value = p.toNice(m.type).string();
      unit = p.unit ? p.unit : "";

      json_object_set_new(jItem, "digits", json_integer(p.digits));
      json_object_set_new(jItem, "min", json_real(p.rMin));
      json_object_set_new(jItem, "max", json_real(p.rMax));
      json_object_set_new(jItem, "default", json_real(p.rDefault));

      editable = m.type == Fs::mstPar || m.type == Fs::mstParSet || m.type == Fs::mstParSet1 ||
         m.type == Fs::mstParSet2 || m.type == Fs::mstParDig || m.type == Fs::mstParZeit;
   }

   if (unit == "°")
      unit = "°C";

   json_object_set_new(jItem, "unit", json_string(unit.c_str()));
   json_object_set_new(jItem, "editable", json_boolean(editable));

   return success;
}

//***************************************************************************
// Command 'menu'
//   the items of one level, like the p4d does for its WEBIF
//***************************************************************************

static std::string trimMenuTitle(const std::string& title)
{
   std::string t {title};

   while (!t.empty() && (isspace((unsigned char)t.back()) || t.back() == '\0'))
      t.pop_back();

   if (!t.empty() && t.back() == ':')
      t.pop_back();

   return t;
}

int P4Mqttd::cmdMenu(json_t* jCmd, json_t* jReply)
{
   uint parent {(uint)getIntFromJson(jCmd, "parent", 1)};
   uint last {0};
   std::string title;
   json_t* jItems {json_array()};
   int failed {0};

   json_object_set_new(jReply, "items", jItems);
   json_object_set_new(jReply, "parent", json_integer(parent));

   // the item which opens this level gives the title and the way back

   for (const auto& m : menu)
   {
      if (m.child == parent)
      {
         last = m.parent;
         title = m.title;
         break;
      }
   }

   json_object_set_new(jReply, "last", json_integer(last));
   json_object_set_new(jReply, "title", json_string(title.c_str()));

   for (const auto& m : menu)
   {
      if (m.parent != parent || doShutDown())
         continue;

      std::string t {trimMenuTitle(m.title)};

      if (t.empty() || m.type == Fs::mstBusValues || m.type == Fs::mstReset)
         continue;

      if (m.address == 9997 || m.address == 9998 || m.address == 9999)
         continue;

      // the weekday lists of the time programs ('Zeiten' of the boiler, circuits, ...) are shown in place

      if ((m.type == Fs::mstMenuChoice || m.type == Fs::mstMenu2) && t == "Zeiten" &&
          (m.child == 230 || m.child == 350 || m.child == 430 || m.child == 573))
      {
         addTimeGroup(m, jItems);
         continue;
      }

      json_t* jItem {json_object()};
      std::string value;

      json_object_set_new(jItem, "id", json_integer(m.id));
      json_object_set_new(jItem, "parent", json_integer(m.parent));
      json_object_set_new(jItem, "child", json_integer(m.child));
      json_object_set_new(jItem, "type", json_integer(m.type));
      json_object_set_new(jItem, "address", json_integer(m.address));
      json_object_set_new(jItem, "title", json_string(t.c_str()));

      int status {menuItemValue(m, jItem, value)};

      if (status == success)
      {
         busOk();
         json_object_set_new(jItem, "value", json_string(value.c_str()));
         usleep(busGapMs * 1000);
      }
      else if (status == fail)
      {
         failed++;
         busFailed(t.c_str());
         json_object_set_new(jItem, "error", json_true());
         json_object_set_new(jItem, "unit", json_string(""));
         usleep(busGapMs * 1000);
      }
      else if (!m.child)
      {
         json_decref(jItem);          // nothing to show, nothing to open
         continue;
      }
      else
         json_object_set_new(jItem, "unit", json_string(""));

      json_array_append_new(jItems, jItem);
   }

   if (failed)
      tell(eloAlways, "Warning: %d values of the menu level %u couldn't be read", failed, parent);

   return reply(jReply, "ok");
}

//***************************************************************************
// Time Programs
//   the control can't be asked for one program, only for the whole list one after the other, so
//   it is read once and kept (in memory and in times.json), 'inittimes' reads it again
//***************************************************************************

int P4Mqttd::readTimes()
{
   std::map<int,Fs::TimeRanges> list;
   int status {success};
   int steps {0};
   BusGuard guard(this);

   tell(eloAlways, "Reading the time programs from the S 3200 ...");

   for (bool first {true}; !doShutDown(); first = false)
   {
      Fs::TimeRanges t;

      status = first ? request->getFirstTimeRanges(&t) : request->getNextTimeRanges(&t);

      if (status == Fs::wrnLast)
         break;

      if (status != success)
      {
         tell(eloAlways, "Error: Reading the time programs failed after %zu entries", list.size());
         return fail;
      }

      list[t.address] = t;

      if (++steps > 300)
      {
         tell(eloAlways, "Error: The list of the time programs doesn't end, giving up");
         return fail;
      }
   }

   if (list.empty() || doShutDown())       // a list which ended by the shutdown is not complete
      return fail;

   times = list;
   timesLoaded = true;
   tell(eloAlways, "Read %zu time programs", times.size());

   return success;
}

int P4Mqttd::saveTimes()
{
   json_t* jRoot {json_object()};
   json_t* jList {json_array()};

   json_object_set_new(jRoot, "firmware", json_string(cacheFirmware.c_str()));
   json_object_set_new(jRoot, "times", jList);

   for (auto& [address, t] : times)
   {
      json_t* jT {json_object()};
      json_t* jFrom {json_array()};
      json_t* jTo {json_array()};

      json_object_set_new(jT, "address", json_integer(address));
      json_object_set_new(jT, "from", jFrom);
      json_object_set_new(jT, "to", jTo);

      for (int n = 0; n < 4; n++)
      {
         json_array_append_new(jFrom, json_integer(t.timesFrom[n]));
         json_array_append_new(jTo, json_integer(t.timesTo[n]));
      }

      json_array_append_new(jList, jT);
   }

   mkdir(opt.cacheDir.c_str(), 0755);

   std::string file {opt.cacheDir + "/times.json"};
   std::string tmp {file + ".tmp"};

   if (json_dump_file(jRoot, tmp.c_str(), JSON_COMPACT) != 0 || rename(tmp.c_str(), file.c_str()) != 0)
      tell(eloAlways, "Warning: Can't write the cache '%s', %s", file.c_str(), strerror(errno));

   json_decref(jRoot);

   return done;
}

int P4Mqttd::loadTimes()
{
   std::string file {opt.cacheDir + "/times.json"};
   json_t* jRoot {json_load_file(file.c_str(), 0, nullptr)};

   if (!jRoot)
      return fail;

   json_t* jList {getObjectFromJson(jRoot, "times")};

   if (std::string(getStringFromJson(jRoot, "firmware", "")) != cacheFirmware || !json_is_array(jList) || !json_array_size(jList))
   {
      json_decref(jRoot);
      return fail;
   }

   std::map<int,Fs::TimeRanges> list;
   size_t index {};
   json_t* jT {};

   json_array_foreach(jList, index, jT)
   {
      Fs::TimeRanges t(getIntFromJson(jT, "address", 0xff));
      json_t* jFrom {getObjectFromJson(jT, "from")};
      json_t* jTo {getObjectFromJson(jT, "to")};

      for (int n = 0; n < 4; n++)
      {
         t.timesFrom[n] = json_integer_value(json_array_get(jFrom, n));
         t.timesTo[n] = json_integer_value(json_array_get(jTo, n));
      }

      list[t.address] = t;
   }

   json_decref(jRoot);
   times = list;
   timesLoaded = true;
   tell(eloAlways, "Loaded %zu time programs from '%s'", times.size(), file.c_str());

   return success;
}

int P4Mqttd::ensureTimes()
{
   if (timesLoaded)
      return success;

   if (loadTimes() == success)
      return success;

   if (readTimes() != success)
      return fail;

   saveTimes();

   return success;
}

int P4Mqttd::cmdInitTimes(json_t* jReply)
{
   if (readTimes() != success)
      return reply(jReply, "error", "Lesen der Zeitprogramme fehlgeschlagen");

   saveTimes();
   json_object_set_new(jReply, "times", json_integer(times.size()));

   return reply(jReply, "ok");
}

//***************************************************************************
// Add Time Group to the items of a menu level
//   the 'Zeiten' of the boiler, the circuits, ...: for each weekday a head and its four ranges
//***************************************************************************

int P4Mqttd::addTimeGroup(const MenuEntry& m, json_t* jItems)
{
   int base {0};

   switch (m.child)
   {
      case 230: base = 0x00 + m.address * 7; break;   // boiler 'n'
      case 350: base = 0x38 + m.address * 7; break;   // circuit 'n'
      case 430: base = 0xb6 + m.address * 7; break;   // buffer 'n'
      case 573: base = 0xd9 + m.address * 7; break;   // circulation
   }

   if (ensureTimes() != success)
   {
      json_t* jItem {json_object()};

      json_object_set_new(jItem, "id", json_integer(0));
      json_object_set_new(jItem, "parent", json_integer(m.parent));
      json_object_set_new(jItem, "child", json_integer(0));
      json_object_set_new(jItem, "type", json_integer(0));
      json_object_set_new(jItem, "address", json_integer(0));
      json_object_set_new(jItem, "title", json_string("Zeiten"));
      json_object_set_new(jItem, "unit", json_string(""));
      json_object_set_new(jItem, "error", json_true());
      json_array_append_new(jItems, jItem);

      return fail;
   }

   for (int wday = 0; wday < 7; wday++)
   {
      int trAddr {base + wday};
      const auto it {times.find(trAddr)};

      json_t* jHead {json_object()};

      json_object_set_new(jHead, "id", json_integer(0));
      json_object_set_new(jHead, "parent", json_integer(m.parent));
      json_object_set_new(jHead, "child", json_integer(0));
      json_object_set_new(jHead, "type", json_integer(0));
      json_object_set_new(jHead, "address", json_integer(0));
      json_object_set_new(jHead, "title", json_string((std::string("Zeiten '") + toWeekdayName(wday) + "'").c_str()));
      json_object_set_new(jHead, "unit", json_string(""));
      json_array_append_new(jItems, jHead);

      if (it == times.end())
         continue;

      Fs::TimeRanges t {it->second};

      for (int n = 0; n < 4; n++)
      {
         json_t* jItem {json_object()};

         json_object_set_new(jItem, "id", json_integer(0));
         json_object_set_new(jItem, "parent", json_integer(m.parent));
         json_object_set_new(jItem, "child", json_integer(0));
         json_object_set_new(jItem, "type", json_integer(0));
         json_object_set_new(jItem, "address", json_integer(trAddr));
         json_object_set_new(jItem, "range", json_integer(n + 1));
         json_object_set_new(jItem, "title", json_string(("Range " + std::to_string(n + 1)).c_str()));
         json_object_set_new(jItem, "unit", json_string(""));
         json_object_set_new(jItem, "value", json_string(t.getTimeRange(n)));
         json_object_set_new(jItem, "editable", json_boolean(true));
         json_array_append_new(jItems, jItem);
      }
   }

   return success;
}

//***************************************************************************
// Error List
//   the whole list at once under one lock (the control keeps its position in the list)
//***************************************************************************

int P4Mqttd::readErrors(std::vector<ErrorEntry>& list)
{
   Fs::ErrorInfo e;
   BusGuard guard(this);

   for (bool first {true}; !doShutDown(); first = false)
   {
      int status {first ? request->getFirstError(&e) : request->getNextError(&e)};

      if (status == Fs::wrnLast)
         break;

      if (status != success)
      {
         tell(eloAlways, "Error: Reading the error list failed after %zu entries (%d)", list.size(), status);
         free(e.text);
         e.text = nullptr;
         return fail;
      }

      ErrorEntry entry;

      entry.number = e.number;
      entry.info = e.info;
      entry.state = e.state;
      entry.time = e.time;
      entry.text = e.text ? e.text : "";
      list.push_back(entry);

      free(e.text);
      e.text = nullptr;

      if (list.size() > 2000)
      {
         tell(eloAlways, "Error: The error list doesn't end after %zu entries, giving up", list.size());
         return fail;
      }
   }

   return doShutDown() ? fail : success;
}

json_t* P4Mqttd::errors2Json(const std::vector<ErrorEntry>& list)
{
   json_t* jList {json_array()};

   for (const auto& e : list)
   {
      json_t* jE {json_object()};

      json_object_set_new(jE, "number", json_integer(e.number));
      json_object_set_new(jE, "info", json_integer(e.info));
      json_object_set_new(jE, "state", json_integer(e.state));
      json_object_set_new(jE, "time", json_integer(e.time));
      json_object_set_new(jE, "text", json_string(e.text.c_str()));
      json_array_append_new(jList, jE);
   }

   return jList;
}

int P4Mqttd::updateErrors()
{
   std::vector<ErrorEntry> list;
   uint64_t start {cTimeMs::Now()};

   errorsDue = false;
   nextErrorsAt = time(0) + 10 * tmeSecondsPerMinute;

   if (readErrors(list) != success)
   {
      nextErrorsAt = time(0) + tmeSecondsPerMinute;      // retry in a minute
      busFailed("error list");
      return fail;
   }

   busOk();

   json_t* jRoot {json_object()};

   json_object_set_new(jRoot, "errors", errors2Json(list));
   json_object_set_new(jRoot, "read", json_integer(time(0)));
   publish((opt.topic + "/errors").c_str(), jRoot, true);

   tell(eloAlways, "Info: Read %zu entries of the error list in %.1f s", list.size(), (cTimeMs::Now() - start) / 1000.0);

   return success;
}

int P4Mqttd::cmdErrors(json_t* jReply)
{
   std::vector<ErrorEntry> list;

   if (readErrors(list) != success)
      return reply(jReply, "comm", "Lesen der Fehlerliste fehlgeschlagen");

   busOk();
   errorsDue = false;
   nextErrorsAt = time(0) + 10 * tmeSecondsPerMinute;

   json_t* jRoot {json_object()};

   json_object_set_new(jRoot, "errors", errors2Json(list));
   json_object_set_new(jRoot, "read", json_integer(time(0)));
   publish((opt.topic + "/errors").c_str(), jRoot, true);

   json_object_set_new(jReply, "errors", errors2Json(list));

   return reply(jReply, "ok");
}

//***************************************************************************
// Writing - parameters
//***************************************************************************

static bool isWritableType(int type)
{
   return type == Fs::mstPar || type == Fs::mstParSet || type == Fs::mstParSet1 || type == Fs::mstParSet2 ||
      type == Fs::mstParDig || type == Fs::mstParZeit;
}

// the type of the parameter at this address, only parameters which are part of the service menu are writable

int P4Mqttd::writableType(uint address)
{
   for (const auto& m : menu)
      if (m.address == address && !m.child && isWritableType(m.type))
         return m.type;

   return na;
}

// the value of a command may be a string, a number or a boolean

static std::string valueOfJson(json_t* jCmd, const char* name)
{
   json_t* j {json_object_get(jCmd, name)};

   if (json_is_string(j))
      return json_string_value(j);

   if (json_is_integer(j))
      return std::to_string(json_integer_value(j));

   if (json_is_real(j))
   {
      char buf[50];
      snprintf(buf, sizeof(buf), "%.6g", json_real_value(j));
      return buf;
   }

   if (json_is_boolean(j))
      return json_is_true(j) ? "ja" : "nein";

   return "";
}

int P4Mqttd::cmdParGet(json_t* jCmd, json_t* jReply)
{
   uint address {(uint)getIntFromJson(jCmd, "address", -1)};
   int type {writableType(address)};

   if (type == na)
      return reply(jReply, "error", "Adresse nicht im Menü oder kein Parameter");

   Fs::ConfigParameter p(address);

   lockBus();
   int status {request->getParameter(&p)};
   unlockBus();

   if (status != success)
      return reply(jReply, "comm", "Lesen des Parameters fehlgeschlagen");

   json_object_set_new(jReply, "address", json_integer(address));
   json_object_set_new(jReply, "type", json_integer(type));
   json_object_set_new(jReply, "value", json_string(p.toNice(type).string()));
   json_object_set_new(jReply, "unit", json_string(p.unit ? p.unit : ""));
   json_object_set_new(jReply, "digits", json_integer(p.digits));
   json_object_set_new(jReply, "min", json_real(p.rMin));
   json_object_set_new(jReply, "max", json_real(p.rMax));
   json_object_set_new(jReply, "default", json_real(p.rDefault));

   return reply(jReply, "ok");
}

int P4Mqttd::cmdParSet(json_t* jCmd, json_t* jReply)
{
   uint address {(uint)getIntFromJson(jCmd, "address", -1)};
   std::string value {valueOfJson(jCmd, "value")};
   std::string user {getStringFromJson(jCmd, "user", "mqtt")};

   if (opt.readonly)
   {
      tell(eloAlways, "Refused: parset 0x%x '%s' by '%s', read only mode", address, value.c_str(), user.c_str());
      return reply(jReply, "readonly", "p4mqttd läuft im Nur-Lesen-Modus");
   }

   int type {writableType(address)};

   if (type == na)
   {
      tell(eloAlways, "Refused: parset 0x%x by '%s', not a parameter of the service menu", address, user.c_str());
      return reply(jReply, "error", "Adresse nicht im Menü oder kein änderbarer Parameter");
   }

   if (value.empty())
      return reply(jReply, "format", "Wert fehlt");

   if (type == Fs::mstParDig)
   {
      if (value == "1" || value == "true" || value == "on")
         value = "ja";
      else if (value == "0" || value == "false" || value == "off")
         value = "nein";
   }

   Fs::ConfigParameter p(address);

   lockBus();
   int status {request->getParameter(&p)};
   unlockBus();

   if (status != success)
      return reply(jReply, "comm", "Lesen des aktuellen Werts fehlgeschlagen");

   std::string old {p.toNice(type).string()};

   if (p.setValue(type, value.c_str()) != success)
   {
      tell(eloAlways, "Refused: parset 0x%x '%s' by '%s', wrong format", address, value.c_str(), user.c_str());
      return reply(jReply, "format", "Wert hat das falsche Format");
   }

   tell(eloAlways, "WRITE parameter 0x%x: '%s' -> '%s' by '%s'", address, old.c_str(), value.c_str(), user.c_str());

   lockBus();
   status = request->setParameter(&p);
   unlockBus();

   json_object_set_new(jReply, "address", json_integer(address));
   json_object_set_new(jReply, "old", json_string(old.c_str()));

   if (status == success)
   {
      busOk();
      std::string now {p.toNice(type).string()};
      tell(eloAlways, "Parameter 0x%x stored, value now '%s'", address, now.c_str());
      json_object_set_new(jReply, "value", json_string(now.c_str()));
      json_object_set_new(jReply, "unit", json_string(p.unit ? p.unit : ""));
      return reply(jReply, "ok");
   }

   json_object_set_new(jReply, "value", json_string(old.c_str()));

   if (status == Fs::wrnNonUpdate)
      return reply(jReply, "nochange", "Wert unverändert");

   if (status == Fs::wrnOutOfRange)
   {
      json_object_set_new(jReply, "min", json_real(p.rMin));
      json_object_set_new(jReply, "max", json_real(p.rMax));
      return reply(jReply, "range", "Wert außerhalb des zulässigen Bereichs");
   }

   tell(eloAlways, "Error: Storing parameter 0x%x failed (%d)", address, status);

   return reply(jReply, "comm", "Schreiben fehlgeschlagen (Kommunikationsfehler)");
}

//***************************************************************************
// Writing - a range of a time program
//***************************************************************************

int P4Mqttd::cmdTimeSet(json_t* jCmd, json_t* jReply)
{
   int address {getIntFromJson(jCmd, "address", -1)};
   int range {getIntFromJson(jCmd, "range", 0)};
   std::string value {valueOfJson(jCmd, "value")};
   std::string user {getStringFromJson(jCmd, "user", "mqtt")};

   if (opt.readonly)
   {
      tell(eloAlways, "Refused: timeset 0x%x/%d '%s' by '%s', read only mode", address, range, value.c_str(), user.c_str());
      return reply(jReply, "readonly", "p4mqttd läuft im Nur-Lesen-Modus");
   }

   if (range < 1 || range > 4)
      return reply(jReply, "range", "Zeitbereich muss 1 bis 4 sein");

   if (ensureTimes() != success)
      return reply(jReply, "comm", "Zeitprogramme konnten nicht gelesen werden");

   const auto it {times.find(address)};

   if (it == times.end())
   {
      tell(eloAlways, "Refused: timeset 0x%x by '%s', unknown time program", address, user.c_str());
      return reply(jReply, "error", "Unbekanntes Zeitprogramm");
   }

   if (value.empty())
      value = "nn:nn - nn:nn";

   char from[100] {};
   char to[100] {};

   if (sscanf(value.c_str(), "%99[^-]-%99[^-]", from, to) != 2)
      return reply(jReply, "format", "Wert hat das falsche Format (von - bis)");

   allTrim(from);
   allTrim(to);

   std::string sFrom {from}, sTo {to};

   Fs::TimeRanges t {it->second};
   std::string old {t.getTimeRange(range - 1)};

   if (t.setTimeRange(range - 1, sFrom.c_str(), sTo.c_str()) != success)
   {
      tell(eloAlways, "Refused: timeset 0x%x/%d '%s' by '%s', wrong format", address, range, value.c_str(), user.c_str());
      return reply(jReply, "format", "Wert hat das falsche Format (hh:mm - hh:mm)");
   }

   tell(eloAlways, "WRITE time program 0x%x range %d: '%s' -> '%s' by '%s'", address, range, old.c_str(), t.getTimeRange(range - 1), user.c_str());

   lockBus();
   int status {request->setTimeRanges(&t)};
   unlockBus();

   json_object_set_new(jReply, "address", json_integer(address));
   json_object_set_new(jReply, "range", json_integer(range));
   json_object_set_new(jReply, "old", json_string(old.c_str()));

   if (status == Fs::wrnOutOfRange)
      return reply(jReply, "range", "Ungültige Zeit");

   if (status != success)
   {
      tell(eloAlways, "Error: Storing time program 0x%x failed (%d)", address, status);
      return reply(jReply, "comm", "Schreiben fehlgeschlagen (Kommunikationsfehler)");
   }

   times[address] = t;        // the answer of the control
   saveTimes();

   json_object_set_new(jReply, "value", json_string(t.getTimeRange(range - 1)));

   return reply(jReply, "ok");
}

//***************************************************************************
// Poll One Sensor and publish the result
//***************************************************************************

int P4Mqttd::pollOne(size_t index)
{
   const Sensor& s {sensors[index]};
   int status {fail};
   json_t* jObj {json_object()};
   char cmode[2] {};

   json_object_set_new(jObj, "type", json_string(s.type.c_str()));
   json_object_set_new(jObj, "address", json_integer(s.address));
   json_object_set_new(jObj, "title", json_string(s.title.c_str()));
   json_object_set_new(jObj, "unit", json_string(s.unit.c_str()));
   json_object_set_new(jObj, "maxage", json_integer(std::max(3 * opt.pollInterval, 180)));

   if (json_t* jParam {widgetDefaults(s.type, s.unit, s.title)})
      json_object_set_new(jObj, "parameter", jParam);

   if (s.type == "P4VA")
   {
      Fs::Value v(s.address);

      lockBus();
      status = request->getValue(&v);
      unlockBus();

      if (status == success)
      {
         int raw {s.dataType == 1 ? (int)(word)v.value : (int)(sword)v.value};
         json_object_set_new(jObj, "value", json_real(raw / (double)s.factor));
      }
   }
   else
   {
      Fs::IoValue v(s.address);

      lockBus();

      if (s.type == "P4DO")
         status = request->getDigitalOut(&v);
      else if (s.type == "P4DI")
         status = request->getDigitalIn(&v);
      else
         status = request->getAnalogOut(&v);

      unlockBus();

      if (status == success)
      {
         // mode: 'A' automatic, 'M' manual; analog outputs: 0xff -> automatic

         cmode[0] = s.type == "P4AO" ? (v.mode == 0xff ? 'A' : 'M') : (isprint(v.mode) ? v.mode : 0);
         json_object_set_new(jObj, "text", json_string(cmode));

         if (s.type == "P4AO")
         {
            json_object_set_new(jObj, "value", json_integer(v.state));
         }
         else
         {
            json_object_set_new(jObj, "kind", json_string("status"));
            json_object_set_new(jObj, "state", json_integer(v.state ? 1 : 0));
         }
      }
   }

   if (status != success)
   {
      tell(eloAlways, "Error: Reading %s 0x%04x '%s' failed (%d)", s.type.c_str(), s.address, s.title.c_str(), status);
      json_decref(jObj);
      busFailed(s.title.c_str());
      return publishSensorInvalid(s);
   }

   busOk();
   lastOk = time(0);

   return publish(dataTopic.c_str(), jObj, false);
}

int P4Mqttd::publishSensorInvalid(const Sensor& s)
{
   json_t* jObj {json_object()};

   json_object_set_new(jObj, "type", json_string(s.type.c_str()));
   json_object_set_new(jObj, "address", json_integer(s.address));
   json_object_set_new(jObj, "title", json_string(s.title.c_str()));
   json_object_set_new(jObj, "unit", json_string(s.unit.c_str()));
   json_object_set_new(jObj, "valid", json_boolean(false));

   return publish(dataTopic.c_str(), jObj, false);
}

//***************************************************************************
// MQTT Connection
//***************************************************************************

int P4Mqttd::mqttConnection()
{
   if (!mqttWriter)
      mqttWriter = new Mqtt();

   if (mqttWriter->isConnected() && (!mqttReader || mqttReader->isConnected()))
      return success;

   if (mqttWriter->isConnected())
   {
      // only the subscriber is gone

      mqttReader->disconnect();
      delete mqttReader;
      mqttReader = nullptr;
      mqttWriter->disconnect();
   }

   if (time(0) < mqttRetryAt)
      return fail;

   mqttRetryAt = time(0) + 5;

   // the broker publishes this if we are gone without saying goodbye

   std::string will {statusJson(false)};
   mqttWriter->setWill((opt.topic + "/status").c_str(), will.c_str(), true);

   if (mqttWriter->connect(opt.mqttUrl.c_str()) != success)
   {
      tell(eloAlways, "Error: MQTT: Connecting publisher to '%s' failed", opt.mqttUrl.c_str());
      return fail;
   }

   tell(eloAlways, "MQTT: Connecting publisher to '%s' succeeded", opt.mqttUrl.c_str());

   publishStatus(true);

   // the configuration of the homectld (retained, we get it right after the subscription)

   if (!mqttReader)
      mqttReader = new Mqtt();

   if (!mqttReader->isConnected())
   {
      if (mqttReader->connect(opt.mqttUrl.c_str()) != success)
         tell(eloAlways, "Error: MQTT: Connecting subscriber to '%s' failed", opt.mqttUrl.c_str());
      else
      {
         for (const char* sub : { "/config", "/cmd" })
            if (mqttReader->subscribe((opt.topic + sub).c_str()) == success)
               tell(eloAlways, "MQTT: Topic '%s%s' subscribed", opt.topic.c_str(), sub);
      }
   }

   if (haveState)
   {
      publishState();
      publishUserDefined();
   }

   return success;
}

int P4Mqttd::mqttDisconnect()
{
   if (mqttReader)
      mqttReader->disconnect();

   delete mqttReader;
   mqttReader = nullptr;

   if (mqttWriter)
      mqttWriter->disconnect();

   delete mqttWriter;
   mqttWriter = nullptr;

   return done;
}

//***************************************************************************
// Publish, takes the reference of 'obj'
//***************************************************************************

int P4Mqttd::publish(const char* t, json_t* obj, bool retained)
{
   int status {fail};

   if (mqttWriter && mqttWriter->isConnected())
   {
      char* message {json_dumps(obj, JSON_REAL_PRECISION(8) | JSON_COMPACT)};

      status = retained ? mqttWriter->writeRetained(t, message) : mqttWriter->write(t, message);
      tell(eloDebug, "Debug: -> '%s' [%s]", t, message);
      free(message);
   }

   json_decref(obj);

   return status;
}

std::string P4Mqttd::statusJson(bool online)
{
   json_t* obj {json_object()};

   json_object_set_new(obj, "online", json_boolean(online));
   json_object_set_new(obj, "comm", json_string(commOk ? "ok" : (serial && serial->isOpen() ? "lost" : "closed")));
   json_object_set_new(obj, "tty", json_string(opt.tty.c_str()));
   json_object_set_new(obj, "lastOk", json_integer(lastOk));
   json_object_set_new(obj, "firmware", json_string(haveState ? currentState.version : ""));
   json_object_set_new(obj, "version", json_string("p4mqttd " P4MQTTD_VERSION));
   json_object_set_new(obj, "readonly", json_boolean(opt.readonly));
   json_object_set_new(obj, "parallel", json_boolean(opt.parallel));

   char* message {json_dumps(obj, JSON_COMPACT)};
   std::string s {message};
   free(message);
   json_decref(obj);

   return s;
}

int P4Mqttd::publishStatus(bool online)
{
   if (!mqttWriter || !mqttWriter->isConnected())
      return fail;

   std::string t {opt.topic + "/status"};
   std::string message {statusJson(online)};

   tell(eloDebug, "Debug: -> '%s' [%s]", t.c_str(), message.c_str());

   return mqttWriter->writeRetained(t.c_str(), message.c_str());
}

int P4Mqttd::publishState()
{
   json_t* obj {json_object()};

   json_object_set_new(obj, "state", json_integer(currentState.state));
   json_object_set_new(obj, "stateinfo", json_string(currentState.stateinfo));
   json_object_set_new(obj, "mode", json_integer(currentState.mode));
   json_object_set_new(obj, "modeinfo", json_string(currentState.modeinfo));
   json_object_set_new(obj, "time", json_integer(currentState.time));
   json_object_set_new(obj, "error", json_boolean(Fs::isError(currentState.state)));

   return publish((opt.topic + "/state").c_str(), obj, true);
}

//***************************************************************************
// Publish the 'user defined' sensors P4UD: state, mode and time of the control
//***************************************************************************

int P4Mqttd::publishUserDefined()
{
   struct Ud
   {
      uint address;
      const char* title;
      const char* unit;
      long value;
      std::string text;
   };

   std::string timeText {l2pTime(currentState.time, "%A, %d. %b. %Y %H:%M:%S")};

   const Ud uds[] =
   {
      { Fs::udState, "Heizungsstatus",     "zst", (long)currentState.state, currentState.stateinfo },
      { Fs::udMode,  "Betriebsart Kessel", "txt", (long)currentState.mode,  currentState.modeinfo },
      { Fs::udTime,  "Uhrzeit",            "txt", (long)currentState.time,  timeText }
   };

   for (const auto& u : uds)
   {
      json_t* obj {json_object()};

      json_object_set_new(obj, "type", json_string("P4UD"));
      json_object_set_new(obj, "address", json_integer(u.address));
      json_object_set_new(obj, "title", json_string(u.title));
      json_object_set_new(obj, "unit", json_string(u.unit));
      json_object_set_new(obj, "kind", json_string("text"));
      json_object_set_new(obj, "value", json_integer(u.value));
      json_object_set_new(obj, "text", json_string(u.text.c_str()));
      json_object_set_new(obj, "maxage", json_integer(std::max(3 * opt.stateInterval, 60)));

      if (json_t* jParam {widgetDefaults("P4UD", u.unit, u.title)})
         json_object_set_new(obj, "parameter", jParam);

      if (u.address == Fs::udState)
      {
         // like the p4d: the animated image of the state, the picture of the heating type for the others

         std::string image {stateImage(currentState.state, iconSet)};

         if (image.empty() && !heatingType.empty())
            image = "img/p4/type/heating-" + heatingType + ".png";

         if (!image.empty())
            json_object_set_new(obj, "image", json_string(image.c_str()));
      }

      publish(dataTopic.c_str(), obj, false);
   }

   return done;
}

//***************************************************************************
// Usage
//***************************************************************************

void showUsage(const char* bin)
{
   printf("Usage: %s [options]\n", bin);
   printf("\n");
   printf("  options:\n");
   printf("     -d <device>     serial device of the S 3200 (default /dev/ttyUSB0), use the fixed name in /dev/serial/by-id/\n");
   printf("     -u <url>        MQTT url (default tcp://localhost:1883)\n");
   printf("     -T <topic>      MQTT base topic (default p4)\n");
   printf("     -D <topic>      topic of the sensor data (default <base topic>/sensor)\n");
   printf("     -C <directory>  cache directory (default /var/cache/p4mqttd)\n");
   printf("     -i <seconds>    interval of the state check (default 10)\n");
   printf("     -I <seconds>    interval of the polling of the values (default 60)\n");
   printf("     -r              read only, refuse all writing requests\n");
   printf("     -P              parallel mode beside the p4d (implies -r, interval at least 60)\n");
   printf("     -l <level>      eloquence (log level)\n");
   printf("     -t              log to stdout\n");
   printf("     -n              don't fork\n");
   printf("     -v              show version\n");
   printf("\n");
   printf("  SIGUSR1 reads the sensor definitions from the control again.\n");
   printf("\n");
   printf("  USE AT YOUR OWN RISK. No warranty. See README.md.\n");
}

//***************************************************************************
// Main
//***************************************************************************

int main(int argc, char** argv)
{
   bool nofork {false};
   int _stdout {na};
   P4Mqttd::Options opt;

   for (int i = 1; argv[i]; i++)
   {
      if (argv[i][0] == '?' || strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0)
      {
         showUsage(argv[0]);
         return 0;
      }

      if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0)
      {
         printf("p4mqttd %s\n", P4MQTTD_VERSION);
         return 0;
      }

      if (argv[i][0] != '-' || strlen(argv[i]) != 2)
         continue;

      switch (argv[i][1])
      {
         case 'l': if (argv[i+1]) eloquence = (Eloquence)strtol(argv[++i], nullptr, 0); break;
         case 'd': if (argv[i+1]) opt.tty = argv[++i];                     break;
         case 'u': if (argv[i+1]) opt.mqttUrl = argv[++i];                 break;
         case 'T': if (argv[i+1]) opt.topic = argv[++i];                   break;
         case 'D': if (argv[i+1]) opt.dataTopic = argv[++i];               break;
         case 'C': if (argv[i+1]) opt.cacheDir = argv[++i];                break;
         case 'i': if (argv[i+1]) opt.stateInterval = std::max(1, atoi(argv[++i])); break;
         case 'I': if (argv[i+1]) opt.pollInterval = std::max(5, atoi(argv[++i]));  break;
         case 'r': opt.readonly = true;                                    break;
         case 'P': opt.parallel = true;                                    break;
         case 't': _stdout = yes;                                          break;
         case 'n': nofork = true;                                          break;
      }
   }

   logstdout = _stdout != na ? _stdout : false;

   P4Mqttd* job = new P4Mqttd(opt);

   if (job->init() != success)
   {
      printf("Initialization failed, see syslog for details\n");
      delete job;
      return 1;
   }

   if (!nofork)
   {
      int pid {fork()};

      if (pid < 0)
      {
         printf("Can't fork daemon, %s\n", strerror(errno));
         return 1;
      }

      if (pid != 0)
         return 0;
   }

   ::signal(SIGINT, P4Mqttd::downF);
   ::signal(SIGTERM, P4Mqttd::downF);
   ::signal(SIGUSR1, P4Mqttd::rediscoverF);

   job->loop();

   tell(eloAlways, "shutdown");
   delete job;

   return 0;
}
