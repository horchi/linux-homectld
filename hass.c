//***************************************************************************
// Automation Control
// File hass.c
// This code is distributed under the terms and conditions of the
// GNU GENERAL PUBLIC LICENSE. See the file LICENSE for details.
// Date 16.04.2020  Jörg Wendel
//***************************************************************************

#include <jansson.h>

#include "lib/json.h"
#include "daemon.h"

//***************************************************************************
// Push Value to MQTT for Home Automation Systems
//***************************************************************************

int Daemon::hassPush(IoType iot, const char* name, const char* title, const char* unit,
                     double value, const char* text, bool forceConfig)
{
   // check/prepare reader/writer connection

   if (isEmpty(mqttHassUrl))
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
   asprintf(&stateTopic, TARGET "2mqtt/%s/%s/state", iot == iotLight ? "light" : "sensor", sName.c_str());

   if (!isEmpty(title))
   {
      if (!mqttHassReader->isConnected())
         return fail;

      // Interface description:
      //   https://www.home-assistant.io/docs/mqtt/discovery/

      mqttHassReader->subscribe(stateTopic);
      status = mqttHassReader->read(&message, 100);
      tp = mqttHassReader->getLastReadTopic();

      if (status != success && status != Mqtt::wrnTimeout)
         return fail;

      if (forceConfig || status == Mqtt::wrnTimeout)
      {
         char* configTopic {nullptr};
         char* configJson {nullptr};

         if (!mqttHassWriter->isConnected())
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
            asprintf(&cmdTopic, TARGET "2mqtt/light/%s/set", sName.c_str());

            asprintf(&configJson, "{"
                     "\"state_topic\"         : \"" TARGET "2mqtt/light/%s/state\","
                     "\"command_topic\"       : \"%s\","
                     "\"name\"                : \"Pool %s\","
                     "\"unique_id\"           : \"%s_" TARGET "2mqtt\","
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
                     "\"state_topic\"         : \"" TARGET "2mqtt/sensor/%s/state\","
                     "\"name\"                : \"Pool %s\","
                     "\"unique_id\"           : \"%s_" TARGET "2mqtt\""
                     "}",
                     unit, sName.c_str(), title, sName.c_str());
         }

         mqttHassWriter->writeRetained(configTopic, configJson);

         free(configTopic);
         free(configJson);
      }

      mqttHassReader->unsubscribe(stateTopic);
   }

   // publish actual value

   if (!mqttHassWriter->isConnected())
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
   mqttHassWriter->writeRetained(stateTopic, j);

   free(stateTopic);

   return success;
}

//***************************************************************************
// Perform MQTT Requests
//   - check 'mqttHassCommandReader' for commands of a home automation
//   - check 'mqttReader' for data from the W1 service and the arduino, etc ...
//      -> w1mqtt (W1 service) is running localy and provide data of the W1 sensors
//      -> the arduino provide the values of his analog inputs
//***************************************************************************

