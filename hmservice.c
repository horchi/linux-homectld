/*
 *  hmservice.c
 *
 *  (c) 2026 Jörg Wendel
 *
 * This code is distributed under the terms and conditions of the
 * GNU GENERAL PUBLIC LICENSE. See the file COPYING for details.
 *
 *  HomeMatic via the service hmmqtt (CUL stick, without CCU and Node-RED),
 *  config 'homeMaticBridge' = 'hmmqtt', MQTT base topic 'homeMaticTopic' (default 'homematic'):
 *
 *    <base>/status, <base>/devices, <base>/state/<channel>, <base>/pairing     (retained)
 *    <base>/event/paired, <base>/event/removed
 *    <base>/set/<channel>, <base>/get/<channel>, <base>/pair, <base>/remove   (to the service)
 *
 *  The channel (<serial>:<channel>, e.g. 'OEQ1853240:1') is the uuid of the table
 *  'homematic' as with the CCU, the blinds keep their address (HMB:0xNN).
 *  The WEBIF page 'HomeMatic' (setup) shows the devices, pairs and removes them.
 */

#include "daemon.h"

//***************************************************************************
// Add Blind (table 'homematic' uuid -> address, value fact HMB)
//   used by the CCU (listDevices) and the service (devices)
//***************************************************************************

int HomeCtl::homeMaticAddBlind(const char* uuid, int direction)
{
   tableHomeMatic->clear();
   tableHomeMatic->setValue("UUID", uuid);

   if (!selectHomeMaticByUuid->find())
   {
      tableHomeMatic->setValue("TYPE", "HMB");
      tableHomeMatic->insert();

      tell(eloDebugHomeMatic, "HomeMatic: Added new blind, uuid '%s'", uuid);

      tableHomeMatic->clear();
      tableHomeMatic->setValue("UUID", uuid);
      selectHomeMaticByUuid->find();
   }

   int address {tableHomeMatic->getIntValueAsInt("ADDRESS")};

   tableHomeMatic->setValue("KIND", "BLIND");
   tableHomeMatic->setValue("NAME", uuid);
   tableHomeMatic->store();
   selectHomeMaticByUuid->freeResult();

   homeMaticUuids[address] = uuid;

   // HMB -> Home-Matic-Blind

   addValueFact(address, "HMB", 1, uuid, "%", "", urControl);

   if (direction != na)
      sensors["HMB"][address].lastDir = direction == 2 ? dirClose : dirOpen;

   // the position: last known one (restored from iostates) until the device reports it

   if (!sensors["HMB"][address].valid())
      sensors["HMB"][address].setValue(100);

   return address;
}

//***************************************************************************
// Blind Update (value, moving, direction) -> WEBIF, HA, Node-RED
//***************************************************************************

int HomeCtl::homeMaticBlindUpdate(uint address, double value, bool working, int direction,
                                  const char* jsonTopic, const char* message)
{
   SensorData& sensor {sensors["HMB"][address]};

   sensor.touch();

   if (jsonTopic)
      sensor.setLastJson(jsonTopic, message);

   sensor.setState(value == 100);                               // on if completely open
   sensor.setValue(value);
   sensor.working = working;

   if (direction != 0)
      sensor.lastDir = direction == 2 ? dirClose : dirOpen;

   json_t* ojData {json_object()};
   sensor2Json(ojData, "HMB", address);
   json_object_set_new(ojData, "value", json_real(value));

   char tuple[50];
   snprintf(tuple, sizeof(tuple), "HMB:0x%02x", address);
   jsonSensorList[tuple] = ojData;

   pushDataUpdate("update", 0L);

   mqttHaPublish(sensor);
   mqttNodeRedPublishSensor(sensor);

   return done;
}

//***************************************************************************
// Dispatch Message of the Service hmmqtt
//***************************************************************************

