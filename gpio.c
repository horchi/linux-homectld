//***************************************************************************
// Automation Control
// File gpio.h
// This code is distributed under the terms and conditions of the
// GNU GENERAL PUBLIC LICENSE. See the file LICENSE for details.
// Date 16.04.2024 Jörg Wendel
//***************************************************************************

#include "gpio.h"

// //***************************************************************************
// // Physical Pin To GPIO Name
// //***************************************************************************

// const char* physToGpioName_odroid_n2[64]
// {
//    // physical header pin number to gpio name

//    nullptr,                                           // kein pin 0
//    "3.3V",                 "5.0V",                    //  1 |  2
//    "GPIOX.17 (I2C-2_SDA)", "5.0V",                    //  3 |  4
//    "GPIOX.18 (I2C-2_SCL)", "GND",                     //  5 |  6
//    "GPIOA.13",             "GPIOX.12 (UART_TX_B)",     //  7 |  8
//    "GND",                  "GPIOX.13 (UART_RX_B)",     //  9 | 10
//    "GPIOX.3",              "GPIOX.16 (PWM_E)",         // 11 | 12
//    "GPIOX.4",              "GND",                     // 13 | 14
//    "GPIOX.7 (PWM_F)",      "GPIOX.0",                 // 15 | 16
//    "3.3V",                 "GPIOX.1",                 // 17 | 18
//    "GPIOX.8 (SPI_MOSI)",   "GND",                     // 19 | 20
//    "GPIOX.9 (SPI_MISO)",   "GPIOX.2",                 // 21 | 22
//    "GPIOX.11 (SPI_SCLK)",  "GPIOX.10 (SPI_CE0)",       // 23 | 24
//    "GND",                  "GPIOA.4 (SPI_CE1)",        // 25 | 26
//    "GPIOA.14 (I2C-3_SDA)", "GPIOA.15 (I2C-3_SCL)",     // 27 | 28
//    "GPIOX.14",             "GND",                     // 29 | 30
//    "GPIOX.15",             "GPIOA.12",                // 31 | 32
//    "GPIOX.5 (PWM_C)",      "GND",                     // 33 | 34
//    "GPIOX.6 (PWM_D)",      "GPIOX.19",                // 35 | 36
//    "ADC.AIN3",             "1.8V REF OUT",            // 37 | 38
//    "GND",                  "ADC.AIN2",                // 39 | 40

//    // Not used

//    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,  // 41...48
//    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,  // 49...56
//    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr            // 57...63
// };

// const char* physToGpioName_raspberry_pi[64]
// {
//    // physical header pin number to gpio name

//    nullptr,                                           // kein pin 0
//    "3.3V",                 "5.0V",                    //  1 |  2
//    "GPIOX.17 (I2C-2_SDA)", "5.0V",                    //  3 |  4
//    "GPIOX.18 (I2C-2_SCL)", "GND",                     //  5 |  6
//    "GPIOA.13",             "GPIOX.12 (UART_TX_B)",     //  7 |  8
//    "GND",                  "GPIOX.13 (UART_RX_B)",     //  9 | 10
//    "GPIOX.3",              "GPIOX.16 (PWM_E)",         // 11 | 12
//    "GPIOX.4",              "GND",                     // 13 | 14
//    "GPIOX.7 (PWM_F)",      "GPIOX.0",                 // 15 | 16
//    "3.3V",                 "GPIOX.1",                 // 17 | 18
//    "GPIOX.8 (SPI_MOSI)",   "GND",                     // 19 | 20
//    "GPIOX.9 (SPI_MISO)",   "GPIOX.2",                 // 21 | 22
//    "GPIOX.11 (SPI_SCLK)",  "GPIOX.10 (SPI_CE0)",       // 23 | 24
//    "GND",                  "GPIOA.4 (SPI_CE1)",        // 25 | 26
//    "GPIOA.14 (I2C-3_SDA)", "GPIOA.15 (I2C-3_SCL)",     // 27 | 28
//    "GPIOX.14",             "GND",                     // 29 | 30
//    "GPIOX.15",             "GPIOA.12",                // 31 | 32
//    "GPIOX.5 (PWM_C)",      "GND",                     // 33 | 34
//    "GPIOX.6 (PWM_D)",      "GPIOX.19",                // 35 | 36
//    "ADC.AIN3",             "1.8V REF OUT",            // 37 | 38
//    "GND",                  "ADC.AIN2",                // 39 | 40

