//***************************************************************************
// i2c Interface
// File i2cmqtt.c
// This code is distributed under the terms and conditions of the
// GNU GENERAL PUBLIC LICENSE. See the file LICENSE for details.
// Date 2024-2024 - Jörg Wendel
//***************************************************************************

#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

#include <signal.h>
#include <fcntl.h>
#include <unistd.h>

#include "lib/common.h"
#include "lib/json.h"
#include "lib/mqtt.h"

//***************************************************************************
// Class - ADS 1115
//***************************************************************************

class Ads1115
{
   public:

      enum Register
      {
         registerConversion = 0x00,   // conversion register
         registerConfig     = 0x01    // config register
      };

      enum COMP_QUE  // bit 0-1
      {
         compQueueAssertAfterOne   = 0b00000000,
         compQueueAssertAfterTwo   = 0b00000001,
         compQueueAssertAfterThree = 0b00000010,
         compQueueDisabled         = 0b00000011  // default
      };

      enum COMP_LAT   // bit 2
      {
         compLatOff = 0b00000000,     // default
         compLatOn  = 0b00000100
      };

      enum COMP_POL  // bit 3
      {
         cpActiveLow   = 0b00000000,  // default
         cpActiveHight = 0b00001000
      };

      enum COMP_MODE  // bit 4
      {
         cmHysteresis = 0b00000000,   // default
         cmWindow     = 0b00010000
      };

      enum DATA_RATE  // bit 5-7
      {
         dr8   = 0b00000000,
         dr16  = 0b00100000,
         dr32  = 0b01000000,
         dr64  = 0b01100000,
         dr128 = 0b10000000,          // default
         dr250 = 0b10100000,
         dr475 = 0b11000000,
         dr860 = 0b11100000
      };

      enum OpMode     // bit 8
      {
         omContinuous = 0b00000000,
         omSingleShot = 0b00000001,   // default
      };

      enum PGA        // gain amplifier bit 9-11
      {
         pga0 = 0b00000000,   // FS = ±6.144V
         pga1 = 0b00000010,   // FS = ±4.096V
         pga2 = 0b00000100,   // FS = ±2.048V default
         pga3 = 0b00000110,   // FS = ±1.024V
         pga4 = 0b00001000,   // FS = ±0.512V
         pga5 = 0b00001010,   // FS = ±0.256V
         pga6 = 0b00001100,   // FS = ±0.256V
         pga7 = 0b00001110    // FS = ±0.256V
      };

      enum Channel    // bit 12-14
      {
         ai01   = 0b00000000,  // compares 0 with 1 (defaulr)
         ai03   = 0b00010000,  // compares 0 with 3
         ai13   = 0b00100000,  // compares 1 with 3
         ai21   = 0b00110000,  // compares 2 with 3

         ai0Gnd = 0b01000000,  // compares 0 with GND
         ai1Gnd = 0b01010000,  // compares 1 with GND
         ai2Gnd = 0b01100000,  // compares 2 with GND
         ai3Gnd = 0b01110000   // compares 3 with GND
      };

      enum OpStatus  // bit 15
      {
         osNone   = 0b00000000,
         osSingle = 0b10000000,
      };

      Ads1115() {};
      ~Ads1115();

      int init(const char* aDevice, uint aAddress);
      int exit();
      int read(uint pin, int& milliVolt);
      void setChannel(Channel channel);

   protected:

      void delayAccToRate(DATA_RATE rate);
      int readRegister(uint8_t reg, int16_t& value);
      int writeRegister(uint8_t reg, uint16_t value);

      std::string device {};
      uint address {0};
      int fd {na};
};

Ads1115::~Ads1115()
{
   exit();
}

//***************************************************************************
// Init
//***************************************************************************

int Ads1115::init(const char* aDevice, uint aAddress)
{
   device = aDevice;
   address = aAddress;

   if ((fd = open(device.c_str(), O_RDWR)) < 0)
   {
      tell(eloAlways, "Error: Couldn't open i2c device!");
      return fail;
   }

   // connect to ads1115 as i2c slave

   if (ioctl(fd, I2C_SLAVE, address) < 0)
   {
      tell(eloAlways, "Error: Couldn't find device on address!");
      return fail;
   }

   uint8_t hByte = omContinuous | pga1 | ai0Gnd | osNone;                               // high byte / bit 8-15
   uint8_t lByte = compQueueDisabled | compLatOff | cpActiveLow | cmHysteresis | dr128; // low byte  / bit 0-7

   uint16_t value = (hByte << 8) + lByte;
   writeRegister(registerConfig, value);

   return success;
}

//***************************************************************************
// Exit
//***************************************************************************

int Ads1115::exit()
{
   if (fd >= 0)
   {
      tell(eloAlways, "Closing ADS device\n");
      ::close(fd);
      fd = na;
   }

   return done;
}

//***************************************************************************
// Delay ACC To Rate
//***************************************************************************

