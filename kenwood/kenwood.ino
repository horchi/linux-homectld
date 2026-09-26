
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
#include <ArduinoOTA.h>
#include <esp_now.h>
#include <esp_wifi.h>
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
   int address {0};
   const char* title {};
   const char* param {};     // Name des Parameters im init/config Paket
   const char* symbol {};
   int code {-1};            // NEC Kommando, -1 = unbekannt
};

// Codes am DMX8019DABS per Scan verifiziert (siehe README), -1 = unbekannt

Key keys[] {
   {  1, "Volume +",     "codeVolumeUp",    "mdi:mdi-volume-plus",       20 },   // 0x14
   {  2, "Volume -",     "codeVolumeDown",  "mdi:mdi-volume-minus",      21 },   // 0x15
   {  3, "ATT",          "codeAtt",         "mdi:mdi-volume-medium",     22 },   // 0x16 Toggle
   {  4, "Source",       "codeSource",      "mdi:mdi-swap-horizontal",   19 },   // 0x13 alle Quellen
   {  5, "Track +",      "codeTrackUp",     "mdi:mdi-skip-next",         10 },   // 0x0A naechster Sender / Titel
   {  6, "Track -",      "codeTrackDown",   "mdi:mdi-skip-previous",     11 },   // 0x0B
   {  7, "Play/Pause",   "codePlayPause",   "mdi:mdi-play-pause",        14 },   // 0x0E ungeprueft
   {  8, "Answer",       "codeAnswer",      "mdi:mdi-phone",             -1 },
   {  9, "Hang up",      "codeHangUp",      "mdi:mdi-phone-hangup",      -1 },
   { 10, "Voice",        "codeVoice",       "mdi:mdi-microphone",        -1 },
   { 11, "Preset +",     "codePresetUp",    "mdi:mdi-playlist-play",    141 },   // 0x8D naechster Preset
   { 12, "Mute",         "codeMute",        "mdi:mdi-volume-mute",       91 },   // 0x5B Toggle
   { 13, "Source Tuner", "codeSourceTuner", "mdi:mdi-radio-tower",       28 },   // 0x1C DAB -> Radio -> 'Standby' Anzeige (kein echter Standby)
   { 14, "Source Media", "codeSourceMedia", "mdi:mdi-music",             30 },   // 0x1E Spotify -> iPod -> Bluetooth
   { 15, "Source Video", "codeSourceVideo", "mdi:mdi-video-input-hdmi",  31 },   // 0x1F HDMI -> AV-In
   { 16, "FM Radio",     "codeFmRadio",     "mdi:mdi-radio",            253 },   // 0xFD analoges Radio direkt
   { 17, "Equalizer",    "codeEqualizer",   "mdi:mdi-equalizer",        130 },   // 0x82 Toggle
   { 20, "Preset 0",     "codePreset0",     "mdi:mdi-numeric-0-box",      0 },
   { 21, "Preset 1",     "codePreset1",     "mdi:mdi-numeric-1-box",      1 },
   { 22, "Preset 2",     "codePreset2",     "mdi:mdi-numeric-2-box",      2 },
   { 23, "Preset 3",     "codePreset3",     "mdi:mdi-numeric-3-box",      3 },
   { 24, "Preset 4",     "codePreset4",     "mdi:mdi-numeric-4-box",      4 },
   { 25, "Preset 5",     "codePreset5",     "mdi:mdi-numeric-5-box",      5 },
   { 26, "Preset 6",     "codePreset6",     "mdi:mdi-numeric-6-box",      6 },
   { 27, "Preset 7",     "codePreset7",     "mdi:mdi-numeric-7-box",      7 },
   { 28, "Preset 8",     "codePreset8",     "mdi:mdi-numeric-8-box",      8 },
   { 29, "Preset 9",     "codePreset9",     "mdi:mdi-numeric-9-box",      9 }
};

// Paket der Lenkrad-Fernbedienung (kenwood-remote/kenwood-remote.ino)