//    // Not used

//    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,  // 41...48
//    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,  // 49...56
//    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr            // 57...63
// };

// #ifndef _NO_RASPBERRY_PI_

// //***************************************************************************
// // compile WITH wiringPi
// //***************************************************************************

// const char* physPinToGpioName(int pin)
// {
//    if (pin < 0 || pin >= 64)
//       return "";

// #ifndef MODEL_ODROID_N2
//    if (!physToGpioName_raspberry_pi[pin])
//       return "";
//    return physToGpioName_raspberry_pi[pin];
// #else
//    if (!physToGpioName_odroid_n2[pin])
//       return "";
//    return physToGpioName_odroid_n2[pin];
// #endif
// }

// #else // _NO_RASPBERRY_PI_

// //***************************************************************************
// // compile WITHOPUT wiringPi
// //***************************************************************************

// const char* physPinToGpioName(int pin)
// {
//    return "";
// }

// int wiringPiISR(int, int, void (*)())
// {
//    return 0;
// }

// void pinMode(int pin, int mode)
// {
// }

// void pullUpDnControl(int pin, int value)
// {
// }

// int digitalWrite(uint pin, bool value)
// {
//    return 0;
// }

// int digitalRead(uint pin)
// {
//    return 0;
// }

// int wiringPiSetupPhys()
// {
//    return 0;
// }

// #endif // _NO_RASPBERRY_PI_

//***************************************************************************
// Detect Hardware Board Model
//***************************************************************************

std::string BoardDetector::detect()
{
   MemoryStruct data;
   std::string model;

   if (loadFromFile("/proc/device-tree/model", &data) == success)
   {
      if (data.memory)
      {
         model = std::string {data.memory, data.size};

         // remove null terminators

         model.erase(std::find(model.begin(), model.end(), '\0'), model.end());
      }
   }

   if (model.empty())
      return "unknown";

   std::transform(model.begin(), model.end(), model.begin(), ::tolower);

   if (model.find("odroid-n2") != std::string::npos)
      return "odroid_n2plus";

   if (model.find("raspberry pi") != std::string::npos)
      return "raspberry_pi";

   tell(eloAlways, "Fatal: GPIO: Unexpected model '%s' detected!", model.c_str());

   return "unknown";
}

//***************************************************************************
// Gpio
//***************************************************************************

Gpio::~Gpio()
{
#ifdef LIBGPIOD_V2

   for (auto const& [phys, request] : activeRequests)
      gpiod_line_request_release(request);

#else

   for (auto const& [phys, line] : activeLines)
      gpiod_line_release(line);

#endif

   for (auto const& [name, chip] : openChips)
      gpiod_chip_close(chip);
}

//***************************************************************************
// Init
//***************************************************************************

int Gpio::init(const char* confDir)
{
#ifdef HAVE_GPIOD
   tell(eloAlways, "Setup gpiod interface ..");

   // detect board

   std::string board {BoardDetector::detect()};

   if (board != "unknown")
   {
      tell(eloAlways, "Info: Detected board type '%s'", board.c_str());
      setBoardName(board.c_str());

      // load gpio mapping config

      char* file {};
      asprintf(&file, "%s/gpio.json", confDir);

      if (loadConfig(file) != success)
         tell(eloAlways, "Error: Reading of gpio mapping configuration failed '%s'", file);
      else
         tell(eloDebugWiringPi, "Info: Read %zu gpio mappings", pinMap.size());

      free(file);

      if (available())
      {
         // // Example: Kühlschrank an Pin 12 überwachen

         // if (setupGpioPin(12, dirIn, Gpio::edgeFalling))
         //    tell(eloAlways, "Started monitoring for '%s'", board.c_str());
      }
   }

   tell(eloAlways, ".. done");

   return success;
#endif

   return fail;
}

//***************************************************************************
// Load Configuration from JSON
//***************************************************************************