void Ads1115::delayAccToRate(DATA_RATE rate)
{
   switch (rate)
   {
      case dr8:   usleep(130000); break;
      case dr16:  usleep(65000);  break;
      case dr32:  usleep(32000);  break;
      case dr64:  usleep(16000);  break;
      case dr128: usleep(8000);   break;
      case dr250: usleep(4000);   break;
      case dr475: usleep(3000);   break;
      case dr860: usleep(2000);   break;
   }
}

//***************************************************************************
// Write Register
//***************************************************************************

int Ads1115::writeRegister(uint8_t reg, uint16_t value)
{
   uint8_t buf[3] {};

   buf[0] = reg;
   buf[1] = value >> 8;     // bit 8-15
   buf[2] = value & 0xFF;   // bit 0-7

   // std::string bh = bin2string(buf[1]);
   // std::string bl = bin2string(buf[2]);
   // tell(eloAlways, "Writing register %d: '%s' [%s/%s]", reg, bin2string((uint16_t)((buf[1] << 8) + buf[2])), bh.c_str(), bl.c_str());

   if (::write(fd, buf, 3) != 3)
   {
      tell(eloAlways, "Error: Writing config register failed");
      return fail;
   }

   time_t timeoutAt = time(0) + 2;

   do {
      if (::read(fd, buf, 2) != 2)
      {
         tell(eloAlways, "Error: Protocol failure, aborting");
         return fail;
      }

      if (time(0) > timeoutAt)
      {
         tell(eloAlways, "Error: Timeout on init ADS1115");
         return fail;
      }

      // tell(eloDebug, "<- '0x%02x'", buf[0]);

   } while ((buf[0] & 0x80) != 0);

   return success;
}

//***************************************************************************
// Read Register
//***************************************************************************

int Ads1115::readRegister(uint8_t reg, int16_t& value)
{
   uint8_t buf[2] {};
   buf[0] = reg;

   if (::write(fd, buf, 1) != 1)
   {
      tell(eloAlways, "Error: Query config register failed");
      return fail;
   }

   usleep(10000);

   if (::read(fd, buf, 2) != 2)
   {
      tell(eloAlways, "Error: ADS Communication failed");
      return fail;
   }

   value = (buf[0] << 8) + buf[1];

   return success;
}

//***************************************************************************
// Set Channel
//***************************************************************************

void Ads1115::setChannel(Channel channel)
{
   uint16_t currentConfig {0};

   readRegister(registerConfig, (int16_t&)currentConfig);

   currentConfig &= ~0x7000;
   currentConfig |= (channel << 8);

   writeRegister(registerConfig, currentConfig);

   // if not single shot mode

   if (!(currentConfig & (omSingleShot << 8)))
   {
      DATA_RATE rate = (DATA_RATE)(currentConfig & 0xE0);
      delayAccToRate(rate);
      delayAccToRate(rate);
   }
}

//***************************************************************************
// Read
//***************************************************************************

int Ads1115::read(uint pin, int& milliVolt)
{
   int16_t digits {0};
   int status = readRegister(registerConversion, digits);

   milliVolt = digits * 4096/32768.0;
   // tell(eloDebug, "Debug: ADC digits: %d; Spannung: %d mV", digits, milliVolt);

   return status;
}

//***************************************************************************
// Class I2CMqtt
//***************************************************************************

class I2CMqtt
{
   public:

      struct SensorData
      {
         uint address {0};
         std::string title;
         std::string unit;
         int value {0};
      };

      I2CMqtt(const char* aDevice, uint aAddress, const char* aMqttUrl, const char* aMqttTopic, int aInterval = 60);
      virtual ~I2CMqtt();

      static void downF(int aSignal) { shutdown = true; }

      int init();
      int loop();
      int update();
      bool doShutDown() { return shutdown; }
      int show();

   protected:

      int mqttConnection();
      int mqttPublish(SensorData& sensor);

      Ads1115 ads;
      const char* mqttUrl {};
      Mqtt* mqttWriter {};
      std::string mqttTopic;
      int interval {60};
      std::string device;
      uint address {0};

      static bool shutdown;
};

//***************************************************************************
// I2CMqtt
//***************************************************************************

bool I2CMqtt::shutdown {false};

I2CMqtt::I2CMqtt(const char* aDevice, uint aAddress, const char* aMqttUrl, const char* aMqttTopic, int aInterval)
   : mqttUrl(aMqttUrl),
     mqttTopic(aMqttTopic),
     interval(aInterval),
     device(aDevice),
     address(aAddress)
{
}

I2CMqtt::~I2CMqtt()
{
}

//***************************************************************************
// Init
//***************************************************************************

int I2CMqtt::init()
{
   tell(eloDetail, "Init ADS1115 device '%s' ..", device.c_str());

   if (ads.init(device.c_str(), address) != success)
   {
      tell(eloAlways, "Error: Init failed");
      return -1;
   }

   tell(eloDetail, ".. done");

   return success;
}

