
//***************************************************************************
// ESP32 Kenwood Bridge
//
//  Steuert ein Kenwood Autoradio (z.B. DMX8019DABS) ueber den Lenkrad-
//  Fernbedienungs-Eingang (hellblau/gelb). Das Radio erwartet dort das
//  NEC Protokoll der IR Fernbedienung, invertiert und ohne 38kHz Traeger:
//  der Draht liegt im Radio auf 3.3V und wird fuer einen 'mark' nach Masse
//  gezogen. Rueckmeldung 'Radio an' ueber den P.CONT Ausgang (12V wenn an).
//
//  MQTT Schnittstelle nach dem Muster von alpicool/alpicool.ino
//***************************************************************************

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <stdarg.h>
#include "config.h"

String getUniqueDeviceId()
{
   uint64_t chipId {ESP.getEfuseMac()};
   char idBuffer[20];

   snprintf(idBuffer, sizeof(idBuffer), "%04X%08X",
            (uint16_t)(chipId >> 32),
            (uint32_t)chipId);

   return String(idBuffer);
}

//***************************************************************************
// Tasten (Adressen der homectld Sensoren)
//
//   Der NEC Code jeder Taste kann per MQTT init Paket geaendert werden, damit
//   Codes die fuer das DMX noch nicht bekannt sind (-1) ohne neues Flashen
//   nachgetragen werden koennen.
//***************************************************************************

struct Key
{
   int address;
   const char* title;
   const char* param;     // Name des Parameters im init/config Paket
   const char* symbol;
   int code;              // NEC Kommando, -1 = unbekannt
};

Key keys[] {
   {  1, "Volume +",   "codeVolumeUp",   "mdi:mdi-volume-plus",     0x14 },
   {  2, "Volume -",   "codeVolumeDown", "mdi:mdi-volume-minus",    0x15 },
   {  3, "ATT",        "codeAtt",        "mdi:mdi-volume-mute",     0x16 },
   {  4, "Source",     "codeSource",     "mdi:mdi-swap-horizontal", 0x13 },
   {  5, "Track +",    "codeTrackUp",    "mdi:mdi-skip-next",       0x0B },
   {  6, "Track -",    "codeTrackDown",  "mdi:mdi-skip-previous",   0x0A },
   {  7, "Play/Pause", "codePlayPause",  "mdi:mdi-play-pause",      0x0E },
   {  8, "Answer",     "codeAnswer",     "mdi:mdi-phone",           -1 },
   {  9, "Hang up",    "codeHangUp",     "mdi:mdi-phone-hangup",    -1 },
   { 10, "Voice",      "codeVoice",      "mdi:mdi-microphone",      -1 }
};

constexpr int AddressPower {0};
int codePower {-1};                    // NEC Code fuer Power (Standby) Taste, -1 = unbekannt

//***************************************************************************
// Class KenwoodBridge
//***************************************************************************

class KenwoodBridge
{
private:

   WiFiClient EspClient;
   PubSubClient MqttClient;

   bool rmtReady {false};
   bool relayState {false};

   bool powerState {false};            // entprellter Zustand von P.CONT
   bool powerRaw {false};
   unsigned long powerRawSince {0};
   bool powerKnown {false};

   unsigned long LastPublishTime {0};
   unsigned long LastLedToggleTime {0};
   bool LedState {false};
   bool initialRun {true};

public:

   KenwoodBridge() : MqttClient(EspClient) {}

   void Setup()
   {
      pinMode(StatusLedPin, OUTPUT);
      digitalWrite(StatusLedPin, LOW);

      Serial.begin(115200);
      tell(eloAlways, "\n[BOOT] ESP32 Kenwood Bridge startet...");

      // Remote Ausgang zuerst in Ruhe legen (Draht darf nie aktiv auf High getrieben werden,
      // daher immer ueber Transistor / Open-Collector!)

      pinMode(RemotePin, OUTPUT);
      digitalWrite(RemotePin, !RemoteMarkLevel);

      if (AccRelayPin >= 0)
      {
         pinMode(AccRelayPin, OUTPUT);
         digitalWrite(AccRelayPin, !AccRelayOnLevel);
      }

      analogSetPinAttenuation(PowerSensePin, ADC_11db);   // bis ca. 3.1V messbar

      // RMT fuer das NEC Timing (1 MHz -> 1 Tick = 1 us)

      rmtReady = rmtInit(RemotePin, RMT_TX_MODE, RMT_MEM_NUM_BLOCKS_2, 1000000);

      if (rmtReady)
         rmtSetEOT(RemotePin, !RemoteMarkLevel);
      else
         tell(eloAlways, "Error: RMT init auf Pin %d fehlgeschlagen", RemotePin);

      tell(eloAlways, "[BOOT] Verbinde mit WLAN ...");
      ConnectToWiFi();

      MqttClient.setServer(mqttServer, mqttPort);
      MqttClient.setCallback([this](char* topic, byte* payload, unsigned int length) {
         this->mqttCallback(topic, payload, length);
      });

      tell(eloAlways, "[BOOT] Verbinde mit MQTT Broker ...");
      ConnectToMqtt();
      tell(eloAlways, "System erfolgreich gebootet. WLAN und MQTT sind bereit");
   }

