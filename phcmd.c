
#include <sys/time.h>
#include <arpa/inet.h>

#include <signal.h>
#include <stdint.h>   // uint_64_t
#include <unistd.h>
#include <stdio.h>

#include "lib/common.h"
#include "lib/serial.h"
#include "ph.h"

//***************************************************************************
// Test PH Interface
//***************************************************************************

cPhInterface phInterface;

void downF(int aSignal)
{
   phInterface.close();
   exit(0);
}

//***************************************************************************
// Main
//***************************************************************************

int main(int argc, char** argv)
{
   cPhBoardService::PhCalResponse calResp;
   cPhBoardService::PhCalSettings calSettings;
   cPhBoardService::PhValue phValue;
   cPhBoardService::PressValue pressValue;
   int every {na};
   bool cal {false};
   bool calGet {false};
   bool calSet {false};
   bool press {false};

   logstdout = yes;
   loglevel = 1;

   if (argc < 2)
   {
      tell(0, "Missing device");
      return 1;
   }

   if (argc == 2)
      ;
   else if (argc > 2 && strcmp(argv[2], "press") == 0)
      press = true;
   else if (argc > 3 && strcmp(argv[2], "every") == 0)
      every = atoi(argv[3]);
   else if (argc > 2 && strcmp(argv[2], "cal") == 0)
      cal = true;
   else if (argc > 2 && strcmp(argv[2], "calget") == 0)
      calGet = true;
   else if (argc > 4 && strcmp(argv[2], "calset") == 0)
   {
      calSet = true;
      calSettings.phA = 7.00;
      calSettings.pointA = atoi(argv[3]);
      calSettings.phB = 9.00;
      calSettings.pointB = atoi(argv[4]);
   }
   else
   {
      tell(0, "Error: Unexpected argumnents");
      return 1;
   }

   ::signal(SIGINT, downF);
   ::signal(SIGTERM, downF);

   if (phInterface.open(argv[1]) != success)
   {
      tell(0, "Error: opening decive failed");
      return 1;
   }

   if (cal)
      phInterface.requestCalibration(calResp, 15);
   else if (calSet)
      phInterface.requestCalSet(calSettings);
   else if (calGet)
      phInterface.requestCalGet(calSettings);

   else if (press)
   {
      while (true)
      {
         phInterface.requestPressure(pressValue);
         sleep(1);
      }
   }
   else
   {

      while (true)
      {
         phInterface.requestPh(phValue);

         if (every > 0)
            sleep(every);
         else
            getchar();
      }
   }

   return 0;
}
