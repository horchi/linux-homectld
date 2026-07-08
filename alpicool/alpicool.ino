
#include <WiFi.h>
#include <PubSubClient.h>
#include <NimBLEDevice.h>
#include <ArduinoJson.h>
#include <stdarg.h>
#include "config.h"

class AlpicoolBridge;
static AlpicoolBridge* bridgeInstance {};

class AlpicoolBridge : public NimBLEClientCallbacks
{
private:

   const char* ServiceUuid {"00001234-0000-1000-8000-00805f9b34fb"};
   const char* TxUuid {"00001235-0000-1000-8000-00805f9b34fb"};
   const char* RxUuid {"00001236-0000-1000-8000-00805f9b34fb"};

   WiFiClient EspClient;
   PubSubClient MqttClient;

   NimBLEClient* pBleClient = nullptr;
   NimBLERemoteCharacteristic* pTxCharacteristic = nullptr;

   bool IsBleConnected {false};
   bool IsBleInitialised {false};

   bool CurrentPowerState {true};     // Fallback: An
   uint8_t CurrentRunMode {0};        // Fallback: Max (0)
   int8_t CurrentTargetTemp {7};      // Fallback: 7 Grad

   unsigned long LastQueryTime {0};
   unsigned long LastBleConnectAttempt {0};

   unsigned long LastLedToggleTime {0};
   bool LedState {false};
   bool initialRun {true};

public:

   AlpicoolBridge() : MqttClient(EspClient)
   {
      bridgeInstance = this;
   }

   void Setup()
   {
      pinMode(StatusLedPin, OUTPUT);
      digitalWrite(StatusLedPin, LOW);

      NimBLEDevice::init("ESP32_Alpicool_Bridge");

      // Just-Works Pairing ohne PIN (BOND) und Verschlüsselung (ENC) aktivieren

      NimBLEDevice::setSecurityAuth(BLE_SM_PAIR_AUTHREQ_BOND);
      NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);

      ConnectToWiFi();

      MqttClient.setServer(MqttServer, MqttPort);
      MqttClient.setCallback([this](char* topic, byte* payload, unsigned int length) {
         this->mqttCallback(topic, payload, length);
      });

      ConnectToMqtt();
      tell(0, "System erfolgreich gebootet. WLAN, MQTT und BLE-Subsystem sind bereit!");
   }

   void InitBluetooth()
   {
      if (IsBleInitialised)
         return;

      tell(1, "Starte Bluetooth-Subsystem...");
      NimBLEDevice::init("ESP32_Alpicool_Bridge");

      // Aktiviert das automatische Just-Works-Pairing für die Zahnrad-Taste der Box

      NimBLEDevice::setSecurityAuth(BLE_SM_PAIR_AUTHREQ_BOND);
      NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);

      IsBleInitialised = true;
      tell(0, "Bluetooth erfolgreich initialisiert. Suche Kühlbox...");
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

      if (!IsBleConnected)
      {
         if (ConnectToBle())
         {
            delay(500);
            QueryFridge();
         }
         else
         {
            UpdateLedBlink();
            delay(2000);
         }
      }
      else
      {
         unsigned long currentMillis {millis()};

         if (currentMillis - LastQueryTime >= QueryInterval)
         {
            LastQueryTime = currentMillis;
            QueryFridge();
         }
      }
   }

   void tell(int eloquence, const char* format, ...)
   {
      char messageBuffer[256] {};
      va_list args;
      va_start(args, format);
      vsnprintf(messageBuffer, sizeof(messageBuffer), format, args);
      va_end(args);

      if (!MqttClient.connected())
         return;

      StaticJsonDocument<384> doc;
      doc["type"] = "alpicool";
      doc["action"] = "log";
      doc["level"] = eloquence;
      doc["message"] = messageBuffer;
      doc["timestamp"] = millis();

      String outputStr;
      serializeJson(doc, outputStr);
      MqttClient.publish(TopicLog, outputStr.c_str());
   }

