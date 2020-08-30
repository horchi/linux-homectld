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

int Poold::readHeader(Header* header, uint timeoutMs)
{
   if (serial.read(header, sizeof(Header), timeoutMs) < 0)
   {
      tell(0, "Error: Timeout wating for responce");
      return fail;
   }

   if (header->id != comId)
   {
      tell(0, "Error: Got unexpected communication ID of 0x%x", header->id);
      return fail;
   }

   return success;
}

//***************************************************************************
// Request Calibration Process
//   get avarage of <duration>
//***************************************************************************

int Poold::requestCalibration(PhCalResponse& calResp, uint duration)
{
   int res;

   Header header;
   PhCalRequest calReq;

   header.id = comId;
   header.command = cPhCalRequest;
   calReq.time = duration;

   tell(2, "Requesting PH calibration cycle ...");

   serial.flush();

   res = serial.write(&header, sizeof(Header))
      + serial.write(&calReq, sizeof(PhCalRequest));

   if (res != success)
   {
      tell(0, "Error write serial line failed");
      return fail;
   }

   // wait for responce ...

   if (readHeader(&header, duration*2*1000) != success)
      return fail;

   if (header.command == cPhCalResponse)
   {
      if (serial.read(&calResp, sizeof(PhCalResponse)) < 0)
         return fail;

      tell(0, "PH Calibration value is (%d)", calResp.value);
      return success;
   }

   tell(0, "Got unexpected command 0x%x", header.command);
   return fail;
}

//***************************************************************************
// Request Current calibration settings (stored in arduino's EEPOROM)
//***************************************************************************

int Poold::requestCalGet(PhCalSettings& calSettings)
{
   Header header;

   header.id = comId;
   header.command = cPhCalGetRequest;

   tell(2, "Requesting current PH calibration settings ...");

   serial.flush();

   if (serial.write(&header, sizeof(Header)) != success)
   {
      tell(0, "Error write serial line failed");
      return fail;
   }

   // wait for responce ...

   if (readHeader(&header) != success)
      return fail;

   if (header.command == cPhCalGetResponse)
   {
      if (serial.read(&calSettings, sizeof(PhCalSettings)) < 0)
         return fail;

      tell(0, "Current PH Calibration values are %d / %d", calSettings.pointA, calSettings.pointB);

      return success;
   }

   tell(0, "Got unexpected command 0x%x", header.command);
   return fail;
}

//***************************************************************************
// Request Setting of calibration data (stored in arduino's EEPOROM)
//***************************************************************************

int Poold::requestCalSet(word pointA, word pointB)
{
   if (!pointA || !pointB)
   {
      tell(0, "Error: Missing values");
      return fail;
   }

   Header header;
   PhCalSettings calSettings;

   header.id = comId;
   header.command = cPhCalSetRequest;
   calSettings.pointA = pointA;
   calSettings.pointB = pointB;

   tell(2, "Requesting set of PH calibration values ...");

   serial.flush();

   int res = serial.write(&header, sizeof(Header))
      + serial.write(&calSettings, sizeof(PhCalSettings));

   if (res != success)
   {
      tell(0, "Error write serial line failed");
      return fail;
   }

   // wait for responce ...

   if (readHeader(&header) != success)
      return fail;

   if (header.command == cPhCalSetResponse)
   {
      if (serial.read(&calSettings, sizeof(PhCalSettings)) < 0)
         return fail;

      tell(0, "PH Calibration values now set to %d / %d", calSettings.pointA, calSettings.pointB);
      return success;
   }

   tell(0, "Got unexpected command 0x%x", header.command);
   return fail;
}

//***************************************************************************
// Request PH
//***************************************************************************

int Poold::requestPh(double& ph)
{
   PhValue phValue;
   Header header;

   header.id = comId;
   header.command = cPhRequest;

   tell(2, "Requesting PH ...");

   serial.flush();

   if (serial.write(&header, sizeof(Header)) != success)
   {
      tell(0, "Error write serial line failed");
      return fail;
   }

   tell(2, ".. requested, waiting for responce ..");

   if (readHeader(&header) != success)
      return fail;

   if (header.command == cPhResponse)
   {
      if (serial.read(&ph, sizeof(PhValue)) < 0)
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
