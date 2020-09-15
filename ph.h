//***************************************************************************
// poold / Linux - PH Control
// File ph.h
// This code is distributed under the terms and conditions of the
// GNU GENERAL PUBLIC LICENSE. See the file LICENSE for details.
// Date 30.08.2020 - Jörg Wendel
//***************************************************************************

#pragma once

#include "lib/serial.h"

#include "phservice.h"

//***************************************************************************
// PH Interface
//***************************************************************************

class cPhInterface : public cPhBoardService
{
   public:

      cPhInterface() {};
      virtual ~cPhInterface() {};

      int open(const char* ttyDevice);
      int close();
      int checkInterface();

      int readHeader(Header* header, uint timeoutMs = 5000);
      int requestPressure(PressValue& pressValue);
      int requestPh(PhValue& phValue);
      double getPh();
      int requestCalibration(PhCalResponse& calResp, uint duration = 30);
      int requestCalGet(PhCalSettings& calSettings);
      int requestCalSet(PhCalSettings& calSettings);

   private:

      Serial serial;
      cMyMutex mutex;
};
