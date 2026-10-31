//***************************************************************************
// TPMS BLE to MQTT Bridge
//
// Passive BLE scan for the tire pressure valve caps (LYTPMS app, company id 172),
// published to homectld per wheel:
//   TPMS:0x1n pressure [bar], 0x2n temperature [°C], 0x3n battery [%], 0x4n battery [V]
//   (n = wheel 1-9, mapped by the ID printed on the cap, config 'wheels' {"<ID>": <wheel>, ...},
//    0 = not active; with 'learn' each new cap is added with 0 and reported to homectld)
//***************************************************************************

#include <WiFi.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <NimBLEDevice.h>
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
// Cap
//***************************************************************************

constexpr int MaxWheels {9};
constexpr int MaxCaps {16};
constexpr uint16_t CompanyId {0x00ac};

struct Cap
{
   char id[7] {};              // ID printed on the cap, e.g. "318F01", empty = free entry
   int wheel {0};              // 1 - MaxWheels, 0 = not active
   bool received {false};      // got a packet since the start
   bool fresh {false};         // new packet, not published yet
   unsigned long lastTime {0}; // millis of the last packet
   float pressure {0.0};
   int temperature {0};
   float voltage {0.0};
   int battery {0};
   int alarm {0};
   int rssi {0};
   uint8_t raw[8] {};          // data bytes 0-7 of the last packet, the caps repeat each packet ~10 times
};

class TpmsBridge;
static TpmsBridge* bridgeInstance {};

class TpmsBridge : public NimBLEScanCallbacks
{
private:

   WiFiClient EspClient;
   PubSubClient MqttClient;

   Cap capList[MaxCaps];
   portMUX_TYPE capMux = portMUX_INITIALIZER_UNLOCKED;
   bool capsAdded {false};          // learned a new cap, report it to homectld (init message)

   unsigned long LastPublishTime {0};
   unsigned long LastLedToggleTime {0};
   unsigned long LastPacketTime {0};
   unsigned long LastWifiAttempt {0};
   unsigned long LastMqttAttempt {0};
   bool LedState {false};
   bool initialRun {true};
   bool otaStarted {false};

public:

   TpmsBridge() : MqttClient(EspClient)
   {
      bridgeInstance = this;
   }

   void Setup()
   {
      pinMode(StatusLedPin, OUTPUT);
      digitalWrite(StatusLedPin, LOW);

      Serial.begin(115200);
      tell(eloAlways, "\n[BOOT] ESP32 TPMS Bridge startet...");

      // Bluetooth ZUERST (schützt den Funkchip vor dem Lockup)

      tell(eloAlways, "[BOOT] Initialisiere Bluetooth...");
      NimBLEDevice::init("");

      // WLAN und MQTT werden im Loop aufgebaut, der Scan laeuft auch ohne
      //   (die Werte stehen dann nur auf der seriellen Konsole)

      MqttClient.setServer(mqttServer, mqttPort);
      MqttClient.setCallback([this](char* topic, byte* payload, unsigned int length) {
         this->mqttCallback(topic, payload, length);
      });

      // passive scan, every advert (no duplicate filter), endless

      NimBLEScan* scan {NimBLEDevice::getScan()};
      scan->setScanCallbacks(this, true);
      scan->setActiveScan(false);
      scan->setInterval(100);
      scan->setWindow(50);      // 50 %, WLAN and BLE share the radio (the caps repeat each packet ~10 times)
      scan->setMaxResults(0);
      scan->start(0, false, true);

      tell(eloAlways, "System erfolgreich gebootet, Scan laeuft");
   }

