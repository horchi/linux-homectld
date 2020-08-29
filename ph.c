//***************************************************************************
// poold / Linux - Pool Steering
// File ph.c
// This code is distributed under the terms and conditions of the
// GNU GENERAL PUBLIC LICENSE. See the file LICENSE for details.
// Date 16.04.2020 - Jörg Wendel
//***************************************************************************

//***************************************************************************
// Include
//***************************************************************************

#include "poold.h"
#include "phservice.h"

//***************************************************************************
//
//***************************************************************************

int cPoold::initPhInterface()
{
   if (serial.isOpen())
      return success;

   return serial.open(phDevice);
}

//***************************************************************************
// Request PH
//***************************************************************************

int cPoold::requestPh(double& ph)
{
   cPhBoardService::PhValue ph;
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

      tell(1, "PH %.2f (%d)", ph.ph, ph.value);
      ph = ph.ph;
      return success;
   }

   tell(0, "Got unexpected command 0x%x", header.command);
   return fail;
}

//***************************************************************************
// Get PH
//***************************************************************************

double cPoold::getPh()
{
   double ph {-1};

   if (initPhInterface() != success)
      return -1;

   if (requestPh(ph) != success)
      return -1;

   return ph;
}
