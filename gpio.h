//***************************************************************************
// Automation Control
// File gpio.c
// This code is distributed under the terms and conditions of the
// GNU GENERAL PUBLIC LICENSE. See the file LICENSE for details.
// Date 04.12.2021 Jörg Wendel
//***************************************************************************

/*
  https://wiki.odroid.com/odroid-n2/hardware/expansion_connectors
*/

#pragma once

// #ifndef _NO_RASPBERRY_PI_
// #  include <wiringPi.h>
// #endif

#include <gpiod.h>
#include <string>
#include <map>
#include <chrono>

#include "lib/json.h"

#define bitSet(value, bit)   (value |= (1UL << bit))
#define bitClear(value, bit) (value &= ~(1UL << bit))
#define bitRead(value, bit)  ((value >> bit) & 0x01)
#define lowByte(word)        ((uint8_t)(word & 0xff))
#define highByte(word)       ((uint8_t)(word >> 8))

//***************************************************************************
// Physical Pin to GPIO Name
//  ODROID n2
//***************************************************************************

// extern const char* physToGpioName_odroid_n2[64];
// extern const char* physToGpioName_raspberry_pi[64];
// const char* physPinToGpioName(int pin);

//***************************************************************************
// dummy functions
//***************************************************************************

// #ifdef _NO_RASPBERRY_PI_

// #include <stdlib.h>   // uint

// #define INT_EDGE_FALLING 0
// #define INT_EDGE_BOTH 2
// #define OUTPUT 0
// #define INPUT 0
// #define PUD_OFF 0
// #define PUD_UP 2
// #define PUD_DOWN 3

// int wiringPiISR(int, int, void (*)());
// void pinMode(int, int);
// void pullUpDnControl(int pin, int value);
// int digitalWrite(uint pin, bool value);
// int digitalRead(uint pin);
// int wiringPiSetupPhys();

// #endif // _NO_RASPBERRY_PI_

// #ifdef MODEL_ODROID_N2

// #else

// #  define INPUT_PULLUP PUD_UP
// #  define INPUT_PULLDOWN PUD_DOWN

// #endif  // MODEL_ODROID_N2

//***************************************************************************
// *** NEW ***
//***************************************************************************

#if defined(GPIOD_VERSION_MAJOR) && GPIOD_VERSION_MAJOR >= 2
   #define LIBGPIOD_V2
#endif

// Bias-Flags für alte v1-Header (Kernel >= 5.5 Support)

#ifndef GPIOD_LINE_REQUEST_FLAG_BIAS_DISABLE
   #define GPIOD_LINE_REQUEST_FLAG_BIAS_DISABLE (1 << 3)
#endif
#ifndef GPIOD_LINE_REQUEST_FLAG_BIAS_PULL_UP
   #define GPIOD_LINE_REQUEST_FLAG_BIAS_PULL_UP (1 << 5)
#endif
#ifndef GPIOD_LINE_REQUEST_FLAG_BIAS_PULL_DOWN
   #define GPIOD_LINE_REQUEST_FLAG_BIAS_PULL_DOWN (1 << 6)
#endif

class BoardDetector
{
   public:

      BoardDetector() {};

      static std::string detect();
};

struct PinConfig
{
   std::string chip {};
   std::string name {};
   unsigned int offset {0};
   unsigned int globalOffset {0};
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

      enum Event
      {
         evtEventNone,
         evtEventRising,
         evtEventFalling,
         evtEventError
      };

      Gpio() {};
      ~Gpio();

      int init(const char* confDir);
      bool available() { return initialzed; }
      void setBoardName(const char* name) { boardName = name; }

      bool pinAvailable(int physPin);
      int pinMode(int physPin, Direction direction);
      void pullUpDnControl(int physPin, PullUpDown value);
      int setupGpioPin(int physPin, Direction dir, Edge edge = edgeNone, PullUpDown pud = pudOff);

      bool digitalRead(int physPin);
      int digitalWrite(int physPin, int value);
      Event waitForEvent(int physPin, int timeoutMs);
      int detectBlinkCode(int physPin, int timeoutMs);

      int wiringPiISR(int pin, Gpio::Edge edge, void (*interruptRoutine)(void)) { printf("wiringPiISR() TO BE IMPLEMENTED!!\n"); return done; }
      const char* physPinToGpioName(int physPin);

   private:

      int loadConfig(const char* filePath);
      int autoResolvePin(int physPin);
      int setupSysfs(int physPin, Direction dir);

      bool initialzed {false};
      std::string boardName {};
      std::string consumer {"homectld"};
      std::map<int, PinConfig> pinMap;
      std::map<int, struct gpiod_line*> activeLines;
      std::map<std::string, struct gpiod_chip*> openChips;
      std::map<int,int> pinPullModes;
      std::map<int, bool> isSysfsPin;

#ifdef LIBGPIOD_V2
      std::map<int, struct gpiod_line_request*> activeRequests {};
#endif
};
