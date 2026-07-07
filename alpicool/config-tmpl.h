
#pragma once

// --- Network & MQTT Settings (WLAN aus Makefile / Make.config) ---

constexpr const char* MqttServer {<MQTT_SERVER>};
constexpr int MqttPort {1883};

constexpr const char* WifiSsid {<WIFI_SSID>};
constexpr const char* WifiPassword {<WIFI_PWD>};

constexpr const char* TopicPublish {"homectld2mqtt/alpicool"};
constexpr const char* TopicSubscribe {"homectld2mqtt/alpicool/in"};
constexpr const char* TopicLog {"homectld2mqtt/alpicool/log"};

// --- homectld Payload Configuration ---

constexpr const char* SensorType {"ALPICOOL"};
constexpr const char* TempChoices {"-1,0,1,2,3,4,5,6,7,8,9,10,11"};

// --- BLE Hardware Target ---

constexpr const char* BleMacStr {"FC:E4:97:72:E9:83"};

// --- Hardware Pins ---

constexpr int StatusLedPin {2};
