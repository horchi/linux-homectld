//***************************************************************************
// poold / Linux - Pool Steering
// File w1.c
// This code is distributed under the terms and conditions of the
// GNU GENERAL PUBLIC LICENSE. See the file LICENSE for details.
// Date 10.08.2020 - Jörg Wendel
//***************************************************************************

#include <signal.h>
#include <dirent.h>
#include <vector>
#include <numeric>

#include "w1.h"
#include "lib/json.h"

int W1::shutdown {false};

W1::W1()
{
   w1Path = strdup("/sys/bus/w1/devices");
   mqttTopic = "poold2mqtt/w1";
   mqttUrl = "tcp://localhost:1883";
}

W1::~W1()
{
   free(w1Path);
}

//***************************************************************************
// Show W1 Sensors
//***************************************************************************

int W1::show()
{
   for (auto it = sensors.begin(); it != sensors.end(); ++it)
      tell(0, "%s: %2.3f", it->first.c_str(), it->second.value);

   return done;
}

//***************************************************************************
// Scan
//***************************************************************************

int W1::scan()
{
   std::vector<std::string> lines;

   if (loadLinesFromFile("/sys/bus/w1/devices/w1_bus_master1/w1_master_slaves", lines) != success)
      return fail;

   for (auto it = lines.begin(); it != lines.end(); ++it)
   {
      if (strcasestr(it->c_str(), "not found") || isEmpty(it->c_str()))
         continue;

      if (sensors.find(it->c_str()) == sensors.end())
      {
         tell(eloAlways, "One Wire Sensor '%s' attached", it->c_str());
         sensors[it->c_str()].value = 0;
      }
   }

   return success;
}

//***************************************************************************
// Action
//***************************************************************************

int W1::loop()
{
   while (!doShutDown())
   {
      time_t nextAt = time(0) + 60;
      update();

      while (!doShutDown() && time(0) < nextAt)
         sleep(1);
   }

   return done;
}

//***************************************************************************
// Update
//***************************************************************************

int W1::update()
{
   tell(0, "Updating ...");

   scan();

   json_t* oJson = json_array();

   for (auto it = sensors.begin(); it != sensors.end(); it++)
   {
      char line[100+TB];
      FILE* in;
      char* path;

      if (isEmpty(it->first.c_str()))
         continue;

      asprintf(&path, "%s/%s/w1_slave", w1Path, it->first.c_str());

      if (!(in = fopen(path, "r")))
      {
         tell(eloAlways, "Error: Opening '%s' failed, error was '%s'", path, strerror(errno));
         tell(eloAlways, "One Wire Sensor '%s' seems to be detached, removing it", path);
         sensors.erase(it--);
         free(path);
         continue;
      }

      while (fgets(line, 100, in))
      {
         char* p;
         line[strlen(line)-1] = 0;

         if (strstr(line, " crc="))
         {
            if (!strstr(line, " YES"))
            {
               tell(0, "Error: CRC check for '%s' failed in [%s], skipping sample", it->first.c_str(), line);
               break;
            }
         }

         else if ((p = strstr(line, " t=")))
         {
            double value = atoi(p+3) / 1000.0;

            if (value >= 80 || value <= -80)
            {
               // at error we get sometimes +85 or -85 from the sensor so limit the range
               //  to -80 - +80

               tell(0, "Error: Ignoring invalid value (%0.2f) of w1 sensor '%s'", value, it->first.c_str());
               break;
            }

            if (sensors[it->first].values.size() >= 3)
            {
               double sum = std::accumulate(sensors[it->first].values.cbegin(), sensors[it->first].values.cend(), 0);
               double average = sum / sensors[it->first].values.size();
               double delta = abs(average - value);

               tell(1, "Info: %s : %0.2f, the average of the last %zd samples is %0.2f (delta is %0.2f)",
                    it->first.c_str(), value, sensors[it->first].values.size(), average, average - value);

               if (delta > 5)
                  tell(0, "Warning: delta %0.2f", delta);
            }

            if (sensors[it->first].values.size() >= 3)
               sensors[it->first].values.erase(sensors[it->first].values.begin());

            sensors[it->first].values.push_back(value);
            sensors[it->first].value = value;

            json_t* ojData = json_object();
            json_array_append_new(oJson, ojData);

            json_object_set_new(ojData, "name", json_string(it->first.c_str()));
            json_object_set_new(ojData, "value", json_real(value));

            tell(0, "%s : %0.2f", it->first.c_str(), value);
         }
      }

      fclose(in);
      free(path);

      // take a breath .. due to a forum post we wait 1 second:
      //   -> "jede Lesung am Bus zieht die Leitungen runter und da alle Sensoren am selben Bus hängen, deswegen min. 1s zwischen den Lesungen"

      sleep(1);
   }

   if (mqttConnection() != success)
      return fail;

   char* p = json_dumps(oJson, JSON_REAL_PRECISION(4));
   json_decref(oJson);
   mqttWriter->writeRetained(mqttTopic, p);
   free(p);

   tell(0, " ... done");

   return done;
}

//***************************************************************************
// MQTT Connection
//***************************************************************************

int W1::mqttConnection()
{
   if (!mqttWriter)
      mqttWriter = new Mqtt();

   if (!mqttWriter->isConnected())
   {
      if (mqttWriter->connect(mqttUrl) != success)
      {
         tell(0, "Error: MQTT: Connecting publisher to '%s' failed", mqttUrl);
         return fail;
      }

      tell(0, "MQTT: Connecting publisher to '%s' succeeded", mqttUrl);
   }

   return success;
}

//***************************************************************************
// Main
//***************************************************************************

int main(int argc, char** argv)
{
   W1* job;
   int nofork = no;
   int pid;
   int _stdout = na;
   int _level = na;

   logstdout = yes;

   // Usage ..

   if (argc > 1 && (argv[1][0] == '?' || (strcmp(argv[1], "-h") == 0) || (strcmp(argv[1], "--help") == 0)))
   {
      // showUsage(argv[0]);
      return 0;
   }

   // Parse command line

   for (int i = 0; argv[i]; i++)
   {
      if (argv[i][0] != '-' || strlen(argv[i]) != 2)
         continue;

      switch (argv[i][1])
      {
         case 'l': if (argv[i+1]) _level = atoi(argv[i+1]); break;
         case 't': _stdout = yes;                           break;
         case 'n': nofork = yes;                            break;
      }
   }

   if (_stdout != na)
      logstdout = _stdout;
   else
      logstdout = no;

   // read configuration ..

   // if (readConfig() != success)
   //    return 1;

   if (_level != na)  loglevel = _level;

   job = new W1();

   if (job->init() != success)
   {
      printf("Initialization failed, see syslog for details\n");
      delete job;
      return 1;
   }

   // fork daemon

   if (!nofork)
   {
      if ((pid = fork()) < 0)
      {
         printf("Can't fork daemon, %s\n", strerror(errno));
         return 1;
      }

      if (pid != 0)
         return 0;
   }

   // register SIGINT

   ::signal(SIGINT, W1::downF);
   ::signal(SIGTERM, W1::downF);

   // do work ...

   job->loop();

   // shutdown

   tell(eloAlways, "shutdown");

   delete job;

   return 0;
}
