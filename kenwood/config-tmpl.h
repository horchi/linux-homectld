
#pragma once

enum Eloquence
{
   eloAlways         = 0x000000,
   eloInfo           = 0x000001,
   eloDetail         = 0x000002,
   eloDebug          = 0x000004,
   eloDebug2         = 0x000008
};

// --- Network & MQTT Settings (WLAN aus Makefile / Make.user) ---

constexpr const char* WifiSsid {<WIFI_SSID>};
constexpr const char* WifiPassword {<WIFI_PWD>};

constexpr const char* TopicPublish {"homectld2mqtt/kenwood"};
constexpr const char* TopicSubscribe {"homectld2mqtt/kenwood/in"};
constexpr const char* TopicLog {"homectld2mqtt/kenwood/log"};

constexpr const char* mqttServer {<MQTT_SERVER>};
constexpr int mqttPort {1883};

// --- Pins ---

constexpr int RemotePin {25};          // -> Basis NPN / Gate N-MOSFET, zieht den Lenkraddraht (hellblau/gelb) nach Masse
constexpr int RemoteMarkLevel {HIGH};  // Pegel am GPIO waehrend eines NEC 'mark' (HIGH = Transistor leitet = Draht auf Masse)
constexpr int PowerSensePin {34};      // Eingang, P.CONT (blau/weiss, 12V wenn Radio an) ueber PC817 Modul (OUT), 2.2k vor dem Moduleingang
constexpr bool PowerSenseInvert {true}; // true: Optokoppler mit Pull-up am Ausgang (OUT ist LOW wenn Radio an); false: Spannungsteiler direkt am Pin
constexpr int AccRelayPin {-1};        // optional: Relais in der ACC Leitung (rot), -1 = nicht vorhanden
constexpr int AccRelayOnLevel {HIGH};  // Pegel am GPIO fuer 'Relais an'

constexpr int StatusLedPin {2};
constexpr int LedBrightness {64};      // 64 entspricht etwa 25% Helligkeit

// --- Kenwood NEC ---

constexpr uint8_t NecAddress {0xB9};   // Geraeteadresse der Kenwood Fernbedienung

// config (can changed via MQTT by init packet)

int eloquence {eloInfo|eloDetail};
unsigned long publishInterval {60};    // seconds, zyklische Veroeffentlichung aller Zustaende
const char* sensorType {"KENWOOD"};
int keyGap {100};                      // ms Pause zwischen zwei Tastendruecken (z.B. Lautstaerke +3)
int powerThreshold {1000};             // mV am PowerSensePin ab dem das Radio als 'an' gilt