   void Loop()
   {
      ConnectToWiFi();

      if (!MqttClient.connected())
      {
         ConnectToMqtt();
         UpdateLedBlink();
         delay(100);
         return;
      }

      MqttClient.loop();
      UpdateLedBlink();

      if (checkPower())
         publishPower();

      unsigned long currentMillis {millis()};

      if (currentMillis - LastPublishTime >= publishInterval * 1000)
      {
         LastPublishTime = currentMillis;
         publishAll();
         initialRun = false;
      }

      delay(10);
   }

   void tell(int elo, const char* format, ...)
   {
      char messageBuffer[256] {};
      va_list args;
      va_start(args, format);
      vsnprintf(messageBuffer, sizeof(messageBuffer), format, args);
      va_end(args);

      Serial.println(messageBuffer);

      if ((eloquence & elo) && MqttClient.connected())
      {
         StaticJsonDocument<384> doc;
         doc["type"] = "kenwood";
         doc["action"] = "log";
         doc["level"] = elo;
         doc["message"] = messageBuffer;
         doc["timestamp"] = millis() / 1000;

         String outputStr;
         serializeJson(doc, outputStr);
         MqttClient.publish(TopicLog, outputStr.c_str());
      }
   }

private:

   //***************************************************************************
   // NEC Frame senden
   //
   //   9ms mark, 4.5ms space, 32 Bit LSB first: Adresse, ~Adresse, Kommando,
   //   ~Kommando, Bit 0 = 560us mark + 560us space, Bit 1 = 560us mark + 1690us
   //   space, Abschluss 560us mark.
   //***************************************************************************

   bool sendFrame(uint8_t command)
   {
      if (!rmtReady)
      {
         tell(eloAlways, "Error: RMT nicht bereit, Code 0x%02X nicht gesendet", command);
         return false;
      }

      uint32_t data {(uint32_t)NecAddress
            | ((uint32_t)(uint8_t)~NecAddress << 8)
            | ((uint32_t)command << 16)
            | ((uint32_t)(uint8_t)~command << 24)};

      rmt_data_t items[34] {};
      int n {0};

      items[n].level0 = RemoteMarkLevel;  items[n].duration0 = 9000;
      items[n].level1 = !RemoteMarkLevel; items[n].duration1 = 4500;
      n++;

      for (int bit {0}; bit < 32; bit++)
      {
         bool one {(data >> bit) & 1};
         items[n].level0 = RemoteMarkLevel;  items[n].duration0 = 560;
         items[n].level1 = !RemoteMarkLevel; items[n].duration1 = one ? 1690 : 560;
         n++;
      }

      items[n].level0 = RemoteMarkLevel;  items[n].duration0 = 560;
      items[n].level1 = !RemoteMarkLevel; items[n].duration1 = 560;
      n++;

      return rmtWrite(RemotePin, items, n, RMT_WAIT_FOR_EVER);
   }

   void sendKey(int code, int count, const char* title)
   {
      if (code < 0 || code > 0xFF)
      {
         tell(eloAlways, "Warning: Code fuer '%s' unbekannt, bitte per Config setzen", title);
         return;
      }

      count = constrain(count, 1, 20);
      tell(eloInfo, "Info: Sende '%s' (0x%02X) %dx", title, code, count);

      for (int i {0}; i < count; i++)
      {
         if (i)
            delay(keyGap);

         sendFrame((uint8_t)code);
      }
   }

