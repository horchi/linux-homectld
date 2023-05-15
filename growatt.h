//***************************************************************************
// Automation Control
// File growatt.h
// This code is distributed under the terms and conditions of the
// GNU GENERAL PUBLIC LICENSE. See the file LICENSE for details.
// Date 04.11.2010 - 01.12.2023  Jörg Wendel
//***************************************************************************

#include <vector>

#include "lib/common.h"

//***************************************************************************
// Gro Watt
//***************************************************************************

class GroWatt
{
   public:

      static int getAddressOf(const char* key);
      static const char* getUnitOf(const char* key);
      static const char* toStatusString(uint status);

   private:

      struct Def
      {
         int address {na};
         const char* unit {};
      };

      static std::map<std::string,Def> sensors;
};
