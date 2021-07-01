//***************************************************************************
// poold / Arduino Sketch
// File main.ino
// This code is distributed under the terms and conditions of the
// GNU GENERAL PUBLIC LICENSE. See the file LICENSE for details.
// Date 17.09.2020 - Jörg Wendel
//***************************************************************************

#include <WiFiNINA.h>
#include <MQTT.h>
#include <ArduinoJson.h>

#include "settings.h"           // set your WiFi an MQTT connection data here!

const char* topicIn  {"poold2mqtt/arduino/in"};
const char* topicOut {"poold2mqtt/arduino/out"};

int ledPin {LED_BUILTIN};
unsigned int updateInterval {60 * 1000};  // update/push sensor data every x seconds
unsigned long lastUpdateAt {0};

WiFiClient net;
MQTTClient mqtt(1024);

const byte inputCount {7};
unsigned int sampleCount[inputCount] {0};
unsigned int sampleSum[inputCount] {0};

//***************************************************************************
// RGB LED
//***************************************************************************

void rgbLed(byte r, byte g, byte b)
{
   WiFiDrv::analogWrite(25, r);   // green
   WiFiDrv::analogWrite(26, g);   // red
   WiFiDrv::analogWrite(27, b);   // blue
}

//***************************************************************************
// Setup
//***************************************************************************

void setup()
{
   WiFiDrv::pinMode(25, OUTPUT);
   WiFiDrv::pinMode(26, OUTPUT);
   WiFiDrv::pinMode(27, OUTPUT);
   digitalWrite(ledPin, LOW);
   rgbLed(20, 0, 0);
   analogReadResolution(12);

   unsigned long startAt = millis();
   Serial.begin(115200);

   while (!Serial)
   {
      if (startAt < millis() - 5*1000)
         break;
   }

   // #> sudo minicom -D /dev/ttyACM0 -b 115200

   connect();
}

//******************************************************************
// Connect WiFi and MQTT
//******************************************************************

void connect()
{
   if (Serial) Serial.println("Connecting to WiFi '" + String(wifiSsid) + "' ..");

   WiFi.disconnect();
   delay(500);

   while (WiFi.begin(wifiSsid, wifiPass) != WL_CONNECTED)
   {
      if (Serial) Serial.print(".");
      static byte r {0};
      r = r ? 0 : 20;
      rgbLed(r, 0, 0);

      delay(200);
   }

   if (Serial) Serial.println("\nconnected to WiFi!");
   mqtt.begin(mqttServer, mqttPort, net);
   if (Serial) Serial.println("Connecting to MQTT broker '" + String(mqttServer) + ":" + mqttPort + "' ..");

   while (!mqtt.connect("poold-arduino", key, secret))
   {
      // if (Serial) Serial.print(".");
      static byte b {0};
      b = b ? 0 : 20;
      rgbLed(0, 0, b);

      delay(300);
   }

   if (Serial) Serial.println("connected to MQTT broker");

   mqtt.onMessage(atMessage);
   mqtt.subscribe(topicIn);
}

//******************************************************************
// Integer To Analog Pin
//******************************************************************

int intToA(int num)
{
   switch (num)
   {
      case 0: return A0;
      case 1: return A1;
      case 2: return A2;
      case 3: return A3;
      case 4: return A4;
      case 5: return A5;
      case 6: return A6;
   }

   return A0; // ??
}

//***************************************************************************
// Loop
//***************************************************************************

unsigned long nextAt = 0;

void loop()
{
   mqtt.loop();

   if (nextAt > millis())
      return;

   // we pass here every 500ms

   nextAt = millis() + 500;    // schedule next in 500ms

   if (!mqtt.connected() || !net.connected())
   {
      digitalWrite(ledPin, LOW);
      connect();
   }

   static byte g {0};
   g = g ? 0 : 20;
   rgbLed(0, g, 0);
   digitalWrite(ledPin, HIGH);

   update();

   if (millis() - lastUpdateAt > updateInterval)
      send();

   mqtt.loop();
}

//***************************************************************************
// Update
//***************************************************************************

void update()
{
   for (int inp = 0; inp < inputCount; inp++)
   {
      sampleSum[inp] += analogRead(intToA(inp));
      sampleCount[inp]++;
   }
}

//***************************************************************************
// Send
//***************************************************************************

void send()
{
   if (!mqtt.connected())
      return ;

   lastUpdateAt = millis();
   digitalWrite(ledPin, LOW);

   StaticJsonDocument<1024> doc;

   for (int inp = 0; inp < inputCount; inp++)
   {
      unsigned int avg = sampleSum[inp] / sampleCount[inp];
      sampleSum[inp] = 0;
      sampleCount[inp] = 0;

      doc[inp]["name"] = String("A") + inp;
      doc[inp]["value"] = avg;
   }

   String outString;
   serializeJson(doc, outString);

   if (!mqtt.publish(topicOut, outString, true, 0))
   {
      if (Serial)
         Serial.println("Failed to publish message " + String(mqtt.lastError()) + " : " + String(mqtt.returnCode()));
   }

   if (Serial) Serial.println(outString);
}

//***************************************************************************
// At Message
//***************************************************************************

void atMessage(String& topic, String& payload)
{
   if (Serial) Serial.println("incoming: " + topic + " - " + payload);

   StaticJsonDocument<512> root;
   deserializeJson(root, payload);

   const char* command = root["command"];
   String parameter = root["parameter"];

   if (strcmp(command, "setUpdateInterval") == 0)
      updateInterval = parameter.toInt() * 1000;
   else if (strcmp(command, "triggerUpdate") == 0)
      send();
}