   //***************************************************************************
   // Power (P.CONT lesen, Relais, Standby Code)
   //***************************************************************************

   bool readPowerRaw()
   {
      uint32_t sum {0};

      for (int i {0}; i < 8; i++)
         sum += analogReadMilliVolts(PowerSensePin);

      bool high {(int)(sum / 8) > powerThreshold};

      return PowerSenseInvert ? !high : high;
   }

   // liefert true wenn sich der entprellte Zustand geaendert hat

   bool checkPower()
   {
      bool raw {readPowerRaw()};
      unsigned long now {millis()};

      if (raw != powerRaw)
      {
         powerRaw = raw;
         powerRawSince = now;
      }

      if (now - powerRawSince < 1000)
         return false;

      if (!powerKnown || raw != powerState)
      {
         powerState = raw;
         powerKnown = true;
         tell(eloInfo, "Info: Radio ist %s", powerState ? "AN" : "AUS");
         return true;
      }

      return false;
   }

   void setPower(bool on)
   {
      tell(eloInfo, "Info: Power Soll %s, Ist %s", on ? "AN" : "AUS", powerState ? "AN" : "AUS");

      if (AccRelayPin >= 0 && relayState != on)
      {
         relayState = on;
         digitalWrite(AccRelayPin, on ? AccRelayOnLevel : !AccRelayOnLevel);
         tell(eloInfo, "Info: ACC Relais %s", on ? "AN" : "AUS");
         return;
      }

      if (on == powerState)
         return;

      if (codePower < 0)
      {
         tell(eloAlways, "Warning: Power Code unbekannt und kein ACC Relais, kann Radio nicht %s", on ? "ein" : "aus");
         return;
      }

      sendKey(codePower, 1, "Power");
   }

   //***************************************************************************
   // Publish
   //***************************************************************************

   void publishAll()
   {
      publishPower();

      for (const Key& key : keys)
         publishKey(key);
   }

   void publishPower()
   {
      PublishStatus(AddressPower, "Radio", powerState, "mdi:mdi-power", "gray", "green", powerKnown);
   }

   void publishKey(const Key& key)
   {
      PublishStatus(key.address, key.title, false, key.symbol, "gray", "gray", true);
   }

   void PublishStatus(int address, const char* title, bool state, const char* symbol,
                      const char* color, const char* colorOn, bool valid)
   {
      StaticJsonDocument<512> doc;
      doc["type"] = sensorType;
      doc["address"] = address;
      doc["state"] = state;
      doc["kind"] = "status";
      doc["title"] = title;

      if (!valid)
         doc["valid"] = false;

      if (initialRun)
      {
         doc["rights"] = 2;    // urControl

         JsonObject param {doc.createNestedObject("parameter")};

         param["widgettype"] = 0;
         param["symbol"] = symbol;
         param["symbolOn"] = symbol;
         param["color"] = color;
         param["colorOn"] = colorOn;
      }

      String outputStr; serializeJson(doc, outputStr);
      MqttClient.publish(TopicPublish, outputStr.c_str());
   }

   void publishInitMessage()
   {
      StaticJsonDocument<768> doc;
      doc["type"] = sensorType;
      doc["action"] = "init";
      doc["deviceid"] = getUniqueDeviceId();
      doc["topic"] = TopicSubscribe;
      doc["config"] = true;   // we accept a config packet

      JsonObject parameters = doc["parameters"].to<JsonObject>();

      parameters["eloquence"] = eloquence;
      parameters["interval"] = publishInterval;
      parameters["keyGap"] = keyGap;
      parameters["powerThreshold"] = powerThreshold;
      parameters["codePower"] = codePower;

      for (const Key& key : keys)
         parameters[key.param] = key.code;

      String outputStr;
      serializeJson(doc, outputStr);
      tell(eloInfo, "Info: -> '%s'", outputStr.c_str());
      MqttClient.publish(TopicPublish, outputStr.c_str());
   }

   //***************************************************************************
   // MQTT Callback
   //
   //   {"type": "KENWOOD", "action": "init", "config": {"eloquence": 3, "codeAnswer": 28}}
   //   {"type": "KENWOOD", "action": "requestinit"}
   //   {"type": "KENWOOD", "action": "raw", "code": 29, "count": 1}      Code zum Testen senden
   //   {"type": "KENWOOD", "address": 1, "value": 3}                    Taste (hier 3x Volume +)
   //   {"type": "KENWOOD", "address": 0, "value": 1}                    Radio an
   //***************************************************************************

