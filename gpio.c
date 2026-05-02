//***************************************************************************
// Automation Control
// File gpio.c
// This code is distributed under the terms and conditions of the
// GNU GENERAL PUBLIC LICENSE. See the file LICENSE for details.
// Date 16.04.2024 Jörg Wendel
//***************************************************************************

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <chrono>
#include <dirent.h>

#include "lib/json.h"
#include "gpio.h"

//***************************************************************************
// Constructor
//***************************************************************************

Gpio::Gpio(const char* consumerName, const char* confPath)
   : programName{consumerName},
     configPath{confPath ? confPath : ""}
{
}

//***************************************************************************
// Destructor
//***************************************************************************

Gpio::~Gpio()
{
   for (auto& it : pins)
   {
      PinState& p = it.second;

      if (p.threadRunning)
         p.threadRunning = false;

      if (p.thread)
      {
         p.thread->join();
         delete p.thread;
         p.thread = nullptr;
      }

#if defined (GPIOD)
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
// Normalize Pin Name
//***************************************************************************

std::string Gpio::normalize(const char* pinName)
{
   std::string s{pinName ? pinName : ""};

   while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
      s.erase(s.begin());

   while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
      s.pop_back();

   std::transform
   (
      s.begin(),
      s.end(),
      s.begin(),
      [](unsigned char c) { return static_cast<char>(std::toupper(c)); }
   );

   return s;
}

//***************************************************************************
// Init Backend
//***************************************************************************

int Gpio::init()
{
   if (!mappingLoaded)
   {
      if (loadMapping() != 0)
      {
         tell(0, "GPIO: Failed to load mapping");
         return fail;
      }
   }

#if defined (WIRINGPI)

   static bool initialized {false};

   if (!initialized)
   {
      int ret{wiringPiSetupPhys()};

      if (ret < 0)
      {
         tell(0, "Error: wiringPiSetupPhys() failed");
         return fail;
      }

      initialized = true;
   }

   return success;

#elif defined (GPIOD)

   return success;

#else

   return success;

#endif
}

//***************************************************************************
// Load Mapping (JSON)
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
      tell(0, "GPIO: Could not load JSON mapping file '%s'", file.c_str());
      return fail;
   }

   // Model aus /proc/device-tree/model lesen

   char buf[256] {};
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
      tell(0, "GPIO: No matching board model found in JSON");
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
      tell(0, "GPIO: 'boards' is missing or not an array");
      return fail;
   }

   size_t index {};
   json_t* board {};

   json_array_foreach(boards, index, board)
   {
      if (!json_is_object(board))
         continue;

      json_t* models{getObjectFromJson(board, "models", nullptr)};

      if (!models || !json_is_array(models))
         continue;

      bool match {false};

      size_t mi {0};
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

         PinInfo info{};

         info.phys          = getIntFromJson(val,    "phys", na);
         info.direction     = getStringFromJson(val, "direction", "inout");
         info.pull          = getStringFromJson(val, "pull", "off");
         info.interrupt     = getBoolFromJson(val,   "interrupt", false);
         info.oneWire       = getBoolFromJson(val,   "oneWire", false);
         info.safeForOutput = getBoolFromJson(val,   "safeForOutput", false);
         info.voltage       = getStringFromJson(val, "voltage", "3.3V");
         info.description   = getStringFromJson(val, "description", "");
         info.blocked       = false;
         info.usable        = false; // wird später gesetzt

         std::string name{normalize(key)};

         if (info.phys > 0)
         {
            pinMapping[name] = info;
         }
      }

      return success;
   }

   return fail;
}

//***************************************************************************
// Detect Blocked Pins (System)
//***************************************************************************

