//***************************************************************************
// TPMS BLE to MQTT Bridge
//
// Passive BLE scan for the tire pressure valve caps (LYTPMS app, company id 172),
// published to homectld per cap by the ID printed on it (e.g. 318C4B):
//   TPMS:0x<ID>1 pressure [bar] (with the battery [%]), TPMS:0x<ID>2 temperature [°C]
// Only the caps of the config 'wheels' ["318C4B", ...] are published, with 'learn'
// each new cap is added and reported to homectld (init message).
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

constexpr int MaxCaps {16};
constexpr uint16_t CompanyId {0x00ac};

struct Cap
{
   char id[7] {};              // ID printed on the cap, e.g. "318F01", empty = free entry
   bool received {false};      // got a packet since the start
   bool fresh {false};         // new data, published at once
   bool pending {false};       // the same data again, published with the next interval
   bool announced {false};     // published with the widget parameter
   bool macMatch {false};      // the data contain the BLE address (bytes 9-14), typical for these caps
   int length {0};             // length of the manufacturer data (without company id)
   uint8_t data[15] {};        // the last manufacturer data (without company id)
   unsigned long lastTime {0}; // millis of the last packet
   float pressure {0.0};
   int temperature {0};
   float voltage {0.0};
   int battery {0};
   int alarm {0};
   int rssi {0};
   uint8_t raw[8] {};          // data bytes 0-7 of the last packet
   int packets {0};            // packets since the last debug log
   bool changed {false};       //   .. with new data among them
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
   bool listFull {false};           // learning failed, the list is full
   bool resendAll {false};          // homectld (re)started - send the last values of all caps
   volatile bool otaActive {false};  // firmware update running - the BLE scan is stopped
   char learnedId[7] {};            // last learned cap, logged by the loop

   unsigned long LastPublishTime {0};
   unsigned long LastLedToggleTime {0};
   unsigned long LastPacketTime {0};
   unsigned long LastWifiAttempt {0};
   unsigned long LastMqttAttempt {0};
   bool LedState {false};
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
      scan->setWindow(50);      // 50 %, WLAN and BLE share the radio (1-3 packets of each transmission received;
                                //  the caps send every ~201 s at rest, ~9 s for ~1 min after a change)
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

      if (!otaActive && !NimBLEDevice::getScan()->isScanning())
      {
         tell(eloInfo, "Scan stand, starte neu");
         NimBLEDevice::getScan()->start(0, false, true);
      }

      unsigned long currentMillis {millis()};
      bool cyclic {currentMillis - LastPublishTime >= publishInterval * 1000};

      if (cyclic)
         LastPublishTime = currentMillis;

      // new data at once, the same data again with the interval (only if a packet came),
      //  nothing without a packet - so the last update at homectld is the last reception

      bool all {resendAll && MqttClient.connected()};

      if (all)
         resendAll = false;

      for (int i = 0; i < MaxCaps; i++)
      {
         Cap c;
         portENTER_CRITICAL(&capMux);
         c = capList[i];
         // after a (re)start of homectld all known caps, also the outdated ones (invalid) - so homectld
         //  knows their maxage and the time of the last reception ('age')

         bool publish {c.fresh || (cyclic && c.pending) || (all && c.received)};
         capList[i].packets = 0;
         capList[i].changed = false;

         if (publish && MqttClient.connected())
            capList[i].fresh = capList[i].pending = false;

         portEXIT_CRITICAL(&capMux);

         // the packets of a cap since the last pass (debug), the loop pauses 10 ms (delay) - so mostly one per line

         if (c.id[0] && c.packets)
            tell(eloDebug, "Debug: <= Kappe %s: %.2f bar, %d °C, %.2f V (%d %%), Alarm %d, %d dBm, %s%s",
                 c.id, c.pressure, c.temperature, c.voltage, c.battery, c.alarm, c.rssi,
                 c.changed ? "neu" : "unverändert",
                 c.packets > 1 ? (String(", ") + c.packets + " Pakete").c_str() : "");

         if (c.id[0] && publish)
         {
            if (publishCap(c))
            {
               portENTER_CRITICAL(&capMux);

               if (strcmp(capList[i].id, c.id) == 0)
                  capList[i].announced = true;

               portEXIT_CRITICAL(&capMux);
            }
         }
      }

