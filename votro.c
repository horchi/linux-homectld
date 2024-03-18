//***************************************************************************
// VOTRO.. Interface
// File votro.c
// This code is distributed under the terms and conditions of the
// GNU GENERAL PUBLIC LICENSE. See the file LICENSE for details.
// Date 17.03.2024 - Jörg Wendel
//***************************************************************************

#include <signal.h>

#include "lib/common.h"
#include "lib/serial.h"
#include "lib/json.h"
#include "lib/mqtt.h"

#define isBitSet(value, bit)  ((value >> bit) & 0x01)

//***************************************************************************
// Class - Votro
//***************************************************************************

class Votro
{
   public:

      struct SensorData
      {
         uint address {0};
         std::string kind;
         std::string title;
         std::string unit;
         double value {0.0};
         std::string text;
      };

      Votro(const char* aDevice, const char* aMqttUrl, const char* aMqttTopic, int aInterval = 60);
      virtual ~Votro();

      static void downF(int aSignal) { shutdown = true; }

      int init() { return success; }
      int loop();
      int update();
      bool doShutDown() { return shutdown; }

   protected:

      int mqttConnection();
      int mqttPublish(SensorData& sensor);

      const char* mqttUrl {};
      Mqtt* mqttWriter {};
      std::string mqttTopic;
      int interval {60};

      static bool shutdown;
};

//***************************************************************************
// Votro
//***************************************************************************

bool Votro::shutdown {false};

Votro::Votro(const char* aDevice, const char* aMqttUrl, const char* aMqttTopic, int aInterval)
   : mqttUrl(aMqttUrl),
     mqttTopic(aMqttTopic),
     interval(aInterval)
{

}

Votro::~Votro()
{
}

//***************************************************************************
// Loop
//***************************************************************************

int Votro::loop()
{
   while (!doShutDown())
   {
      time_t nextAt = time(0) + interval;
      update();

      while (!doShutDown() && time(0) < nextAt)
         sleep(1);
   }

   return done;
}

//***************************************************************************
// Update
//***************************************************************************

int Votro::update()
{
   tell(eloInfo, "Updating ...");

   if (mqttConnection() != success)
      return fail;

   SensorData sensor {};

   // mqttPublish(sensor = {0x02, "value", "Strom", "A", state.current});
   // mqttPublish(sensor = {0x03, "value", "Leistung", "W", state.current * state.voltage});

   tell(eloInfo, " ... done");

   return done;
}

//***************************************************************************
// MQTT Publish
//***************************************************************************

int Votro::mqttPublish(SensorData& sensor)
{
   json_t* obj = json_object();

   json_object_set_new(obj, "type", json_string("VOTRO"));
   json_object_set_new(obj, "address", json_integer(sensor.address));
   json_object_set_new(obj, "title", json_string(sensor.title.c_str()));
   json_object_set_new(obj, "unit", json_string(sensor.unit.c_str()));
   json_object_set_new(obj, "kind", json_string(sensor.kind.c_str()));

   if (sensor.kind == "value")
      json_object_set_new(obj, "value", json_real(sensor.value));
   else
      json_object_set_new(obj, "text", json_string(sensor.text.c_str()));

   char* message = json_dumps(obj, JSON_REAL_PRECISION(8));
   tell(eloMqtt, "-> %s", message);
   mqttWriter->write(mqttTopic.c_str(), message);
   free(message);
   json_decref(obj);

   return success;
}

//***************************************************************************
// MQTT Connection
//***************************************************************************

int Votro::mqttConnection()
{
   if (!mqttWriter)
      mqttWriter = new Mqtt();

   if (!mqttWriter->isConnected())
   {
      if (mqttWriter->connect(mqttUrl) != success)
      {
         tell(eloAlways, "Error: MQTT: Connecting publisher to '%s' failed", mqttUrl);
         return fail;
      }

      tell(eloAlways, "MQTT: Connecting publisher to '%s' succeeded", mqttUrl);
      tell(eloAlways, "MQTT: Publish VOTRO data to topic '%s'", mqttTopic.c_str());
   }

   return success;
}

//***************************************************************************
// Read State
//***************************************************************************

byte calcChecksum(unsigned char buffer[], size_t size)
{
   byte checksum {0x55};

   for (uint i = 0; i < size; ++i)
      checksum ^= buffer[i];

   return checksum;
}

