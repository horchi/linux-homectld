/*
 *  zigbee.c
 *
 *  (c) 2026 Jörg Wendel
 *
 * This code is distributed under the terms and conditions of the
 * GNU GENERAL PUBLIC LICENSE. See the file COPYING for details.
 *
 *  Zigbee devices of deCONZ - the WEBIF setup page 'Zigbee' (admin only): pairing (search / permit
 *  join), renaming and removing of the devices, activating their sensors, the API key of deCONZ.
 *
 *  A device (one MAC) has one or more resources of deCONZ (lights / sensors, e.g. temperature,
 *  humidity and pressure of an Aqara weather sensor), each one is a sensor DZL resp. DZS of homectld.
 *
 *  New devices come by the websocket event 'added' of deCONZ (also paired by Phoscon), while a search
 *  runs 'lights/new' and 'sensors/new' are checked too (an event may get lost). The sensors of a
 *  device paired by a search of this page are activated at once.
 *
 *  WEBIF event 'zigbee', object { "action": ..., ... }:
 *    list                                       the data below to the asking client
 *    search                                     starts the search of deCONZ (180 s)
 *    stop                                       ends it (permitjoin 0)
 *    rename    { resource, id, name }           name of a resource at deCONZ (as Phoscon shows it)
 *    activate  { type, address, state, usrtitle }   sensor of homectld on / off, own title
 *    delete    { mac, reset }                   removes all resources of the device at deCONZ (reset:
 *                                               lights leave the network) and its sensors in homectld
 *    apikey                                     registers homectld at deCONZ (gateway unlocked)
 *  reply / push (event 'zigbee', to all admins on the page):
 *    { "gateway": { "url", "apikey", "connected", "name", "swversion", "fwversion", "channel", "panid", "error" },
 *      "search": { "active", "remaining" },
 *      "devices": [ { "mac", "name", "manufacturer", "model", "swversion", "reachable", "battery",
 *                     "lastseen", "inserted", "new", "resources": [ { "resource", "id", "uniqueid", "name", "dztype",
 *                     "type", "address", "state", "usrtitle", "reachable" } ] } ] }
 */

#include "daemon.h"

//***************************************************************************
// Helper
//***************************************************************************

static std::string macOfUuid(const char* uuid)
{
   // '00:15:8d:00:02:13:87:2c-01-0012' -> '00:15:8d:00:02:13:87:2c'

   std::string mac {uuid ? uuid : ""};
   return mac.substr(0, mac.find('-'));
}

static bool isInternalType(const std::string& dzType)
{
   // resources of deCONZ / Phoscon itself, no devices (see Deconz::processDevices())

   return dzType == "Daylight" || dzType == "CLIPDaylightOffset" || dzType == "Configuration tool" ||
      dzType.starts_with("CLIP");
}

//***************************************************************************
// Perform Zigbee
//***************************************************************************

