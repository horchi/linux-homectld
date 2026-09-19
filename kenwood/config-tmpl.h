
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

// --- OTA (Firmware Update ueber WLAN, Passwort leer = ohne Passwort) ---

constexpr const char* OtaHostname {"kenwood-bridge"};
constexpr const char* OtaPassword {<OTA_PWD>};

// --- Pins ---

constexpr int RemotePin {25};          // -> + des PC817 Moduls 1, dessen OUT zieht den Lenkraddraht (hellblau/gelb) nach Masse (Modul VCC an 5V!)
constexpr int RemoteMarkLevel {HIGH};  // Pegel am GPIO waehrend eines NEC 'mark' (HIGH = Transistor leitet = Draht auf Masse)
constexpr int PowerSensePin {34};      // Eingang, P.CONT (blau/weiss, 12V wenn Radio an) ueber PC817 Modul (OUT), 2.2k vor dem Moduleingang; -1 = nicht angeschlossen, dann gilt der Relaiszustand
constexpr bool PowerSenseInvert {true}; // true: Optokoppler mit Pull-up am Ausgang (OUT ist LOW wenn Radio an); false: Spannungsteiler direkt am Pin
constexpr int AccRelayPin {27};        // Relais in der ACC Leitung (rot), IO27 am D1 Mini, -1 = nicht vorhanden
constexpr int AccRelayOnLevel {HIGH};  // Pegel am GPIO fuer 'Relais an': HIGH = Relaismodul mit Jumper auf H (Low-Trigger haelt mit 3.3V am GPIO nicht sicher aus)

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
