//***************************************************************************
// Automation Control
// File gpio.h
// This code is distributed under the terms and conditions of the
// GNU GENERAL PUBLIC LICENSE. See the file LICENSE for details.
// Date 04.12.2021 Jörg Wendel
//***************************************************************************

#pragma once

#include <string>
#include <map>
#include <functional>
#include <thread>

#include <jansson.h>

#if defined(GPIOD)
#  include <gpiod.h>
#endif

#if defined(WIRINGPI)
#  include <wiringPi.h>
#endif

struct PinInfo
{
   std::string name;
   bool gpio;
   bool pull;
   bool interrupt;
   std::string voltage;
   std::string description;
   bool blocked;
   bool usable;
};

class Gpio
{
   public:

      enum Direction  { dirIn, dirOut };
      enum PullUpDown { pudOff = 0, pudUp = 1, pudDown = 2 };
      enum Edge       { edgeNone, edgeRising, edgeFalling, edgeBoth };

      Gpio(const char* consumerName, const char* confPath);
      ~Gpio();

      int  init();

      std::string getBoardType() {return modelName; }
      int setupPin(int physPin, Direction dir, Edge edge, PullUpDown pud);
      bool pinAvailable(int physPin);
      bool digitalRead(int physPin);
      int digitalWrite(int physPin, bool value);
      int digitalToggle(int physPin);
      int pinMode(int physPin, Direction direction);
      int pullUpDnControl(int physPin, PullUpDown value);
      int enableInterrupt(int physPin, std::function<void(int physPin, bool value)> cb);
      int disableInterrupt(int physPin);
      int setIsr(int physPin, Edge edge, std::function<void(int physPin, bool value)> cb);
      int getPinList(std::map<int, PinInfo>& out);
      std::string pinToName(int physPin);

   private:

      struct PinState
      {
#if defined(GPIOD)
         gpiod_chip* chip {};
         gpiod_line_request* request {};
         unsigned int offset {};
#endif
         bool initialized {};
         Direction direction {};
         PullUpDown pud {};
         Edge edge {};
         std::thread* thread {};
         bool threadRunning {};
         bool lastValue {};
         std::function<void(int physPin, bool value)> callback{};
      };

#if defined(GPIOD)
      struct PhysEntry { std::string chipPath; unsigned int offset; };
      int buildPhysMap();
      static gpiod_chip* findChipForLine(const char* lineName, unsigned int* offsetOut);
      std::map<int, PhysEntry> physToChip;
#endif

      int loadMapping();
      int loadJsonForModel(json_t* root, const std::string& model);
      int detectBlockedPins();

      std::string programName;
      std::string configPath;
      std::string modelName;

      bool mappingLoaded {};
      std::map<int, PinInfo>  pinMapping;
      std::map<int, PinState> pins;
};
