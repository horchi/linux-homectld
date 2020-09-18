//***************************************************************************
// poold / Linux - Arduino Interface
// File arduinoif.c
// This code is distributed under the terms and conditions of the
// GNU GENERAL PUBLIC LICENSE. See the file LICENSE for details.
// Date 29.08.2020 - Jörg Wendel
//***************************************************************************

#include <cmath>

#include "arduinoif.h"

//***************************************************************************
// Open / Close Interface
//***************************************************************************

int cArduinoInterface::open(const char* ttyDevice)
{
   if (serial.isOpen())
      return success;

   return serial.open(ttyDevice);
}

int cArduinoInterface::close()
{
   return serial.close();
}

int cArduinoInterface::checkInterface()
{
   if (serial.isOpen())
      return success;

   tell(0, "Error: Interface not open!");

   // #TODO try re-open here
   // return open(ttyDevice);

   return fail;
}

//***************************************************************************
// Read Header
//***************************************************************************

int cArduinoInterface::readHeader(Header* header, uint timeoutMs)
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
// Pressure Request
//***************************************************************************

int cArduinoInterface::requestAi(AnalogValue& aiValue, uint input)
{
   if (checkInterface() != success)
      return fail;

   cMyMutexLock lock(&mutex);

   Header header(cAiRequest, input);
   header.id = comId;

   tell(2, "Requesting Pressure ...");

   serial.flush();

   if (serial.write(&header, sizeof(Header)) != success)
   {
      tell(0, "Error write serial line failed");
      return fail;
   }

   tell(2, ".. requested, waiting for responce ..");

   if (readHeader(&header) != success)
      return fail;

   if (header.command == cAiResponse)
   {
      if (serial.read(&aiValue, sizeof(AnalogValue)) < 0)
         return fail;

      if (std::isnan(aiValue.value))
      {
         tell(0, "Error: Got unexpected value for A%d of %.2f (%d)", input, aiValue.value, aiValue.digits);
         return fail;
      }

      tell(1, "A%d value %.2f (%d)", input, aiValue.value, aiValue.digits);
      return success;
   }

   tell(0, "Got unexpected command 0x%x", header.command);

   return fail;
}

//***************************************************************************
// Request Calibration Process
//   get avarage of <duration>
//***************************************************************************

int cArduinoInterface::requestCalibration(CalResponse& calResp, uint input, uint duration)
{
   int res;
   CalRequest calReq;

   if (checkInterface() != success)
      return fail;

   cMyMutexLock lock(&mutex);

   Header header(cCalRequest, input);
   header.id = comId;
   calReq.time = duration;

   tell(0, "Requesting calibration cycle of A%d ...", input);

   serial.flush();

   res = serial.write(&header, sizeof(Header))
      + serial.write(&calReq, sizeof(CalRequest));

   if (res != success)
   {
      tell(0, "Error write serial line failed");
      return fail;
   }

   // wait for responce ...

   if (readHeader(&header, duration*2*1000) != success)
      return fail;

   if (header.command == cCalResponse)
   {
      if (serial.read(&calResp, sizeof(CalResponse)) < 0)
         return fail;

      tell(0, "Calibration value of A%d is (%d)", input, calResp.digits);
      return success;
   }

   tell(0, "Got unexpected command 0x%x", header.command);
   return fail;
}

//***************************************************************************
// Request Current calibration settings (stored in arduino's EEPOROM)
//***************************************************************************

int cArduinoInterface::requestCalGet(CalSettings& calSettings, uint input)
{
   if (checkInterface() != success)
      return fail;

   cMyMutexLock lock(&mutex);

   Header header(cCalGetRequest, input);
   header.id = comId;

   tell(2, "Requesting current calibration settings ...");

   serial.flush();

   if (serial.write(&header, sizeof(Header)) != success)
   {
      tell(0, "Error: Write serial line failed");
      return fail;
   }

   // wait for responce ...

   if (readHeader(&header) != success)
      return fail;

   if (header.command == cCalGSResponse)
   {
      if (serial.read(&calSettings, sizeof(CalSettings)) < 0)
         return fail;

      tell(0, "Current Calibration values of A%d are\n value %2.1f => %d\n value %2.1f => %d", input,
           calSettings.valueA, calSettings.digitsA, calSettings.valueB, calSettings.digitsB);

      return success;
   }

   tell(0, "Got unexpected command 0x%x", header.command);
   return fail;
}

//***************************************************************************
// Request Setting of calibration data (stored in arduino's EEPOROM)
//***************************************************************************

int cArduinoInterface::requestCalSet(CalSettings& calSettings, uint input)
{
   if (checkInterface() != success)
      return fail;

   if (!calSettings.digitsA || !calSettings.digitsB || /* !calSettings.valueA || */ !calSettings.valueB)
   {
      tell(0, "Error: Missing at least one calibration value");
      return fail;
   }

   //

   cMyMutexLock lock(&mutex);

   Header header(cCalSetRequest, input);
   header.id = comId;

   tell(2, "Requesting set of calibration values for A%d ...", header.input);

   serial.flush();

   int res = serial.write(&header, sizeof(Header))
      + serial.write(&calSettings, sizeof(CalSettings));

   if (res != success)
   {
      tell(0, "Error write serial line failed");
      return fail;
   }

   // wait for responce ...

   if (readHeader(&header) != success)
      return fail;

   if (header.command == cCalGSResponse)
   {
      if (serial.read(&calSettings, sizeof(CalSettings)) < 0)
         return fail;

      tell(0, "Calibration values for A%c now set to %u / %u", header.input, (uint)calSettings.digitsA, (uint)calSettings.digitsB);
      return success;
   }

   tell(0, "Got unexpected command 0x%x", header.command);
   return fail;
}
