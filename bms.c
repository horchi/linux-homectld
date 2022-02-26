//***************************************************************************
// BMS Interface
// File bms.c
// This code is distributed under the terms and conditions of the
// GNU GENERAL PUBLIC LICENSE. See the file LICENSE for details.
// Date 22.02.2022 - Jörg Wendel
//***************************************************************************

#include <signal.h>

#include "lib/common.h"
#include "lib/serial.h"
#include "lib/json.h"
#include "lib/mqtt.h"

//***************************************************************************
// Class - Battery Management System Communication
//***************************************************************************

class BmsCom
{
   public:

      enum Commands
      {
         cState       = 0x03,
         cCellVoltage = 0x04
      };

      struct BatteryState
      {
         double voltage {0};
         double current {0};
         double capacity {0};
         double maxCapacity {0};
         word cycles {0};
         word prodDate {0};
         word balanceState {0};
         word balanceStateHigh {0};
         word protectionState {0};
         char version[10] {'\0'};
         byte resCapacity {0};
         byte fetControlState {0};
         byte cellBlockSeriesCount {0};
         byte ntcCount {0};
         std::map<int,double> ntc;
         std::map<int,word> cellVoltages;
      };

      BmsCom(const char* aDevice);
      virtual ~BmsCom();

      int requestState(BatteryState& state);
      int requestCellVoltage(BatteryState& state);

   protected:

      int request(byte command);
      int finish();

      Serial serial;
      std::string device;
      byte payloadSize {0};
};

BmsCom::BmsCom(const char* aDevice)
   : serial(B9600),
     device(aDevice)
{
}

BmsCom::~BmsCom()
{
   serial.close();
}

//***************************************************************************
// Request
//***************************************************************************

int BmsCom::request(byte command)
{
   int status {success};
   byte b {0};

   if (!serial.isOpen())
      return fail;

   while ((status = serial.look(b, 100)) == success)
      printf("flushing '%d'", b);

   int size {0x00};
   unsigned char buffer[100] {'\0'};
   size_t contentLen {0};
   word checksum = ~(command+size) +1;

   tell(eloDebug, "Debug: Checksum: '%s' (0x%x)", bin2string(checksum), checksum);

   buffer[contentLen++] = 0xdd;               // start byte
   buffer[contentLen++] = 0xa5;               // read
   buffer[contentLen++] = command;            // command
   buffer[contentLen++] = 0x00;               // payload size
   buffer[contentLen++] = checksum >> 8;      // checksum hight byte
   buffer[contentLen++] = checksum & 0x00FF;  // checksum low byte
   buffer[contentLen++] = 0x77;               // end byte

   if ((status = serial.write(buffer, contentLen)) != success)
   {
      tell(eloAlways, "Writing request failed, state was %d", status);
      return status;
   }

   tell(eloDebug, "Debug: Wrote %zu bytes", contentLen);

   byte cmd {0};
   byte state {0};

   if (serial.readByte(b, 1000) != success)
   {
      tell(eloAlways, "Timeout reading start byte");
      return fail;
   }

   tell(eloDebug, "Debug: Read start byte 0x%2x", b);

   if (serial.readByte(cmd) != success)
      return fail;

   tell(eloDebug, "Debug: Read command 0x%2x", cmd);

   if (serial.readByte(state) != success)
      return fail;

   tell(eloDebug, "Debug: Got state  %d", state);

   if (state != 0 || cmd != command)
   {
      tell(eloAlways, "Communication failed, %d / %d", state, cmd);
      return fail;
   }

   serial.readByte(payloadSize);           // payload byte count
   tell(eloDebug, "Debug: %d bytes of payload to read", payloadSize);

   return success;
}

//***************************************************************************
// Cell Voltage
//***************************************************************************

int BmsCom::requestCellVoltage(BatteryState& state)
{
   serial.open(device.c_str());

   if (request(cCellVoltage) != success)
   {
      serial.close();
      return fail;
   }

   word w {0};

   for (int i = 0; i < payloadSize / 2; i++)
   {
      serial.readWord(w);
      state.cellVoltages[i] = w;
   }

   return finish();
}