int HomeCtl::performZigbee(json_t* obj, long client)
{
   std::string action {getStringFromJson(obj, "action", "list")};

   if (action == "list")
      return zigbeePush(client);

   if (action == "apikey")
   {
      std::string message;
      int status {zigbeeApiKey(message)};
      replyResult(status, client, "%s", message.c_str());
      return zigbeePush();
   }

   if (isEmpty(deconz.getHttpUrl()) || isEmpty(deconz.getApiKey()))
      return replyResult(fail, client, "deCONZ ist nicht eingerichtet (URL / API-Key)");

   if (action == "search")
   {
      std::string message;
      int status {zigbeeStartSearch(message)};
      replyResult(status, client, "%s", message.c_str());
   }
   else if (action == "stop")
   {
      json_t* jResult {};
      deconz.rest("PUT", "config", json_pack("{s:i}", "permitjoin", 0), jResult);
      json_decref(jResult);
      zigbeeSearchUntil = 0;

      for (auto& found : zigbeeFound)            // 'new' for zigbeeNewFor from the end of the search
         found.second = time(0);

      tell(eloAlways, "DECONZ: Search for new devices stopped");
   }
   else if (action == "switch")     // a light by deCONZ directly, also if not active in homectld
   {
      std::string id {getStringFromJson(obj, "id", "")};
      bool on {getBoolFromJson(obj, "on")};

      if (id.empty() || id.find_first_not_of("0123456789") != std::string::npos)
         return replyResult(fail, client, "Ungültige Licht-Id '%s'", id.c_str());

      json_t* jResult {};
      std::string path {"lights/" + id + "/state"};
      int status {deconz.rest("PUT", path.c_str(), json_pack("{s:b}", "on", on), jResult)};
      std::string error {status == success ? deconz.resultError(jResult) : "deCONZ nicht erreichbar"};
      json_decref(jResult);

      if (!error.empty())
         return replyResult(fail, client, "%s", error.c_str());

      tell(eloAlways, "DECONZ: Light 'lights/%s' switched %s on the zigbee page", id.c_str(), on ? "on" : "off");

      return done;     // no push, the state of deCONZ may follow later - the page shows the new state already
   }
   else if (action == "rename")
   {
      std::string error;

      if (zigbeeRename(getStringFromJson(obj, "resource", ""), getStringFromJson(obj, "id", ""),
                       getStringFromJson(obj, "name", ""), error) != success)
         return replyResult(fail, client, "%s", error.c_str());

      replyResult(success, client, "Name gespeichert");
   }
   else if (action == "activate")
   {
      const char* type {getStringFromJson(obj, "type", "")};
      int address {getIntFromJson(obj, "address", na)};

      if (zigbeeActivate(type, address, getBoolFromJson(obj, "state"), getStringFromJson(obj, "usrtitle")) != success)
         return replyResult(fail, client, "Sensor %s:0x%02x nicht gefunden", type, address);

      json_t* oJson {json_object()};
      valueFacts2Json(oJson, false);
      pushOutMessage(oJson, "valuefacts");

      replyResult(success, client, "Sensor %s:0x%02x gespeichert", type, address);
   }
   else if (action == "delete")
   {
      std::string error;

      if (zigbeeDelete(getStringFromJson(obj, "mac", ""), getBoolFromJson(obj, "reset"), error) != success)
         replyResult(fail, client, "%s", error.c_str());
      else
         replyResult(success, client, "Gerät entfernt");

      json_t* oJson {json_object()};
      valueFacts2Json(oJson, false);
      pushOutMessage(oJson, "valuefacts");
   }
   else
      return replyResult(fail, client, "Unbekannte Aktion '%s'", action.c_str());

   return zigbeePush();
}

//***************************************************************************
// Zigbee Push
//***************************************************************************

int HomeCtl::zigbeePush(long client)
{
   // the REST calls only if someone looks at the page

   if (!client)
   {
      bool any {false};

      for (const auto& [wsi, cl] : wsClients)
         any = any || (cl.page == "zigbee" && cl.rights & urAdmin);

      if (!any)
         return done;
   }

   json_t* oJson {json_object()};
   zigbee2Json(oJson);

   if (client)
      return pushOutMessage(oJson, "zigbee", client);

   for (const auto& [wsi, cl] : wsClients)
   {
      if (cl.page == "zigbee" && cl.rights & urAdmin)
         pushOutMessage(json_deep_copy(oJson), "zigbee", (long)wsi);
   }

   json_decref(oJson);

   return done;
}

//***************************************************************************
// Zigbee 2 Json
//***************************************************************************