   void Loop()
   {
      if (ConnectToWiFi())
      {
         ArduinoOTA.handle();

         if (!MqttClient.connected())
            ConnectToMqtt();
         else
            MqttClient.loop();
      }

      UpdateLedBlink();

      // the scan may stop (e.g. after an error of the controller) - restart it

      if (!NimBLEDevice::getScan()->isScanning())
      {
         tell(eloInfo, "Scan stand, starte neu");
         NimBLEDevice::getScan()->start(0, false, true);
      }

      unsigned long currentMillis {millis()};
      bool cyclic {currentMillis - LastPublishTime >= publishInterval * 1000};

      if (cyclic)
         LastPublishTime = currentMillis;

      for (int i = 0; i < MaxCaps; i++)
      {
         Cap c;
         portENTER_CRITICAL(&capMux);
         c = capList[i];
         capList[i].fresh = false;
         portEXIT_CRITICAL(&capMux);

         if (!c.id[0])
            continue;

         if (c.fresh && !c.wheel)
            tell(eloDetail, "Kappe %s (nicht aktiv): %.2f bar, %d °C, %d dBm", c.id, c.pressure, c.temperature, c.rssi);
         else if (c.wheel && (c.fresh || (cyclic && c.received)))
            publishWheel(c);
      }

      if (capsAdded && MqttClient.connected())
      {
         capsAdded = false;
         publishInitMessage();   // homectld adds the new caps to its config
      }

      if (cyclic)
         initialRun = false;

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

      if ((eloquence & elo) == elo && MqttClient.connected())
      {
         JsonDocument doc;
         doc["type"] = sensorType;
         doc["action"] = "log";
         doc["level"] = elo;
         doc["message"] = messageBuffer;
         doc["timestamp"] = millis() / 1000;

         String outputStr;
         serializeJson(doc, outputStr);
         MqttClient.publish(TopicLog, outputStr.c_str());
      }
   }

   //***************************************************************************
   // BLE Scan Callback (runs in the NimBLE task!)
   //   manufacturer data: company id (2 byte, little endian) + 15 byte
   //     0     battery V = b0 * 0.01 + 1.22
   //     1, 6  pressure bar = (b1 + 256 * b6) * 0.03144
   //     2     temperature °C = b2 - 55
   //     3     alarm
   //     4, 5  battery mV (big endian)
   //     9-14  MAC reversed
   //   the address is the ID printed on the cap + 11:11:11 (318F01 -> 31:8F:01:11:11:11)
   //***************************************************************************

   void onResult(const NimBLEAdvertisedDevice* dev) override
   {
      std::string md {dev->getManufacturerData()};

      if (md.size() < 2 + 8)
         return;

      const uint8_t* d {(const uint8_t*)md.data()};

      if ((d[0] | (d[1] << 8)) != CompanyId)
         return;

      d += 2;

      const uint8_t* mac {dev->getAddress().getVal()};   // little endian, mac[5] is the first byte
      char id[7];
      snprintf(id, sizeof(id), "%02X%02X%02X", mac[5], mac[4], mac[3]);

      float pressure {(d[1] + 256 * d[6]) * 0.03144f};
      float voltage {d[0] * 0.01f + 1.22f};
      int battery {(int)roundf((voltage - 1.95f) * 100.0f)};   // matches the LYTPMS app within 2-3 %

      battery = battery < 0 ? 0 : battery > 100 ? 100 : battery;
      LastPacketTime = millis();

      portENTER_CRITICAL(&capMux);

      int i {capOf(id)};

      if (i < 0 && learn)
      {
         // learn the new cap, not active until a wheel is set in the config

         for (i = 0; i < MaxCaps && capList[i].id[0]; i++) ;

         if (i < MaxCaps)
         {
            capList[i] = Cap();
            strcpy(capList[i].id, id);
            capsAdded = true;
         }
         else
            i = -1;   // list full
      }

      if (i >= 0)
      {
         Cap& w {capList[i]};

         if (!w.received || memcmp(w.raw, d, sizeof(w.raw)) != 0)
            w.fresh = true;

         memcpy(w.raw, d, sizeof(w.raw));
         w.received = true;
         w.lastTime = millis();
         w.pressure = roundf(pressure * 100.0f) / 100.0f;
         w.temperature = d[2] - 55;
         w.voltage = roundf(voltage * 100.0f) / 100.0f;
         w.battery = battery;
         w.alarm = d[3];
         w.rssi = dev->getRSSI();
      }

      portEXIT_CRITICAL(&capMux);
   }

   void onScanEnd(const NimBLEScanResults& results, int reason) override
   {
      NimBLEDevice::getScan()->start(0, false, true);
   }

private:

   // call with capMux taken

   int capOf(const char* id)
   {
      for (int i = 0; i < MaxCaps; i++)
         if (capList[i].id[0] && strcmp(capList[i].id, id) == 0)
            return i;

      return -1;
   }

   //***************************************************************************
   // Parse Wheels - {"318F01": 1, "31953C": 0, ...}
   //   the received data of the caps is kept, caps missing in the config are dropped
   //***************************************************************************

