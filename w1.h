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

//***************************************************************************
// Class W1
//***************************************************************************

#define W1_UDEF std::numeric_limits<double>::max()

class W1
{
   public:

      typedef std::map<std::string, double> SensorList;

      W1()  { w1Path = strdup("/sys/bus/w1/devices"); }
      ~W1() { free(w1Path); }

      int scan();
      int show();
      int update();

      SensorList* getList() { return &sensors; }
      double valueOf(const char* id) { return !isEmpty(id) ? sensors[id] : 0; }

      static unsigned int toId(const char* name);

   protected:

      char* w1Path;
      SensorList sensors;
};
