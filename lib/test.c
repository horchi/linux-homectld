
#include <sys/time.h>
#include <arpa/inet.h>

#include <signal.h>
#include <stdint.h>   // uint_64_t
#include <unistd.h>
#include <stdio.h>

#include "common.h"
#include "serial.h"
#include "../phservice.h"

Serial serial;

void downF(int aSignal)
{
   serial.close();
   exit(0);
}

//***************************************************************************
// Read Header
//***************************************************************************

int readHeader(cPhBoardService::Header* header, uint timeoutMs = 5000)
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
// PH Request
//***************************************************************************

int requestPh()
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

      tell(0, "PH %.2f (%d)", ph.ph, ph.value);
      return success;
   }

   tell(0, "Got unexpected command 0x%x", header.command);
   return fail;
}

//***************************************************************************
// Calibration
//***************************************************************************

int requestCal()
{
   int res;
   const int duration = 20;

   cPhBoardService::Header header;
   cPhBoardService::PhCalResponse calResp;
   cPhBoardService::PhCalRequest calReq;

   header.id = cPhBoardService::comId;
   header.command = cPhBoardService::cPhCalRequest;
   calReq.time = duration;

   tell(2, "Requesting PH calibration cycle ...");

   serial.flush();

   res = serial.write(&header, sizeof(cPhBoardService::Header))
      + serial.write(&calReq, sizeof(cPhBoardService::PhCalRequest));

   if (res != success)
   {
      tell(0, "Error write serial line failed");
      return fail;
   }

   // wait for responce ...

   if (readHeader(&header, duration*2*1000) != success)
      return fail;

   if (header.command == cPhBoardService::cPhCalResponse)
   {
      if (serial.read(&calResp, sizeof(cPhBoardService::PhCalResponse)) < 0)
         return fail;

      tell(0, "PH Calibration value is (%d)", calResp.value);
      return success;
   }

   tell(0, "Got unexpected command 0x%x", header.command);
   return fail;
}

//***************************************************************************
//
//***************************************************************************

int requestCalGet()
{
   cPhBoardService::Header header;
   cPhBoardService::PhCalSettings calSettings;

   header.id = cPhBoardService::comId;
   header.command = cPhBoardService::cPhCalGetRequest;

   tell(2, "Requesting current PH calibration settings ...");

   serial.flush();

   if (serial.write(&header, sizeof(cPhBoardService::Header)) != success)
   {
      tell(0, "Error write serial line failed");
      return fail;
   }

   // wait for responce ...

   if (readHeader(&header) != success)
      return fail;

   if (header.command == cPhBoardService::cPhCalGetResponse)
   {
      if (serial.read(&calSettings, sizeof(cPhBoardService::PhCalSettings)) < 0)
         return fail;

      tell(0, "Current PH Calibration values are %d / %d", calSettings.pointA, calSettings.pointB);

      return success;
   }

   tell(0, "Got unexpected command 0x%x", header.command);
   return fail;
}

int requestCalSet(word pointA, word pointB)
{
   if (!pointA || !pointB)
   {
      tell(0, "Error: Missing values");
      return fail;
   }

   cPhBoardService::Header header;
   cPhBoardService::PhCalSettings calSettings;

   header.id = cPhBoardService::comId;
   header.command = cPhBoardService::cPhCalSetRequest;
   calSettings.pointA = pointA;
   calSettings.pointB = pointB;

   tell(2, "Requesting set of PH calibration values ...");

   serial.flush();

   int res = serial.write(&header, sizeof(cPhBoardService::Header))
      + serial.write(&calSettings, sizeof(cPhBoardService::PhCalSettings));

   if (res != success)
   {
      tell(0, "Error write serial line failed");
      return fail;
   }

   // wait for responce ...

   if (readHeader(&header) != success)
      return fail;

   if (header.command == cPhBoardService::cPhCalSetResponse)
   {
      if (serial.read(&calSettings, sizeof(cPhBoardService::PhCalSettings)) < 0)
         return fail;

      tell(0, "PH Calibration values now set to %d / %d", calSettings.pointA, calSettings.pointB);
      return success;
   }

   tell(0, "Got unexpected command 0x%x", header.command);
   return fail;
}

//***************************************************************************
// Main
//***************************************************************************

int main(int argc, char** argv)
{
   word pointA {0};
   word pointB {0};
   int every {na};
   bool cal {false};
   bool calGet {false};
   bool calSet {false};

   logstdout = yes;
   loglevel = 1;

   if (argc < 2)
   {
      tell(0, "Missing device");
      return 1;
   }

   if (argc == 2)
      ;
   else if (argc > 3 && strcmp(argv[2], "every") == 0)
      every = atoi(argv[3]);
   else if (argc > 2 && strcmp(argv[2], "cal") == 0)
      cal = true;
   else if (argc > 2 && strcmp(argv[2], "calget") == 0)
      calGet = true;
   else if (argc > 4 && strcmp(argv[2], "calset") == 0)
   {
      calSet = true;
      pointA = atoi(argv[3]);
      pointB = atoi(argv[4]);
   }
   else
   {
      tell(0, "Error: Unexpected argumnents");
      return 1;
   }

   ::signal(SIGINT, downF);
   ::signal(SIGTERM, downF);

   if (serial.open(argv[1]) != success)
   {
      tell(0, "Error: opening decive failed");
      return 1;
   }

   if (cal)
      requestCal();
   else if (calSet)
      requestCalSet(pointA, pointB);
   else if (calGet)
      requestCalGet();

   else
   {
      while (true)
      {
         requestPh();

         if (every > 0)
            sleep(every);
         else
            getchar();
      }
   }

   return 0;
}
