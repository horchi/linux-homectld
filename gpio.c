//***************************************************************************
// Automation Control
// File gpio.c
// This code is distributed under the terms and conditions of the
// GNU GENERAL PUBLIC LICENSE. See the file LICENSE for details.
// Date 16.04.2024 Jörg Wendel
//***************************************************************************

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <chrono>

#include "lib/json.h"
#include "gpio.h"

#if defined(GPIOD)

//***************************************************************************
// Find Chip For Line
// Scan all /dev/gpiochipN for a line with the given name.
// Returns the chip (caller must close it) and sets *offsetOut, nullptr on failure.
//***************************************************************************

gpiod_chip* Gpio::findChipForLine(const char* lineName, unsigned int* offsetOut)
{
   for (int i = 0; i < 16; ++i)
   {
      char path[32];
      snprintf(path, sizeof(path), "/dev/gpiochip%d", i);

      gpiod_chip* chip {gpiod_chip_open(path)};

      if (!chip)
         continue;

      int off {gpiod_chip_get_line_offset_from_name(chip, lineName)};

      if (off >= 0)
      {
         *offsetOut = static_cast<unsigned int>(off);
         return chip;
      }

      gpiod_chip_close(chip);
   }

   return nullptr;
}

//***************************************************************************
// Build Phys Map
// Scan all gpiochips for PIN_<N> named lines and build phys -> chip+offset map.
//***************************************************************************

int Gpio::buildPhysMap()
{
   physToChip.clear();

   for (int i = 0; i < 16; ++i)
   {
      char path[32];
      snprintf(path, sizeof(path), "/dev/gpiochip%d", i);

      gpiod_chip* chip {gpiod_chip_open(path)};

      if (!chip)
         continue;

      gpiod_chip_info* cinfo {gpiod_chip_get_info(chip)};
      unsigned int nlines = cinfo ? gpiod_chip_info_get_num_lines(cinfo) : 0;

      for (unsigned int j = 0; j < nlines; ++j)
      {
         gpiod_line_info* linfo {gpiod_chip_get_line_info(chip, j)};

         if (!linfo)
            continue;

         const char* name {gpiod_line_info_get_name(linfo)};

         if (name && strncmp(name, "PIN_", 4) == 0)
         {
            int phys {atoi(name + 4)};

            if (phys > 0)
            {
               physToChip[phys] = {std::string(path), j};

               auto it = pinMapping.find(phys);

               if (it != pinMapping.end())
               {
                  PinInfo& info {it->second};

                  // Blocked: line already claimed by another consumer
                  info.blocked = gpiod_line_info_is_used(linfo);
                  info.usable  = info.gpio && !info.blocked;
               }
            }
         }

         gpiod_line_info_free(linfo);
      }

      if (cinfo)
         gpiod_chip_info_free(cinfo);

      gpiod_chip_close(chip);
   }

   tell(eloDebug, "GPIO: physToChip map built with %zu entries", physToChip.size());

   return success;
}

#endif

//***************************************************************************
// Constructor / Destructor
//***************************************************************************

Gpio::Gpio(const char* consumerName, const char* confPath)
   : programName {consumerName},
     configPath {confPath ? confPath : ""}
{
}

Gpio::~Gpio()
{
   for (auto& [physPin, p] : pins)
   {
      if (p.threadRunning)
         p.threadRunning = false;

      if (p.thread)
      {
         p.thread->join();
         delete p.thread;
         p.thread = nullptr;
      }

#if defined(GPIOD)
      if (p.request)
      {
         gpiod_line_request_release(p.request);
         p.request = nullptr;
      }

      if (p.chip)
      {
         gpiod_chip_close(p.chip);
         p.chip = nullptr;
      }
#endif
   }
}

//***************************************************************************
// Init
//***************************************************************************

int Gpio::init()
{
   if (!mappingLoaded)
   {
      if (loadMapping() != 0)
      {
         tell(eloAlways, "GPIO: Failed to load mapping");
         return fail;
      }
   }

#if defined(GPIOD)

   buildPhysMap();
   return success;

#elif defined(WIRINGPI)

   static bool initialized {false};

   if (!initialized)
   {
      if (wiringPiSetupPhys() < 0)
      {
         tell(eloAlways, "Error: wiringPiSetupPhys() failed");
         return fail;
      }

      initialized = true;
   }

   return success;

#else

   return success;

#endif
}