   void parseWheels(JsonObject wheels)
   {
      Cap newList[MaxCaps];
      int count {0}, active {0};
      bool used[MaxWheels+1] {};

      for (JsonPair kv : wheels)
      {
         String id {kv.key().c_str()};
         int n {kv.value().as<int>()};
         id.trim();
         id.toUpperCase();

         if (id.length() != 6 || n < 0 || n > MaxWheels)
         {
            tell(eloAlways, "Error: Ungueltiger Eintrag '%s: %d' in 'wheels' (ID 6-stellig, Rad 0-%d)", kv.key().c_str(), n, MaxWheels);
            continue;
         }

         if (n && used[n])
         {
            tell(eloAlways, "Error: Rad %d in 'wheels' mehrfach vergeben, '%s' ignoriert", n, id.c_str());
            n = 0;
         }

         if (count >= MaxCaps)
         {
            tell(eloAlways, "Error: Mehr als %d Kappen in 'wheels', '%s' ignoriert", MaxCaps, id.c_str());
            continue;
         }

         portENTER_CRITICAL(&capMux);
         int i {capOf(id.c_str())};

         if (i >= 0)
            newList[count] = capList[i];

         portEXIT_CRITICAL(&capMux);

         // a new / changed wheel is published at once (not with the next packet / cycle)

         if (newList[count].wheel != n && newList[count].received)
            newList[count].fresh = true;

         strcpy(newList[count].id, id.c_str());
         newList[count].wheel = n;
         used[n] = true;
         count++;

         if (n)
            active++;
      }

      portENTER_CRITICAL(&capMux);

      for (int i = 0; i < MaxCaps; i++)
         capList[i] = newList[i];

      portEXIT_CRITICAL(&capMux);

      tell(eloAlways, "Info: %d Kappe(n), %d Rad/Raeder aktiv", count, active);
   }

   //***************************************************************************
   // Publish
   //***************************************************************************

   void publishWheel(const Cap& w)
   {
      int n {w.wheel};
      bool valid {millis() - w.lastTime < maxAge * 1000};
      char title[30];

      snprintf(title, sizeof(title), "Rad %d Druck", n);
      PublishValue(0x10 + n, title, w.pressure, "bar", valid);
      snprintf(title, sizeof(title), "Rad %d Temperatur", n);
      PublishValue(0x20 + n, title, w.temperature, "°C", valid);
      snprintf(title, sizeof(title), "Rad %d Batterie", n);
      PublishValue(0x30 + n, title, w.battery, "%", valid);
      snprintf(title, sizeof(title), "Rad %d Batteriespannung", n);
      PublishValue(0x40 + n, title, w.voltage, "V", valid);

      if (w.fresh)
         tell(eloDetail, "Rad %d (%s): %.2f bar, %d °C, %.2f V (%d %%), Alarm %d, %d dBm",
              n, w.id, w.pressure, w.temperature, w.voltage, w.battery, w.alarm, w.rssi);
   }

   void PublishValue(int address, const char* title, float value, const char* unit, bool valid)
   {
      JsonDocument doc;
      doc["type"] = sensorType;
      doc["address"] = address;
      doc["value"] = value;
      doc["kind"] = "value";
      doc["title"] = title;
      doc["unit"] = unit;

      if (!valid)
         doc["valid"] = false;

      if (initialRun)
      {
         JsonObject param {doc["parameter"].to<JsonObject>()};
         param["widgettype"] = 3;
      }

      String outputStr;
      serializeJson(doc, outputStr);
      MqttClient.publish(TopicPublish, outputStr.c_str());
   }

   void publishInitMessage()
   {
      JsonDocument doc;
      doc["type"] = sensorType;
      doc["action"] = "init";
      doc["deviceid"] = getUniqueDeviceId();
      doc["topic"] = TopicSubscribe;
      doc["config"] = true;   // we accept a config packet

      JsonObject parameters {doc["parameters"].to<JsonObject>()};

      parameters["eloquence"] = eloquence;
      parameters["interval"] = publishInterval;
      parameters["maxAge"] = maxAge;
      parameters["learn"] = learn;

      JsonObject wheels {parameters["wheels"].to<JsonObject>()};

      portENTER_CRITICAL(&capMux);
      Cap list[MaxCaps];

      for (int i = 0; i < MaxCaps; i++)
         list[i] = capList[i];

      portEXIT_CRITICAL(&capMux);

      for (int i = 0; i < MaxCaps; i++)
         if (list[i].id[0])
            wheels[list[i].id] = list[i].wheel;

      String outputStr;
      serializeJson(doc, outputStr);
      tell(eloInfo, "Info: -> '%s'", outputStr.c_str());
      MqttClient.publish(TopicPublish, outputStr.c_str());
   }

