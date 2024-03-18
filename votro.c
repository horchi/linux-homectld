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

#define isBitSet(value, bit)  ((value >> (bit)) & 0x01)

//***************************************************************************
// Class - VotroCom (RS485)
//***************************************************************************

class VotroCom
{
   public:

      enum DeviceAddress
      {
         daMainsCharger = 0x04,
         daSolar        = 0x10,
         daBooster      = 0xc4,

         daDisplay      = 0xf4
      };

      enum Parameter
      {
         // daSolar

         pCurrent       = 0x02,
         pBattVoltage   = 0x03,
         pPanelVoltage  = 0x04,

         pKfzBattVolage = 0x62,
         pPower         = 0x87,

         // daMainsCharger

         pBattVoltage04   = 0x05,
         pKfzBattVolage04 = 0x09,
         pCurrent04       = 0xa1,

         // daBooster

      };

      enum ComBytes
      {
         cbStart       = 0x00,
         cpPacketType,
         cbDestination,
         cbSource,
         cbCommand,
         cbParameter,
         cbValueLow,          // little endian -> low byte first
         cbValueHight,
         cbChecksum
      };

      enum Command
      {
         cReadValue  = 0x03,
         cWriteValue = 0x09   // not shure - only speculation
      };

      enum Specials
      {
         sStartByte = 0xaa
      };

      enum MessageType
      {
         mtRequest  = 0x22,
         mtResponce = 0x62
      };

      enum ParameterType
      {
         ptBit,
         ptByte,
         ptWord
      };

      struct ParameterDefinition
      {
         std::string title;
         ParameterType type {ptWord};
         uint factor {0};             // factor or bit number (0-7)
         std::string unit;
      };

      struct Response
      {
         DeviceAddress source {};
         Parameter parameter {};
         byte valueHight {0x00};
         byte valueLow {0x00};
         double value {0.0};
         bool state {false};
      };

      VotroCom(const char* aDevice);
      ~VotroCom() { close(); }

      int open();
      int close();

      int request(byte address, byte parameter);
      int parseResponse(byte buffer[], size_t size);
      byte calcChecksum(unsigned char buffer[], size_t size);

      Response* getResponse() { return &response; }

      static std::map<DeviceAddress,std::map<Parameter,ParameterDefinition>> parameterDefinitions;

   private:

      Response response;
      std::string device;
      Serial serial;
      Sem sem;            // to protect synchronous access to device
};

//***************************************************************************
// VotroCom
//***************************************************************************

std::map<VotroCom::DeviceAddress,std::map<VotroCom::Parameter,VotroCom::ParameterDefinition>> VotroCom::parameterDefinitions
{
   //   parameter,      title,                 format, factor, unit
   //                                                  or bit
   { daSolar, {
         { pCurrent,         { "Strom",               ptWord,  10, "A" } },
         { pBattVoltage,     { "Batterie Spannung",   ptWord, 100, "V" } },
         { pPanelVoltage,    { "Panel Spannung",      ptWord, 100, "V" } },
         { pPower,           { "Leistung",            ptWord,  10, "W" } },
         { pKfzBattVolage,   { "Kfz Batterie",        ptWord, 100, "V" } }
      }},
   { daBooster, {
         { pCurrent,         { "Strom ??",            ptWord,  10, "A" } }
      }},
   { daMainsCharger, {
         { pCurrent04,       { "Strom",               ptWord,  10, "A" } },
         { pBattVoltage04,   { "Batterie Spannung",   ptWord, 100, "V" } },
         { pKfzBattVolage04, { "Kfz Batterie",        ptWord, 100, "V" } }
      }}
};

//***************************************************************************
// VotroCom
//***************************************************************************

VotroCom::VotroCom(const char* aDevice)
   : device(aDevice),
     serial(B19200, CS8 | PARENB),
     sem(0x4da00001)
{
   // 19200 bits per second, 8 bit, even parity and 1 stop bit
}

//***************************************************************************
// Open / Close
//***************************************************************************

int VotroCom::open()
{
   return serial.open(device.c_str());
}

int VotroCom::close()
{
   return serial.close();
}

//***************************************************************************
// Calc Checksum
//***************************************************************************

byte VotroCom::calcChecksum(unsigned char buffer[], size_t size)
{
   byte checksum {0x55};

   for (uint i = 0; i < size; ++i)
      checksum ^= buffer[i];

   return checksum;
}

//***************************************************************************
// Parse Response
//***************************************************************************

