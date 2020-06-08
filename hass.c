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
   int status = success;

   // check/prepare reader/writer connection

   if (hassCheckConnection() != success)
      return fail;

   // check if state topic already exists

   std::string message;
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
      mqttReader->subscribe(stateTopic);
      status = mqttReader->read(&message, &tp);

      if (status != success && status != MqTTClient::wrnNoMessagePending)
         return fail;

      if (forceConfig || status == MqTTClient::wrnNoMessagePending)
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

         mqttWriter->write(configTopic, configJson);

         free(configTopic);
         free(configJson);
      }

      mqttReader->unsubscribe();
   }

   // publish actual value

   char* valueJson = 0;

   if (iot == iotLight)
      asprintf(&valueJson, "{ \"state\" : \"%s\", \"brightness\": 255 }", value ? "ON" :"OFF");
   else if (!isEmpty(text))
      asprintf(&valueJson, "{ \"value\" : \"%s\" }", text);
   else
      asprintf(&valueJson, "{ \"value\" : \"%.2f\" }", value);

   mqttWriter->write(stateTopic, valueJson);

   free(valueJson);
   free(stateTopic);

   return success;
}

//***************************************************************************
// Perform Hass Requests
//***************************************************************************

int Poold::performHassRequests()
{
   if (hassCheckConnection() != success)
      return fail;

   std::string message;
   std::string topic;
   json_error_t error;

   if (mqttCommandReader->read(&message, &topic) == success)
   {
      // tell(0, "(%s) GOT [%s]", topic.c_str(), message.c_str());

      json_t* jData = json_loads(message.c_str(), 0, &error);

      if (!jData)
      {
         tell(0, "Error: Can't parse json in '%s'", message.c_str());
         return fail;
      }

      tell(eloAlways, "<- (%s) [%s]", topic.c_str(), message.c_str());

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

   while (mqttW1Reader->read(&message, &topic) == success)
   {
      json_t* jArray = json_loads(message.c_str(), 0, &error);

      if (!jArray)
      {
         tell(0, "Error: Can't parse json in '%s'", message.c_str());
         return fail;
      }

      tell(eloAlways, "<- (%s) [%s]", topic.c_str(), message.c_str());

      size_t index;
      json_t* jValue;

      json_array_foreach(jArray, index, jValue)
      {
         const char* name = getStringFromJson(jValue, "name");
         double value = getDoubleFromJson(jValue, "value");

         updateW1(name, value);
      }
   }

   return success;
}

//***************************************************************************
// Check MQTT Connection
//***************************************************************************

int Poold::hassCheckConnection()
{
   if (!mqttCommandReader)
   {
      mqttCommandReader = new MqTTSubscribeClient(hassMqttUrl, "poold_com");
      mqttCommandReader->setConnectTimeout(15);   // seconds
      mqttCommandReader->setTimeout(15);          // milli seconds
   }

   if (!mqttW1Reader)
   {
      mqttW1Reader = new MqTTSubscribeClient(hassMqttUrl, "poold_w1_reader");
      mqttW1Reader->setConnectTimeout(15);   // seconds
      mqttW1Reader->setTimeout(15);          // milli seconds
   }

   if (!mqttWriter)
   {
      mqttWriter = new MqTTPublishClient(hassMqttUrl, "poold_publisher");
      mqttWriter->setConnectTimeout(15);          // seconds
   }

   if (!mqttReader)
   {
      mqttReader = new MqTTSubscribeClient(hassMqttUrl, "poold_subscriber");
      mqttReader->setConnectTimeout(15);          // seconds
      mqttReader->setTimeout(100);                // milli seconds
   }

   if (!mqttCommandReader->isConnected())
   {
      if (mqttCommandReader->connect() != success)
      {
         tell(0, "Error: MQTT: Connecting subscriber to '%s' failed", hassMqttUrl);
         return fail;
      }

      mqttCommandReader->subscribe("poold2mqtt/light/+/set/#");
      tell(0, "MQTT: Connecting command subscriber to '%s' succeeded", hassMqttUrl);
   }

   if (!mqttW1Reader->isConnected())
   {
      if (mqttW1Reader->connect() != success)
      {
         tell(0, "Error: MQTT: Connecting subscriber to '%s' failed", hassMqttUrl);
         return fail;
      }

      mqttW1Reader->subscribe("poold2mqtt/w1");
      tell(0, "MQTT: Connecting w1 subscriber to '%s' succeeded", hassMqttUrl);
   }

   if (!mqttWriter->isConnected())
   {
      if (mqttWriter->connect() != success)
      {
         tell(0, "Error: MQTT: Connecting publisher to '%s' failed", hassMqttUrl);
         return fail;
      }

      tell(0, "MQTT: Connecting publisher to '%s' succeeded", hassMqttUrl);
   }

   if (!mqttReader->isConnected())
   {
      if (mqttReader->connect() != success)
      {
         tell(0, "Error: MQTT: Connecting subscriber to '%s' failed", hassMqttUrl);
         return fail;
      }

      tell(0, "MQTT: Connecting subscriber to '%s' succeeded", hassMqttUrl);
   }

   return success;
}