int HomeCtl::zigbee2Json(json_t* obj)
{
   json_t* jGateway {json_object()};
   json_object_set_new(obj, "gateway", jGateway);

   json_object_set_new(jGateway, "url", json_string(deconz.getHttpUrl()));
   json_object_set_new(jGateway, "apikey", json_boolean(!isEmpty(deconz.getApiKey())));
   json_object_set_new(jGateway, "connected", json_boolean(Deconz::wsConnected()));

   time_t now {time(0)};
   json_t* jSearch {json_object()};
   json_object_set_new(jSearch, "active", json_boolean(zigbeeSearchUntil > now));
   json_object_set_new(jSearch, "remaining", json_integer(zigbeeSearchUntil > now ? zigbeeSearchUntil - now : 0));
   json_object_set_new(jSearch, "duration", json_integer(zigbeeSearchDuration));
   json_object_set_new(obj, "search", jSearch);

   json_t* jDevices {json_array()};
   json_object_set_new(obj, "devices", jDevices);

   if (isEmpty(deconz.getHttpUrl()) || isEmpty(deconz.getApiKey()))
      return done;

   json_t* jConfig {};

   if (deconz.rest("GET", "config", nullptr, jConfig) != success)
   {
      json_object_set_new(jGateway, "error", json_string("deCONZ nicht erreichbar"));
      return done;
   }

   if (json_is_object(jConfig))
   {
      json_object_set_new(jGateway, "name", json_string(getStringFromJson(jConfig, "name", "")));
      json_object_set_new(jGateway, "swversion", json_string(getStringFromJson(jConfig, "swversion", "")));
      json_object_set_new(jGateway, "fwversion", json_string(getStringFromJson(jConfig, "fwversion", "")));
      json_object_set_new(jGateway, "channel", json_integer(getIntFromJson(jConfig, "zigbeechannel", 0)));
      json_object_set_new(jGateway, "panid", json_integer(getIntFromJson(jConfig, "panid", 0)));
      json_object_set_new(jGateway, "devicename", json_string(getStringFromJson(jConfig, "devicename", "")));
   }

   json_decref(jConfig);

   // the devices, their resources grouped by the MAC

   std::map<std::string,json_t*> devices;
   std::vector<std::string> order;
   std::set<std::string> withResource;          // MACs with any resource (also the internal ones as the gateway)
   bool resourcesRead {true};

   for (const char* resource : { "lights", "sensors" })
   {
      json_t* jData {};

      if (deconz.rest("GET", resource, nullptr, jData) != success)
      {
         resourcesRead = false;
         continue;
      }

      if (!json_is_object(jData))     // [{"error": {"description": "unauthorized user", ...}}]
      {
         resourcesRead = false;
         std::string error {deconz.resultError(jData)};
         json_object_set_new(jGateway, "error", json_string(error.empty() ? "unerwartete Antwort von deCONZ" : error.c_str()));
         json_decref(jData);
         continue;
      }

      const char* id {};
      json_t* jItem {};
      bool isLight {strcmp(resource, "lights") == 0};

      json_object_foreach(jData, id, jItem)
      {
         const char* uuid {getStringFromJson(jItem, "uniqueid", "")};
         std::string dzType {getStringFromJson(jItem, "type", "")};

         if (!isEmpty(uuid))
            withResource.insert(macOfUuid(uuid));

         if (isEmpty(uuid) || isInternalType(dzType))
            continue;

         std::string mac {macOfUuid(uuid)};
         json_t* jDevice {devices[mac]};
         bool reachable {getBoolByPath(jItem, isLight ? "state/reachable" : "config/reachable", true)};
         int battery {getIntByPath(jItem, "config/battery", na)};

         if (!jDevice)
         {
            jDevice = json_object();
            devices[mac] = jDevice;
            order.push_back(mac);

            json_object_set_new(jDevice, "mac", json_string(mac.c_str()));
            json_object_set_new(jDevice, "name", json_string(getStringFromJson(jItem, "name", "")));
            json_object_set_new(jDevice, "manufacturer", json_string(getStringFromJson(jItem, "manufacturername", "")));
            json_object_set_new(jDevice, "model", json_string(getStringFromJson(jItem, "modelid", "")));
            json_object_set_new(jDevice, "swversion", json_string(getStringFromJson(jItem, "swversion", "")));
            json_object_set_new(jDevice, "reachable", json_boolean(reachable));
            json_object_set_new(jDevice, "new", json_false());
            json_object_set_new(jDevice, "resources", json_array());
         }

         // the device: reachable if one of its resources is, the lowest battery, the last seen

         if (reachable)
            json_object_set_new(jDevice, "reachable", json_true());

         // new: found by the last search (while it runs and zigbeeNewFor after its end) or first seen
         //  by homectld within zigbeeNewFor (also paired in Phoscon, taken by a later read of the devices)

         time_t now {time(0)};
         time_t since {deconz.knownSince(isLight ? "light" : "sensor", uuid)};
         auto itFound {zigbeeFound.find(mac)};

         if (itFound != zigbeeFound.end() && (zigbeeSearchUntil || itFound->second > now - zigbeeNewFor))
         {
            json_object_set_new(jDevice, "new", json_true());

            if (!zigbeeSearchUntil && (!zigbeeNewUntil || itFound->second + zigbeeNewFor < zigbeeNewUntil))
               zigbeeNewUntil = itFound->second + zigbeeNewFor;
         }

         // inserted: the first resource of the device known by homectld (sorting of the WEBIF)

         if (since && (!getIntFromJson(jDevice, "inserted", 0) || since < getIntFromJson(jDevice, "inserted", 0)))
            json_object_set_new(jDevice, "inserted", json_integer(since));

         if (since && since > now - zigbeeNewFor)
         {
            if (!zigbeeNewUntil || since + zigbeeNewFor < zigbeeNewUntil)
               zigbeeNewUntil = since + zigbeeNewFor;

            json_object_set_new(jDevice, "new", json_true());

            if (since > getIntFromJson(jDevice, "since", 0))
               json_object_set_new(jDevice, "since", json_integer(since));
         }

         if (battery != na && (!json_object_get(jDevice, "battery") || battery < getIntFromJson(jDevice, "battery")))
            json_object_set_new(jDevice, "battery", json_integer(battery));

         const char* lastSeen {getStringFromJson(jItem, "lastseen", "")};

         if (!isEmpty(lastSeen) && strcmp(lastSeen, getStringFromJson(jDevice, "lastseen", "")) > 0)
            json_object_set_new(jDevice, "lastseen", json_string(lastSeen));

         // the resource, its sensor in homectld

         json_t* jRes {json_object()};
         json_object_set_new(jRes, "resource", json_string(resource));
         json_object_set_new(jRes, "id", json_string(id));
         json_object_set_new(jRes, "uniqueid", json_string(uuid));
         json_object_set_new(jRes, "name", json_string(getStringFromJson(jItem, "name", "")));
         json_object_set_new(jRes, "dztype", json_string(dzType.c_str()));
         json_object_set_new(jRes, "reachable", json_boolean(reachable));

         if (isLight)                  // for switching a light not active in homectld (no state there)
            json_object_set_new(jRes, "on", json_boolean(getBoolByPath(jItem, "state/on", false)));

         const char* type {isLight ? "DZL" : "DZS"};
         long address {deconz.addressOf(isLight ? "light" : "sensor", uuid)};

         json_object_set_new(jRes, "type", json_string(type));

         if (address != na)
         {
            json_object_set_new(jRes, "address", json_integer(address));

            cDbRow* fact {valueFactRowOf(type, address)};

            if (fact)
            {
               json_object_set_new(jRes, "state", json_string(fact->getStrValue("STATE")));
               json_object_set_new(jRes, "usrtitle", json_string(fact->getStrValue("USRTITLE")));
            }
         }

         json_array_append_new(json_object_get(jDevice, "resources"), jRes);
      }

      json_decref(jData);
   }

   for (const auto& mac : order)
      json_array_append_new(jDevices, devices[mac]);

   // devices paired with deCONZ but without any resource: deCONZ has no DDF (device description)
   //   for them, e.g. a Tuya device with a new manufacturer name ('_TZ3000_...') - homectld can't use them

   std::set<std::string> dzMacs;

   if (resourcesRead && zigbeeDeviceMacs(dzMacs) == success)
   {
      for (const auto& mac : dzMacs)
      {
         if (withResource.count(mac))
            continue;

         json_t* jItem {};
         std::string path {"devices/" + mac};

         if (deconz.rest("GET", path.c_str(), nullptr, jItem) != success || !json_is_object(jItem))
         {
            json_decref(jItem);
            continue;
         }

         // {"manufacturername": "_TZ3000_hbxsdd6k", "modelid": "TS0011", "lastseen": "...", "subdevices": [], ...}
         //  (empty of an incomplete interview, the basic cluster not read)

         const char* manufacturer {getStringFromJson(jItem, "manufacturername", "")};
         const char* model {getStringFromJson(jItem, "modelid", "")};
         auto itFound {zigbeeFound.find(mac)};
         bool isNew {itFound != zigbeeFound.end() && (zigbeeSearchUntil || itFound->second > time(0) - zigbeeNewFor)};

         json_t* jDevice {json_object()};
         json_object_set_new(jDevice, "mac", json_string(mac.c_str()));
         json_object_set_new(jDevice, "name", json_string(!isEmpty(model) ? model : mac.c_str()));
         json_object_set_new(jDevice, "manufacturer", json_string(manufacturer));
         json_object_set_new(jDevice, "model", json_string(model));
         json_object_set_new(jDevice, "swversion", json_string(""));
         json_object_set_new(jDevice, "reachable", json_true());
         json_object_set_new(jDevice, "noddf", json_true());
         json_object_set_new(jDevice, "new", json_boolean(isNew));
         json_object_set_new(jDevice, "resources", json_array());

         if (isNew && !zigbeeSearchUntil && (!zigbeeNewUntil || itFound->second + zigbeeNewFor < zigbeeNewUntil))
            zigbeeNewUntil = itFound->second + zigbeeNewFor;

         const char* lastSeen {getStringFromJson(jItem, "lastseen", "")};

         if (!isEmpty(lastSeen))
            json_object_set_new(jDevice, "lastseen", json_string(lastSeen));

         json_array_append_new(jDevices, jDevice);
         json_decref(jItem);
      }
   }

   return done;
}