int VotroCom::parseResponse(byte buffer[], size_t size)
{
   if (buffer[cbStart] != sStartByte ||
       buffer[cpPacketType] != mtResponce ||
       buffer[cbDestination] != daDisplay ||
       buffer[cbCommand] != cReadValue)
   {
      tell(eloAlways, "Error: Got unexpected response, ignoring");
      return fail;
   }

   response.source = (DeviceAddress)buffer[cbSource];
   response.parameter = (Parameter)buffer[cbParameter];
   response.valueLow = buffer[cbValueLow];
   response.valueHight = buffer[cbValueHight];
   response.value = 0;
   response.state = false;

   byte checksum = calcChecksum(buffer, 8);

   if (buffer[cbChecksum] != checksum)
      tell(eloAlways, "Warning: Checksum failure in received packet");

   // lookup definition

   if (parameterDefinitions.find(response.source) != parameterDefinitions.end())
   {
      auto& deviceDef = parameterDefinitions[response.source];

      if (deviceDef.find(response.parameter) != deviceDef.end())
      {
         const auto& def = deviceDef[response.parameter];

         if (def.type == ptWord)
            response.value = (response.valueHight << 8) + response.valueLow;
         else if (def.type == ptBit && def.factor < 8)
            response.state = isBitSet(response.valueLow, def.factor);
         else if (def.type == ptBit && def.factor >= 8)
            response.state = isBitSet(response.valueHight, def.factor-8);
         else
            response.value = response.valueLow;

         response.value /= def.factor;
      }
      else
      {
         response.value = (response.valueHight << 8) + response.valueLow;
      }
   }

   return success;
}

//***************************************************************************
// Request
//***************************************************************************

int VotroCom::request(byte address, byte parameter)
{
   open();

   if (!serial.isOpen())
      return fail;

   sem.p();

   unsigned char buffer[10] {};
   size_t contentLen {0};

   buffer[contentLen++] = sStartByte;         // start byte
   buffer[contentLen++] = mtRequest;          // request (packet type)
   buffer[contentLen++] = address;            // votro device address
   buffer[contentLen++] = daDisplay;          // source id
   buffer[contentLen++] = 0x03;               // command (read value)
   buffer[contentLen++] = parameter;          // the parameter address to read
   buffer[contentLen++] = 0x00;               // value low byte
   buffer[contentLen++] = 0x00;               // value heigh byte
   byte checksum = calcChecksum(buffer, contentLen);
   buffer[contentLen++] = checksum;           // checksum

   int status {success};

   if ((status = serial.write(buffer, contentLen)) != success)
   {
      tell(eloAlways, "Writing request failed, state was %d", status);
      sem.v();
      return status;
   }

   // read response

   byte b {0};
   bool shown {false};

   byte _response[10] {};
   size_t cnt {0};

   while ((status = serial.look(b, 50)) == success)
   {
      if (!cnt && b != sStartByte)
      {
         tell(eloAlways, "Skipping unexpected start byte 0x%02x", b);
         continue;
      }

      if (cnt >= 9)
         break;

      _response[cnt++] = b;

      if (eloquence & eloDetail)
      {
         if (!shown)
         {
            shown = true;
            printf("-> '");
            for (uint i = 0; i < contentLen; ++i)
               printf("0x%02x,", buffer[i]);
            printf("'\n");
         }

         printf("<- '0x%02x'\n", b);
         fflush(stdout);
      }
   }

   status = parseResponse(_response, cnt);

   serial.close();
   sem.v();

   return status;
}

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

      static bool doShutDown() { return shutdown; }

   protected:

      int mqttConnection();
      int mqttPublish(SensorData& sensor, const char* type);

      std::string device;
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
   : device(aDevice),
     mqttUrl(aMqttUrl),
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

   VotroCom com(device.c_str());
   SensorData sensor {};

   for (const auto& deviceDef : VotroCom::parameterDefinitions)
   {
      for (const auto& def : deviceDef.second)
      {
         tell(eloDebug, "Requesting parameter '0x%02x/%s' (0x%02x)", deviceDef.first, def.second.title.c_str(), def.first);

         com.request(deviceDef.first, def.first);
         VotroCom::Response* response = com.getResponse();

         // tell(eloAlways, "%s: %.2f %s", def.second.title.c_str(), response->value, def.second.unit.c_str());

         std::string type = "VOTRO" + horchi::to_string(deviceDef.first, 0, true);
         mqttPublish(sensor = {def.first, "value", def.second.title.c_str(), def.second.unit.c_str(), response->value}, type.c_str());
      }
   }

   tell(eloInfo, " ... done");

   return done;
}

//***************************************************************************
// MQTT Publish
//***************************************************************************