struct RemotePacket
{
   uint8_t magic {0};
   uint8_t address {0};   // KENWOOD Adresse der Taste
   uint8_t count {1};
   uint8_t seq {0};       // laufende Nummer gegen Doppelverarbeitung
};

class KenwoodBridge;
static KenwoodBridge* bridgeInstance {};

constexpr int AddressPower {0};
constexpr int AddressPreset {18};      // Preset direkt waehlen: value 0..9 = Code 0..9
constexpr const char* PresetChoices {"0,1,2,3,4,5,6,7,8,9"};

// Power nur ueber das ACC Relais: Das DMX merkt sich den Standby (Code 131) ueber
// das Aus-/Einschalten der Zuendung hinweg und kommt aus dem Standby nur ueber die
// Taste am Geraet zurueck, kein Code wird im Standby ausgewertet. Ohne Relais bleiben
// die Codes als Option (per init Paket setzbar).

int codePowerOff {-1};
int codePowerOn {-1};

// Codes die nie gesendet werden: 51 und 216 schalten in das Production Menu (Rueckweg
// nur per Aus-/Einschalten), 131 fuehrt in den gemerkten Standby (s.o.)

constexpr int BlockedCodes[] {51, 216, 131};

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

   // ESP-NOW Empfang (Lenkrad-Fernbedienung), aktiv wenn RemoteMacStr gesetzt ist

   bool remoteEnabled {false};
   uint8_t remoteMac[6] {0};
   bool acceptAllRemoteMac {false};
   volatile bool remotePending {false};
   RemotePacket remotePacket;
   int remoteLastSeq {-1};

   unsigned long LastPublishTime {0};
   unsigned long LastLedToggleTime {0};
   bool LedState {false};
   bool initialRun {true};             // rights und Widget Parameter mitsenden (nach Connect und requestinit)