int Gpio::loadConfig(const char* filePath)
{
   json_t* root {jsonLoadFile(filePath)};

   if (!root)
      return fail;

   std::string path {"boards/" + boardName};
   json_t* boardObj {getObjectByPath(root, path.c_str())};

   if (!boardObj)
   {
      tell(eloAlways, "Error: Missing definition of GPIO mapping for board '%s'", path.c_str());
      json_decref(root);
      return fail;
   }

   const char* defaultChip {getStringFromJson(boardObj, "defaultChip", "gpiochip1")};
   json_t* mapping {getObjectFromJson(boardObj, "mapping")};
   size_t index {0};
   json_t* value {};

   json_array_foreach(mapping, index, value)
   {
      int phys {getIntFromJson(value, "phys", -1)};

      if (phys == -1)
         continue;

      PinConfig pc
      {
         .chip {defaultChip},
         .name {getStringFromJson(value, "name", "")},
         .offset {(unsigned int)getIntFromJson(value, "offset", 0)},
         .globalOffset {(unsigned int)getIntFromJson(value, "global", 0)}
      };

      pinMap[phys] = pc;
   }

   json_decref(root);
   initialzed = true;

   return success;
}

//***************************************************************************
// Check if Physical Pin is Available in Configuration
//***************************************************************************

bool Gpio::pinAvailable(int physPin)
{
   return pinMap.find(physPin) != pinMap.end();
}

//***************************************************************************
// Get Hardware Name from Physical Pin
//***************************************************************************

const char* Gpio::physPinToGpioName(int physPin)
{
   if (pinMap.find(physPin) != pinMap.end())
      return pinMap[physPin].name.c_str();

   return "<unknown>";
}

//***************************************************************************
// Pin Mode Wrapper for WiringPi Compatibility
//***************************************************************************

int Gpio::pinMode(int physPin, Direction direction)
{
   return setupGpioPin(physPin, direction, edgeNone);
}

//***************************************************************************
// Pull Up Down Control
//***************************************************************************

void Gpio::pullUpDnControl(int physPin, PullUpDown value)
{
   if (pinMap.find(physPin) == pinMap.end())
      return;

   pinPullModes[physPin] = value;

#ifdef LIBGPIOD_V2

   if (activeRequests.count(physPin))
   {
      struct gpiod_line_settings* settings {gpiod_line_settings_new()};
      int bias {GPIOD_LINE_BIAS_DISABLED};

      if (value == PUD_UP)
         bias = GPIOD_LINE_BIAS_PULL_UP;
      else if (value == PUD_DOWN)
         bias = GPIOD_LINE_BIAS_PULL_DOWN;

      gpiod_line_settings_set_bias(settings, bias);

      struct gpiod_line_config* lineCfg {gpiod_line_config_new()};
      unsigned int offset {pinMap[physPin].offset};

      gpiod_line_config_add_line_settings(lineCfg, &offset, 1, settings);
      gpiod_line_request_reconfigure_lines(activeRequests[physPin], lineCfg);

      gpiod_line_settings_free(settings);
      gpiod_line_config_free(lineCfg);
   }

#else
   // Workaround zum setzen des Pull Up/Down Wiedestanbdes mit libgpio V1.x
   //   funktioniert mit Kernel >= 5.5
   //   davor wird das generell nicht unterstützt und der Pegel ist 'floating'.
   //   Bedarf dann daher eines externen pull up/down oder anderweitig 'definierten' signals

   if (activeLines.count(physPin))
   {
      // Workaround für v1: Pin schließen und mit neuem Bias neu öffnen

      struct gpiod_line* line {activeLines[physPin]};

      // Wir merken uns die aktuelle Richtung

      Direction dir {dirIn};
      if (gpiod_line_direction(line) == GPIOD_LINE_DIRECTION_OUTPUT)
         dir = dirOut;

      // Wir merken uns die aktuelle Edge-Konfiguration (vereinfacht)
      // In der Praxis müsste man hier ggf. den Edge-Typ aus einem Member lesen

      gpiod_line_release(line);
      activeLines.erase(physPin);

      // Neu initialisieren mit dem jetzt in pinPullModes gespeicherten Wert

      setupGpioPin(physPin, dir, edgeNone, value);
   }
#endif
}

//***************************************************************************
// Setup GPIO Pin
//***************************************************************************

