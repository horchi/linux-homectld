
//***************************************************************************
// Kenwood Remote - Lenkrad-Fernbedienung (Seeed XIAO ESP32C3)
//
//  Schlaeft im Deep Sleep, wird ueber den Weckpin (alle Tasten per Diode) geweckt,
//  ermittelt die gedrueckte Taste und schickt deren KENWOOD Adresse per ESP-NOW
//  an den Kenwood-ESP (kenwood/kenwood.ino), der daraus den NEC Code auf den
//  Lenkraddraht macht. Danach wieder Deep Sleep.
//
//  Konzept und Verdrahtung: kenwood/README-remote.md
//***************************************************************************

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_sleep.h>
#include <driver/gpio.h>
#include "config.h"

struct RemotePacket
{
   uint8_t magic {PacketMagic};
   uint8_t address {0};
   uint8_t count {1};
   uint8_t seq {0};
};

RTC_DATA_ATTR uint8_t sequence {0};   // ueberlebt den Deep Sleep

uint8_t peerMac[6] {};
volatile bool sendDone {false};
volatile bool sendOk {false};

//***************************************************************************
// Helpers
//***************************************************************************

bool parseMac(const char* str, uint8_t* mac)
{
   int v[6] {};

   if (sscanf(str, "%x:%x:%x:%x:%x:%x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6)
      return false;

   for (int i {0}; i < 6; i++)
      mac[i] = (uint8_t)v[i];

   return true;
}

// gedrueckte Taste ermitteln (Index in ButtonPins), -1 = keine

int pressedButton()
{
   for (int i {0}; i < ButtonCount; i++)
   {
      if (digitalRead(ButtonPins[i]) == LOW)
      {
         delay(10);                                  // entprellen

         if (digitalRead(ButtonPins[i]) == LOW)
            return i;
      }
   }

   return -1;
}

void goToSleep()
{
   Serial.println("Deep Sleep");
   Serial.flush();

   esp_now_deinit();
   WiFi.mode(WIFI_OFF);

   // Weckpin: low weckt, interner Pull-up bleibt im Deep Sleep aktiv (GPIO 0..5 des C3)

   gpio_pullup_en((gpio_num_t)WakePin);
   gpio_pulldown_dis((gpio_num_t)WakePin);
   esp_deep_sleep_enable_gpio_wakeup(1ULL << WakePin, ESP_GPIO_WAKEUP_GPIO_LOW);

   esp_deep_sleep_start();
}

//***************************************************************************
// ESP-NOW
//***************************************************************************

void onSent(const wifi_tx_info_t* info, esp_now_send_status_t status)
{
   sendOk = status == ESP_NOW_SEND_SUCCESS;
   sendDone = true;
}

bool setupEspNow()
{
   WiFi.mode(WIFI_STA);
   WiFi.disconnect();                                 // keine Verbindung, nur Funk

   esp_wifi_set_channel(WifiChannel, WIFI_SECOND_CHAN_NONE);

   if (esp_now_init() != ESP_OK)
   {
      Serial.println("Error: ESP-NOW init fehlgeschlagen");
      return false;
   }

   esp_now_register_send_cb(onSent);

   esp_now_peer_info_t peer {};
   memcpy(peer.peer_addr, peerMac, 6);
   peer.channel = WifiChannel;
   peer.encrypt = false;

   if (esp_now_add_peer(&peer) != ESP_OK)
   {
      Serial.println("Error: ESP-NOW peer fehlgeschlagen");
      return false;
   }

   return true;
}

bool sendKey(int address)
{
   RemotePacket packet;
   packet.address = (uint8_t)address;
   packet.seq = ++sequence;

   sendDone = false;
   sendOk = false;

   if (esp_now_send(peerMac, (const uint8_t*)&packet, sizeof(packet)) != ESP_OK)
   {
      Serial.println("Error: ESP-NOW send fehlgeschlagen");
      return false;
   }

   unsigned long start {millis()};

   while (!sendDone && millis() - start < (unsigned long)SendTimeoutMs)
      delay(1);

   Serial.printf("Taste Adresse %d, seq %d -> %s\n", address, packet.seq, sendOk ? "ok" : "keine Bestaetigung");

   return sendOk;
}

//***************************************************************************
// Arduino
//***************************************************************************

void setup()
{
   Serial.begin(115200);

   pinMode(WakePin, INPUT_PULLUP);

   for (int i {0}; i < ButtonCount; i++)
      pinMode(ButtonPins[i], INPUT_PULLUP);

   bool wokeByButton {esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_GPIO};
   Serial.printf("\n[BOOT] Kenwood Remote, Grund: %s\n", wokeByButton ? "Taste" : "Power/Reset");

   if (!parseMac(PeerMacStr, peerMac))
   {
      Serial.printf("Error: Ungueltige MAC '%s'\n", PeerMacStr);
      goToSleep();
   }

   int button {pressedButton()};

   if (button < 0)
   {
      // Reset, erstes Einschalten oder Taste schon wieder losgelassen

      if (!DebugStayAwake)
         goToSleep();

      Serial.println("Debug: bleibe wach, warte auf Tasten");
      return;
   }

   if (!setupEspNow())
      goToSleep();

   int address {ButtonAddresses[button]};
   sendKey(address);

   // Taste gehalten -> wiederholen (Lautstaerke), mit Sicherheitsgrenze

   unsigned long lastSend {millis()};
   int repeats {0};
   unsigned long nextDelay {(unsigned long)RepeatDelayMs};

   while (pressedButton() == button && repeats < MaxRepeats)
   {
      if (millis() - lastSend >= nextDelay)
      {
         sendKey(address);
         lastSend = millis();
         nextDelay = RepeatIntervalMs;
         repeats++;
      }

      delay(5);
   }

   if (!DebugStayAwake)
      goToSleep();
}

void loop()
{
   // nur im Debug-Betrieb (DebugStayAwake) erreicht: Tasten pollen statt schlafen

   static bool espNowReady {false};

   int button {pressedButton()};

   if (button < 0)
   {
      delay(20);
      return;
   }

   if (!espNowReady)
      espNowReady = setupEspNow();

   if (espNowReady)
      sendKey(ButtonAddresses[button]);

   while (pressedButton() == button)
      delay(20);
}