public:

   KenwoodBridge() : MqttClient(EspClient) { bridgeInstance = this; }

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

      if (PowerSensePin >= 0)
         analogSetPinAttenuation(PowerSensePin, ADC_11db);   // bis ca. 3.1V messbar
      else
         powerKnown = true;                                  // ohne P.CONT gilt der Relaiszustand

      // RMT fuer das NEC Timing (1 MHz -> 1 Tick = 1 us)

      rmtReady = rmtInit(RemotePin, RMT_TX_MODE, RMT_MEM_NUM_BLOCKS_2, 1000000);

      if (rmtReady)
         rmtSetEOT(RemotePin, !RemoteMarkLevel);
      else
         tell(eloAlways, "Error: RMT init auf Pin %d fehlgeschlagen", RemotePin);

      tell(eloAlways, "[BOOT] Verbinde mit WLAN ...");
      ConnectToWiFi();
      SetupOta();
      SetupRemote();

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
      ArduinoOTA.handle();
      processRemote();          // Lenkrad-Fernbedienung, auch ohne MQTT

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

      for (int blocked : BlockedCodes)
      {
         if (code == blocked)
         {
            tell(eloAlways, "Warning: Code %d (0x%02X) ist gesperrt (Production Menu), nicht gesendet", code, code);
            return;
         }
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
      if (PowerSensePin < 0)
         return false;

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

      // mit Relais ausschliesslich ueber das Relais schalten, nie per Code (s. codePowerOff)

      if (AccRelayPin >= 0)
      {
         if (relayState != on)
         {
            relayState = on;
            digitalWrite(AccRelayPin, on ? AccRelayOnLevel : !AccRelayOnLevel);
            tell(eloInfo, "Info: ACC Relais %s", on ? "AN" : "AUS");
         }

         if (PowerSensePin < 0)     // kein P.CONT -> Relaiszustand melden
         {
            powerState = on;
            publishPower();
         }

         return;
      }

      if (on == powerState)
         return;

      int code {on ? codePowerOn : codePowerOff};

      if (code < 0)
      {
         tell(eloAlways, "Warning: Kein Code fuer '%s' und kein ACC Relais, kann Radio nicht %sschalten", on ? "Power On" : "Power Off", on ? "ein" : "aus");
         return;
      }

      sendKey(code, 1, on ? "Power On" : "Power Off");
   }

   //***************************************************************************
   // Publish
   //***************************************************************************

   void publishAll()
   {
      publishPower();

      for (const Key& key : keys)
         publishKey(key);

      publishPreset();
   }

   void publishPreset()
   {
      // Zustand unbekannt, das Radio meldet den aktuellen Preset nicht zurueck

      StaticJsonDocument<512> doc;
      doc["type"] = sensorType;
      doc["address"] = AddressPreset;
      doc["text"] = "-";
      doc["kind"] = "text";
      doc["title"] = "Preset";
      doc["choices"] = PresetChoices;

      if (initialRun)
      {
         doc["rights"] = 2;    // urControl

         JsonObject param {doc.createNestedObject("parameter")};
         param["widgettype"] = 8;   // wtChoice
      }

      String outputStr; serializeJson(doc, outputStr);
      MqttClient.publish(TopicPublish, outputStr.c_str());
   }

   void publishPower()
   {
      PublishStatus(AddressPower, "Power", powerState, "mdi:mdi-power", "gray", "green", powerKnown);
   }

   void publishKey(const Key& key)
   {
      PublishStatus(key.address, key.title, false, key.symbol, "white", "white", true);
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

      // rights und Widget Parameter nur mit der ersten Meldung nach dem Connect und
      // nach einem 'requestinit' von homectld (es wertet sie nur beim Anlegen des Sensors aus)

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
      StaticJsonDocument<2048> doc;
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
      parameters["codePowerOff"] = codePowerOff;
      parameters["codePowerOn"] = codePowerOn;

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
   //   {"type": "KENWOOD", "action": "raw", "code": 29, "count": 1}     Code zum Testen senden
   //   {"type": "KENWOOD", "address": 1, "value": 3}                    Taste (hier 3x Volume +)
   //***************************************************************************

   void mqttCallback(char* topic, byte* payload, unsigned int length)
   {
      StaticJsonDocument<1024> doc;

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

         if (config.containsKey("codePowerOff"))
            codePowerOff = config["codePowerOff"].as<int>();

         if (config.containsKey("codePowerOn"))
            codePowerOn = config["codePowerOn"].as<int>();

         for (Key& key : keys)
         {
            if (config.containsKey(key.param))
               key.code = config[key.param].as<int>();
         }

         tell(eloInfo, "Info: INIT: Eloquence %d; Interval %lu s; keyGap %d ms; powerThreshold %d mV; codePowerOff %d; codePowerOn %d",
              eloquence, publishInterval, keyGap, powerThreshold, codePowerOff, codePowerOn);
      }
      else if (strcmp(action, "requestinit") == 0)
      {
         publishInitMessage();
         initialRun = true;         // homectld (neu) gestartet -> Zustaende samt Parametern sofort senden
         LastPublishTime = 0;
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

         pressAddress(address, value);
      }
   }

   //***************************************************************************
   // Taste per Adresse ausloesen (MQTT Kommando oder Lenkrad-Fernbedienung)
   //***************************************************************************

   void pressAddress(int address, int value)
   {
      if (address == AddressPreset)
      {
         if (value < 0 || value > 9)
         {
            tell(eloAlways, "Warning: Preset %d ungueltig (0..9)", value);
            return;
         }

         char title[16];
         snprintf(title, sizeof(title), "Preset %d", value);
         sendKey(value, 1, title);
         publishPreset();
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

   //***************************************************************************
   // Lenkrad-Fernbedienung (ESP-NOW Empfang)
   //
   //   Das Bedienteil sendet RemotePacket an unsere WLAN-MAC, auf dem Kanal des Routers.
   //   Der Callback laeuft im WLAN-Task und legt das Paket nur ab, gesendet wird in Loop().
   //***************************************************************************

   static bool parseMac(const char* str, uint8_t* mac)
   {
      int v[6] {};

      if (sscanf(str, "%x:%x:%x:%x:%x:%x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6)
         return false;

      for (int i {0}; i < 6; i++)
         mac[i] = (uint8_t)v[i];

      return true;
   }

   static void onRemoteReceive(const esp_now_recv_info_t* info, const uint8_t* data, int len)
   {
      if (bridgeInstance)
         bridgeInstance->remoteReceived(info->src_addr, data, len);
   }

   void remoteReceived(const uint8_t* src, const uint8_t* data, int len)
   {
      static bool first {true};

      if (first)
      {
         tell(eloAlways, "Got message from mac %02X:%02X:%02X:%02X:%02X:%02X",
              src[0], src[1], src[2], src[3], src[4], src[5]);
         first = false;
      }

      if (!acceptAllRemoteMac)
      {
         if (len != (int)sizeof(RemotePacket) || memcmp(src, remoteMac, 6) != 0)
            return;
      }

      const RemotePacket* packet {(const RemotePacket*)data};

      if (packet->magic != RemoteMagic || remotePending)
         return;

      remotePacket = *packet;
      remotePending = true;
   }

   void SetupRemote()
   {
      acceptAllRemoteMac = strcmp(RemoteMacStr, "ALL") == 0;

      if (isEmptyStr(RemoteMacStr))
      {
         tell(eloAlways, "[BOOT] Lenkrad-Fernbedienung nicht konfiguriert (KENWOOD_REMOTE_MAC)");
         return;
      }

      if (!acceptAllRemoteMac && !parseMac(RemoteMacStr, remoteMac))
      {
         tell(eloAlways, "Error: Ungueltige MAC der Fernbedienung '%s'", RemoteMacStr);
         return;
      }

      if (esp_now_init() != ESP_OK)
      {
         tell(eloAlways, "Error: ESP-NOW init fehlgeschlagen");
         return;
      }

      esp_now_register_recv_cb(onRemoteReceive);
      esp_wifi_set_ps(WIFI_PS_NONE);      // sonst gehen ESP-NOW Pakete im Stromsparmodus verloren

      remoteEnabled = true;
      tell(eloAlways, "[BOOT] Lenkrad-Fernbedienung aktiv, MAC %s, Kanal %d", RemoteMacStr, WiFi.channel());
   }

   void processRemote()
   {
      if (!remotePending)
         return;

      RemotePacket packet {remotePacket};
      remotePending = false;

      if (packet.seq == remoteLastSeq)
         return;                          // Wiederholung desselben Pakets

      remoteLastSeq = packet.seq;
      tell(eloInfo, "Info: Fernbedienung: Adresse %d, %dx (seq %d)", packet.address, packet.count, packet.seq);
      pressAddress(packet.address, packet.count);
   }

   //***************************************************************************
   // OTA (Firmware Update ueber WLAN, siehe 'make upload-ota')
   //***************************************************************************

   void SetupOta()
   {
      ArduinoOTA.setHostname(OtaHostname);

      if (!isEmptyStr(OtaPassword))
         ArduinoOTA.setPassword(OtaPassword);

      ArduinoOTA.onStart([this]() {
         tell(eloAlways, "OTA: Update startet ...");
         digitalWrite(RemotePin, !RemoteMarkLevel);
      });
      ArduinoOTA.onEnd([this]() {
         tell(eloAlways, "OTA: Update fertig, Neustart");
      });
      ArduinoOTA.onError([this](ota_error_t error) {
         tell(eloAlways, "OTA: Fehler %u", error);
      });

      ArduinoOTA.begin();
      tell(eloAlways, "[BOOT] OTA bereit als '%s' auf %s", OtaHostname, WiFi.localIP().toString().c_str());
   }

   static bool isEmptyStr(const char* s) { return !s || !*s; }

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
         MqttClient.setBufferSize(2048);

         String clientId {"ESP32_Kenwood"};
         clientId += String((uint32_t)ESP.getEfuseMac(), HEX);

         if (MqttClient.connect(clientId.c_str()))
         {
            MqttClient.subscribe(TopicSubscribe);
            publishInitMessage();
            initialRun = true;
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
