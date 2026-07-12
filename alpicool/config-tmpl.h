
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

constexpr const char* TopicPublish {"homectld2mqtt/alpicool"};
constexpr const char* TopicSubscribe {"homectld2mqtt/alpicool/in"};
constexpr const char* TopicLog {"homectld2mqtt/alpicool/log"};

constexpr const char* mqttServer {<MQTT_SERVER>};
constexpr int mqttPort {1883};

constexpr const char* TempChoices {"-3,-2,-1,0,1,2,3,4,5,6,7,8,9,10"};

constexpr const char* BleMacStr {<ALPI_MAC>};

constexpr int StatusLedPin {2};
constexpr int LedBrightness {64};    // 64 entspricht etwa 25% Helligkeit

// config (can changed via MQTT by init packet)

int eloquence {eloInfo|eloDetail|eloDebug};
unsigned long queryInterval {30};      // seconds
const char* sensorType {"ALPICOOL"};
double correctionFactor {0.0};
double correctionOffset {0.0};

int i2cAddress {0x40};     // Standard-I2C-Adresse des INA226 (Analog Strom Sensor)
