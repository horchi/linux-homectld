
#pragma once

// --- ESP-NOW ---

constexpr const char* PeerMacStr {<KENWOOD_MAC>};   // WLAN-MAC des Kenwood-ESP (D1 Mini), z.B. "b0:cb:d8:98:6b:40"
constexpr int WifiChannel {<WIFI_CHANNEL>};         // fester Kanal des Routers, muss zum Kenwood-ESP passen
constexpr uint8_t PacketMagic {0x4B};                // 'K', kennzeichnet unsere Pakete

// --- Tasten (ESP32-C3 Super Mini) ---
//   nur GPIO 0..5 koennen aus dem Deep Sleep wecken, GPIO 2 ist Strapping-Pin (muss beim Boot high sein)
//   -> jede Taste direkt an ihrem GPIO gegen Masse, alle fuenf sind Weckquelle, keine Dioden

constexpr int ButtonPins[] {0, 1, 3, 4, 5};      // oben, unten, rechts, links, Mitte (Navimec)
constexpr int ButtonAddresses[] {1, 2, 5, 6, 3};  // KENWOOD Adressen: Volume +, Volume -, Track +, Track -, ATT
constexpr int ButtonCount {sizeof(ButtonPins) / sizeof(ButtonPins[0])};

constexpr int RepeatDelayMs {400};     // Taste gehalten: erste Wiederholung nach ..
constexpr int RepeatIntervalMs {250};  // .. dann alle ..
constexpr int MaxRepeats {30};         // Sicherheitsgrenze, danach schlafen auch bei gehaltener Taste

constexpr int SendTimeoutMs {100};     // Warten auf die Sendebestaetigung

// --- Debug ---

constexpr bool DebugStayAwake {false}; // true: kein Deep Sleep, Tasten werden gepollt, serielle Ausgaben bleiben lesbar
