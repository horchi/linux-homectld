
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

int usage(const char* name)
{
   printf("Usage: %s <device> <command> [<options>]\n", name);
   printf(" commands:\n");
   printf("    read   <analog input> \n");
   printf("    cal    <analog input> \n");
   printf("    calget <analog input> \n");
   printf("    calset <analog input> <value point A> <digits point A> <value point B> <digits point B>\n");

   return 1;
}

//***************************************************************************
// Main
//***************************************************************************

int main(int argc, char** argv)
{
   cPhBoardService::CalResponse calResp;
   cPhBoardService::CalSettings calSettings;
   cPhBoardService::AnalogValue phValue;
   cPhBoardService::AnalogValue aiValue;
   bool cal {false};
   bool calGet {false};
   bool calSet {false};
   bool readAnalog {false};

   logstdout = yes;
   loglevel = 1;

   if (argc < 4)
      return usage(argv[0]);

   uint input = atoi(argv[3]);

   if (strcmp(argv[2], "read") == 0)
      readAnalog = true;
   else if (strcmp(argv[2], "cal") == 0)
      cal = true;
   else if (strcmp(argv[2], "calget") == 0)
      calGet = true;
   else if (argc > 7 && strcmp(argv[2], "calset") == 0)
   {
      calSet = true;
      calSettings.valueA = atof(argv[4]);    // for example 7.00
      calSettings.digitsA = atoi(argv[5]);   // for example 379
      calSettings.valueB = atof(argv[6]);    // for example 9.00
      calSettings.digitsB = atoi(argv[7]);   // for example 435
   }
   else
      return usage(argv[0]);

   ::signal(SIGINT, downF);
   ::signal(SIGTERM, downF);

   if (phInterface.open(argv[1]) != success)
   {
      tell(0, "Error: opening device '%s' failed", argv[1]);
      return 1;
   }

   if (cal)
      phInterface.requestCalibration(calResp, input, 15);
   else if (calSet)
      phInterface.requestCalSet(calSettings, input);
   else if (calGet)
      phInterface.requestCalGet(calSettings, input);

   else if (readAnalog)
   {
      while (true)
      {
         phInterface.requestAi(aiValue, input);
         sleep(1);
      }
   }

   return 0;
}