int Gpio::detectBlockedPins()
{
#if defined (GPIOD)

   // Default: alles unblocked

   for (auto& it : pinMapping)
      it.second.blocked = false;

   // Versuche /sys/kernel/debug/gpio zu lesen

   std::ifstream in("/sys/kernel/debug/gpio");

   if (!in.is_open())
   {
      // Fallback: usable = safeForOutput

      for (auto& it : pinMapping)
         it.second.usable = it.second.safeForOutput;

      return success;
   }

   std::string line;

   while (std::getline(in, line))
   {
      // grobe Heuristik: Zeilen mit "gpio-" enthalten Nummer und evtl. "used" oder "alt"
      // Beispiel:
      // gpio-2  (SDA1) in  hi IRQ
      // gpio-14 (TXD1) alt5 hi

      if (line.find("gpio-") == std::string::npos)
         continue;

      std::istringstream iss(line);
      std::string gpioToken;

      iss >> gpioToken;

      if (gpioToken.rfind("gpio-", 0) != 0)
         continue;

      int num {-1};

      try
      {
         num = std::stoi(gpioToken.substr(5));
      }
      catch (...)
      {
         continue;
      }

      bool isAlt {line.find("alt") != std::string::npos};
      bool isUsed {line.find("used") != std::string::npos};

      if (!isAlt && !isUsed)
         continue;

      // Wir haben eine belegte GPIO-Nummer, müssen aber physische Pins matchen.
      // Das ist board-spezifisch und in JSON nicht direkt abgebildet.
      // Hier lassen wir es bewusst generisch und setzen blocked nur über JSON,
      // falls du später eine Zuordnung gpio-Nummer -> phys ergänzt.
   }

   // usable ableiten

   for (auto& it : pinMapping)
      it.second.usable = it.second.safeForOutput && !it.second.blocked;

   return success;

#else

   for (auto& it : pinMapping)
   {
      it.second.blocked = false;
      it.second.usable  = it.second.safeForOutput;
   }

   return success;

#endif
}

//***************************************************************************
// Setup Pin
//***************************************************************************

