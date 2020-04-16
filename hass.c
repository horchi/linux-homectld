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

   // check/prepare reader/writer connection

   if (hassCheckConnection() != success)
      return fail;

   // check if state topic already exists

   std::string message;
   char* stateTopic = 0;
   asprintf(&stateTopic, "poold2mqtt/sensor/%s/state", name);

   mqttReader->subscribe(stateTopic);
   status = mqttReader->read(&message);

   if (status != success && status != MqTTClient::wrnNoMessagePending)
      return fail;

   if (forceConfig || status == MqTTClient::wrnNoMessagePending)
   {
      char* configTopic = 0;
      char* configJson = 0;

      // topic don't exists -> create sensor

      if (strcmp(unit, "°") == 0)
         unit = "°C";
      else if (strcmp(unit, "Heizungsstatus") == 0 ||
               strcmp(unit, "zst") == 0 ||
               strcmp(unit, "T") == 0)
         unit = "";

      tell(1, "Info: Sensor '%s' not found at home assistants MQTT, "
           "sendig config message", name);

      asprintf(&configTopic, "homeassistant/sensor/%s/config", name);
      asprintf(&configJson, "{"
               "\"unit_of_measurement\" : \"%s\","
               "\"value_template\"      : \"{{ value_json.value }}\","
               "\"state_topic\"         : \"poold2mqtt/sensor/%s/state\","
               "\"name\"                : \"%s\","
               "\"unique_id\"           : \"%s_poold2mqtt\""
               "}",
               unit, name, title, name);

      mqttWriter->write(configTopic, configJson);

      free(configTopic);
      free(configJson);
   }

   mqttReader->unsubscribe();

   // publish actual value

   char* valueJson = 0;

   if (!isEmpty(text))
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