int Votro::mqttPublish(SensorData& sensor, const char* type)
{
   json_t* obj = json_object();

   json_object_set_new(obj, "type", json_string(type));
   json_object_set_new(obj, "address", json_integer(sensor.address));
   json_object_set_new(obj, "title", json_string(sensor.title.c_str()));
   json_object_set_new(obj, "unit", json_string(sensor.unit.c_str()));
   json_object_set_new(obj, "kind", json_string(sensor.kind.c_str()));

   if (sensor.kind == "value")
      json_object_set_new(obj, "value", json_real(sensor.value));
   else
      json_object_set_new(obj, "text", json_string(sensor.text.c_str()));

   char* message = json_dumps(obj, JSON_REAL_PRECISION(8));
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
// Show
//***************************************************************************

int showParameter(VotroCom* com, byte address, byte parameter, int factor)
{
   com->request(address, parameter);

   VotroCom::Response* response = com->getResponse();
   std::string byte1 = bin2string(response->valueLow);
   std::string byte2 = bin2string(response->valueHight);

   word wValue = (response->valueHight << 8) + response->valueLow;

   tell(eloAlways, "Parameter 0x%02x: byte1 %03d, byte2 %03d, word %d /  '%s' '%s'  (%.2f)",
        response->parameter, response->valueLow, response->valueHight, wValue,
        byte1.c_str(), byte2.c_str(),
        (double)(wValue)/(double)factor);

   return success;
}

int show(const char* device, byte address, int parameter, int factor)
{
   VotroCom com(device);

   if (parameter != na)
   {
      while (!Votro::doShutDown())
      {
         showParameter(&com, address, parameter, factor);
         sleep(1);
      }
   }
   else
   {
      for (uint par = 0; par <= 0xff; ++par)
         showParameter(&com, address, par, factor);
   }

   return done;
}

//***************************************************************************
// Detect
//***************************************************************************

int detect(const char* device, byte parameter)
{
   VotroCom com(device);

   for (uint addr = 0x00; addr <= 0xff; ++addr)
      com.request(addr, parameter);

   fflush(stdout);

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
   printf("     -d <device>          serial device file (defaults to /dev/ttyVotro)\n");
   printf("     -l <eloquence>       set eloquence\n");
   printf("     -t                   log to terminal\n");
   printf("     -i <interval>        interval [s] (default 60)\n");
   printf("     -u <url>             MQTT url (default tcp://localhost:1883)\n");
   printf("     -T <topic>           MQTT topic (default " TARGET "2mqtt/votro)\n");
   printf("     -n                   Don't fork in background\n");
   printf("     -s <device-addr>     show and exit, if <parameter> is specified the value is displayed in a loop every second\n");
   printf("        [<parameter-id>]   show only <parameter> instead of all 256 possible\n");
   printf("        [<factor>]         calculate with the assumption that it is transferred with this factor\n");
   printf("     -D <parameter-id>    Try to detect devices\n");
}

//***************************************************************************
// Main
//***************************************************************************

int main(int argc, char** argv)
{
   bool nofork {false};
   int _stdout {na};
   const char* mqttTopic {TARGET "2mqtt/votro"};
   const char* mqttUrl {"tcp://localhost:1883"};
   const char* device {"/dev/ttyVotro"};
   bool showMode {false};
   bool detectMode {false};
   int interval {60};
   int parameter {na};
   byte address {0x10};
   uint showFactor {1};

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
         case 'l':
            if (argv[i+1])
               eloquence = (Eloquence)strtol(argv[++i], nullptr, 0);
            break;
         case 'u': mqttUrl = argv[i+1];                       break;
         case 'i': if (argv[i+1]) interval = atoi(argv[++i]); break;
         case 'T': if (argv[i+1]) mqttTopic = argv[i+1];      break;
         case 't': _stdout = yes;                             break;
         case 'd': if (argv[i+1]) device = argv[++i];         break;
         case 's':
            showMode = true;
            address = strtol(argv[++i], nullptr, 0);
            if (argc > i+1)
               parameter = strtol(argv[++i], nullptr, 0);
            if (argc > i+1)
               showFactor = strtol(argv[++i], nullptr, 0);
            break;
         case 'D':
            detectMode = true;
            parameter = strtol(argv[++i], nullptr, 0);
         break;
         case 'n': nofork = true;                             break;
      }
   }

   // do work ...

   if (detectMode)
      return detect(device, parameter);

   if (showMode)
      return show(device, address, parameter, showFactor);

   if (_stdout != na)
      logstdout = _stdout;
   else
      logstdout = false;

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