int BmsCom::requestState(BatteryState& state)
{
   serial.open(device.c_str());

   if (request(cState) != success)
   {
      serial.close();
      return fail;
   }

   byte b {0};
   word w {0};

   serial.readWord(w);
   state.voltage = w / 100.0;
   serial.readWord(w);
   state.current = w / 100.0;
   serial.readWord(w);
   state.capacity = w / 100.0;
   serial.readWord(w);
   state.maxCapacity = w / 100.0;

   serial.readWord(state.cycles);
   serial.readWord(state.prodDate);
   serial.readWord(state.balanceState);
   serial.readWord(state.balanceStateHigh);
   serial.readWord(state.protectionState);
   serial.readByte(b);
   sprintf(state.version, "%x.%x", b >> 4, b & 0x0F);
   serial.readByte(state.resCapacity);
   serial.readByte(state.fetControlState);
   serial.readByte(state.cellBlockSeriesCount);
   serial.readByte(state.ntcCount);

   for (int i = 0; i < state.ntcCount; i++)
   {
      serial.readWord(w);
      state.ntc[i] = (w-2731) / 10.0;
   }

   return finish();
}

int BmsCom::finish()
{
   byte b {0};
   word w {0};

   serial.readWord(w);          // checksum
   serial.readByte(b);          // end byte
   payloadSize = 0;
   serial.close();

   return success;
}

//***************************************************************************
// Calss - Battery Management System
//***************************************************************************

class Bms
{
   public:

      struct SensorData
      {
         uint address {0};
         std::string title;
         std::string unit;
         double value {0.0};
      };

      Bms(const char* aDevice, const char* aMqttUrl, const char* aMqttTopic, int aInterval = 60);
      virtual ~Bms();

      static void downF(int aSignal) { shutdown = true; }

      int init() { return success; }
      int loop();
      int update();
      bool doShutDown() { return shutdown; }

   protected:

      int mqttConnection();
      int mqttPublish(SensorData& sensor);

      const char* mqttUrl {nullptr};
      Mqtt* mqttWriter {nullptr};
      std::string mqttTopic;
      int interval {60};
      BmsCom bmsCom;

      static bool shutdown;
};

//***************************************************************************
// BMS
//***************************************************************************

bool Bms::shutdown {false};

Bms::Bms(const char* aDevice, const char* aMqttUrl, const char* aMqttTopic, int aInterval)
   : mqttUrl(aMqttUrl),
     mqttTopic(aMqttTopic),
     interval(aInterval),
     bmsCom(aDevice)
{

}

Bms::~Bms()
{
}

//***************************************************************************
// Loop
//***************************************************************************

int Bms::loop()
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

int Bms::update()
{
   tell(eloDetail, "Updating ...");

   if (mqttConnection() != success)
      return fail;

   BmsCom::BatteryState state;

   if (bmsCom.requestState(state) != success)
      return fail;

   if (bmsCom.requestCellVoltage(state) != success)
      return fail;

   SensorData sensor {};
   uint addr{1};

   mqttPublish(sensor = {addr++, "Spannung", "V", state.voltage});
   mqttPublish(sensor = {addr++, "Strom", "A", state.current});
   mqttPublish(sensor = {addr++, "Leistung", "W", state.current * state.voltage});
   mqttPublish(sensor = {addr++, "Maximale Kapazität", "Ah", state.maxCapacity});
   mqttPublish(sensor = {addr++, "Kapazität", "Ah", state.capacity});
   mqttPublish(sensor = {addr++, "Ladestatus", "%", (double)state.resCapacity});
   mqttPublish(sensor = {addr++, "Zyklen", "", (double)state.cycles});
   mqttPublish(sensor = {addr++, "Zellen-Blöcke", "", (double)state.cellBlockSeriesCount});
   // mqttPublish(sensor = {addr++, "BMS Version", "", state.version});
   mqttPublish(sensor = {addr++, "FET Status", "", (double)state.fetControlState}); // bin2string(state.fetControlState));
   mqttPublish(sensor = {addr++, "Protection State", "", (double)state.protectionState}); // bin2string(state.protectionState));

   for (int i = 0; i < state.ntcCount; i++)
      mqttPublish(sensor = {addr++, "NTC", "°C", state.ntc[i]});

   return done;
}

//***************************************************************************
// MQTT Publish
//***************************************************************************

int Bms::mqttPublish(SensorData& sensor)
{
   json_t* obj = json_object();

   json_object_set_new(obj, "type", json_string("BMS"));
   json_object_set_new(obj, "address", json_integer(sensor.address));
   json_object_set_new(obj, "title", json_string(sensor.title.c_str()));
   json_object_set_new(obj, "unit", json_string(sensor.unit.c_str()));
   json_object_set_new(obj, "value", json_real(sensor.value));

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

int Bms::mqttConnection()
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
      tell(eloAlways, "MQTT: Publishe BMS data to topic '%s'", mqttTopic.c_str());
   }

   return success;
}

