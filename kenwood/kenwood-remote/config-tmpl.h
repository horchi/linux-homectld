
#pragma once

// --- ESP-NOW ---

constexpr const char* PeerMacStr {<KENWOOD_MAC>};   // WLAN-MAC des Kenwood-ESP (D1 Mini), z.B. "b0:cb:d8:98:6b:40"
constexpr int WifiChannel {<WIFI_CHANNEL>};         // fester Kanal des Routers, muss zum Kenwood-ESP passen
constexpr uint8_t PacketMagic {0x4B};                // 'K', kennzeichnet unsere Pakete

// --- Tasten (XIAO ESP32C3, D-Nummern -> GPIO) ---

constexpr int WakePin {3};             // D1, gemeinsamer Weckpin ueber Dioden
constexpr int ButtonPins[] {6, 7, 21, 20, 10};   // D4, D5, D6, D7, D10
constexpr int ButtonAddresses[] {1, 2, 5, 6, 12}; // KENWOOD Adressen: Volume +, Volume -, Track +, Track -, Mute
constexpr int ButtonCount {sizeof(ButtonPins) / sizeof(ButtonPins[0])};

constexpr int RepeatDelayMs {400};     // Taste gehalten: erste Wiederholung nach ..
constexpr int RepeatIntervalMs {250};  // .. dann alle ..
constexpr int MaxRepeats {30};         // Sicherheitsgrenze, danach schlafen auch bei gehaltener Taste

constexpr int SendTimeoutMs {100};     // Warten auf die Sendebestaetigung

// --- Debug ---

constexpr bool DebugStayAwake {false}; // true: kein Deep Sleep, Tasten werden gepollt, serielle Ausgaben bleiben lesbar