//***************************************************************************
// Load Mapping
//***************************************************************************

int Gpio::loadMapping()
{
   if (mappingLoaded)
      return success;

   std::string file {configPath};

   if (!file.empty() && file.back() != '/')
      file += "/";

   file += "gpio.json";

   json_t* root {jsonLoadFile(file.c_str())};

   if (!root)
   {
      tell(eloAlways, "GPIO: Could not load JSON mapping file '%s'", file.c_str());
      return fail;
   }

   char buf[256]{};
   FILE* f {std::fopen("/proc/device-tree/model", "r")};

   if (f)
   {
      size_t n {std::fread(buf, 1, sizeof(buf) - 1, f)};
      buf[n] = '\0';
      modelName = buf;
      std::fclose(f);
   }
   else
   {
      modelName.clear();
   }

   int rc {loadJsonForModel(root, modelName)};

   json_decref(root);

   if (rc != success)
   {
      tell(eloAlways, "GPIO: No matching board model found in JSON");
      return fail;
   }

   detectBlockedPins();
   mappingLoaded = true;

   return success;
}

//***************************************************************************
// Load JSON For Model
//***************************************************************************

int Gpio::loadJsonForModel(json_t* root, const std::string& model)
{
   if (!root)
      return fail;

   json_t* boards {getObjectFromJson(root, "boards", nullptr)};

   if (!boards || !json_is_array(boards))
   {
      tell(eloAlways, "GPIO: 'boards' is missing or not an array");
      return fail;
   }

   size_t index {};
   json_t* board {};

   json_array_foreach(boards, index, board)
   {
      if (!json_is_object(board))
         continue;

      json_t* models {getObjectFromJson(board, "models", nullptr)};

      if (!models || !json_is_array(models))
         continue;

      bool match {false};
      size_t mi {};
      json_t* m {};

      json_array_foreach(models, mi, m)
      {
         if (!json_is_string(m))
            continue;

         const char* ms {json_string_value(m)};

         if (ms && model.find(ms) != std::string::npos)
         {
            match = true;
            break;
         }
      }

      if (!match)
         continue;

      json_t* pinsObj {getObjectFromJson(board, "pins", nullptr)};

      if (!pinsObj || !json_is_object(pinsObj))
         continue;

      const char* key {};
      json_t* val {};

      json_object_foreach(pinsObj, key, val)
      {
         if (!key || !json_is_object(val))
            continue;

         int phys {atoi(key)};

         if (phys <= 0)
            continue;

         PinInfo info {};
         info.name          = getStringFromJson(val, "name", key);
         info.gpio          = getBoolFromJson(val,   "gpio", false);
         info.pull          = getBoolFromJson(val,   "pull", false);
         info.interrupt     = getBoolFromJson(val,   "interrupt", false);
         info.voltage       = getStringFromJson(val, "voltage", "3.3V");
         info.description   = getStringFromJson(val, "description", "");
         info.blocked       = false;
         info.usable        = false;

         pinMapping[phys] = info;
      }

      return success;
   }

   return fail;
}

//***************************************************************************
// Detect Blocked Pins
//***************************************************************************

int Gpio::detectBlockedPins()
{
   for (auto& [phys, info] : pinMapping)
      info.blocked = false;

#if defined(GPIOD)

   std::ifstream in("/sys/kernel/debug/gpio");

   if (!in.is_open())
   {
      for (auto& [phys, info] : pinMapping)
         info.usable = info.gpio;

      return success;
   }

   std::string line;

   while (std::getline(in, line))
   {
      if (line.find("gpio-") == std::string::npos)
         continue;

      std::istringstream iss(line);
      std::string gpioToken;
      iss >> gpioToken;

      if (gpioToken.rfind("gpio-", 0) != 0)
         continue;

      bool isAlt {line.find("alt")  != std::string::npos};
      bool isUsed {line.find("used") != std::string::npos};

      if (!isAlt && !isUsed)
         continue;
   }

#endif

   for (auto& [phys, info] : pinMapping)
      info.usable = info.gpio && !info.blocked;

   return success;
}

//***************************************************************************
// Setup Pin
//***************************************************************************