//***************************************************************************
// Read State
//***************************************************************************

int show(const char* device)
{
   BmsCom bms(device);
   BmsCom::BatteryState state;

   if (bms.requestState(state) != success)
      return fail;

   if (bms.requestCellVoltage(state) != success)
      return fail;

   // show

   tell(eloAlways, "-----------------------");

   tell(eloAlways, " Spannung:\t%.2f V", state.voltage);
   tell(eloAlways, " Strom:\t%.2f A", state.current);
   tell(eloAlways, " Leistung:\t%.2f W", state.current * state.voltage);
   tell(eloAlways, " Ladestatus:\t%.2f Ah", state.capacity);
   tell(eloAlways, " Max Kapazität:\t%.2f Ah", state.maxCapacity);
   tell(eloAlways, " Kapazität:\t%d %%", state.resCapacity);

   for (int i = 0; i < state.ntcCount; i++)
      tell(eloAlways, " NTC %d:\t%0.1f °C", i+1, state.ntc[i]);

   tell(eloAlways, " Zyklen:\t%d", state.cycles);
   tell(eloAlways, " Zellen-Blöcke in Reihe:\t%d", state.cellBlockSeriesCount);
   tell(eloAlways, " BMS Version:\t%s", state.version);
   tell(eloAlways, " FET Status:\t%d '%s'", state.fetControlState, bin2string(state.fetControlState));
   tell(eloAlways, " Protection State:\t%d '%s'", state.protectionState, bin2string(state.protectionState));

   // tell(eloAlways, " balanceState:\t%d", state.balanceState);
   // tell(eloAlways, " balanceState:\t%d", state.balanceStateHigh);

   tell(eloAlways, "-----------------------");

   if (bms.requestCellVoltage(state) == success)
   {
      for (const auto& v : state.cellVoltages)
         tell(eloAlways, " Zelle %d: %d mV", v.first, v.second);
   }

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
   printf("     -d <device>     serial device file (defaults to /dev/ttyUSB0)\n");
   printf("     -l <eloquence>  set eloquence\n");
   printf("     -t              log to terminal\n");
   printf("     -s              show and exit\n");
   printf("     -i <interval>   interval\n");
   printf("     -u <url>        MQTT url\n");
   printf("     -T <topic>      MQTT topic\n");
}

//***************************************************************************
// Main
//***************************************************************************

int main(int argc, char** argv)
{
   int _stdout = na;
   const char* mqttTopic = TARGET "2mqtt/bms";
   const char* mqttUrl = "tcp://localhost:1883";
   const char* device = "/dev/ttyUSB0";
   bool showMode {false};
   int interval {60};

   // usage ..

   if (argc <= 1 || (argv[1][0] == '?' || (strcmp(argv[1], "-h") == 0) || (strcmp(argv[1], "--help") == 0)))
   {
      showUsage(argv[0]);
      return 0;
   }

   logstdout = true;

   for (int i = 1; argv[i]; i++)
   {
      if (argv[i][0] != '-' || strlen(argv[i]) != 2)
         continue;

      switch (argv[i][1])
      {
         case 'u': mqttUrl = argv[i+1];                     break;
         case 'l': if (argv[i+1]) eloquence = (Eloquence)atoi(argv[++i]); break;
         case 'i': if (argv[i+1]) interval = atoi(argv[++i]); break;
         case 'T': if (argv[i+1]) mqttTopic = argv[i+1];    break;
         case 't': _stdout = yes;                           break;
         case 'd': if (argv[i+1]) device = argv[++i];       break;
         case 's': showMode = true;                         break;
      }
   }

   // do work ...

   if (showMode)
      return show(device);

   if (_stdout != na)
      logstdout = _stdout;
   else
      logstdout = no;

   Bms* job = new Bms(device, mqttUrl, mqttTopic, interval);

   if (job->init() != success)
   {
      printf("Initialization failed, see syslog for details\n");
      delete job;
      return 1;
   }

   // register SIGINT

   ::signal(SIGINT, Bms::downF);
   ::signal(SIGTERM, Bms::downF);

   job->loop();

   // shutdown

   tell(eloAlways, "shutdown");
   delete job;

   return 0;
}
