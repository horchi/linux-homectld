//***************************************************************************
// poold / Linux - Arduino Interface
// File arduinoif.h
// This code is distributed under the terms and conditions of the
// GNU GENERAL PUBLIC LICENSE. See the file LICENSE for details.
// Date 30.08.2020 - Jörg Wendel
//***************************************************************************

#pragma once

#include "lib/serial.h"

#include "aiservice.h"

//***************************************************************************
// Arduini Mini - Interface
//***************************************************************************

class cArduinoInterface : public cArduinoInterfaceService
{
   public:

      cArduinoInterface() {};
      virtual ~cArduinoInterface() {};

      int open(const char* ttyDevice);
      int close();
      int checkInterface();

      int readHeader(Header* header, uint timeoutMs = 5000);
      int requestAi(AnalogValue& aiValue, uint input);
      int requestCalibration(CalResponse& calResp, uint input, uint duration = 30);
      int requestCalGet(CalSettings& calSettings, uint input);
      int requestCalSet(CalSettings& calSettings, uint input);

   private:

      Serial serial;
      cMyMutex mutex;
};
