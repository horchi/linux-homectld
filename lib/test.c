
#include <stdint.h>   // uint_64_t
#include <sys/time.h>

#include <unistd.h>
#include <stdio.h>

#include "common.h"
#include "serial.h"

//***************************************************************************
// Main
//***************************************************************************

int main(int argc, char** argv)
{
   logstdout = yes;
   loglevel = 2;

   if (argc < 2)
   {
      tell(0, "Missing device");
      return 1;
   }

   Serial serial;

   if (serial.open(argv[1]) != success)
   {
      tell(0, "Error: opening devive failed");
      return 1;
   }

   serial.close();

   return 0;
}