private:

   void UpdateLedBlink()
   {
      unsigned long currentMillis {millis()};
      unsigned long interval {1000};

      // Wenn alles verbunden ist -> LED leuchtet dauerhaft gedimmt

      if (WiFi.status() == WL_CONNECTED && MqttClient.connected() && IsBleConnected)
      {
         analogWrite(StatusLedPin, LedBrightness);
         return;
      }
      else if (WiFi.status() == WL_CONNECTED && MqttClient.connected())
      {
         interval = 200;
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

         String clientId {"ESP32_Alpicool"};
         clientId += String((uint32_t)ESP.getEfuseMac(), HEX);

         if (MqttClient.connect(clientId.c_str()))
         {
            MqttClient.subscribe(TopicSubscribe);
            PublishInitMessage();
         }
      }
   }

   void PublishInitMessage()
   {
      StaticJsonDocument<128> doc;
      doc["type"] = SensorType;
      doc["action"] = "init";
      doc["topic"] = TopicSubscribe;

      String outputStr;
      serializeJson(doc, outputStr);
      MqttClient.publish(TopicPublish, outputStr.c_str());
   }

    bool ConnectToBle()
   {
      if (pBleClient == nullptr)
      {
         pBleClient = NimBLEDevice::createClient();
         pBleClient->setClientCallbacks(this, false);
      }

      if (!pBleClient->isConnected())
      {
         NimBLEAddress targetAddress(BleMacStr, BLE_ADDR_RANDOM);
         if (!pBleClient->connect(targetAddress, false))
         {
            return false;
         }
         delay(1000);
      }

      NimBLERemoteService* pRemoteService {pBleClient->getService(ServiceUuid)};
      if (pRemoteService == nullptr)
      {
         pBleClient->disconnect();
         return false;
      }

      pTxCharacteristic = pRemoteService->getCharacteristic(TxUuid);
      NimBLERemoteCharacteristic* pRxCharacteristic = pRemoteService->getCharacteristic(RxUuid);

      if (pTxCharacteristic == nullptr || pRxCharacteristic == nullptr)
      {
         pBleClient->disconnect();
         return false;
      }

      if (pRxCharacteristic->canNotify())
      {
         tell(4, "Debug: Rx-Charakteristik unterstuetzt Notifications. Registriere Handler...");

         auto notifyHandler = [](NimBLERemoteCharacteristic* pChar, uint8_t* pData, size_t len, bool isNotify) {
            if (bridgeInstance != nullptr)
            {
               bridgeInstance->tell(4, "Debug: BLE-Daten empfangen! Laenge: %d Bytes", len);
               bridgeInstance->ParseNotification(pData, len);
            }
         };

         if (!pRxCharacteristic->subscribe(true, notifyHandler))
         {
            tell(4, "Debug: Subscription-Aufruf fehlgeschlagen.");
            pBleClient->disconnect();
            return false;
         }

         NimBLERemoteDescriptor* pCccdDesc = pRxCharacteristic->getDescriptor(NimBLEUUID((uint16_t)0x2902));
         if (pCccdDesc != nullptr)
         {
            uint8_t val[] {0x01, 0x00};
            pCccdDesc->writeValue(val, 2, true);
            tell(4, "Debug: CCCD Descriptor (0x2902) auf Kühlbox erfolgreich scharfgeschaltet!");
         }
      }

      tell(0, "Erfolgreich mit Kuehlbox via BLE verbunden und gekoppelt");
      return true;
   }

   void onConnect(NimBLEClient* pClient) override { IsBleConnected = true; }
   void onDisconnect(NimBLEClient* pClient, int reason) override { IsBleConnected = false; tell(0, "BLE Verbindung verloren"); }

   void SendBleCommand(uint8_t command, uint8_t p1, uint8_t p2, uint8_t p3)
   {
      if (!IsBleConnected || pTxCharacteristic == nullptr)
         return;

      uint8_t payloadLen = (p1 == 0xFF) ? 1 : 2;
      if (p2 != 0xFF) payloadLen = 3;

      uint8_t packetSize = 3 + payloadLen + 2; // 3 Bytes (FE FE LEN) + Payload + 2 Bytes Checksumme
      uint8_t finalPacket[8] {}; // Maximalgröße für unsere Befehle

      finalPacket[0] = 0xFE;
      finalPacket[1] = 0xFE;
      finalPacket[2] = payloadLen + 2; // struct.pack('B', len(data) + 2)
      finalPacket[3] = command;

      if (payloadLen >= 2) finalPacket[4] = p1;
      if (payloadLen == 3) finalPacket[5] = p2;

      uint16_t checksum {0};

      for (int i {0}; i < (3 + payloadLen); i++)
         checksum += finalPacket[i];

      finalPacket[3 + payloadLen] = (uint8_t)((checksum >> 8) & 0xFF);     // High Byte
      finalPacket[3 + payloadLen + 1] = (uint8_t)(checksum & 0xFF);        // Low Byte
      pTxCharacteristic->writeValue(finalPacket, packetSize, false);
   }

   void QueryFridge()
   {
      SendBleCommand(0x01, 0xFF, 0xFF, 0xFF);
   }

   void PublishStatus(int address, const char* title, bool state, int rights = 0)
   {
      StaticJsonDocument<512> doc;
      doc["type"] = SensorType; doc["address"] = address; doc["state"] = state;
      doc["kind"] = "status"; doc["title"] = title;

      if (initialRun)
      {
         if (rights > 0) doc["rights"] = rights;
         JsonObject param {doc.createNestedObject("parameter")};
         param["widgettype"] = 0;

         if (address == 0)
         {
            param["symbol"] = "mdi:mdi-power"; param["symbolOn"] = "mdi:mdi-snowflake";
            param["color"] = "gray"; param["colorOn"] = "rgb(3 169 244)";
         }
         else if (address == 6)
         {
            param["symbol"] = "mdi:mdi-lock-open-outline"; param["symbolOn"] = "mdi:mdi-lock-outline";
            param["color"] = "gray"; param["colorOn"] = "rgb(3 169 244)";
         }
      }

      String outputStr; serializeJson(doc, outputStr);
      MqttClient.publish(TopicPublish, outputStr.c_str());
   }

   void PublishValue(int address, const char* title, float value, const char* unit = nullptr, int rights = 0, const char* choices = nullptr)
   {
      StaticJsonDocument<512> doc;
      doc["type"] = SensorType; doc["address"] = address; doc["value"] = value;
      doc["kind"] = "value"; doc["title"] = title;
      if (unit != nullptr) doc["unit"] = unit;

      if (initialRun)
      {
         if (rights > 0) doc["rights"] = rights;
         if (choices != nullptr) doc["choices"] = choices;
         JsonObject param {doc.createNestedObject("parameter")};
         if (address == 1) param["widgettype"] = 8;
         else if (address == 2) param["widgettype"] = 6;
         else if (address == 3) param["widgettype"] = 3;
      }

      String outputStr; serializeJson(doc, outputStr);
      MqttClient.publish(TopicPublish, outputStr.c_str());
   }

   void PublishText(int address, const char* title, const char* text, int rights = 0, const char* choices = nullptr)
   {
      StaticJsonDocument<512> doc;
      doc["type"] = SensorType; doc["address"] = address; doc["text"] = text;
      doc["kind"] = "text"; doc["title"] = title;

      if (initialRun)
      {
         if (rights > 0) doc["rights"] = rights;
         if (choices != nullptr) doc["choices"] = choices;
         JsonObject param {doc.createNestedObject("parameter")};
         if (address == 4) param["widgettype"] = 8;
         else if (address == 5) { param["widgettype"] = 2; param["colorCondition"] = "0=green,>0=red"; }
      }

      String outputStr; serializeJson(doc, outputStr);
      MqttClient.publish(TopicPublish, outputStr.c_str());
   }

   const char* GetErrorText(uint8_t code)
   {
      if (code == 1) return "F1 (Low Voltage)";
      if (code == 2) return "F2 (Fan Overload)";
      if (code == 3) return "F3 (Motor Start Error)";
      if (code == 4) return "F4 (Motor Speed Error)";
      if (code == 5) return "F5 (Overheating)";
      if (code == 6) return "F6 (Controller Error)";
      if (code == 7) return "F7 (Sensor Error)";
      return "OK";
   }