      if (learnedId[0])
      {
         char id[7];
         portENTER_CRITICAL(&capMux);
         strcpy(id, learnedId);
         learnedId[0] = 0;
         int i {capOf(id)};
         Cap c {i >= 0 ? capList[i] : Cap()};
         portEXIT_CRITICAL(&capMux);

         tell(eloAlways, "Info: <= Neue Kappe '%s' gelernt (Format LYTPMS / company 172, %d Byte, Adresse in den Daten: %s, %.2f bar, %d dBm)",
              id, c.length, c.macMatch ? "ja" : "nein", c.pressure, c.rssi);
      }

      if (capsAdded && MqttClient.connected())
      {
         capsAdded = false;
         publishInitMessage();   // homectld adds the new caps to its config
      }

      if (listFull)
      {
         listFull = false;
         tell(eloAlways, "Error: Mehr als %d Kappen, keine weitere gelernt ('learn' ausschalten?)", MaxCaps);
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

      portENTER_CRITICAL(&capMux);

      int i {capOf(id)};

      if (i < 0 && learn)
      {
         // learn the new cap

         for (i = 0; i < MaxCaps && capList[i].id[0]; i++) ;

         if (i < MaxCaps)
         {
            capList[i] = Cap();
            strcpy(capList[i].id, id);
            strcpy(learnedId, id);
            capsAdded = true;
         }
         else
         {
            i = -1;
            listFull = true;
         }
      }

      if (i >= 0)
      {
         Cap& c {capList[i]};

         if (!c.received || memcmp(c.raw, d, sizeof(c.raw)) != 0)
            c.fresh = c.changed = true;
         else
            c.pending = true;

         memcpy(c.raw, d, sizeof(c.raw));
         c.length = md.size() - 2 > sizeof(c.data) ? sizeof(c.data) : md.size() - 2;
         memcpy(c.data, d, c.length);
         c.macMatch = c.length >= 15 && d[9] == mac[0] && d[10] == mac[1] && d[11] == mac[2]
            && d[12] == mac[3] && d[13] == mac[4] && d[14] == mac[5];
         c.received = true;
         c.lastTime = millis();
         c.pressure = roundf(pressure * 100.0f) / 100.0f;
         c.temperature = d[2] - 55;
         c.voltage = roundf(voltage * 100.0f) / 100.0f;
         c.battery = battery;
         c.alarm = d[3];
         c.rssi = dev->getRSSI();
         c.packets++;
         LastPacketTime = millis();
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
   // Parse Wheels - ["318F01", "31953C", ...]
   //   the received data of the caps is kept, caps missing in the config are dropped
   //***************************************************************************

   void parseWheels(JsonArray wheels)
   {
      Cap newList[MaxCaps];
      int count {0};

      for (JsonVariant v : wheels)
      {
         String id {v.as<const char*>() ? v.as<const char*>() : ""};
         id.trim();
         id.toUpperCase();

         if (id.length() != 6)
         {
            tell(eloAlways, "Error: Ungueltiger Eintrag '%s' in 'wheels' (ID 6-stellig wie auf der Kappe)", id.c_str());
            continue;
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

         strcpy(newList[count].id, id.c_str());
         count++;
      }

      portENTER_CRITICAL(&capMux);

      for (int i = 0; i < MaxCaps; i++)
         capList[i] = newList[i];

      portEXIT_CRITICAL(&capMux);

      tell(eloAlways, "Info: %d Kappe(n) konfiguriert", count);
   }

   //***************************************************************************
   // Publish
   //   address = ID of the cap * 16 + 1 (pressure) / 2 (temperature)
   //***************************************************************************

   bool publishCap(const Cap& c)
   {
      long address {strtol(c.id, nullptr, 16) << 4};
      char title[30];

      if (!MqttClient.connected())
         return false;

      unsigned long age {(millis() - c.lastTime) / 1000};    // [s] since the reception

      snprintf(title, sizeof(title), "Reifen %s Druck", c.id);
      PublishValue(address + 1, title, c.pressure, "bar", c.announced, age, &c);
      snprintf(title, sizeof(title), "Reifen %s Temperatur", c.id);
      PublishValue(address + 2, title, c.temperature, "°C", c.announced, age);

      if (c.fresh)
         tell(eloDetail, "-> Kappe %s: %.2f bar, %d °C, %.2f V (%d %%), Alarm %d, %d dBm",
              c.id, c.pressure, c.temperature, c.voltage, c.battery, c.alarm, c.rssi);

      return true;
   }

   // 'cap' -> the pressure, carries the battery and the details of the cap (JSON tab of the info dialog)
   // 'age' -> [s] since the reception, homectld takes 'now - age' as time of the data; older
   //   than maxAge (resent after a restart of homectld) the data is published as invalid

   void PublishValue(long address, const char* title, float value, const char* unit, bool announced,
                     unsigned long age, const Cap* cap = nullptr)
   {
      JsonDocument doc;
      doc["type"] = sensorType;
      doc["address"] = address;
      doc["value"] = value;
      doc["kind"] = "value";
      doc["title"] = title;
      doc["unit"] = unit;
      doc["maxage"] = maxAge;      // homectld invalidates the sensor without data for this time
      doc["age"] = age;

      if (age >= maxAge)
         doc["valid"] = false;

      if (cap)
      {
         char hex[2 * sizeof(cap->data) + 1] {};

         for (int i = 0; i < cap->length; i++)
            sprintf(hex + 2 * i, "%02x", cap->data[i]);

         doc["battery"] = cap->battery;
         doc["voltage"] = cap->voltage;
         doc["alarm"] = cap->alarm;
         doc["rssi"] = cap->rssi;
         doc["format"] = "LYTPMS (company 172)";
         doc["macmatch"] = cap->macMatch;
         doc["raw"] = hex;
      }

      // the first message of a cap since the start carries the widget default,
      //  homectld takes it when it creates the sensor

      if (!announced)
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

      JsonArray wheels {parameters["wheels"].to<JsonArray>()};

      portENTER_CRITICAL(&capMux);
      Cap list[MaxCaps];

      for (int i = 0; i < MaxCaps; i++)
         list[i] = capList[i];

      portEXIT_CRITICAL(&capMux);

      for (int i = 0; i < MaxCaps; i++)
         if (list[i].id[0])
            wheels.add(list[i].id);

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

      // the BLE scan shares the radio with the WLAN - stopped during the update, else the
      //  transfer breaks (OTA error 3, receive error); after an error the loop restarts it

      ArduinoOTA.onStart([this]() {
         otaActive = true;
         NimBLEDevice::getScan()->stop();
         tell(eloAlways, "OTA: Update startet, Scan angehalten ...");
      });
      ArduinoOTA.onEnd([this]() { tell(eloAlways, "OTA: Update fertig, Neustart"); });
      ArduinoOTA.onError([this](ota_error_t error) {
         otaActive = false;
         tell(eloAlways, "OTA: Fehler %u, Scan läuft wieder", error);
      });

      ArduinoOTA.begin();
      tell(eloAlways, "[BOOT] OTA bereit als '%s' auf %s", OtaHostname, WiFi.localIP().toString().c_str());
   }

   //***************************************************************************
   // LED
   //   WLAN fehlt 1 s, MQTT fehlt 500 ms, verbunden: an, kurz aus bei jedem Paket einer Kappe
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
            resendAll = true;
         }
      }
   }

   void mqttCallback(char* topic, byte* payload, unsigned int length)
   {
      // {"type": "TPMS", "action": "init", "config": {"eloquence": 1, "interval": 60, "maxAge": 1800, "learn": true, "wheels": ["318F01"]} }
      // {"type": "TPMS", "action": "requestinit"}

      JsonDocument doc;

      if (deserializeJson(doc, payload, length))
         return;

      if (doc["action"] == "init")
      {
         JsonObject config {doc["config"]};
         String configStr;
         serializeJson(config, configStr);

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

         if (config["wheels"].is<JsonArray>())
            parseWheels(config["wheels"].as<JsonArray>());

         tell(eloInfo, "Info: <- INIT: Eloquence %d; Interval %lu s; MaxAge %lu s; Learn %s",
              eloquence, publishInterval, maxAge, learn ? "an" : "aus");
         tell(eloDetail, "Detail: <- INIT von homectld: %s", configStr.c_str());
      }
      else if (doc["action"] == "requestinit")
      {
         tell(eloDetail, "Detail: <- requestinit von homectld, sende Init und die letzten Werte");
         publishInitMessage();
         resendAll = true;       // homectld (re)started
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