int Gpio::setupPin(const char* pinName, Direction dir, Edge edge, PullUpDown pud)
{
   std::string key {normalize(pinName)};
   PinState& p {pins[key]};

   auto itInfo = pinMapping.find(key);

   if (itInfo == pinMapping.end())
   {
      tell(0, "GPIO: setupPin('%s') unknown pin", key.c_str());
      return fail;
   }

   const PinInfo& info = itInfo->second;

   if (!info.usable)
   {
      tell(0, "GPIO: setupPin('%s') not usable", key.c_str());
      return fail;
   }

#if defined (GPIOD)

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

   // Wir nutzen physische Pins nicht direkt bei gpiod, sondern Line-Namen.
   // Hier gehen wir davon aus, dass pinName dem Line-Namen entspricht.

   p.chip = gpiod_chip_open_lookup(key.c_str(), &p.offset);

   if (!p.chip)
   {
      tell(0, "GPIO: Could not find gpiochip for pin '%s'", key.c_str());
      return fail;
   }

   gpiod_line_settings* settings{gpiod_line_settings_new()};

   if (!settings)
   {
      tell(0, "GPIO: Could not allocate line settings");
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

   gpiod_line_config* lcfg{gpiod_line_config_new()};

   if (!lcfg)
   {
      tell(0, "GPIO: Could not allocate line config");
      gpiod_chip_close(p.chip);
      p.chip = nullptr;
      return fail;
   }

   gpiod_line_config_add_line_settings(lcfg, &p.offset, 1, settings);

   gpiod_request_config* rcfg{gpiod_request_config_new()};
   gpiod_request_config_set_consumer(rcfg, programName.c_str());

   p.request = gpiod_chip_request_lines(p.chip, rcfg, lcfg);

   if (!p.request)
   {
      tell(0, "GPIO: Could not request gpio line");
      gpiod_chip_close(p.chip);
      p.chip = nullptr;
      return fail;
   }

   p.initialized = true;
   p.direction   = dir;
   p.pud         = pud;
   p.edge        = edge;

   int v{0};

   if (dir == dirIn)
      v = gpiod_line_request_get_value(p.request, p.offset);

   p.lastValue = (v == 1);

   return success;

#elif defined (WIRINGPI)

   int physPin {nameToPin(key)};

   if (physPin <= 0)
   {
      tell(0, "GPIO: setupPin('%s') unknown phys pin", key.c_str());
      return fail;
   }

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

   int v{0};

   if (dir == dirIn)
   {
      v = ::digitalRead(physPin);
   }

   p.lastValue = (v != 0);

   return success;

#else

   // Stub: nichts tun, aber Fehler melden, wenn Pin nicht usable

   if (!info.usable)
      return fail;

   p.initialized = true;
   p.direction   = dir;
   p.pud         = pud;
   p.edge        = edge;
   p.lastValue   = false;

   return success;

#endif
}

int Gpio::setIsr(const char* pinName, Edge edge, void (*func)(void))
{
    if (!pinName || !*pinName)
    {
        tell(0, "GPIO: setIsr() invalid pinName");
        return -1;
    }

    int physPin = nameToPin(pinName);
    if (physPin <= 0)
    {
        tell(0, "GPIO: setIsr('%s') unknown pin", pinName);
        return -1;
    }

    // Wir nutzen die physische ISR-Implementierung
    return setIsrPhys(physPin, edge, func);
}

//***************************************************************************
// Digital Read
//***************************************************************************

bool Gpio::digitalRead(const char* pinName)
{
   std::string key{normalize(pinName)};
   auto it = pins.find(key);

   if (it == pins.end() || !it->second.initialized)
   {
      tell(0, "GPIO: digitalRead('%s') pin not initialized", key.c_str());
      return false;
   }

   PinState& p = it->second;

#if defined (GPIOD)

   int value {gpiod_line_request_get_value(p.request, p.offset)};

   return value == 1;

#elif defined (WIRINGPI)

   int physPin {nameToPin(key)};

   if (physPin <= 0)
   {
      tell(0, "GPIO: digitalRead('%s') unknown phys pin", key.c_str());
      return false;
   }

   int value{::digitalRead(physPin)};

   return value != 0;

#else

   return p.lastValue;

#endif
}

//***************************************************************************
// Digital Write
//***************************************************************************

int Gpio::digitalWrite(const char* pinName, bool value)
{
   std::string key{normalize(pinName)};
   auto it = pins.find(key);

   if (it == pins.end() || !it->second.initialized)
   {
      tell(0, "GPIO: digitalWrite('%s') pin not initialized", key.c_str());
      return fail;
   }

   PinState& p = it->second;

   if (p.direction != dirOut)
   {
      tell(0, "GPIO: digitalWrite('%s') not output", key.c_str());
      return fail;
   }

#if defined (GPIOD)

   int ret {gpiod_line_request_set_value(p.request, p.offset, value ? 1 : 0)};

   if (!ret)
   {
      p.lastValue = value;
      return success;
   }

   return fail;

#elif defined (WIRINGPI)

   int physPin {nameToPin(key)};

   if (physPin <= 0)
   {
      tell(0, "GPIO: digitalWrite('%s') unknown phys pin", key.c_str());
      return fail;
   }

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

int Gpio::digitalToggle(const char* pinName)
{
   std::string key {normalize(pinName)};
   auto it = pins.find(key);

   if (it == pins.end() || !it->second.initialized)
   {
      tell(0, "GPIO: digitalToggle('%s') pin not initialized", key.c_str());
      return fail;
   }

   PinState& p = it->second;

   if (p.direction != dirOut)
   {
      tell(0, "GPIO: digitalToggle('%s') not output", key.c_str());
      return fail;
   }

#if defined (GPIOD)

   int current {gpiod_line_request_get_value(p.request, p.offset)};
   int ret {gpiod_line_request_set_value(p.request, p.offset, current ? 0 : 1)};

   if (!ret)
   {
      p.lastValue = !p.lastValue;
      return success;
   }

   return fail;

#elif defined (WIRINGPI)

   int physPin {nameToPin(key)};

   if (physPin <= 0)
   {
      tell(0, "GPIO: digitalToggle('%s') unknown phys pin", key.c_str());
      return fail;
   }

   int current {::digitalRead(physPin)};

   ::digitalWrite(physPin, current ? LOW : HIGH);
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

int Gpio::pinMode(const char* pinName, Direction direction)
{
   std::string key {normalize(pinName)};
   auto it = pins.find(key);

   Edge edge {edgeNone};
   PullUpDown pud {pudOff};

   if (it != pins.end() && it->second.initialized)
   {
      edge = it->second.edge;
      pud  = it->second.pud;
   }

   return setupPin(pinName, direction, edge, pud);
}

//***************************************************************************
// Pull Up / Down Control
//***************************************************************************

int Gpio::pullUpDnControl(const char* pinName, PullUpDown value)
{
   std::string key {normalize(pinName)};
   auto it = pins.find(key);

   Edge edge {edgeNone};
   Direction dir {dirIn};

   if (it != pins.end() && it->second.initialized)
   {
      edge = it->second.edge;
      dir  = it->second.direction;
   }

   return setupPin(pinName, dir, edge, value);
}

//***************************************************************************
// Enable Interrupt
//***************************************************************************

int Gpio::enableInterrupt(const char* pinName, std::function<void(const char*, bool)> cb)
{
   std::string key {normalize(pinName)};
   auto it = pins.find(key);

   if (it == pins.end() || !it->second.initialized)
   {
      tell(0, "GPIO: enableInterrupt('%s') pin not initialized", key.c_str());
      return fail;
   }

   PinState& p = it->second;

   if (p.direction != dirIn)
   {
      tell(0, "GPIO: enableInterrupt('%s') not input", key.c_str());
      return fail;
   }

   if (p.edge == edgeNone)
   {
      tell(0, "GPIO: enableInterrupt('%s') no edge configured", key.c_str());
      return fail;
   }

   if (p.threadRunning)
      return success;

   p.callback = cb;
   p.threadRunning = true;

   p.thread = new std::thread
   (
      [this, key]()
      {
         PinState& ps {pins[key]};

         while (ps.threadRunning)
         {
            bool v{digitalRead(key.c_str())};

            bool rising{(!ps.lastValue && v)};
            bool falling{(ps.lastValue && !v)};

            bool fire{false};

            switch (ps.edge)
            {
               case edgeRising:  fire = rising;            break;
               case edgeFalling: fire = falling;           break;
               case edgeBoth:    fire = rising || falling; break;
               default:          fire = false;             break;
            }

            if (fire && ps.callback)
            {
               ps.callback(key.c_str(), v);
            }

            ps.lastValue = v;

            std::this_thread::sleep_for(std::chrono::milliseconds(5));
         }
      }
   );

   return success;
}

//***************************************************************************
// Disable Interrupt
//***************************************************************************

int Gpio::disableInterrupt(const char* pinName)
{
   std::string key{normalize(pinName)};
   auto it = pins.find(key);

   if (it == pins.end())
      return success;

   PinState& p = it->second;

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
// Get Pin List
//***************************************************************************

int Gpio::getPinList(std::map<std::string, PinInfo>& out)
{
   out.clear();

#if !defined (GPIOD) && !defined (WIRINGPI)
   return success;
#endif

   if (!mappingLoaded)
   {
      if (loadMapping() != 0)
         return fail;
   }

#if defined (GPIOD)

   // System-Pins ermitteln

   std::map<std::string, bool> systemPins;
   DIR* d {opendir("/dev")};

   if (d)
   {
      struct dirent* ent{};

      while ((ent = readdir(d)) != nullptr)
      {
         if (std::strncmp(ent->d_name, "gpiochip", 8) == 0)
         {
            std::string path{"/dev/"};
            path += ent->d_name;

            gpiod_chip* c{gpiod_chip_open(path.c_str())};

            if (!c)
            {
               continue;
            }

            unsigned int numLines{gpiod_chip_num_lines(c)};

            for (unsigned int i = 0; i < numLines; i++)
            {
               const char* name{gpiod_chip_get_line_name(c, i)};

               if (name && name[0] != '\0')
               {
                  std::string n{name};
                  std::transform
                  (
                     n.begin(),
                     n.end(),
                     n.begin(),
                     [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); }
                  );

                  systemPins[n] = true;
               }
            }

            gpiod_chip_close(c);
         }
      }

      closedir(d);
   }

   // Schnittmenge JSON <-> System

   for (const auto& it : pinMapping)
   {
      if (systemPins.find(it.first) != systemPins.end())
         out[it.first] = it.second;
   }

   return success;

#elif defined (WIRINGPI)

   out = pinMapping;      // wiringPi kennt keine Line-Namen -> alle JSON-Pins

   return success;

#endif
}

//***************************************************************************
// Name To Pin
//***************************************************************************

int Gpio::nameToPin(const char* pinName)
{
   std::string key {normalize(pinName)};

   auto it = pinMapping.find(key);

   if (it == pinMapping.end())
      return na;

   return it->second.phys;
}

//***************************************************************************
// Pin To Name
//***************************************************************************

std::string Gpio::pinToName(int physPin)
{
   for (const auto& it : pinMapping)
   {
      if (it.second.phys == physPin)
         return it.first;
   }

   return "";
}

bool Gpio::digitalRead(int physPin)
{
    std::string name = pinToName(physPin);

    if (name.empty())
    {
        tell(0, "GPIO: digitalRead(%d) unknown pin", physPin);
        return false;
    }

    return digitalRead(name.c_str());
}

int Gpio::digitalWrite(int physPin, bool value)
{
    std::string name = pinToName(physPin);
    if (name.empty())
    {
        tell(0, "GPIO: digitalWrite(%d) unknown pin", physPin);
        return -1;
    }

    return digitalWrite(name.c_str(), value);
}

int Gpio::digitalToggle(int physPin)
{
    std::string name = pinToName(physPin);
    if (name.empty())
    {
        tell(0, "GPIO: digitalToggle(%d) unknown pin", physPin);
        return -1;
    }

    return digitalToggle(name.c_str());
}

int Gpio::pinMode(int physPin, Direction direction)
{
    std::string name = pinToName(physPin);
    if (name.empty())
    {
        tell(0, "GPIO: pinMode(%d) unknown pin", physPin);
        return -1;
    }

    return pinMode(name.c_str(), direction);
}

int Gpio::pullUpDnControl(int physPin, PullUpDown value)
{
    std::string name = pinToName(physPin);
    if (name.empty())
    {
        tell(0, "GPIO: pullUpDnControl(%d) unknown pin", physPin);
        return -1;
    }

    return pullUpDnControl(name.c_str(), value);
}