int Gpio::setupPin(int physPin, Direction dir, Edge edge, PullUpDown pud)
{
   auto itInfo = pinMapping.find(physPin);

   if (itInfo == pinMapping.end())
   {
      tell(eloAlways, "GPIO: setupPin(%d) unknown pin", physPin);
      return fail;
   }

   const PinInfo& info {itInfo->second};

   if (!info.usable)
   {
      tell(eloAlways, "GPIO: setupPin(%d '%s') not usable (%s)", physPin, info.name.c_str(),
           !info.gpio ? "not a GPIO pin" : "blocked by kernel driver");
      return fail;
   }

   PinState& p {pins[physPin]};

#if defined(GPIOD)

   if (p.threadRunning)
      p.threadRunning = false;

   if (p.thread)
   {
      p.thread->join();
      delete p.thread;
      p.thread = nullptr;
   }

   if (p.request)
   {
      gpiod_line_request_release(p.request);
      p.request = nullptr;
   }

   if (p.chip)
   {
      gpiod_chip_close(p.chip);
      p.chip = nullptr;
   }

   // PIN_<N> convention (e.g. ODROID N2+ Hardkernel kernel)
   auto itPhys = physToChip.find(physPin);

   if (itPhys != physToChip.end())
   {
      p.chip   = gpiod_chip_open(itPhys->second.chipPath.c_str());
      p.offset = itPhys->second.offset;
   }
   else
   {
      // Fallback: use name field as kernel line name (e.g. RPi: GPIO17)
      p.chip = findChipForLine(info.name.c_str(), &p.offset);
   }

   if (!p.chip)
   {
      tell(eloAlways, "GPIO: Could not find gpiochip for pin %d ('%s')", physPin, info.name.c_str());
      return fail;
   }

   gpiod_line_settings* settings {gpiod_line_settings_new()};

   if (!settings)
   {
      tell(eloAlways, "GPIO: Could not allocate line settings");
      gpiod_chip_close(p.chip);
      p.chip = nullptr;
      return fail;
   }

   gpiod_line_settings_set_direction
   (
      settings,
      dir == dirIn ? GPIOD_LINE_DIRECTION_INPUT : GPIOD_LINE_DIRECTION_OUTPUT
   );

   switch (pud)
   {
      case pudUp:   gpiod_line_settings_set_bias(settings, GPIOD_LINE_BIAS_PULL_UP);   break;
      case pudDown: gpiod_line_settings_set_bias(settings, GPIOD_LINE_BIAS_PULL_DOWN); break;
      default:      gpiod_line_settings_set_bias(settings, GPIOD_LINE_BIAS_DISABLED);  break;
   }

   switch (edge)
   {
      case edgeRising:  gpiod_line_settings_set_edge_detection(settings, GPIOD_LINE_EDGE_RISING);  break;
      case edgeFalling: gpiod_line_settings_set_edge_detection(settings, GPIOD_LINE_EDGE_FALLING); break;
      case edgeBoth:    gpiod_line_settings_set_edge_detection(settings, GPIOD_LINE_EDGE_BOTH);    break;
      default:          gpiod_line_settings_set_edge_detection(settings, GPIOD_LINE_EDGE_NONE);    break;
   }

   gpiod_line_config* lcfg {gpiod_line_config_new()};

   if (!lcfg)
   {
      tell(eloAlways, "GPIO: Could not allocate line config");
      gpiod_line_settings_free(settings);
      gpiod_chip_close(p.chip);
      p.chip = nullptr;
      return fail;
   }

   gpiod_line_config_add_line_settings(lcfg, &p.offset, 1, settings);
   gpiod_line_settings_free(settings);

   gpiod_request_config* rcfg {gpiod_request_config_new()};
   gpiod_request_config_set_consumer(rcfg, programName.c_str());

   p.request = gpiod_chip_request_lines(p.chip, rcfg, lcfg);

   gpiod_line_config_free(lcfg);
   gpiod_request_config_free(rcfg);

   if (!p.request)
   {
      tell(eloAlways, "GPIO: Could not request gpio line for pin %d", physPin);
      gpiod_chip_close(p.chip);
      p.chip = nullptr;
      return fail;
   }

   p.initialized = true;
   p.direction   = dir;
   p.pud         = pud;
   p.edge        = edge;

   gpiod_line_value v {GPIOD_LINE_VALUE_INACTIVE};

   if (dir == dirIn)
      v = gpiod_line_request_get_value(p.request, p.offset);

   p.lastValue = (v == GPIOD_LINE_VALUE_ACTIVE);

   return success;

#elif defined(WIRINGPI)

   ::pinMode(physPin, dir == dirIn ? INPUT : OUTPUT);

   switch (pud)
   {
      case pudUp:   ::pullUpDnControl(physPin, PUD_UP);   break;
      case pudDown: ::pullUpDnControl(physPin, PUD_DOWN); break;
      default:      ::pullUpDnControl(physPin, PUD_OFF);  break;
   }

   p.initialized = true;
   p.direction   = dir;
   p.pud         = pud;
   p.edge        = edge;
   p.lastValue   = (dir == dirIn) ? (::digitalRead(physPin) != 0) : false;

   return success;

#else

   p.initialized = true;
   p.direction   = dir;
   p.pud         = pud;
   p.edge        = edge;
   p.lastValue   = false;

   return success;

#endif
}

