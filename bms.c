//***************************************************************************
// BMS Interface
// File bms.c
// This code is distributed under the terms and conditions of the
// GNU GENERAL PUBLIC LICENSE. See the file LICENSE for details.
// Date 22.02.2022 - Jörg Wendel
//
// https://www.lithiumbatterypcb.com/product/4s-lifepo4-battery-bluetooth-bms-with-100a-constant-discharge-current-for-12v-lfp-battery-pack/
//***************************************************************************

#include <signal.h>

#include "lib/common.h"
#include "lib/serial.h"
#include "lib/json.h"
#include "lib/mqtt.h"

#define isBitSet(value, bit)  ((value >> bit) & 0x01)

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
         word balanceState {0};     // each bit represents each cell block’s balance, 0 is off, 1 is on ; 1~16pcs in series.
         word balanceStateHigh {0}; // each bit represents each cell block’s balance, 0 is off, 1 is on ; 17~32pcs in series, 32pcs at the most.
         word protectionState {0};  // each bit represents a protective state, 0 is unprotected, and 1 is protected.
         char version[10] {'\0'};   // 0x10 is for Version 1.0
         byte resCapacity {0};      // it means the percentage of the residual capacity
         byte fetControlState {0};  // MOS is status, bit0 is charging. bit1 is discharging, 0 is MOS OFF
         byte cellBlockSeriesCount {0};
         byte ntcCount {0};
         std::map<int,double> ntc;
         std::map<int,word> cellVoltages;

         std::string protectionStateString;
      };

      enum ProtectionState
      {
         psCellBlockOverVol        = 0x0,   // Cell Block Over-Vol
         psCellBlockUnderVol       = 0x1,   // Cell Block Under-Vol
         psBatteryOverVol          = 0x2,   // Battery Over-Vol
         psBatteryUnderVol         = 0x3,   // Battery Under-Vol
         psChargingOverTemp        = 0x4,   // Charging Over-temp
         psChargingLowTemp         = 0x5,   // Charging Low-temp
         psDischargingOverTemp     = 0x6,   // Discharging Over-temp
         psDischargingLowTemp      = 0x7,   // Discharging Low-temp
         psChargingOverCurrent     = 0x8,   // Charging Over-current
         psDischargingOverCurrent  = 0x9,   // Discharging Over-current
         psShortCircuit            = 0xA,   // Short Circuit
         psForeEndICError          = 0xB,   // Fore-end IC Error
         psMOSSoftwareLockIn       = 0xC,   // MOS Software Lock-in

         psCount

         // bit13-bit15 Reserve
      };

      BmsCom(const char* aDevice);
      virtual ~BmsCom();

      int open()  { return serial.open(device.c_str()); }
      int close() { return serial.close(); }

      int requestState(BatteryState& state);
      int requestCellVoltage(BatteryState& state);

   protected:

      int request(byte command);
      int finish();

      Serial serial;
      std::string device;
      byte payloadSize {0};

      static std::map<uint8_t,std::string> protectionStates;
};

//***************************************************************************
// Protection States
//***************************************************************************

std::map<uint8_t,std::string> BmsCom::protectionStates
{
   { psCellBlockOverVol,       "Cell Block Over Volt" },
   { psCellBlockUnderVol,      "Cell Block Under Volt" },
   { psBatteryOverVol,         "Battery Over Volt" },
   { psBatteryUnderVol,        "Battery Under Volt" },
   { psChargingOverTemp,       "Charging Over Temp" },
   { psChargingLowTemp,        "Charging Low-temp" },
   { psDischargingOverTemp,    "Discharging Over Temp" },
   { psDischargingLowTemp,     "Discharging Low Temp" },
   { psChargingOverCurrent,    "Charging Over Current" },
   { psDischargingOverCurrent, "Discharging Over Current" },
   { psShortCircuit,           "Short Circuit" },
   { psForeEndICError,         "Fore-end IC Error" },
   { psMOSSoftwareLockIn,      "MOS Software Lock-in" }
};

//***************************************************************************
// BmsCom
//***************************************************************************

BmsCom::BmsCom(const char* aDevice)
   : serial(B9600),
     device(aDevice)
{
   // serial.setSpecialSpeed(9600);
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

   while ((status = serial.look(b, 200)) == success)
      printf("flushing '0x%02x'\n", b);

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

   if (serial.readByte(b, 250) != success)
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
   const uint maxTries {2};
   int status {success};

   for (uint i = 0; i < maxTries; i++)
   {
      if ((status = request(cCellVoltage)) == success)
         break;

      tell(eloDetail, "retrying #%d ..", i);
   }

   if (status != success)
      return fail;

   sword w {0};

   for (int i = 0; i < payloadSize / 2; i++)
   {
      serial.readSWord(w);
      state.cellVoltages[i] = w;
   }

   return finish();
}