//***************************************************************************
// Loop
//***************************************************************************

int I2CMqtt::loop()
{
   init();

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

int I2CMqtt::update()
{
   tell(eloInfo, "Updating ...");

   if (mqttConnection() != success)
      return fail;

   for (uint pin = 0; pin < 4; ++pin)
   {
      Ads1115::Channel ch {};

      switch (pin)
      {
         case 0: ch = Ads1115::ai0Gnd; break;
         case 1: ch = Ads1115::ai1Gnd; break;
         case 2: ch = Ads1115::ai2Gnd; break;
         case 3: ch = Ads1115::ai3Gnd; break;
      }

      ads.setChannel(ch);

      int value {0};

      if (ads.read(0, value) != success)
         return fail;

      tell(eloDebug, "%d) %4d mV", pin, value);

      SensorData sensor {};
      mqttPublish(sensor = {pin, "Analog Input (ADS1115)", "mV", value});
   }

   tell(eloInfo, " ... done");

   return done;
}

//***************************************************************************
// MQTT Publish
//***************************************************************************

int I2CMqtt::mqttPublish(SensorData& sensor)
{
   json_t* obj = json_object();

   // { "value": 77.0, "type": "P4VA", "address": 1, "unit": "°C", "title": "Abgas" }

   json_object_set_new(obj, "type", json_string("I2C"));
   json_object_set_new(obj, "address", json_integer(sensor.address));
   json_object_set_new(obj, "title", json_string(sensor.title.c_str()));
   json_object_set_new(obj, "unit", json_string(sensor.unit.c_str()));
   json_object_set_new(obj, "value", json_integer(sensor.value));

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

int I2CMqtt::mqttConnection()
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
      tell(eloAlways, "MQTT: Publish i2c data to topic '%s'", mqttTopic.c_str());
   }

   return success;
}

//***************************************************************************
// Show
//***************************************************************************

int I2CMqtt::show()
{
   // show

   tell(eloAlways, "-----------------------");

   for (uint pin = 0; pin < 4; ++pin)
   {
      Ads1115::Channel ch {};

      switch (pin)
      {
         case 0: ch = Ads1115::ai0Gnd; break;
         case 1: ch = Ads1115::ai1Gnd; break;
         case 2: ch = Ads1115::ai2Gnd; break;
         case 3: ch = Ads1115::ai3Gnd; break;
      }

      ads.setChannel(ch);

      int value {0};

      if (ads.read(0, value) != success)
         return fail;

      tell(eloAlways, "%d: %4d mV", pin, value);
   }

   tell(eloAlways, "-----------------------");

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
   printf("     -d <device>     i2c device (defaults to /dev/i2c-0)\n");
   printf("     -a <i2c>        i2c address (defaults to 0x48)\n");
   printf("     -l <eloquence>  set eloquence\n");
   printf("     -t              log to terminal\n");
   printf("     -s              show and exit\n");
   printf("     -i <interval>   interval seconds (default 60)\n");
   printf("     -u <url>        MQTT url\n");
   printf("     -T <topic>      MQTT topic\n");
}

//***************************************************************************
// Main
//***************************************************************************

int main(int argc, char** argv)
{
   bool nofork {false};
   int _stdout {na};
   bool showMode {false};

   const char* mqttTopic {TARGET "2mqtt/i2c"};
   const char* mqttUrl {"tcp://localhost:1883"};
   const char* device {"/dev/i2c-0"};
   int interval {60};
   uint address {0x48};

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
         case 'u': mqttUrl = argv[i+1];                      break;
         case 'l': if (argv[i+1]) eloquence = (Eloquence)atoi(argv[++i]); break;
         case 'i': if (argv[i+1]) interval = atoi(argv[++i]); break;
         case 'T': if (argv[i+1]) mqttTopic = argv[i+1];     break;
         case 't': _stdout = yes;                            break;
         case 'd': if (argv[i+1]) device = argv[++i];        break;
         case 'a': if (argv[i+1]) address = atoi(argv[++i]); break;
         case 's': showMode = true;                          break;
         case 'n': nofork = true;                            break;
      }
   }

   // do work ...

   if (_stdout != na)
      logstdout = _stdout;
   else
      logstdout = no;

   I2CMqtt* job = new I2CMqtt(device, address, mqttUrl, mqttTopic, interval);

   if (job->init() != success)
   {
      printf("Initialization failed, see syslog for details\n");
      delete job;
      return 1;
   }

   if (showMode)
      return job->show();

   // fork daemon

   if (!nofork)
   {
      int pid;

      if ((pid = fork()) < 0)
      {
         printf("Can't fork daemon, %s\n", strerror(errno));
         return 1;
      }

      if (pid != 0)
         return 0;
   }

   // register SIGINT

   ::signal(SIGINT, I2CMqtt::downF);
   ::signal(SIGTERM, I2CMqtt::downF);

   job->loop();

   // shutdown

   tell(eloAlways, "shutdown");
   delete job;

   return 0;
}