//***************************************************************************
// Pin Available
//***************************************************************************

bool Gpio::pinAvailable(int physPin)
{
   auto it = pinMapping.find(physPin);
   return it != pinMapping.end() && it->second.usable;
}

//***************************************************************************
// Digital Read
//***************************************************************************

bool Gpio::digitalRead(int physPin)
{
   auto it = pins.find(physPin);

   if (it == pins.end() || !it->second.initialized)
   {
      tell(eloAlways, "GPIO: digitalRead(%d) pin not initialized", physPin);
      return false;
   }

   PinState& p {it->second};

#if defined(GPIOD)

   return gpiod_line_request_get_value(p.request, p.offset) == GPIOD_LINE_VALUE_ACTIVE;

#elif defined(WIRINGPI)

   return ::digitalRead(physPin) != 0;

#else

   return p.lastValue;

#endif
}

//***************************************************************************
// Digital Write
//***************************************************************************

int Gpio::digitalWrite(int physPin, bool value)
{
   auto it = pins.find(physPin);

   if (it == pins.end() || !it->second.initialized)
   {
      tell(eloAlways, "GPIO: digitalWrite(%d) pin not initialized", physPin);
      return fail;
   }

   PinState& p {it->second};

   if (p.direction != dirOut)
   {
      tell(eloAlways, "GPIO: digitalWrite(%d) not output", physPin);
      return fail;
   }

#if defined(GPIOD)

   int ret {gpiod_line_request_set_value(p.request, p.offset, value ? GPIOD_LINE_VALUE_ACTIVE : GPIOD_LINE_VALUE_INACTIVE)};

   if (ret == 0)
   {
      p.lastValue = value;
      return success;
   }

   return fail;

#elif defined(WIRINGPI)

   ::digitalWrite(physPin, value ? HIGH : LOW);
   p.lastValue = value;
   return success;

#else

   p.lastValue = value;
   return success;

#endif
}

//***************************************************************************
// Digital Toggle
//***************************************************************************

int Gpio::digitalToggle(int physPin)
{
   auto it = pins.find(physPin);

   if (it == pins.end() || !it->second.initialized)
   {
      tell(eloAlways, "GPIO: digitalToggle(%d) pin not initialized", physPin);
      return fail;
   }

   PinState& p {it->second};

   if (p.direction != dirOut)
   {
      tell(eloAlways, "GPIO: digitalToggle(%d) not output", physPin);
      return fail;
   }

#if defined(GPIOD)

   gpiod_line_value current {gpiod_line_request_get_value(p.request, p.offset)};
   gpiod_line_value next {current == GPIOD_LINE_VALUE_ACTIVE ? GPIOD_LINE_VALUE_INACTIVE : GPIOD_LINE_VALUE_ACTIVE};
   int ret {gpiod_line_request_set_value(p.request, p.offset, next)};

   if (ret == 0)
   {
      p.lastValue = !p.lastValue;
      return success;
   }

   return fail;

#elif defined(WIRINGPI)

   ::digitalWrite(physPin, ::digitalRead(physPin) ? LOW : HIGH);
   p.lastValue = !p.lastValue;
   return success;

#else

   p.lastValue = !p.lastValue;
   return success;

#endif
}

//***************************************************************************
// Pin Mode
//***************************************************************************

int Gpio::pinMode(int physPin, Direction direction)
{
   auto it = pins.find(physPin);
   Edge       edge {edgeNone};
   PullUpDown pud {pudOff};

   if (it != pins.end() && it->second.initialized)
   {
      edge = it->second.edge;
      pud  = it->second.pud;
   }

   return setupPin(physPin, direction, edge, pud);
}