   void mqttCallback(char* topic, byte* payload, unsigned int length)
   {
      StaticJsonDocument<768> doc;

      char* jsonStr {new char[length + 1]()};
      memcpy(jsonStr, payload, length);

      DeserializationError res {deserializeJson(doc, jsonStr)};
      delete[] jsonStr;

      if (res)
      {
         tell(eloAlways, "Error: Ungueltiges JSON empfangen");
         return;
      }

      const char* action {doc["action"] | ""};

      if (strcmp(action, "init") == 0)
      {
         JsonObject config {doc["config"]};

         if (config.isNull())
            return;

         if (config.containsKey("eloquence"))
            eloquence = config["eloquence"].as<int>();

         if (config.containsKey("interval"))
            publishInterval = config["interval"].as<int>();

         if (config.containsKey("keyGap"))
            keyGap = config["keyGap"].as<int>();

         if (config.containsKey("powerThreshold"))
            powerThreshold = config["powerThreshold"].as<int>();

         if (config.containsKey("codePower"))
            codePower = config["codePower"].as<int>();

         for (Key& key : keys)
         {
            if (config.containsKey(key.param))
               key.code = config[key.param].as<int>();
         }

         tell(eloInfo, "Info: INIT: Eloquence %d; Interval %lu s; keyGap %d ms; powerThreshold %d mV; codePower %d",
              eloquence, publishInterval, keyGap, powerThreshold, codePower);
      }
      else if (strcmp(action, "requestinit") == 0)
      {
         publishInitMessage();
      }
      else if (strcmp(action, "raw") == 0)
      {
         int code {doc["code"] | -1};
         int count {doc["count"] | 1};

         sendKey(code, count, "raw");
      }
      else
      {
         // process commands

         int address {doc["address"] | -1};
         JsonVariant valueVariant {doc["value"]};
         int value {1};

         if (valueVariant.is<int>() || valueVariant.is<bool>())
            value = valueVariant.as<int>();
         else if (valueVariant.is<const char*>())
            value = atoi(valueVariant.as<const char*>());

         if (address == AddressPower)
         {
            setPower(value != 0);
            return;
         }

         for (const Key& key : keys)
         {
            if (key.address == address)
            {
               sendKey(key.code, value, key.title);
               publishKey(key);     // Taste hat keinen Zustand, immer wieder 'aus' melden
               return;
            }
         }

         tell(eloAlways, "Warning: Unbekannte Adresse %d", address);
      }
   }

   //***************************************************************************
   // LED / WiFi / MQTT
   //***************************************************************************

   void UpdateLedBlink()
   {
      unsigned long currentMillis {millis()};
      unsigned long interval {1000};

      if (WiFi.status() == WL_CONNECTED && MqttClient.connected())
      {
         analogWrite(StatusLedPin, LedBrightness);
         return;
      }
      else if (WiFi.status() == WL_CONNECTED)
      {
         interval = 500;
      }

      if (currentMillis - LastLedToggleTime >= interval)
      {
         LastLedToggleTime = currentMillis;
         LedState = !LedState;

         analogWrite(StatusLedPin, LedState ? LedBrightness : 0);
      }
   }

   void ConnectToWiFi()
   {
      if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0,0,0,0))
         return;

      WiFi.begin(WifiSsid, WifiPassword);

      while (WiFi.status() != WL_CONNECTED || WiFi.localIP() == IPAddress(0,0,0,0))
      {
         UpdateLedBlink();
         delay(100);
      }
   }

   void ConnectToMqtt()
   {
      if (!MqttClient.connected())
      {
         MqttClient.setBufferSize(1024);

         String clientId {"ESP32_Kenwood"};
         clientId += String((uint32_t)ESP.getEfuseMac(), HEX);

         if (MqttClient.connect(clientId.c_str()))
         {
            MqttClient.subscribe(TopicSubscribe);
            publishInitMessage();
            initialRun = true;         // Widget Parameter nach Reconnect erneut mitsenden
            LastPublishTime = 0;
         }
      }
   }
};

//***************************************************************************
// Arduino
//***************************************************************************

KenwoodBridge bridge;

void setup()
{
   bridge.Setup();
}

void loop()
{
   bridge.Loop();
}