int Gpio::setupGpioPin(int physPin, Direction dir, Edge edge, PullUpDown pud)
{
   if (pinMap.find(physPin) == pinMap.end())
   {
      tell(eloAlways, "Info: Pin (%d) not defined in gpio mapping", physPin);
      return fail;
   }

   // Prüfen, ob der Pin bereits in einer der aktiven Maps existiert

   bool alreadyOpen {false};

#ifdef LIBGPIOD_V2
   if (activeRequests.count(physPin))
      alreadyOpen = true;
#else
   if (activeLines.count(physPin))
      alreadyOpen = true;
#endif

   if (alreadyOpen)
   {
      tell(eloAlways, "Info: Pin (%d) already initialzed", physPin);
      return success;
   }

   int activePud {pud};

   if (activePud == pudOff && pinPullModes.count(physPin))
      activePud = pinPullModes[physPin];
   else
      pinPullModes[physPin] = activePud;

   PinConfig& conf {pinMap[physPin]};

   if (openChips.find(conf.chip) == openChips.end())
   {
      struct gpiod_chip* chip {gpiod_chip_open_by_name(conf.chip.c_str())};

      if (!chip)
      {
         tell(eloAlways, "Error: Could not open chip '%s'", conf.chip.c_str());
         return fail;
      }

      openChips[conf.chip] = chip;
   }

   struct gpiod_chip* chip {openChips[conf.chip]};

#ifdef LIBGPIOD_V2

   unsigned int offset {conf.offset};
   int dynamicOffset {gpiod_chip_get_line_offset_from_name(chip, conf.name.c_str())};

   if (dynamicOffset >= 0)
      offset = (unsigned int)dynamicOffset;

   struct gpiod_line_settings* settings {gpiod_line_settings_new()};
   gpiod_line_settings_set_direction(settings, (dir == In) ? GPIOD_LINE_DIRECTION_INPUT : GPIOD_LINE_DIRECTION_OUTPUT);

   // Bias/Pull für v2 setzen
   int bias {GPIOD_LINE_BIAS_DISABLED};

   if (activePud == pudUp)
      bias = GPIOD_LINE_BIAS_PULL_UP;
   else if (activePud == pudDown)
      bias = GPIOD_LINE_BIAS_PULL_DOWN;

   gpiod_line_settings_set_bias(settings, bias);

   if (dir == In && edge != None)
   {
      int e {(edge == Rising) ? GPIOD_LINE_EDGE_RISING : (edge == Falling) ? GPIOD_LINE_EDGE_FALLING : GPIOD_LINE_EDGE_BOTH};

      gpiod_line_settings_set_edge_detection(settings, e);
   }

   struct gpiod_line_config* lineCfg {gpiod_line_config_new()};
   gpiod_line_config_add_line_settings(lineCfg, &offset, 1, settings);

   struct gpiod_line_request_config* reqCfg {gpiod_line_request_config_new()};
   gpiod_line_request_config_set_consumer(reqCfg, consumer.c_str());

   struct gpiod_line_request* request {gpiod_chip_request_lines(chip, reqCfg, lineCfg)};

   gpiod_line_settings_free(settings);
   gpiod_line_config_free(lineCfg);
   gpiod_line_request_config_free(reqCfg);

   if (!request)
      return fail;

   activeRequests[physPin] = request;

   return success;

#else

   // v1

   int status {fail};

   // 1. Zuerst Sysfs-Reste entfernen (falls vorhanden)

   char unexportCmd[64];
   snprintf(unexportCmd, sizeof(unexportCmd), "echo %d > /sys/class/gpio/unexport 2>/dev/null",
            pinMap[physPin].globalOffset);

   system(unexportCmd);

   struct gpiod_line* line {gpiod_chip_find_line(chip, conf.name.c_str())};

   if (!line)
      line = gpiod_chip_get_line(chip, conf.offset);

   if (!line)
   {
      tell(eloAlways, "Error: Could not get line for offset (%d) of pin '%s' (%d) failed", conf.offset, conf.name.c_str(), physPin);
      return fail;
   }

   if (dir == dirOut)
   {
      status = gpiod_line_request_output(line, consumer.c_str(), 0) == 0 ? success : fail;
   }

   else
   {
      // Bias/Pull für v1 (Kernel >= 5.5 erforderlich)

      int flags {0};

      if (activePud == pudUp)
         flags |= GPIOD_LINE_REQUEST_FLAG_BIAS_PULL_UP;
      else if (activePud == pudDown)
         flags |= GPIOD_LINE_REQUEST_FLAG_BIAS_PULL_DOWN;
      else
         flags |= GPIOD_LINE_REQUEST_FLAG_BIAS_DISABLE;

      if (edge == edgeNone)
         status = gpiod_line_request_input_flags(line, consumer.c_str(), flags) == 0 ? success : fail;

      else
      {
         struct gpiod_line_request_config config
            {
               consumer.c_str(),
               (edge == edgeRising) ? GPIOD_LINE_REQUEST_EVENT_RISING_EDGE :
               (edge == edgeFalling) ? GPIOD_LINE_REQUEST_EVENT_FALLING_EDGE :
               GPIOD_LINE_REQUEST_EVENT_BOTH_EDGES,
               flags
            };

         status = gpiod_line_request(line, &config, 0) == 0 ? success : fail;
      }
   }

   if (status != success)
   {
      tell(eloAlways, "Info: libgpiod failed for pin %d, trying Sysfs-Fallback ..", physPin);

      // berechnen dws globalen Offset (Basis 413 + lokaler Offset)

      status = setupSysfs(physPin, dir);
   }

   if (status != success)
   {
      tell(eloAlways, "Error: Initializing pin (%d/%d) as '%s' failed, pud = '%s' edge = '%s' system error '%s'",
           physPin, pinMap[physPin].globalOffset,
           dir == dirOut ? "OUTPUT" : "INPUT",
           edge == edgeFalling ? "falling" : edge == edgeRising ? "rising" : edge == edgeBoth ? "both" : "none",
           activePud == pudUp ? "up" : activePud == pudDown ? "down" : "none",
           strerror(errno));
   }
   else
   {
      tell(eloDebugWiringPi, "Debug: WiringPi: Initialzed pin (%d) successful as '%s'", physPin, dir == dirOut ? "OUTPUT" : "INPUT");

      if (isSysfsPin.find(physPin) == isSysfsPin.end())
         activeLines[physPin] = line;
   }

   return status;
#endif
}

