//***************************************************************************
// poold / Linux - Pool Steering
// File w1.c
// This code is distributed under the terms and conditions of the
// GNU GENERAL PUBLIC LICENSE. See the file LICENSE for details.
// Date 16.04.2020 - Jörg Wendel
//***************************************************************************

#include <dirent.h>
#include <vector>

#include "w1.h"

//***************************************************************************
// Show W1 Sensors
//***************************************************************************

int W1::show()
{
   for (auto it = sensors.begin(); it != sensors.end(); ++it)
      tell(0, "%s: %2.3f", it->first.c_str(), it->second);

   return done;
}

//***************************************************************************
// Exist
//***************************************************************************

int W1::exist(const char* id)
{
   auto it = sensors.find(id);

   return it != sensors.end();
}

//***************************************************************************
// Scan
//***************************************************************************

int W1::scan()
{
   std::vector<std::string> lines;

   if (loadLinesFromFile("/sys/bus/w1/devices/w1_bus_master1/w1_master_slaves", lines) != success)
      return fail;

   sensors.clear();

   for (auto it = lines.begin(); it != lines.end(); ++it)
      sensors[it->c_str()] = 0;

   return success;
}


//***************************************************************************
// Update
//***************************************************************************

int W1::update()
{
   scan();

   for (auto it = sensors.begin(); it != sensors.end(); ++it)
   {
      char line[100+TB];
      FILE* in;
      char* path;

      asprintf(&path, "%s/%s/w1_slave", w1Path, it->first.c_str());

      if (!(in = fopen(path, "r")))
      {
         tell(eloAlways, "Error: Opening '%s' failed, %s", path, strerror(errno));
         free(path);
         continue;
      }

      while (fgets(line, 100, in))
      {
         char* p;

         line[strlen(line)-1] = 0;

         if ((p = strstr(line, " t=")))
         {
            double temp = atoi(p+3) / 1000.0;
            sensors[it->first] = temp;
         }
      }

      fclose(in);
      free(path);
   }

   return done;
}

//***************************************************************************
// To ID
//***************************************************************************

unsigned int W1::toId(const char* name)
{
   const char* p;
   int len = strlen(name);

   // use 4 minor bytes as id

   if (len <= 2)
      return na;

   if (len <= 8)
      p = name;
   else
      p = name + (len - 8);

   return strtoull(p, 0, 16);
}
