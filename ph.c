//***************************************************************************
// poold / Linux - Pool Steering
// File ph.c
// This code is distributed under the terms and conditions of the
// GNU GENERAL PUBLIC LICENSE. See the file LICENSE for details.
// Date 29.08.2020 - Jörg Wendel
//***************************************************************************

//***************************************************************************
// Include
//***************************************************************************

#include "poold.h"

//***************************************************************************
// Init/Exit PH Interface
//***************************************************************************

int Poold::initPhInterface()
{
   if (serial.isOpen())
      return success;

   return serial.open(phDevice);
}

int Poold::exitPhInterface()
{
   return serial.close();
}

//***************************************************************************
// Read Header
//***************************************************************************

int Poold::readHeader(cPhBoardService::Header* header, uint timeoutMs)
{
   if (serial.read(header, sizeof(cPhBoardService::Header), timeoutMs) < 0)
   {
      tell(0, "Error: Timeout wating for responce");
      return fail;
   }

   if (header->id != cPhBoardService::comId)
   {
      tell(0, "Error: Got unexpected communication ID of 0x%x", header->id);
      return fail;
   }

   return success;
}

//***************************************************************************
// Request PH
//***************************************************************************

int Poold::requestPh(double& ph)
{
   cPhBoardService::PhValue phValue;
   cPhBoardService::Header header;

   header.id = cPhBoardService::comId;
   header.command = cPhBoardService::cPhRequest;

   tell(2, "Requesting PH ...");

   serial.flush();

   if (serial.write(&header, sizeof(cPhBoardService::Header)) != success)
   {
      tell(0, "Error write serial line failed");
      return fail;
   }

   tell(2, ".. requested, waiting for responce ..");

   if (readHeader(&header) != success)
      return fail;

   if (header.command == cPhBoardService::cPhResponse)
   {
      if (serial.read(&ph, sizeof(cPhBoardService::PhValue)) < 0)
         return fail;

      tell(1, "PH %.2f (%d)", phValue.ph, phValue.value);
      ph = phValue.ph;
      return success;
   }

   tell(0, "Got unexpected command 0x%x", header.command);
   return fail;
}

//***************************************************************************
// Get PH
//***************************************************************************

double Poold::getPh()
{
   double ph {-1};

   if (initPhInterface() != success)
      return -1;

   requestPh(ph);

   return ph;
}
