#pragma once

enum Eloquence
{
   eloAlways         = 0x000000,
   eloInfo           = 0x000001,
   eloDetail         = 0x000002,
   eloDebug          = 0x000004,
   eloDebug2         = 0x000008
};

// --- Network & MQTT Settings (WLAN aus Makefile / Make.config) ---

constexpr const char* WifiSsid {<WIFI_SSID>};
constexpr const char* WifiPassword {<WIFI_PWD>};

constexpr const char* TopicPublish {"homectld2mqtt/tpms"};
constexpr const char* TopicSubscribe {"homectld2mqtt/tpms/in"};
constexpr const char* TopicLog {"homectld2mqtt/tpms/log"};

constexpr const char* mqttServer {<MQTT_SERVER>};
constexpr int mqttPort {1883};

// --- OTA (Firmware Update ueber WLAN, Passwort leer = ohne Passwort) ---

constexpr const char* OtaHostname {"tpms-bridge"};
constexpr const char* OtaPassword {<OTA_PWD>};

constexpr int StatusLedPin {2};
constexpr int LedBrightness {64};    // 64 entspricht etwa 25% Helligkeit

// config (can changed via MQTT by init packet)

int eloquence {eloInfo};
unsigned long publishInterval {60};    // seconds, cyclic publish of the last values
unsigned long maxAge {1800};           // seconds, older values are published as invalid
bool learn {true};                     // add each new cap to 'wheels' (switch off when all caps are known)
const char* sensorType {"TPMS"};
