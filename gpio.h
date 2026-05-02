//***************************************************************************
// Automation Control
// File gpio.h
// This code is distributed under the terms and conditions of the
// GNU GENERAL PUBLIC LICENSE. See the file LICENSE for details.
// Date 04.12.2021 Jörg Wendel
//***************************************************************************

#pragma once

#pragma once

#include <string>
#include <map>
#include <vector>
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
   int phys;
   std::string direction;
   std::string pull;
   bool interrupt;
   bool oneWire;
   bool safeForOutput;
   std::string voltage;
   std::string description;
   bool blocked;
   bool usable;
};

class Gpio
{
   public:

      enum Direction
      {
         dirIn,
         dirOut
      };

      enum PullUpDown
      {
         pudOff  = 0,
         pudUp   = 1,
         pudDown = 2
      };

      enum Edge
      {
         edgeNone,
         edgeRising,
         edgeFalling,
         edgeBoth
      };

      Gpio(const char* consumerName, const char* confPath);
      ~Gpio();

      int init();

      int setupPin(const char* pinName, Direction dir, Edge edge, PullUpDown pud);
      bool pinAvailable(const char* pinName) { return true; } // #TODO to be implemented
      bool digitalRead(const char* pinName);
      int  digitalWrite(const char* pinName, bool value);
      int  digitalToggle(const char* pinName);

      int pinMode(const char* pinName, Direction direction);
      int pullUpDnControl(const char* pinName, PullUpDown value);

      int enableInterrupt(const char* pinName, std::function<void(const char* pinName, bool value)> cb);
      int disableInterrupt(const char* pinName);

      int getPinList(std::map<std::string, PinInfo>& out);
      int nameToPin(const char* pinName);
      std::string pinToName(int physPin);

   private:

      struct PinState
      {
#if defined(GPIOD)
         gpiod_chip* chip{};
         gpiod_line_request* request{};
         unsigned int offset{};
#endif
         bool initialized{};
         Direction direction{};
         PullUpDown pud{};
         Edge edge{};
         std::thread* thread{};
         bool threadRunning{};
         bool lastValue{};
         std::function<void(const char* pinName, bool value)> callback{};
      };

      std::string normalize(const char* pinName);

      int loadMapping();
      int loadJsonForModel(json_t* root, const std::string& model);
      int detectBlockedPins();

      std::string programName;
      std::string configPath;
      std::string modelName;

      bool mappingLoaded{};
      std::map<std::string, PinInfo> pinMapping;
      std::map<std::string, PinState> pins;
};