int HomeCtl::dispatchHmMqtt(const char* topic, const char* message)
{
   std::string sub {topic + homeMaticTopic.length() + 1};

   tell(eloHomeMatic, "<- (hmmqtt) '%s' '%s'", topic, message);

   json_t* jData {jsonLoad(message)};

   if (!jData)
   {
      tell(eloAlways, "Error: (hmmqtt) Can't parse json in '%s'", message);
      return fail;
   }

   if (sub == "status")
   {
      const char* state {getStringFromJson(jData, "state", "")};

      if (!homeMaticServiceStatus || strcmp(state, getStringFromJson(homeMaticServiceStatus, "state", "")) != 0)
         tell(eloAlways, "HomeMatic: Service hmmqtt is %s (%s)", state, getStringFromJson(jData, "cul", ""));

      if (homeMaticServiceStatus) json_decref(homeMaticServiceStatus);
      homeMaticServiceStatus = json_incref(jData);
   }
   else if (sub == "devices" && json_is_array(jData))
   {
      size_t index {0};
      json_t* jDev {};

      json_array_foreach(jData, index, jDev)
      {
         size_t i {0};
         json_t* jCh {};

         json_array_foreach(getObjectFromJson(jDev, "channels"), i, jCh)
         {
            if (strcmp(getStringFromJson(jCh, "kind", ""), "BLIND") == 0)
               homeMaticAddBlind(getStringFromJson(jCh, "channel", ""), na);
         }
      }

      if (homeMaticServiceDevices) json_decref(homeMaticServiceDevices);
      homeMaticServiceDevices = json_incref(jData);
   }
   else if (sub.rfind("state/", 0) == 0)
   {
      std::string uuid {sub.substr(6)};
      long address {na};

      for (const auto& [addr, u] : homeMaticUuids)
         if (u == uuid)
            address = addr;

      if (address == na)
         tell(eloDetail, "HomeMatic: State of unknown channel '%s' ignored", uuid.c_str());
      else if (getObjectFromJson(jData, "level"))
      {
         std::string dir {getStringFromJson(jData, "direction", "none")};

         homeMaticBlindUpdate(address, getDoubleFromJson(jData, "level"), getBoolFromJson(jData, "working", false),
                              dir == "up" ? 1 : dir == "down" ? 2 : 0, "homematic/state", message);
      }

      json_decref(jData);
      return done;                                    // no push of the HomeMatic page for each state
   }
   else if (sub == "pairing")
   {
      if (homeMaticServicePairing) json_decref(homeMaticServicePairing);
      homeMaticServicePairing = json_incref(jData);
   }
   else if (sub.rfind("event/", 0) == 0)
   {
      json_object_set_new(jData, "event", json_string(sub.substr(6).c_str()));
      json_object_set_new(jData, "time", json_integer(time(0)));

      tell(eloAlways, "HomeMatic: %s '%s' -> %s %s", sub.substr(6).c_str(), getStringFromJson(jData, "serial", ""),
           getStringFromJson(jData, "result", ""), getStringFromJson(jData, "text", ""));

      if (homeMaticServiceEvent) json_decref(homeMaticServiceEvent);
      homeMaticServiceEvent = json_incref(jData);
   }

   json_decref(jData);

   // update the WEBIF page 'HomeMatic'

   json_t* oJson {json_object()};
   homeMatic2Json(oJson);
   pushOutMessage(oJson, "homematic");

   return done;
}

//***************************************************************************
// Blind Action via the Service (set level / stop)
//***************************************************************************

int HomeCtl::homeMaticServiceAction(SensorData& sensor, double value, bool noStop)
{
   if (!mqttWriter || !mqttWriter->isConnected())
      return fail;

   std::string uuid {homeMaticUuids[sensor.address]};

   if (uuid.empty())
   {
      tell(eloAlways, "Warning: HomeMatic: No channel for 'HMB:0x%02x'", sensor.address);
      return fail;
   }

   json_t* oJson {json_object()};

   if (sensor.working && !noStop)
      json_object_set_new(oJson, "stop", json_true());
   else
      json_object_set_new(oJson, "level", json_real(value));

   char* message {json_dumps(oJson, JSON_REAL_PRECISION(4) | JSON_COMPACT)};
   std::string topic {homeMaticTopic + "/set/" + uuid};

   tell(eloHomeMatic, "-> (hmmqtt) '%s' '%s'", topic.c_str(), message);
   int status {mqttWriter->write(topic.c_str(), message)};

   free(message);
   json_decref(oJson);

   return status;
}

