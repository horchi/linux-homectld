//***************************************************************************
// poold / Linux - Pool Steering
// File hass.c
// This code is distributed under the terms and conditions of the
// GNU GENERAL PUBLIC LICENSE. See the file LICENSE for details.
// Date 16.04.2020  Jörg Wendel
//***************************************************************************

#include <jansson.h>

#include "lib/json.h"
#include "poold.h"

//***************************************************************************
// Push Value to Home Assistant
//***************************************************************************

int Poold::hassPush(IoType iot, const char* name, const char* title, const char* unit,
                  double value, const char* text, bool forceConfig)
{
   // check/prepare reader/writer connection

   if (mqttCheckConnection() != success)
      return fail;

   // check if state topic already exists

   int status {success};
   MemoryStruct message;
   std::string tp;
   std::string sName = name;

   sName = strReplace("ß", "ss", sName);
   sName = strReplace("ü", "ue", sName);
   sName = strReplace("ö", "oe", sName);
   sName = strReplace("ä", "ae", sName);
   sName = strReplace(" ", "_", sName);

   char* stateTopic = 0;
   asprintf(&stateTopic, "poold2mqtt/%s/%s/state", iot == iotLight ? "light" : "sensor", sName.c_str());

   if (!isEmpty(title))
   {
      // Interface description:
      //   https://www.home-assistant.io/docs/mqtt/discovery/

      mqttReader->subscribe(stateTopic);
      status = mqttReader->read(&message, 100);
      tp = mqttReader->getLastReadTopic();

      if (status != success && status != Mqtt::wrnTimeout)
         return fail;

      if (forceConfig || status == Mqtt::wrnTimeout)
      {
         char* configTopic {nullptr};
         char* configJson {nullptr};

         // topic don't exists -> create sensor

         if (strcmp(unit, "°") == 0)
            unit = "°C";
         else if (strcmp(unit, "Heizungsstatus") == 0 ||
                  strcmp(unit, "zst") == 0 ||
                  strcmp(unit, "T") == 0)
            unit = "";

         tell(1, "Info: Sensor '%s' not found at home assistants MQTT, sendig config message", sName.c_str());

         asprintf(&configTopic, "homeassistant/%s/pool/%s/config", iot == iotLight ? "light" : "sensor", sName.c_str());

         if (iot == iotLight)
         {
            char* cmdTopic;
            asprintf(&cmdTopic, "poold2mqtt/light/%s/set", sName.c_str());

            asprintf(&configJson, "{"
                     "\"state_topic\"         : \"poold2mqtt/light/%s/state\","
                     "\"command_topic\"       : \"%s\","
                     "\"name\"                : \"Pool %s\","
                     "\"unique_id\"           : \"%s_poold2mqtt\","
                     "\"schema\"              : \"json\","
                     "\"brightness\"          : \"false\""
                     "}",
                     sName.c_str(), cmdTopic, title, sName.c_str());

            hassCmdTopicMap[cmdTopic] = name;
            free(cmdTopic);
         }
         else
         {
            asprintf(&configJson, "{"
                     "\"unit_of_measurement\" : \"%s\","
                     "\"value_template\"      : \"{{ value_json.value }}\","
                     "\"state_topic\"         : \"poold2mqtt/sensor/%s/state\","
                     "\"name\"                : \"Pool %s\","
                     "\"unique_id\"           : \"%s_poold2mqtt\""
                     "}",
                     unit, sName.c_str(), title, sName.c_str());
         }

         mqttWriter->writeRetained(configTopic, configJson);

         free(configTopic);
         free(configJson);
      }

      mqttReader->unsubscribe(stateTopic);
   }

   // publish actual value

   json_t* oValue = json_object();

   if (iot == iotLight)
   {
      json_object_set_new(oValue, "state", json_string(value ? "ON" :"OFF"));
      json_object_set_new(oValue, "brightness", json_integer(255));
   }
   else if (!isEmpty(text))
      json_object_set_new(oValue, "value", json_string(text));
   else
      json_object_set_new(oValue, "value", json_real(value));

   char* j = json_dumps(oValue, JSON_REAL_PRECISION(4));
   mqttWriter->writeRetained(stateTopic, j);

   free(stateTopic);

   return success;
}