//***************************************************************************
// Zigbee Device MACs
//   'devices' of deCONZ: ["00:15:8d:00:02:05:9f:8e", ...] - all paired devices, also the ones
//   without a resource (no DDF)
//***************************************************************************

int HomeCtl::zigbeeDeviceMacs(std::set<std::string>& macs)
{
   json_t* jData {};

   if (deconz.rest("GET", "devices", nullptr, jData) != success || !json_is_array(jData))
   {
      json_decref(jData);
      return fail;
   }

   size_t i {};
   json_t* jMac {};

   json_array_foreach(jData, i, jMac)
   {
      if (json_is_string(jMac))
         macs.insert(json_string_value(jMac));
   }

   json_decref(jData);

   return success;
}

//***************************************************************************
// Zigbee Service
//   called by the main loop: the events 'added' / 'deleted' of deCONZ (by dispatchDeconz()),
//   the running search
//***************************************************************************

int HomeCtl::zigbeeService()
{
   bool changed {false};

   while (!deconzEvents.empty())
   {
      json_t* oEvent {jsonLoad(deconzEvents.front().c_str())};
      deconzEvents.pop();

      if (!oEvent)
         continue;

      zigbeeEvent(oEvent);
      json_decref(oEvent);
      changed = true;
   }

   if (deconzReinitAt && time(0) >= deconzReinitAt)
   {
      deconzReinitAt = 0;
      tell(eloDetail, "DECONZ: Reading the devices again (%d s after the connect)", deconzReinitDelay);
      deconz.initDevices();
      changed = true;
   }

   if (zigbeeNewUntil && time(0) > zigbeeNewUntil)
   {
      zigbeeNewUntil = 0;                       // the 'new' mark of a device expired, set again by zigbeePush()
      changed = true;
   }

   if (zigbeeSearchUntil)
   {
      time_t now {time(0)};

      if (now >= zigbeeSearchNextCheck)
      {
         zigbeeSearchNextCheck = now + zigbeeCheckInterval;

         if (zigbeeCheckNew() > 0)
            changed = true;
      }

      if (now >= zigbeeSearchUntil)
      {
         tell(eloAlways, "DECONZ: Search for new devices finished, %zu found", zigbeeFound.size());
         zigbeeSearchUntil = 0;

         for (auto& found : zigbeeFound)         // 'new' for zigbeeNewFor from the end of the search
            found.second = now;

         changed = true;
      }
   }

   if (changed)
      zigbeePush();

   return done;
}