//***************************************************************************
// HomeMatic to Json (WEBIF page 'HomeMatic')
//***************************************************************************

int HomeCtl::homeMatic2Json(json_t* obj)
{
   json_object_set_new(obj, "interface", json_boolean(homeMaticInterface));
   json_object_set_new(obj, "bridge", json_string(homeMaticBridge.c_str()));
   json_object_set_new(obj, "topic", json_string(homeMaticTopic.c_str()));

   if (homeMaticServiceStatus)
      json_object_set(obj, "status", homeMaticServiceStatus);

   if (homeMaticServicePairing)
      json_object_set(obj, "pairing", homeMaticServicePairing);

   if (homeMaticServiceEvent)
      json_object_set(obj, "lastEvent", homeMaticServiceEvent);

   // the devices with the keys / titles of their channels

   json_t* jDevices {json_array()};

   if (homeMaticServiceDevices)
   {
      size_t index {0};
      json_t* jDev {};

      json_array_foreach(homeMaticServiceDevices, index, jDev)
      {
         json_t* jCopy {json_deep_copy(jDev)};
         size_t i {0};
         json_t* jCh {};

         json_array_foreach(getObjectFromJson(jCopy, "channels"), i, jCh)
         {
            const char* uuid {getStringFromJson(jCh, "channel", "")};

            for (const auto& [addr, u] : homeMaticUuids)
            {
               if (u != uuid)
                  continue;

               char key[50];
               snprintf(key, sizeof(key), "HMB:0x%02x", addr);
               json_object_set_new(jCh, "key", json_string(key));
               json_object_set_new(jCh, "title", json_string(sensors["HMB"][addr].title.c_str()));
               json_object_set_new(jCh, "value", json_real(sensors["HMB"][addr].value));
            }
         }

         json_array_append_new(jDevices, jCopy);
      }
   }

   json_object_set_new(obj, "devices", jDevices);

   return done;
}

//***************************************************************************
// Perform HomeMatic (WEBIF)
//   action: '' (data of the page), 'pair' (duration), 'pairstop', 'remove' (serial), 'refresh'
//***************************************************************************

int HomeCtl::performHomeMatic(json_t* obj, long client)
{
   std::string action {getStringFromJson(obj, "action", "")};

   if (!action.empty())
   {
      if (!homeMaticInterface || homeMaticBridge != "hmmqtt")
         return replyResult(fail, client, "HomeMatic ist nicht über den Dienst hmmqtt angebunden (Konfiguration -> HomeMatic -> Anbindung)");

      if (mqttCheckConnection() != success)
         return replyResult(fail, client, "MQTT nicht verbunden");

      if (action == "pair")
      {
         char message[100];
         snprintf(message, sizeof(message), "{\"enable\":true,\"duration\":%d}", getIntFromJson(obj, "duration", 60));
         mqttWriter->write((homeMaticTopic + "/pair").c_str(), message);
         return replyResult(success, client, "Anlernen aktiv – jetzt die Anlerntaste am Gerät drücken");
      }

      if (action == "pairstop")
      {
         mqttWriter->write((homeMaticTopic + "/pair").c_str(), "{\"enable\":false}");
         return replyResult(success, client, "Anlernen beendet");
      }

      if (action == "remove")
      {
         const char* serial {getStringFromJson(obj, "serial", "")};

         if (isEmpty(serial))
            return replyResult(fail, client, "Seriennummer fehlt");

         char message[100];
         snprintf(message, sizeof(message), "{\"serial\":\"%s\"}", serial);
         mqttWriter->write((homeMaticTopic + "/remove").c_str(), message);
         return replyResult(success, client, "Abmelden von '%s' gesendet", serial);
      }

      if (action == "refresh")
      {
         for (const auto& [addr, uuid] : homeMaticUuids)
            mqttWriter->write((homeMaticTopic + "/get/" + uuid).c_str(), "{}");

         return replyResult(success, client, "Status bei den Geräten angefragt");
      }

      return replyResult(fail, client, "Unbekannte Aktion '%s'", action.c_str());
   }

   json_t* oJson {json_object()};
   homeMatic2Json(oJson);

   return pushOutMessage(oJson, "homematic", client);
}