int Daemon::performMqttRequests()
{
   static time_t lastPoolRead {0};
   static time_t lastMqttRecover {0};

   if (!lastPoolRead)
      lastPoolRead = time(0);

   mqttCheckConnection();

   MemoryStruct message;
   json_error_t error;

   if (!isEmpty(mqttHassUrl) && mqttHassCommandReader->isConnected())
   {
      if (mqttHassCommandReader->read(&message, 10) == success)
      {
         json_t* jData = json_loads(message.memory, 0, &error);

         if (!jData)
         {
            tell(0, "Error: Can't parse json in '%s'", message.memory);
            return fail;
         }

         tell(eloAlways, "<- (%s) [%s]", mqttHassCommandReader->getLastReadTopic().c_str(), message.memory);

         auto it = hassCmdTopicMap.find(mqttHassCommandReader->getLastReadTopic().c_str());

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

   if (!isEmpty(mqttUrl) && mqttReader->isConnected())
   {
      // tell(0, "Try reading topic '%s'", mqttReader->getTopic());

      while (mqttReader->read(&message, 10) == success)
      {
         if (isEmpty(message.memory))
            continue;

         lastPoolRead = time(0);

         std::string tp = mqttReader->getLastReadTopic();
         tell(eloAlways, "<- (%s) [%s] retained %d", tp.c_str(), message.memory, mqttReader->isRetained());

         if (strcmp(tp.c_str(), TARGET "2mqtt/w1") == 0)
            dispatchW1Msg(message.memory);
         else if (strcmp(tp.c_str(), TARGET "2mqtt/arduino/out") == 0)
            dispatchArduinoMsg(message.memory);
      }
   }

   // last successful W1 read at least in last 5 minutes?

   if (lastPoolRead < time(0)-300 && lastMqttRecover < time(0)-60)
   {
      lastMqttRecover = time(0);
      tell(eloAlways, "Error: No update from poolMQTT since '%s', "
           "disconnect from MQTT to force recover", l2pTime(lastPoolRead).c_str());
      mqttDisconnect();
   }

   return success;
}

//***************************************************************************
// Check MQTT Connection
//***************************************************************************

int Daemon::mqttDisconnect()
{
   if (mqttReader)               mqttReader->disconnect();
   if (mqttHassCommandReader)    mqttHassCommandReader->disconnect();
   if (mqttHassWriter)           mqttHassWriter->disconnect();
   if (mqttHassReader)           mqttHassReader->disconnect();

   delete mqttReader;            mqttReader = nullptr;
   delete mqttHassCommandReader; mqttHassCommandReader = nullptr;
   delete mqttHassWriter;        mqttHassWriter = nullptr;
   delete mqttHassReader;        mqttHassReader = nullptr;

   return done;
}

//***************************************************************************
// Check MQTT Connection and Connect
//***************************************************************************

int Daemon::mqttCheckConnection()
{
   bool renonnectNeeded {false};

   if (!mqttReader)
      mqttReader = new Mqtt();

   if (!mqttHassCommandReader)
      mqttHassCommandReader = new Mqtt();

   if (!mqttHassWriter)
      mqttHassWriter = new Mqtt();

   if (!mqttHassReader)
      mqttHassReader = new Mqtt();

   if (!isEmpty(mqttHassUrl) && (!mqttHassCommandReader->isConnected() || !mqttHassWriter->isConnected() || !mqttHassReader->isConnected()))
      renonnectNeeded = true;

   if (!isEmpty(mqttUrl) && !mqttReader->isConnected())
      renonnectNeeded = true;

   if (!renonnectNeeded)
      return success;

   if (lastMqttConnectAt >= time(0) - 60)
      return fail;

   lastMqttConnectAt = time(0);

   if (!isEmpty(mqttHassUrl))
   {
      if (!mqttHassCommandReader->isConnected())
      {
         if (mqttHassCommandReader->connect(mqttHassUrl, mqttHassUser, mqttHassPassword) != success)
         {
            tell(0, "Error: MQTT: Connecting subscriber to '%s' failed", mqttHassUrl);
         }
         else
         {
            mqttHassCommandReader->subscribe(TARGET "2mqtt/light/+/set/#");
            tell(0, "MQTT: Connecting command subscriber to '%s' succeeded", mqttHassUrl);
         }
      }

      if (!mqttHassWriter->isConnected())
      {
         if (mqttHassWriter->connect(mqttHassUrl, mqttHassUser, mqttHassPassword) != success)
            tell(0, "Error: MQTT: Connecting publisher to '%s' failed", mqttHassUrl);
         else
            tell(0, "MQTT: Connecting publisher to '%s' succeeded", mqttHassUrl);
      }

      if (!mqttHassReader->isConnected())
      {
         if (mqttHassReader->connect(mqttHassUrl, mqttHassUser, mqttHassPassword) != success)
            tell(0, "Error: MQTT: Connecting subscriber to '%s' failed", mqttHassUrl);
         else
            tell(0, "MQTT: Connecting subscriber to '%s' succeeded", mqttHassUrl);

         // subscription is done in hassPush
      }
   }

   if (!isEmpty(mqttUrl))
   {
      if (!mqttReader->isConnected())
      {
         if (mqttReader->connect(mqttUrl) != success)
         {
            tell(0, "Error: MQTT: Connecting subscriber to '%s' failed", mqttUrl);
         }
         else
         {
            mqttReader->subscribe(TARGET "2mqtt/w1");
            tell(0, "MQTT: Connecting subscriber to '%s' - '%s' succeeded", mqttUrl, TARGET "2mqtt/w1");
            mqttReader->subscribe(TARGET "2mqtt/arduino/out");
            tell(0, "MQTT: Connecting subscriber to '%s' - '%s' succeeded", mqttUrl, TARGET "2mqtt/arduino/out");
         }
      }
   }

   return success;
}