//***************************************************************************
// Zigbee Event
//   { "dzevent": "added" | "deleted", "resource": "lights" | "sensors", "id": "41", "uniqueid": "...", "item": {...} }
//***************************************************************************

int HomeCtl::zigbeeEvent(json_t* oEvent)
{
   std::string event {getStringFromJson(oEvent, "dzevent", "")};
   const char* resource {getStringFromJson(oEvent, "resource", "")};
   const char* id {getStringFromJson(oEvent, "id", "")};

   if (isEmpty(id))
      return fail;

   if (event == "added")
   {
      json_t* jItem {json_object_get(oEvent, "item")};

      if (jItem)
      {
         if (isEmpty(getStringFromJson(jItem, "uniqueid")) && !isEmpty(getStringFromJson(oEvent, "uniqueid")))
            json_object_set_new(jItem, "uniqueid", json_string(getStringFromJson(oEvent, "uniqueid")));

         return zigbeeAdded(resource, id, jItem);
      }

      // the event without the resource -> query it

      char* path {};
      asprintf(&path, "%s/%s", resource, id);
      int status {deconz.rest("GET", path, nullptr, jItem)};
      free(path);

      if (status == success && json_is_object(jItem))
         status = zigbeeAdded(resource, id, jItem);

      json_decref(jItem);
      return status;
   }

   if (event == "deleted")
   {
      // removed at deCONZ (e.g. by Phoscon) - the sensor of homectld stays (its history, the rules ...),
      //  it can be deleted by the sensor setup

      const char* uuid {getStringFromJson(oEvent, "uniqueid", "")};
      long address {isEmpty(uuid) ? na : deconz.addressOf(strcmp(resource, "lights") == 0 ? "light" : "sensor", uuid)};

      tell(eloAlways, "DECONZ: '%s/%s' (%s) was deleted at deCONZ%s", resource, id, uuid,
           address != na ? ", its sensor of homectld stays" : "");

      zigbeeKnownIds.erase(std::string(resource) + "/" + id);
   }

   return done;
}

