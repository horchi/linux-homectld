//***************************************************************************
// Automation Control
// File deconz.h
// This code is distributed under the terms and conditions of the
// GNU GENERAL PUBLIC LICENSE. See the file LICENSE for details.
// Date 04.11.2010 - 15.12.2021  Jörg Wendel
//***************************************************************************

#pragma once

#include <atomic>

#include "lib/common.h"

class HomeCtl;

//***************************************************************************
// Class Deconz
//***************************************************************************

class Deconz
{
   public:

      Deconz();
      ~Deconz();

      int init(HomeCtl* parent, cDbConnection* connection);
      int exit();

      void setApiKey(const char* key)  { apiKey = key; }
      void setHttpUrl(const char* url);   // 'host:port' of the REST API, an 'http://' in front is skipped

      const char* getHttpUrl() { return httpUrl.c_str(); }
      const char* getApiKey() { return apiKey.c_str(); }

      int query(json_t*& jResult, const char* method, const char* apiKey);
      int put(const std::string& type, json_t*& jResult, const char* uuid, json_t* jData);
      int post(json_t*& jResult, const char* method, const char* payload);
      int checkResult(json_t* jArray);
      int rest(const char* method, const char* path, json_t* jData, json_t*& jResult);   // GET|PUT|POST|DELETE /api/<key>/<path>, jData taken
      std::string resultError(json_t* jArray);   // the description of the first error of a REST result, empty if none

      int queryApiKey(std::string& result);
      int initDevices();
      int processDevices(json_t* jData, std::string kind);

      std::string macOf(const char* type, uint address);   // MAC of a light / sensor (as in the deCONZ log)
      long addressOf(const char* kind, const char* uuid);  // address of a 'light' / 'sensor' by its uniqueid, na if unknown
      void forget(const char* kind, const char* uuid);     // a deleted light / sensor (table and map)
      time_t knownSince(const char* kind, const char* uuid);   // first seen by homectld (insert into deconzl / deconzs), 0 if unknown
      bool wsRunning() const { return syncThread != 0; }
      static bool wsConnected() { return connected; }

      int toggle(const char* type, uint address, bool state, int bri = na, int transitiontime = na, bool explicitState = false);
      int color(const char* type, uint address, int hue, int saturation, int bri);
      int colorTemperature(const char* type, uint address, int ct, int bri);   // white mode, ct [mired]
      int dim(const char* type, uint address, int inc, int transitiontime, bool switchOn = false);   // bri_inc -254..254, 0 stops

      static cMyMutex messagesInMutex;
      static std::queue<std::string> messagesIn;

      int initWsClient();

   private:

      int exitWsClient();
      int service();
      static int callbackDeconzWs(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len);

      //

      std::string httpUrl;
      std::string apiKey;
      HomeCtl* daemon {};

      std::map<std::string,uint> lights;
      std::map<std::string,uint> sensors;
      static cMyMutex mapsMutex;

      cDbTable* tableDeconzLights {};
      cDbTable* tableDeconzSensors {};
      cDbStatement* selectLightByUuid {};
      cDbStatement* selectSensorByUuid {};

      lws_protocols protocols[2];

      // sync thread stuff

      struct ThreadControl
      {
         bool active {false};
         bool close {false};
         Deconz* deconz {};
         int timeout {60};
      };

      pthread_t syncThread {0};
      ThreadControl threadCtl;

      static void* syncFct(void* user);
      int atInMessage(const char* data, size_t size = 0);

   public:
      static Deconz* singleton;
      static struct lws_context* context;
      static struct lws* client_wsi;
      static std::string wsHost;          // websocket of deCONZ: host of the REST API (deconzHttpUrl)
      static int wsPort;                  //   and the port deCONZ reports in its config ('websocketport')
      static std::atomic<bool> connected; // websocket of deCONZ connected
};