//***************************************************************************
// Setup Sysfs Fallback (mit festem Global-Offset aus JSON)
//***************************************************************************

int Gpio::setupSysfs(int physPin, Direction dir)
{
   uint gOffset {pinMap[physPin].globalOffset};
   char cmd[128] {};
   char path[128] {};

   snprintf(cmd, sizeof(cmd), "echo %d > /sys/class/gpio/export 2>/dev/null", gOffset);
   system(cmd);

   // 2. Warten bis der Pfad existiert (udev/Kernel Latenz)

   snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/direction", gOffset);

   int retry {50}; // Max 150ms warten

   while (access(path, F_OK) != 0 && retry-- > 0)
      usleep(10000); // 10ms Schritte

   if (retry <= 0)
   {
      tell(eloAlways, "Error: Sysfs path %s did not appear in time", path);
      return fail;
   }

   snprintf(cmd, sizeof(cmd), "echo %s > /sys/class/gpio/gpio%d/direction",
            dir == dirIn ? "in" : "out", gOffset);

   if (system(cmd) == 0)
   {
      isSysfsPin[physPin] = true;
      tell(eloDebugWiringPi, "Info: WiringPi: Pin (%d) successfully initialized via Sysfs-Fallback", physPin);
      return success;
   }

   return fail;
}

//***************************************************************************
// Find Correct Chip and Offset
//***************************************************************************

int Gpio::autoResolvePin(int physPin)
{
   PinConfig& conf {pinMap[physPin]};

   // Wir scannen alle im System gemeldeten Chips

   for (int i {0}; i < 1024; i++)
   {
      std::string chipPath {"gpiochip" + std::to_string(i)};
      struct gpiod_chip* chip {gpiod_chip_open_by_name(chipPath.c_str())};

      if (!chip)
         continue;

      // Wir suchen die Line anhand des Namens (z.B. "GPIOX_12")
      struct gpiod_line* line {gpiod_chip_find_line(chip, conf.name.c_str())};

      if (line)
      {
         conf.chip = chipPath;
         conf.offset = gpiod_line_offset(line);

         gpiod_chip_close(chip);
         return success;
      }

      gpiod_chip_close(chip);
   }

   return fail;
}

//***************************************************************************
// Read Digital Input
//***************************************************************************