//***************************************************************************
// Pull Up/Down Control
//***************************************************************************

int Gpio::pullUpDnControl(int physPin, PullUpDown value)
{
   auto it = pins.find(physPin);
   Edge      edge {edgeNone};
   Direction dir {dirIn};

   if (it != pins.end() && it->second.initialized)
   {
      edge = it->second.edge;
      dir  = it->second.direction;
   }

   return setupPin(physPin, dir, edge, value);
}

//***************************************************************************
// Enable Interrupt
//***************************************************************************

int Gpio::enableInterrupt(int physPin, std::function<void(int, bool)> cb)
{
   auto it = pins.find(physPin);

   if (it == pins.end() || !it->second.initialized)
   {
      tell(eloAlways, "GPIO: enableInterrupt(%d) pin not initialized", physPin);
      return fail;
   }

   PinState& p {it->second};

   if (p.direction != dirIn)
   {
      tell(eloAlways, "GPIO: enableInterrupt(%d) not input", physPin);
      return fail;
   }

   if (p.edge == edgeNone)
   {
      tell(eloAlways, "GPIO: enableInterrupt(%d) no edge configured", physPin);
      return fail;
   }

   if (p.threadRunning)
      return success;

   p.callback      = cb;
   p.threadRunning = true;

   p.thread = new std::thread
   (
      [this, physPin]()
      {
         PinState& ps {pins[physPin]};

#if defined(GPIOD)

         while (ps.threadRunning)
         {
            int ret {gpiod_line_request_wait_edge_events(ps.request, 100000000LL)};

            if (ret < 0)
               break;

            if (ret == 0)
               continue;

            gpiod_edge_event_buffer* evbuf {gpiod_edge_event_buffer_new(8)};
            gpiod_line_request_read_edge_events(ps.request, evbuf, 8);
            gpiod_edge_event_buffer_free(evbuf);

            gpiod_line_value val {gpiod_line_request_get_value(ps.request, ps.offset)};
            bool bval {val == GPIOD_LINE_VALUE_ACTIVE};

            if (ps.callback)
               ps.callback(physPin, bval);

            ps.lastValue = bval;
         }

#else

         while (ps.threadRunning)
         {
            bool v {digitalRead(physPin)};
            bool rising {!ps.lastValue && v};
            bool falling {ps.lastValue && !v};
            bool fire {false};

            switch (ps.edge)
            {
               case edgeRising:  fire = rising;            break;
               case edgeFalling: fire = falling;           break;
               case edgeBoth:    fire = rising || falling; break;
               default:          fire = false;             break;
            }

            if (fire && ps.callback)
               ps.callback(physPin, v);

            ps.lastValue = v;

            std::this_thread::sleep_for(std::chrono::milliseconds(5));
         }

#endif
      }
   );

   return success;
}

//***************************************************************************
// Disable Interrupt
//***************************************************************************

int Gpio::disableInterrupt(int physPin)
{
   auto it = pins.find(physPin);

   if (it == pins.end())
      return success;

   PinState& p {it->second};

   if (!p.threadRunning)
      return success;

   p.threadRunning = false;

   if (p.thread)
   {
      p.thread->join();
      delete p.thread;
      p.thread = nullptr;
   }

   return success;
}

//***************************************************************************
// Set ISR
//***************************************************************************

int Gpio::setIsr(int physPin, Edge edge, void (*func)(void))
{
   PullUpDown pud {pudOff};
   auto it = pins.find(physPin);

   if (it != pins.end() && it->second.initialized)
      pud = it->second.pud;

   if (setupPin(physPin, dirIn, edge, pud) != success)
      return fail;

   return enableInterrupt(physPin, [func](int, bool) { func(); });
}

//***************************************************************************
// Pin To Name
//***************************************************************************

std::string Gpio::pinToName(int physPin)
{
   auto it = pinMapping.find(physPin);

   if (it != pinMapping.end())
      return it->second.name;

   return {};
}

//***************************************************************************
// Get Pin List
//***************************************************************************

int Gpio::getPinList(std::map<int, PinInfo>& out)
{
   out.clear();

   if (!mappingLoaded)
   {
      if (loadMapping() != 0)
         return fail;
   }

   out = pinMapping;
   return success;
}
