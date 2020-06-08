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
#include "lib/thread.h"

//***************************************************************************
// Class W1
//***************************************************************************

#define W1_UDEF std::numeric_limits<double>::max()

class W1 : public cThread
{
   public:

      typedef std::map<std::string, double> SensorList;

      W1();
      ~W1();

      void action() override;
      int show();
      int scan();
      int exist(const char* id);
      int update();

      const SensorList* getList()    { return sensors; }
      size_t getCount()              { return sensors->size(); }
      double valueOf(const char* id);

      static unsigned int toId(const char* name);

   protected:

      char* w1Path {nullptr};
      SensorList* sensors {nullptr};
};