//***************************************************************************
// Zigbee Added
//***************************************************************************

int HomeCtl::zigbeeAdded(const char* resource, const char* id, json_t* jItem)
{
   const char* uuid {getStringFromJson(jItem, "uniqueid", "")};
   std::string dzType {getStringFromJson(jItem, "type", "")};
   bool isLight {strcmp(resource, "lights") == 0};
   const char* kind {isLight ? "light" : "sensor"};

   zigbeeKnownIds.insert(std::string(resource) + "/" + id);

   if (isEmpty(uuid) || isInternalType(dzType))
      return done;

   bool known {deconz.addressOf(kind, uuid) != na};

   if (zigbeeSearchUntil)
      zigbeeFound[macOfUuid(uuid)] = time(0);   // also a known device paired again

   // as by the start: table deconzl / deconzs, address, valuefact, the state

   json_t* jData {json_object()};
   json_object_set(jData, id, jItem);
   deconz.processDevices(jData, kind);
   json_decref(jData);

   long address {deconz.addressOf(kind, uuid)};

   if (address == na || known)
      return done;

   const char* type {isLight ? "DZL" : "DZS"};

   tell(eloAlways, "DECONZ: New %s '%s' (%s / %s, %s) as %s:0x%02lx", kind, getStringFromJson(jItem, "name", ""),
        getStringFromJson(jItem, "manufacturername", ""), getStringFromJson(jItem, "modelid", ""), dzType.c_str(), type, address);

   // paired by the search of the page 'Zigbee' -> active at once (else as all new sensors inactive)

   if (zigbeeSearchUntil)
      zigbeeActivate(type, address, true, nullptr);

   valueFactsChanged = true;

   return success;
}

//***************************************************************************
// Zigbee Check New
//   'lights/new' / 'sensors/new' of the running search: { "7": { "name": "..." }, "lastscan": "active" }
//***************************************************************************

int HomeCtl::zigbeeCheckNew()
{
   int count {0};

   for (const char* resource : { "lights", "sensors" })
   {
      json_t* jNew {};
      char* path {};
      asprintf(&path, "%s/new", resource);
      int status {deconz.rest("GET", path, nullptr, jNew)};
      free(path);

      if (status != success || !json_is_object(jNew))
      {
         json_decref(jNew);
         continue;
      }

      const char* id {};
      json_t* jItem {};

      json_object_foreach(jNew, id, jItem)
      {
         if (strcmp(id, "lastscan") == 0 || zigbeeKnownIds.count(std::string(resource) + "/" + id))
            continue;

         json_t* jRes {};
         asprintf(&path, "%s/%s", resource, id);

         if (deconz.rest("GET", path, nullptr, jRes) == success && json_is_object(jRes))
         {
            if (zigbeeAdded(resource, id, jRes) == success)
               count++;
         }
         else
            zigbeeKnownIds.insert(std::string(resource) + "/" + id);    // not again

         free(path);
         json_decref(jRes);
      }

      json_decref(jNew);
   }

   // joined devices which got no resource (no DDF at deCONZ) - not in '<resource>/new'

   std::set<std::string> macs;

   if (!zigbeeKnownMacs.empty() && zigbeeDeviceMacs(macs) == success)
   {
      for (const auto& mac : macs)
      {
         if (zigbeeKnownMacs.count(mac))
            continue;

         zigbeeKnownMacs.insert(mac);

         if (!zigbeeFound.count(mac))
         {
            tell(eloAlways, "DECONZ: Device '%s' joined", mac.c_str());
            zigbeeFound[mac] = time(0);
            count++;
         }
      }
   }

   return count;
}

