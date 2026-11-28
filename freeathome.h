//***************************************************************************
// Automation Control
// File freeathome.h
// This code is distributed under the terms and conditions of the
// GNU GENERAL PUBLIC LICENSE. See the file LICENSE for details.
// Date 2026 Jörg Wendel
//***************************************************************************

#pragma once

#include <atomic>
#include <queue>
#include <map>
#include <vector>

#include <libwebsockets.h>
#include <jansson.h>

#include "lib/common.h"
#include "lib/db.h"
#include "actors.h"

class HomeCtl;

//***************************************************************************
// Class FreeAtHome
//   Busch-Jaeger / ABB free@home by the 'free@home Local API' of the SysAP 2.0
//   (REST + websocket, HTTP basic auth, see freeathome.c)
//***************************************************************************

class FreeAtHome : public LightBackend, public BlindBackend
{
   public:

      // a change of a channel, applied to the sensor by HomeCtl::dispatchFreeAtHome()

      struct Update
      {
         std::string type;
         uint address {0};
         int state {na};             // light on / off, motion, rain alarm
         int bri {na};               // light [%]
         int ct {na};                // light, color temperature [mired]
         int level {na};             // blind position [%] (100 = open)
         int moving {na};            // blind: 0 stopped, 1 opening, 2 closing
         int btnevent {na};          // wall switch: button event like deCONZ (1002 on, 2002 off, 1001 / 1003 dim up ...)
         double value {0.0};         // measuring sensors
         bool hasValue {false};
         int ctMin {na};             // init: range of the color temperature [mired]
         int ctMax {na};
         bool init {false};          // from the configuration / repeated after a connect: no event for the light rules
      };

      FreeAtHome();
      ~FreeAtHome() override;

      int init(HomeCtl* parent);
      int exit();
      int initDb(cDbConnection* connection);
      int exitDb();

      void setAccess(const char* host, const char* user, const char* password);   // host: 'host[:port]', 'http://' for no TLS
      bool configured() const { return !host.empty() && !user.empty(); }
      const char* getHost() const { return host.c_str(); }

      int initWsClient();
      bool wsRunning() const { return syncThread != 0; }
      static bool wsConnected() { return connected; }

      int readConfiguration(std::vector<Update>& updates);   // all channels (REST), creates the sensors
      int processMessage(const char* msg, std::vector<Update>& updates, bool& reread);   // a message of the websocket

      // LightBackend / BlindBackend

      int toggle(const char* type, uint address, bool state, int bri = na, int transitiontime = na, bool explicitState = false) override;
      int dim(const char* type, uint address, int inc, int transitiontime, bool switchOn = false) override;
      int color(const char* type, uint address, int hue, int saturation, int bri) override;
      int colorTemperature(const char* type, uint address, int ct, int bri) override;
      int blindMove(const char* type, uint address, int level, bool noStop) override;

      static cMyMutex messagesInMutex;
      static std::queue<std::string> messagesIn;

      static constexpr int ctMinDefault {153};    // [mired] 6500 K - the actors know 0..100 % only (0 = warm)
      static constexpr int ctMaxDefault {454};    // [mired] 2200 K

   private:

      struct Channel
      {
         std::string type;               // FHL, FHB, FHS
         std::string serial;             // device, e.g. 'ABB7F500E17A'
         std::string channel;            // e.g. 'ch0003'
         uint functionId {0};
         std::map<int,std::string> inputs;    // pairing id -> 'idp0000'
         std::map<std::string,int> outputs;   // 'odp0000' -> pairing id
         bool moving {false};                 // blind is moving (AL_INFO_MOVE_UP_DOWN)
      };

      enum Kind { kNone, kLight, kBlind, kSensor };

      static Kind kindOf(uint functionId);
      static const char* unitOf(uint functionId);
      const Channel* channelOf(const char* type, uint address) const;
      int setDatapoint(const Channel* ch, int pairing, const char* value);
      int processDevice(const char* serial, json_t* jDevice, std::vector<Update>& updates);
      static void datapoint2Update(Channel& ch, uint address, int pairing, const char* value, std::vector<Update>& updates);
      long addressOf(const char* uuid, const char* type, const char* name);

      int request(const char* method, const char* path, const char* body, std::string& response);
      int exitWsClient();
      static int atInMessage(const char* data, size_t size);
      static void* syncFct(void* user);
      static void connectClient(lws_sorted_usec_list_t* sul);
      static int callbackWs(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len);

      HomeCtl* daemon {};
      std::string host;                  // 'host[:port]'
      std::string user;
      std::string password;
      bool tls {true};
      std::string sysapUuid;

      std::map<uint,Channel> channels;               // address -> channel
      std::map<std::string,uint> addresses;          // 'SERIAL/ch0003' -> address

      cDbTable* tableChannels {};
      cDbStatement* selectChannelByUuid {};

      lws_protocols protocols[2];
      pthread_t syncThread {0};
      std::atomic<bool> threadClose {false};
      std::atomic<bool> threadActive {false};
      std::string rxBuffer;              // fragments of a websocket message

      static FreeAtHome* singleton;
      static lws_context* context;
      static lws* clientWsi;
      static lws_sorted_usec_list_t sul;
      static std::string wsHost;
      static int wsPort;
      static std::string wsAuth;         // 'Basic <base64>'
      static bool wsTls;
      static std::atomic<bool> connected;
};