bool Gpio::digitalRead(int physPin)
{
   if (!available())
      return false;

   if (isSysfsPin.count(physPin))
   {
      char path[64];
      char valStr[4];
      unsigned int globalOffset {413 + pinMap[physPin].offset};

      snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", globalOffset);

      FILE* fp {fopen(path, "r")};

      if (!fp)
         return false;

      if (fgets(valStr, sizeof(valStr), fp))
      {
         fclose(fp);
         return (valStr[0] == '1');
      }

      fclose(fp);
      return false;
   }

#ifdef LIBGPIOD_V2
   if (activeRequests.count(physPin))
   {
      int val {gpiod_line_request_get_value(activeRequests[physPin], pinMap[physPin].offset)};
      return (val > 0);
   }
#else
   if (activeLines.count(physPin))
   {
      int val {gpiod_line_get_value(activeLines[physPin])};

      if (val < 0)
      {
         tell(eloAlways, "Error: Kernel read failed for pin %d (errno %s)", physPin, strerror(errno));
         return false;
      }

      // tell(eloAlways, "Read %d -> %d returning (%d)", physPin, val, (bool)val > 0);

      return val > 0;
   }
#endif

   tell(eloAlways, "Error: Reading gpio pin %d failed - pin not initialized!", physPin);

   return false;
}

//***************************************************************************
// Write Digital Output (horchi-style with Sysfs Fallback)
//***************************************************************************

int Gpio::digitalWrite(int physPin, int value)
{
   if (!available())
      return fail;

   if (isSysfsPin.count(physPin))
   {
      char path[64];
      unsigned int globalOffset {413 + pinMap[physPin].offset};

      snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", globalOffset);

      FILE* fp {fopen(path, "w")};

      if (!fp)
         return fail;

      fprintf(fp, "%d", value > 0 ? 1 : 0);
      fclose(fp);

      return success;
   }

   int status {fail};

#ifdef LIBGPIOD_V2
   if (activeRequests.count(physPin))
      status = gpiod_line_request_set_value(activeRequests[physPin], pinMap[physPin].offset, value) == 0 ? success : fail;
#else
   if (activeLines.count(physPin))
      status = gpiod_line_set_value(activeLines[physPin], value) == 0 ? success : fail;
#endif

   if (status != success)
   {
      tell(eloAlways, "Error: Kernel rejected value %d for pin %d (errno %s)",
           value, physPin, strerror(errno));
   }

   return status;
}

//***************************************************************************
// Wait for GPIO Event
//***************************************************************************

Gpio::Event Gpio::waitForEvent(int physPin, int timeoutMs)
{
   if (!available())
      return evtEventError;

#ifdef LIBGPIOD_V2

   if (!activeRequests.count(physPin))
      return EventError;

   int res {gpiod_line_request_wait_edge_events(activeRequests[physPin], timeoutMs * 1000000LL)};

   if (res <= 0)
      return res == 0 ? EventNone : EventError;

   struct gpiod_edge_event_buffer* buffer {gpiod_edge_event_buffer_new(1)};
   gpiod_line_request_read_edge_events(activeRequests[physPin], buffer, 1);
   struct gpiod_edge_event* ev {gpiod_edge_event_buffer_get_event(buffer, 0)};
   int type {gpiod_edge_event_get_event_type(ev)};
   gpiod_edge_event_buffer_free(buffer);

   return (type == GPIOD_EDGE_EVENT_RISING_EDGE) ? EventRising : EventFalling;

#else

   if (!activeLines.count(physPin))
      return evtEventError;

   struct timespec ts { timeoutMs / 1000, (timeoutMs % 1000) * 1000000 };
   int res {gpiod_line_event_wait(activeLines[physPin], &ts)};

   if (res <= 0)
      return (res == 0) ? evtEventNone : evtEventError;

   struct gpiod_line_event ev {};
   gpiod_line_event_read(activeLines[physPin], &ev);

   return (ev.event_type == GPIOD_LINE_EVENT_RISING_EDGE) ? evtEventRising : evtEventFalling;

#endif
}

//***************************************************************************
// Detect Blink Codes
//***************************************************************************

int Gpio::detectBlinkCode(int physPin, int timeoutMs)
{
   int pulseCount {0};
   auto startTime {std::chrono::steady_clock::now()};

   while (true)
   {
      Event ev {waitForEvent(physPin, timeoutMs)};

      if (ev == evtEventFalling)
      {
         pulseCount++;
         startTime = std::chrono::steady_clock::now();
      }
      else if (ev == evtEventNone || ev == evtEventError)
      {
         if (pulseCount > 0)
            return pulseCount;

         return 0;
      }

      auto elapsed {std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - startTime).count()};

      if (elapsed > 5)
         break;
   }

   return pulseCount;
}