//***************************************************************************
// Perform MQTT Requests
//   - check 'mqttCommandReader' for commands of a home automation
//   - check 'mqttW1Reader' for data from the W1 service
//***************************************************************************

int Poold::performMqttRequests()
{
   if (mqttCheckConnection() != success)
      return fail;

   MemoryStruct message;
   std::string topic;
   json_error_t error;

   if (mqttCommandReader->read(&message, 10) == success)
   {
      topic = mqttCommandReader->getLastReadTopic();
      json_t* jData = json_loads(message.memory, 0, &error);

      if (!jData)
      {
         tell(0, "Error: Can't parse json in '%s'", message.memory);
         return fail;
      }

      tell(eloAlways, "<- (%s) [%s]", topic.c_str(), message.memory);

      auto it = hassCmdTopicMap.find(topic);

      if (it != hassCmdTopicMap.end())
      {
         for (auto itOutput = digitalOutputStates.begin(); itOutput != digitalOutputStates.end(); ++itOutput)
         {
            const char* s = getStringFromJson(jData, "state", "");
            int state = strcmp(s, "ON") == 0;

            if (strcmp(it->second.c_str(), itOutput->second.name) == 0)
            {
               gpioWrite(itOutput->first, state);
               break;
            }
         }
      }
   }

   while (mqttW1Reader->read(&message, 10) == success)
   {
      topic = mqttW1Reader->getLastReadTopic();
      json_t* jArray = json_loads(message.memory, 0, &error);

      if (!jArray)
      {
         tell(0, "Error: Can't parse json in '%s'", message.memory);
         return fail;
      }

      tell(eloAlways, "<- (%s) [%s]", topic.c_str(), message.memory);

      size_t index {0};
      json_t* jValue {nullptr};

      json_array_foreach(jArray, index, jValue)
      {
         const char* name = getStringFromJson(jValue, "name");
         double value = getDoubleFromJson(jValue, "value");

         updateW1(name, value);
      }

      cleanupW1();
   }

   return success;
}

//***************************************************************************
// Check MQTT Connection
//***************************************************************************

int Poold::mqttCheckConnection()
{
   if (!mqttCommandReader)
      mqttCommandReader = new Mqtt();

   if (!mqttW1Reader)
      mqttW1Reader = new Mqtt();

   if (!mqttWriter)
      mqttWriter = new Mqtt();

   if (!mqttReader)
      mqttReader = new Mqtt();

   if (mqttCommandReader->isConnected() &&
       mqttW1Reader->isConnected() &&
       mqttWriter->isConnected() &&
       mqttReader->isConnected())
   {
      return success;
   }

   if (lastMqttConnectAt >= time(0) - 60)
      return fail;

   lastMqttConnectAt = time(0);

   if (!mqttCommandReader->isConnected())
   {
      if (mqttCommandReader->connect(mqttUrl) != success)
      {
         tell(0, "Error: MQTT: Connecting subscriber to '%s' failed", mqttUrl);
         return fail;
      }

      mqttCommandReader->subscribe("poold2mqtt/light/+/set/#");
      tell(0, "MQTT: Connecting command subscriber to '%s' succeeded", mqttUrl);
   }

   if (!mqttW1Reader->isConnected())
   {
      if (mqttW1Reader->connect(mqttUrl) != success)
      {
         tell(0, "Error: MQTT: Connecting subscriber to '%s' failed", mqttUrl);
         return fail;
      }

      mqttW1Reader->subscribe("poold2mqtt/w1");
      tell(0, "MQTT: Connecting w1 subscriber to '%s' succeeded", mqttUrl);
   }

   if (!mqttWriter->isConnected())
   {
      if (mqttWriter->connect(mqttUrl) != success)
      {
         tell(0, "Error: MQTT: Connecting publisher to '%s' failed", mqttUrl);
         return fail;
      }

      tell(0, "MQTT: Connecting publisher to '%s' succeeded", mqttUrl);
   }

   if (!mqttReader->isConnected())
   {
      if (mqttReader->connect(mqttUrl) != success)
      {
         tell(0, "Error: MQTT: Connecting subscriber to '%s' failed", mqttUrl);
         return fail;
      }

      tell(0, "MQTT: Connecting subscriber to '%s' succeeded", mqttUrl);
   }

   return success;
}