public:

   void ParseNotification(uint8_t* pData, size_t length)
   {
      static uint8_t reassemblyBuffer[128]; // Fester, großer Sammel-Puffer
      static size_t bufferIndex {0};

      // Schutz vor Pufferüberlauf

      if (bufferIndex + length > sizeof(reassemblyBuffer))
      {
         bufferIndex = 0;
         tell(3, "DIAGNOSE PARSER: Pufferueberlauf verhindert. Setze zurueck.");
         return;
      }

      // Neue Fragmente einfach hinten anfügen

      memcpy(&reassemblyBuffer[bufferIndex], pData, length);
      bufferIndex += length;

      // Solange wie genug Daten für eine Auswertung im Puffer liegen, loopen wir

      while (bufferIndex >= 3)
      {
         // Suche nach dem Start-Header FE FE im Puffer

         if (reassemblyBuffer[0] != 0xFE || reassemblyBuffer[1] != 0xFE)
         {
            // Wenn der Anfang kein FE FE ist, schieben wir den Puffer um 1 Byte nach vorne

            memmove(&reassemblyBuffer[0], &reassemblyBuffer[1], --bufferIndex);
            continue;
         }

         uint8_t payloadLen {reassemblyBuffer[2]};
         size_t expectedTotalLength {3 + payloadLen};

         // Wenn das Paket noch nicht vollständig im Puffer liegt: Abbrechen und auf das nächste Fragment warten

         if (bufferIndex < expectedTotalLength)
            return;

         uint32_t calculatedChecksum {0};

         for (size_t i {0}; i < expectedTotalLength - 2; i++)
            calculatedChecksum += (uint32_t)reassemblyBuffer[i];

         uint16_t packetChecksum = (reassemblyBuffer[expectedTotalLength - 2] << 8) | reassemblyBuffer[expectedTotalLength - 1];

         if ((calculatedChecksum & 0xFFFF) != packetChecksum)
         {
            tell(2, "DIAGNOSE PARSER ABBRUCH: Checksummenfehler. Erwartet: 0x%04X, Berechnet: 0x%04X. Schiebe Puffer.",
                 packetChecksum, (calculatedChecksum & 0xFFFF));

            memmove(&reassemblyBuffer[0], &reassemblyBuffer[2], bufferIndex -= 2);
            continue;
         }

         uint8_t msgType {reassemblyBuffer[3]};
         uint8_t* msgData {&reassemblyBuffer[4]};

         if (msgType == 1 && payloadLen >= 15)
         {
            bool controls_locked = (msgData[0] == 0x01);
            bool powered_on = (msgData[1] == 0x01);
            uint8_t run_mode = msgData[2];
            uint8_t battery_saver = msgData[3];
            int8_t unit1_target = (int8_t)msgData[4];
            int8_t unit1_current = (int8_t)msgData[14];
            uint8_t battery_voltage_int = msgData[16];
            uint8_t battery_voltage_frac = msgData[17];
            float battery_voltage = (float)battery_voltage_int + ((float)battery_voltage_frac / 10.0f);

            PublishStatus(0, "Power", powered_on, 2);
            PublishValue(1, "Target Temp", unit1_target, "°C", 2, TempChoices);
            PublishValue(2, "Actual Temp", unit1_current, "°C");
            PublishValue(3, "Battery", battery_voltage, "V");
            PublishText(4, "Mode", (run_mode == 1) ? "Eco" : "Max", 2, "Max,Eco");
            PublishText(5, "Status", GetErrorText(0));
            PublishStatus(6, "Lock", controls_locked, 2);

            initialRun = false;
         }

         size_t remainingBytes {bufferIndex - expectedTotalLength};

         if (remainingBytes > 0)
            memmove(&reassemblyBuffer[0], &reassemblyBuffer[expectedTotalLength], remainingBytes);

         bufferIndex = remainingBytes;
      }
   }

