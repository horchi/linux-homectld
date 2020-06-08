//***************************************************************************
// poold / Linux - Pool Steering
// File w1.h
// This code is distributed under the terms and conditions of the
// GNU GENERAL PUBLIC LICENSE. See the file LICENSE for details.
// Date 16.04.2020 - Jörg Wendel
//***************************************************************************

#pragma once

#include <stdio.h>
#include <map>
#include <limits>

#include "lib/common.h"
#include "lib/mqtt.h"

//***************************************************************************
// Class W1
//***************************************************************************

#define W1_UDEF std::numeric_limits<double>::max()

class W1
{
   public:

      typedef std::map<std::string, double> SensorList;

      W1();
      ~W1();

      static void downF(int aSignal) { shutdown = yes; }

      int init() { return success; }
      int loop();
      int show();
      int scan();
      int update();
      int doShutDown() { return shutdown; }

      const SensorList* getList()    { return &sensors; }
      size_t getCount()              { return sensors.size(); }

   protected:

      int mqttConnection();

      char* w1Path {nullptr};
      SensorList sensors;
      const char* mqttUrl {nullptr};
      const char* mqttTopic {nullptr};
      MqTTPublishClient* mqttWriter {nullptr};

      static int shutdown;
};