int request(const char* device, byte address, byte parameter)
{
   // 19200 bits per second, 8 bit, even parity and 1 stop bit

   Serial serial(B19200, CS8 | PARENB);
   serial.open(device);

   unsigned char buffer[100] {};
   size_t contentLen {0};

   buffer[contentLen++] = 0xaa;               // start byte
   buffer[contentLen++] = 0x22;               // request (packet type)
   buffer[contentLen++] = address;            // votro device address
   buffer[contentLen++] = 0xf4;               // sorce id (0xf4 = bus master display)
   buffer[contentLen++] = 0x03;               // command (read value)
   buffer[contentLen++] = parameter;          // the parameter address to read
   buffer[contentLen++] = 0x00;               // value
   buffer[contentLen++] = 0x00;               // value
   byte checksum = calcChecksum(buffer, contentLen);
   buffer[contentLen++] = checksum;           // checksum

   printf("-> '");
   for (uint i = 0; i < contentLen; ++i)
      printf("0x%02x,", buffer[i]);
   printf("'\n");

   int status {success};

   if ((status = serial.write(buffer, contentLen)) != success)
   {
      tell(eloAlways, "Writing request failed, state was %d", status);
      return status;
   }

   // read response

   byte b {0};
   size_t received {0};

   while ((status = serial.look(b, 250)) == success)
   {
      printf("<- '0x%02x'\n", b);
      received++;
   }

   serial.close();

   return received;
}

int show(const char* device)
{
   size_t count {0};
   byte parameter {0x02};

   for (uint addr = 0x00; addr <= 0xff; ++addr)
      count += request(device, addr, parameter);

   printf("received %zu bytes\n", count);

   return success;
}

//***************************************************************************
// Usage
//***************************************************************************

void showUsage(const char* bin)
{
   printf("Usage: %s <command> [-l <log-level>] [-d <device>]\n", bin);
   printf("\n");
   printf("  options:\n");
   printf("     -d <device>     serial device file (defaults to /dev/ttyVotro)\n");
   printf("     -l <eloquence>  set eloquence\n");
   printf("     -t              log to terminal\n");
   printf("     -s              show and exit\n");
   printf("     -i <interval>   interval [s] (default 60)\n");
   printf("     -u <url>        MQTT url (default tcp://localhost:1883)\n");
   printf("     -T <topic>      MQTT topic (default " TARGET "2mqtt/votro)\n");
   printf("     -n              Don't fork in background\n");
}

//***************************************************************************
// Main
//***************************************************************************

int main(int argc, char** argv)
{
   bool nofork {false};
   int _stdout {na};
   const char* mqttTopic = TARGET "2mqtt/votro";
   const char* mqttUrl = "tcp://localhost:1883";
   const char* device = "/dev/ttyUSB1";
   bool showMode {false};
   int interval {60};

   logstdout = true;
   eloquence = eloInfo;

   // usage ..

   if (argc <= 1 || (argv[1][0] == '?' || (strcmp(argv[1], "-h") == 0) || (strcmp(argv[1], "--help") == 0)))
   {
      showUsage(argv[0]);
      return 0;
   }

   for (int i = 1; argv[i]; i++)
   {
      if (argv[i][0] != '-' || strlen(argv[i]) != 2)
         continue;

      switch (argv[i][1])
      {
         case 'l': if (argv[i+1]) eloquence = (Eloquence)atoi(argv[++i]); break;
         case 'u': mqttUrl = argv[i+1];                       break;
         case 'i': if (argv[i+1]) interval = atoi(argv[++i]); break;
         case 'T': if (argv[i+1]) mqttTopic = argv[i+1];      break;
         case 't': _stdout = yes;                             break;
         case 'd': if (argv[i+1]) device = argv[++i];         break;
         case 's': showMode = true;                           break;
         case 'n': nofork = true;                             break;
      }
   }

   // do work ...

   if (showMode)
      return show(device);

   if (_stdout != na)
      logstdout = _stdout;
   else
      logstdout = no;

   Votro* job = new Votro(device, mqttUrl, mqttTopic, interval);

   if (job->init() != success)
   {
      printf("Initialization failed, see syslog for details\n");
      delete job;
      return 1;
   }

   // fork daemon

   if (!nofork)
   {
      int pid {0};

      if ((pid = fork()) < 0)
      {
         printf("Can't fork daemon, %s\n", strerror(errno));
         return 1;
      }

      if (pid != 0)
         return 0;
   }

   // register SIGINT

   ::signal(SIGINT, Votro::downF);
   ::signal(SIGTERM, Votro::downF);

   job->loop();

   // shutdown

   tell(eloAlways, "shutdown");
   delete job;

   return 0;
}
