//***************************************************************************
// poold / Linux - Pool Steering
// File hass.c
// This code is distributed under the terms and conditions of the
// GNU GENERAL PUBLIC LICENSE. See the file LICENSE for details.
// Date 16.04.2020  Jörg Wendel
//***************************************************************************

#include "poold.h"

//***************************************************************************
// Push Value to Home Assistant
//***************************************************************************

int Poold::hassPush(const char* name, const char* title, const char* unit,
                  double value, const char* text, bool forceConfig)
{
   int status = success;
   bool light {false};

   if (strcasestr(name, "licht") || strcasestr(name, "light"))  // #TODO - detect better even if title is deit to 'Licht' and title is empty ;)
      light = true;

   // check/prepare reader/writer connection

   if (hassCheckConnection() != success)
      return fail;

   // check if state topic already exists

   std::string message;
   std::string sName = name;

   sName = strReplace("ß", "ss", sName);
   sName = strReplace("ü", "ue", sName);
   sName = strReplace("ö", "oe", sName);
   sName = strReplace("ä", "ae", sName);
   sName = strReplace(" ", "_", sName);

   char* stateTopic = 0;
   asprintf(&stateTopic, "poold2mqtt/%s/%s/state", light ? "light" : "sensor", sName.c_str());

   if (!isEmpty(title))
   {
      mqttReader->subscribe(stateTopic);
      status = mqttReader->read(&message);

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

         asprintf(&configTopic, "homeassistant/%s/pool/%s/config", light ? "light" : "sensor", sName.c_str());

         if (light)
         {
            asprintf(&configJson, "{"
                     "\"state_topic\"         : \"poold2mqtt/light/%s/state\","
                     "\"command_topic\"       : \"poold2mqtt/light/%s/set\","
                     "\"name\"                : \"Pool %s\","
                     "\"unique_id\"           : \"%s_poold2mqtt\","
                     "\"schema\"              : \"json\","
                     "\"brightness\"          : \"false\""
                     "}",
                     sName.c_str(), sName.c_str(), title, sName.c_str());
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

   if (light)
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
// Check MQTT Connection
//***************************************************************************

int Poold::hassCheckConnection()
{
   if (!mqttWriter)
   {
      mqttWriter = new MqTTPublishClient(hassMqttUrl, "poold_publisher");
      mqttWriter->setConnectTimeout(15); // seconds
   }

   if (!mqttReader)
   {
      mqttReader = new MqTTSubscribeClient(hassMqttUrl, "poold_subscriber");
      mqttReader->setConnectTimeout(15); // seconds
      mqttReader->setTimeout(100);       // milli seconds
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