//***************************************************************************
// Zigbee Start Search
//***************************************************************************

int HomeCtl::zigbeeStartSearch(std::string& message)
{
   // the resources known now, the new ones of '<resource>/new' are the found ones

   zigbeeKnownIds.clear();
   zigbeeFound.clear();
   zigbeeKnownMacs.clear();
   zigbeeDeviceMacs(zigbeeKnownMacs);

   for (const char* resource : { "lights", "sensors" })
   {
      json_t* jData {};

      if (deconz.rest("GET", resource, nullptr, jData) == success && json_is_object(jData))
      {
         const char* id {};
         json_t* jItem {};

         json_object_foreach(jData, id, jItem)
            zigbeeKnownIds.insert(std::string(resource) + "/" + id);
      }

      json_decref(jData);
   }

   // the search of lights and sensors (as Phoscon), both open the network (permit join)
   //   [{"success": {"/lights": "Searching for new devices", "/lights/duration": 180}}]

   int duration {0};

   for (const char* resource : { "lights", "sensors" })
   {
      json_t* jResult {};

      if (deconz.rest("POST", resource, nullptr, jResult) != success)
      {
         message = "deCONZ nicht erreichbar";
         return fail;
      }

      std::string error {deconz.resultError(jResult)};

      if (!error.empty())
      {
         json_decref(jResult);
         message = "deCONZ: " + error;
         return fail;
      }

      json_t* jSuccess {json_object_get(json_array_get(jResult, 0), "success")};
      std::string key {std::string("/") + resource + "/duration"};

      if (jSuccess && json_object_get(jSuccess, key.c_str()))
         duration = std::max(duration, getIntFromJson(jSuccess, key.c_str()));

      json_decref(jResult);
   }

   if (duration <= 0)
      duration = 180;

   zigbeeSearchDuration = duration;
   zigbeeSearchUntil = time(0) + duration;
   zigbeeSearchNextCheck = time(0) + zigbeeCheckInterval;

   tell(eloAlways, "DECONZ: Search for new devices started (%d s)", duration);
   message = "Suche gestartet";

   return success;
}

//***************************************************************************
// Zigbee Rename
//***************************************************************************

int HomeCtl::zigbeeRename(const char* resource, const char* id, const char* name, std::string& error)
{
   if (strcmp(resource, "lights") != 0 && strcmp(resource, "sensors") != 0)
   {
      error = "Unbekannte Resource";
      return fail;
   }

   std::string n {name};
   n = n.substr(n.find_first_not_of(' ') == std::string::npos ? n.size() : n.find_first_not_of(' '));

   while (!n.empty() && n.back() == ' ')
      n.pop_back();

   if (n.empty() || n.length() > 32)      // deCONZ: 1 - 32 characters
   {
      error = "Der Name muss 1 bis 32 Zeichen lang sein";
      return fail;
   }

   char* path {};
   asprintf(&path, "%s/%s", resource, id);

   json_t* jResult {};
   int status {deconz.rest("PUT", path, json_pack("{s:s}", "name", n.c_str()), jResult)};

   if (status != success)
      error = "deCONZ nicht erreichbar";
   else if (!(error = deconz.resultError(jResult)).empty())
      status = fail;

   json_decref(jResult);

   // the new name for homectld (table, valuefact) - as by the start

   if (status == success)
   {
      json_t* jItem {};

      if (deconz.rest("GET", path, nullptr, jItem) == success && json_is_object(jItem))
      {
         json_t* jData {json_object()};
         json_object_set(jData, id, jItem);
         deconz.processDevices(jData, strcmp(resource, "lights") == 0 ? "light" : "sensor");
         json_decref(jData);
      }

      json_decref(jItem);
      tell(eloAlways, "DECONZ: Renamed '%s' to '%s'", path, n.c_str());
   }

   free(path);

   return status;
}

//***************************************************************************
// Zigbee Activate
//***************************************************************************

int HomeCtl::zigbeeActivate(const char* type, uint address, bool active, const char* usrtitle)
{
   tableValueFacts->clear();
   tableValueFacts->setValue("TYPE", type);
   tableValueFacts->setValue("ADDRESS", (long)address);

   if (!tableValueFacts->find())
      return fail;

   tableValueFacts->clearChanged();
   tableValueFacts->setValue("STATE", active ? "A" : "D");

   if (usrtitle)
      tableValueFacts->setValue("USRTITLE", usrtitle);

   if (tableValueFacts->getChanges())
   {
      tableValueFacts->store();
      initSensorByFact(type, address);
      valueFactsChanged = true;
   }

   return success;
}

