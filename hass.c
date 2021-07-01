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
// Push Value to MQTT for Home Automation Systems
//***************************************************************************

int Poold::hassPush(IoType iot, const char* name, const char* title, const char* unit,
                    double value, const char* text, bool forceConfig)
{
   // check/prepare reader/writer connection

   if (isEmpty(mqttUrl))
      return done;

   mqttCheckConnection();

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

   char* stateTopic {nullptr};
   asprintf(&stateTopic, "poold2mqtt/%s/%s/state", iot == iotLight ? "light" : "sensor", sName.c_str());

   if (!isEmpty(title))
   {
      if (!mqttReader->isConnected())
         return fail;

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

         if (!mqttWriter->isConnected())
            return fail;

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

   if (!mqttWriter->isConnected())
      return fail;

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
//   - check 'mqttPoolReader' for data from the W1 service and the arduino
//      -> w1mqtt (W1 service) is running localy and provide data of the W1 sensors
//      -> the arduino provide the values of his analog inputs
//***************************************************************************

int Poold::performMqttRequests()
{
   mqttCheckConnection();

   MemoryStruct message;
   json_error_t error;

   if (!isEmpty(mqttUrl) && mqttCommandReader->isConnected())
   {
      if (mqttCommandReader->read(&message, 10) == success)
      {
         json_t* jData = json_loads(message.memory, 0, &error);

         if (!jData)
         {
            tell(0, "Error: Can't parse json in '%s'", message.memory);
            return fail;
         }

         tell(eloAlways, "<- (%s) [%s]", mqttCommandReader->getLastReadTopic().c_str(), message.memory);

         auto it = hassCmdTopicMap.find(mqttCommandReader->getLastReadTopic().c_str());

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
   }

   if (!isEmpty(poolMqttUrl) && mqttPoolReader->isConnected())
   {
      static time_t lastW1Read {0};

      if (!lastW1Read)
         lastW1Read = time(0);

      // tell(0, "Try reading topic '%s'", mqttPoolReader->getTopic());

      while (mqttPoolReader->read(&message, 10) == success)
      {
         // tell(0, "read w1 success [%s]", message.memory);
         lastW1Read = time(0);

         if (isEmpty(message.memory))
            continue;

         json_t* jArray = json_loads(message.memory, 0, &error);

         if (!jArray)
         {
            tell(0, "Error: Can't parse json in '%s'", message.memory);
            return fail;
         }

         std::string tp = mqttPoolReader->getLastReadTopic();

         tell(eloAlways, "<- (%s) [%s] retained %d", tp.c_str(), message.memory, mqttPoolReader->isRetained());

         if (strcmp(tp.c_str(), "poold2mqtt/w1") == 0)
         {
            size_t index {0};
            json_t* jValue {nullptr};

            json_array_foreach(jArray, index, jValue)
            {
               const char* name = getStringFromJson(jValue, "name");
               double value = getDoubleFromJson(jValue, "value");
               time_t stamp = getIntFromJson(jValue, "time");

               if (stamp < time(0)-300)
               {
                  tell(eloAlways, "Skipping old (%ld seconds) w1 value", time(0)-stamp);
                  continue;
               }

               updateW1(name, value, stamp);
            }

            cleanupW1();

            // last successful W1 read at least in last 5 minutes?

            if (lastW1Read < time(0)-300)
            {
               tell(eloAlways, "Error: No w1 update since '%s', disconnect from MQTT to force recover", l2pTime(lastW1Read).c_str());
               mqttDisconnect();
            }
         }
         else if (strcmp(tp.c_str(), "poold2mqtt/arduino/out") == 0)
         {
            size_t index {0};
            json_t* jValue {nullptr};

            json_array_foreach(jArray, index, jValue)
            {
               const char* name = getStringFromJson(jValue, "name");
               double value = getDoubleFromJson(jValue, "value");

               updateAnalogInput(name, value, time(0));
            }
         }
      }
   }

   return success;
}

//***************************************************************************
// Check MQTT Connection
//***************************************************************************

int Poold::mqttDisconnect()
{
   if (mqttPoolReader)       mqttPoolReader->disconnect();
   if (mqttCommandReader)    mqttCommandReader->disconnect();
   if (mqttWriter)           mqttWriter->disconnect();
   if (mqttReader)           mqttReader->disconnect();

   delete mqttPoolReader;    mqttPoolReader = nullptr;
   delete mqttCommandReader; mqttCommandReader = nullptr;
   delete mqttWriter;        mqttWriter = nullptr;
   delete mqttReader;        mqttReader = nullptr;

   return done;
}

//***************************************************************************
// Check MQTT Connection and Connect
//***************************************************************************

int Poold::mqttCheckConnection()
{
   bool renonnectNeeded {false};

   if (!mqttCommandReader)
      mqttCommandReader = new Mqtt();

   if (!mqttPoolReader)
      mqttPoolReader = new Mqtt();

   if (!mqttWriter)
      mqttWriter = new Mqtt();

   if (!mqttReader)
      mqttReader = new Mqtt();

   if (!isEmpty(mqttUrl) && (!mqttCommandReader->isConnected() || !mqttWriter->isConnected() || !mqttReader->isConnected()))
      renonnectNeeded = true;

   if (!isEmpty(poolMqttUrl) && !mqttPoolReader->isConnected())
      renonnectNeeded = true;

   if (!renonnectNeeded)
      return success;

   if (lastMqttConnectAt >= time(0) - 60)
      return fail;

   lastMqttConnectAt = time(0);

   if (!isEmpty(mqttUrl))
   {
      if (!mqttCommandReader->isConnected())
      {
         if (mqttCommandReader->connect(mqttUrl, mqttUser, mqttPassword) != success)
         {
            tell(0, "Error: MQTT: Connecting subscriber to '%s' failed", mqttUrl);
         }
         else
         {
            mqttCommandReader->subscribe("poold2mqtt/light/+/set/#");
            tell(0, "MQTT: Connecting command subscriber to '%s' succeeded", mqttUrl);
         }
      }

      if (!mqttWriter->isConnected())
      {
         if (mqttWriter->connect(mqttUrl, mqttUser, mqttPassword) != success)
            tell(0, "Error: MQTT: Connecting publisher to '%s' failed", mqttUrl);
         else
            tell(0, "MQTT: Connecting publisher to '%s' succeeded", mqttUrl);
      }

      if (!mqttReader->isConnected())
      {
         if (mqttReader->connect(mqttUrl, mqttUser, mqttPassword) != success)
            tell(0, "Error: MQTT: Connecting subscriber to '%s' failed", mqttUrl);
         else
            tell(0, "MQTT: Connecting subscriber to '%s' succeeded", mqttUrl);

         // subscription is done in hassPush
      }
   }

   if (!isEmpty(poolMqttUrl))
   {
      if (!mqttPoolReader->isConnected())
      {
         if (mqttPoolReader->connect(poolMqttUrl) != success)
         {
            tell(0, "Error: MQTT: Connecting subscriber to '%s' failed", poolMqttUrl);
         }
         else
         {
            mqttPoolReader->subscribe("poold2mqtt/w1");
            tell(0, "MQTT: Connecting subscriber to '%s' - '%s' succeeded", poolMqttUrl, "poold2mqtt/w1");
            mqttPoolReader->subscribe("poold2mqtt/arduino/out");
            tell(0, "MQTT: Connecting subscriber to '%s' - '%s' succeeded", poolMqttUrl, "poold2mqtt/arduino/out");
         }
      }
   }

   return success;
}