   //***************************************************************************
   // OTA (Firmware Update ueber WLAN, siehe 'make upload-ota')
   //***************************************************************************

   void SetupOta()
   {
      ArduinoOTA.setHostname(OtaHostname);

      if (OtaPassword && *OtaPassword)
         ArduinoOTA.setPassword(OtaPassword);

      ArduinoOTA.onStart([this]() { tell(eloAlways, "OTA: Update startet ..."); });
      ArduinoOTA.onEnd([this]() { tell(eloAlways, "OTA: Update fertig, Neustart"); });
      ArduinoOTA.onError([this](ota_error_t error) { tell(eloAlways, "OTA: Fehler %u", error); });

      ArduinoOTA.begin();
      tell(eloAlways, "[BOOT] OTA bereit als '%s' auf %s", OtaHostname, WiFi.localIP().toString().c_str());
   }

   //***************************************************************************
   // LED
   //   WLAN fehlt 1 s, MQTT fehlt 500 ms, verbunden: an, kurz aus bei jedem Paket eines Sensors
   //***************************************************************************

   void UpdateLedBlink()
   {
      unsigned long currentMillis {millis()};
      unsigned long interval {1000};

      if (WiFi.status() == WL_CONNECTED && MqttClient.connected())
      {
         analogWrite(StatusLedPin, currentMillis - LastPacketTime < 100 ? 0 : LedBrightness);
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

   // not blocking, the scan goes on without WLAN

   bool ConnectToWiFi()
   {
      if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0,0,0,0))
      {
         if (!otaStarted)
         {
            tell(eloAlways, "WLAN '%s' verbunden, IP %s", WifiSsid, WiFi.localIP().toString().c_str());
            SetupOta();
            otaStarted = true;
         }

         return true;
      }

      if (!LastWifiAttempt || millis() - LastWifiAttempt >= 30000)
      {
         LastWifiAttempt = millis();
         tell(eloAlways, "Verbinde mit WLAN '%s' (Status %d) ...", WifiSsid, WiFi.status());
         WiFi.disconnect();
         WiFi.begin(WifiSsid, WifiPassword);
      }

      return false;
   }

   void ConnectToMqtt()
   {
      if (LastMqttAttempt && millis() - LastMqttAttempt < 10000)
         return;

      LastMqttAttempt = millis();

      if (!MqttClient.connected())
      {
         tell(eloAlways, "Verbinde mit MQTT Broker %s ...", mqttServer);
         MqttClient.setBufferSize(1024);

         String clientId {"ESP32_Tpms"};
         clientId += String((uint32_t)ESP.getEfuseMac(), HEX);

         if (MqttClient.connect(clientId.c_str()))
         {
            MqttClient.subscribe(TopicSubscribe);
            publishInitMessage();
            initialRun = true;
         }
      }
   }

   void mqttCallback(char* topic, byte* payload, unsigned int length)
   {
      // {"type": "TPMS", "action": "init", "config": {"eloquence": 1, "interval": 60, "maxAge": 1800, "learn": true, "wheels": {"318F01": 1}} }
      // {"type": "TPMS", "action": "requestinit"}

      JsonDocument doc;

      if (deserializeJson(doc, payload, length))
         return;

      if (doc["action"] == "init")
      {
         JsonObject config {doc["config"]};

         if (config.isNull())
            return;

         if (config["eloquence"].is<int>())
            eloquence = config["eloquence"].as<int>();

         if (config["interval"].is<int>())
            publishInterval = config["interval"].as<int>();

         if (config["maxAge"].is<int>())
            maxAge = config["maxAge"].as<int>();

         if (config["learn"].is<bool>())
            learn = config["learn"].as<bool>();

         if (config["wheels"].is<JsonObject>())
            parseWheels(config["wheels"].as<JsonObject>());

         tell(eloInfo, "Info: INIT: Eloquence %d; Interval %lu s; MaxAge %lu s; Learn %s",
              eloquence, publishInterval, maxAge, learn ? "an" : "aus");
      }
      else if (doc["action"] == "requestinit")
      {
         publishInitMessage();
      }
   }
};

TpmsBridge tpmsBridge;

void setup()
{
   tpmsBridge.Setup();
}

void loop()
{
   tpmsBridge.Loop();
}