//***************************************************************************
// Zigbee Delete
//   all resources of the device (MAC) at deCONZ, their sensors in homectld (valuefact, table
//   deconzl / deconzs), the history of the sensors stays in the table samples
//***************************************************************************

int HomeCtl::zigbeeDelete(const char* mac, bool reset, std::string& error)
{
   if (isEmpty(mac))
   {
      error = "Keine MAC";
      return fail;
   }

   int count {0};

   for (const char* resource : { "lights", "sensors" })
   {
      json_t* jData {};

      if (deconz.rest("GET", resource, nullptr, jData) != success || !json_is_object(jData))
      {
         json_decref(jData);
         error = "deCONZ nicht erreichbar";
         return fail;
      }

      bool isLight {strcmp(resource, "lights") == 0};
      const char* kind {isLight ? "light" : "sensor"};
      const char* id {};
      json_t* jItem {};

      json_object_foreach(jData, id, jItem)
      {
         const char* uuid {getStringFromJson(jItem, "uniqueid", "")};

         if (isEmpty(uuid) || macOfUuid(uuid) != mac)
            continue;

         // a light with 'reset' leaves the network (ZDP leave request) and resets to factory defaults

         char* path {};
         asprintf(&path, "%s/%s", resource, id);
         json_t* jResult {};
         int status {deconz.rest("DELETE", path, isLight && reset ? json_pack("{s:b}", "reset", 1) : nullptr, jResult)};
         std::string err {status == success ? deconz.resultError(jResult) : "deCONZ nicht erreichbar"};
         json_decref(jResult);

         if (!err.empty())
         {
            error = std::string(path) + ": " + err;
            free(path);
            json_decref(jData);
            return fail;
         }

         tell(eloAlways, "DECONZ: Deleted '%s' (%s, '%s')", path, uuid, getStringFromJson(jItem, "name", ""));
         free(path);

         zigbeeKnownIds.erase(std::string(resource) + "/" + id);

         // the sensor of homectld

         long address {deconz.addressOf(kind, uuid)};

         if (address != na)
         {
            const char* type {isLight ? "DZL" : "DZS"};
            deleteValueFact(type, address);
            sensors[type].erase(address);
         }

         deconz.forget(kind, uuid);
         count++;
      }

      json_decref(jData);
   }

   zigbeeFound.erase(mac);

   if (!count)
   {
      error = std::string("Kein Gerät mit der MAC ") + mac + " bei deCONZ";
      return fail;
   }

   valueFactsChanged = true;

   return success;
}

//***************************************************************************
// Zigbee Api Key
//   registers homectld at deCONZ - only within 60 s after the gateway was unlocked in Phoscon
//   ('Gateway -> Erweitert -> App verbinden')
//***************************************************************************

int HomeCtl::zigbeeApiKey(std::string& message)
{
   if (isEmpty(deconz.getHttpUrl()))
   {
      message = "Die URL von deCONZ fehlt (Konfiguration -> DECONZ -> deCONZ HTTP URL)";
      return fail;
   }

   std::string result;

   if (deconz.queryApiKey(result) != success)
   {
      tell(eloAlways, "DECONZ: Registration at '%s' failed, '%s'", deconz.getHttpUrl(), result.c_str());
      message = "Registrierung fehlgeschlagen" + (result.empty() ? std::string(" (deCONZ nicht erreichbar)") : ": " + result) +
         " - das Gateway in Phoscon entsperren (Gateway -> Erweitert -> App verbinden) und innerhalb von 60 Sekunden erneut versuchen";
      return fail;
   }

   tell(eloAlways, "DECONZ: Registered at '%s', got a new API key", deconz.getHttpUrl());

   setConfigItem("deconzApiKey", result.c_str());
   deconz.setApiKey(result.c_str());
   deconz.initDevices();

   if (!deconz.wsRunning())
      deconz.initWsClient();

   json_t* oJson {json_object()};
   config2Json(oJson);
   pushOutMessage(oJson, "config");

   message = "API-Key von deCONZ erhalten und gespeichert";

   return success;
}