private:

   void mqttCallback(char* topic, byte* payload, unsigned int length)
   {
      StaticJsonDocument<256> doc;

      char* jsonStr {new char[length + 1]()};
      memcpy(jsonStr, payload, length);

      ArduinoJson::V743PB22::DeserializationError res {deserializeJson(doc, jsonStr)};
      delete[] jsonStr;

      if (res)
         return;

      int address {doc["address"] | -1};
      JsonVariant valueVariant {doc["value"]};
      int value {1};

      if (valueVariant.is<int>() || valueVariant.is<bool>())
      {
         value = valueVariant.as<int>();
      }
      else if (valueVariant.is<const char*>())
      {
         String valStr {valueVariant.as<const char*>()};
         valStr.toLowerCase();

         if (valStr != "eco" && valStr != "true")
            value = atoi(valStr.c_str());
      }

      // Adresse 0: Power schalten (An / Aus)

      if (address == 0)
      {
         uint8_t powerPacket[20] {
            0xFE, 0xFE, 0x11, 0x02,
            0x00,                                // controls_locked
            (uint8_t)(value == 1 ? 0x01 : 0x00), // powered_on (Der MQTT-Sollwert!)
            CurrentRunMode,                      // run_mode
            0x01,                                // battery_saver (Medium)
            (uint8_t)CurrentTargetTemp,          // unit1.target_temperature
            0x0B,                                // max_selectable_temperature (+11)
            0xFF,                                // min_selectable_temperature (-1)
            0x02,                                // unit1.hysteresis (2)
            0x00,                                // start_delay
            0x00,                                // temperature_unit (0 = Celsius)
            0x00, 0x00, 0x00, 0x00,              // 4 Korrektur-Bytes laut Python '>B??BBbbbbBBbbbb'
            0x00, 0x00                           // Checksumme auf Index 18 und 19
         };

         uint16_t cksum {0};

         for (int i {0}; i < 18; i++)
            cksum += powerPacket[i];

         powerPacket[18] = (uint8_t)((cksum >> 8) & 0xFF);
         powerPacket[19] = (uint8_t)(cksum & 0xFF);

         pTxCharacteristic->writeValue(powerPacket, 20, false);
         tell(1, "MQTT BEFEHL: Power auf %s geschaltet.", (value == 1) ? "AN" : "AUS");
      }

      // Adresse 1: Zieltemperatur setzen (Target Temp) -> Bleibt unverändert funktionsfähig

      else if (address == 1)
      {
         uint8_t targetPacket[] {
            0xFE, 0xFE, 0x04, 0x05,
            (uint8_t)((int8_t)value),
            0x00, 0x00
         };

         uint16_t targetChecksum {0};
         for (int i {0}; i < 5; i++) targetChecksum += targetPacket[i];

         targetPacket[5] = (uint8_t)((targetChecksum >> 8) & 0xFF);
         targetPacket[6] = (uint8_t)(targetChecksum & 0xFF);

         pTxCharacteristic->writeValue(targetPacket, 7, false);
         tell(1, "MQTT BEFEHL: Zieltemperatur erfolgreich auf %d Grad geaendert.", value);
      }

      // Adresse 4: Betriebsmodus ändern (Max / Eco)

      else if (address == 4)
      {
         uint8_t modePacket[20] {
            0xFE, 0xFE, 0x11, 0x02,
            0x00,                                // controls_locked
            (uint8_t)(CurrentPowerState ? 0x01 : 0x00), // powered_on
            (uint8_t)(value == 1 ? 0x01 : 0x00), // run_mode (Der MQTT-Sollwert!)
            0x01,                                // battery_saver
            (uint8_t)CurrentTargetTemp,          // unit1.target_temperature
            0x0B,                                // max_selectable_temperature
            0xFF,                                // min_selectable_temperature
            0x02,                                // unit1.hysteresis
            0x00,                                // start_delay
            0x00,                                // temperature_unit
            0x00, 0x00, 0x00, 0x00,              // 4 Korrektur-Bytes
            0x00, 0x00                           // Checksumme auf Index 18 und 19
         };

         uint16_t cksum {0};

         for (int i {0}; i < 18; i++)
            cksum += modePacket[i];

         modePacket[18] = (uint8_t)((cksum >> 8) & 0xFF);
         modePacket[19] = (uint8_t)(cksum & 0xFF);

         pTxCharacteristic->writeValue(modePacket, 20, false);
         tell(1, "MQTT BEFEHL: Modus auf %s umgestellt.", (value == 1) ? "Eco" : "Max");
      }

      delay(200);
      QueryFridge();
   }
};

AlpicoolBridge alpiBridge;

void setup()
{
   alpiBridge.Setup();
}

void loop()
{
   alpiBridge.Loop();
}