int BmsCom::requestState(BatteryState& state)
{
   const uint maxTries {20};
   int status {success};

   for (uint i = 0; i < maxTries; i++)
   {
      if ((status = request(cState)) == success)
         break;

      tell(eloDetail, "retrying #%d ..", i);
   }

   if (status != success)
      return fail;

   byte b {0};
   sword w {0};

   serial.readSWord(w);
   state.voltage = w / 100.0;
   serial.readSWord(w);
   state.current = w / 100.0;
   serial.readSWord(w);
   state.capacity = w / 100.0;
   serial.readSWord(w);
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
      serial.readSWord(w);
      state.ntc[i] = (w-2731) / 10.0;
   }

   // determine protection state

   for (uint8_t i = 0; i < psCount; ++i)
   {
      if (isBitSet(state.protectionState, i))
         state.protectionStateString += protectionStates[i] + "\n";
   }

   if (state.protectionStateString == "")
      state.protectionStateString = "Online";

   // very crude validity check

   if (state.ntcCount != 2)
      return fail;

   return finish();
}

int BmsCom::finish()
{
   byte b {0};
   word w {0};

   serial.readWord(w);          // checksum
   serial.readByte(b);          // end byte
   payloadSize = 0;

   return success;
}

//***************************************************************************
// Class - Battery Management System
//***************************************************************************

class Bms
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

      const char* mqttUrl {};
      Mqtt* mqttWriter {};
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
   tell(eloInfo, "Updating ...");

   if (mqttConnection() != success)
      return fail;

   bmsCom.open();
   BmsCom::BatteryState state;

   if (bmsCom.requestState(state) != success)
   {
      tell(eloInfo, " ... failed");
      return fail;
   }

   SensorData sensor {};

   mqttPublish(sensor = {0x01, "value", "Spannung", "V", state.voltage});
   mqttPublish(sensor = {0x02, "value", "Strom", "A", state.current});
   mqttPublish(sensor = {0x03, "value", "Leistung", "W", state.current * state.voltage});
   mqttPublish(sensor = {0x04, "value", "Maximale Kapazität", "Ah", state.maxCapacity});
   mqttPublish(sensor = {0x05, "value", "Kapazität", "Ah", state.capacity});
   mqttPublish(sensor = {0x06, "value", "Ladestatus", "%", (double)state.resCapacity});
   mqttPublish(sensor = {0x07, "value", "Zyklen", "", (double)state.cycles});
   mqttPublish(sensor = {0x08, "value", "Zellen-Blöcke", "", (double)state.cellBlockSeriesCount});
   mqttPublish(sensor = {0x09, "value", "FET Status", "", (double)state.fetControlState}); // bin2string(state.fetControlState));
   mqttPublish(sensor = {0x0A, "text",  "Protection State", "", 0.0, state.protectionStateString});
   mqttPublish(sensor = {0x0B, "value", "NTC", "°C", state.ntc[0]});

   if (state.ntcCount > 1)
      mqttPublish(sensor = {0x0C, "value", "NTC", "°C", state.ntc[1]});

   // mqttPublish(sensor = {0x0D, "value", "BMS Version", "", state.version});

   if (bmsCom.requestCellVoltage(state) != success)
      return fail;

   std::string text;

   for (const auto& v : state.cellVoltages)
   {
      char buf[50];

      sprintf(buf, "Zelle %d\t%d mV", v.first, v.second);
      text += std::string(buf) + "\n";
   }

   mqttPublish(sensor = {0x0E, "text", "Cell State", "", 0.0, text});

   tell(eloInfo, " ... done");
   bmsCom.close();

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
      tell(eloAlways, "MQTT: Publish BMS data to topic '%s'", mqttTopic.c_str());
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
   bms.open();

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

   bms.close();

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
   printf("     -d <device>     serial device file (defaults to /dev/ttyBms)\n");
   printf("     -l <eloquence>  set eloquence\n");
   printf("     -t              log to terminal\n");
   printf("     -s              show and exit\n");
   printf("     -i <interval>   interval [s] (default 60)\n");
   printf("     -u <url>        MQTT url (default tcp://localhost:1883)\n");
   printf("     -T <topic>      MQTT topic (default " TARGET "2mqtt/bms)\n");
   printf("     -n              Don't fork in background\n");
}

//***************************************************************************
// Main
//***************************************************************************

int main(int argc, char** argv)
{
   bool nofork {false};
   int _stdout {na};
   const char* mqttTopic {TARGET "2mqtt/bms"};
   const char* mqttUrl {"tcp://localhost:1883"};
   const char* device {"/dev/ttyBms"};
   bool showMode {false};
   int interval {60};

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
         case 'l': if (argv[i+1]) eloquence = (Eloquence)strtol(argv[++i], nullptr, 0); break;
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
      logstdout = false;

   Bms* job = new Bms(device, mqttUrl, mqttTopic, interval);

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

   ::signal(SIGINT, Bms::downF);
   ::signal(SIGTERM, Bms::downF);

   job->loop();

   // shutdown

   tell(eloAlways, "shutdown");
   delete job;

   return 0;
}
