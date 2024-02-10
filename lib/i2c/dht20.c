//***************************************************************************
// i2c Interface
// File dht20.c
// This code is distributed under the terms and conditions of the
// GNU GENERAL PUBLIC LICENSE. See the file LICENSE for details.
// Date 2024-2024 - Jörg Wendel
//***************************************************************************

#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

#include <fcntl.h>
#include <unistd.h>

#include "../common.h"

#include "dht20.h"

//***************************************************************************
// Init
//***************************************************************************

int Dht20::init(const char* aDevice, uint8_t aAddress)
{
   int status {I2C::init(aDevice, aAddress)};

   if (status != success)
      return status;

   uint16_t word {0};

   if (readRegister(0x71, word) != success)
      return fail;

   tell(eloDetail, "Info: Init status is '0x%04x'", word);

   if (word != 0x18)
   {
      if (resetSensor() != success)
         return fail;
   }

   usleep(1000);  // not needed

   return success;
}

//***************************************************************************
// Is Measuring
//***************************************************************************

bool Dht20::isMeasuring()
{
   uint8_t statusByte {0};

   if (readStatus(statusByte) != success)
      return false;

   return (statusByte & 0x80) == 0x80;
}

//***************************************************************************
// Read the Sensor
//***************************************************************************

int Dht20::read()
{
   if (cTimeMs::now() - lastReadMs < 1000)
      return errLastRead;

   if (writeRegister(0xAC, 0x33, 0x00) != success)
      return fail;

   lastRequestMs = cTimeMs::now();

   uint64_t timeoutAt = cTimeMs::now() + 1000;

   while (isMeasuring())
   {
      if (cTimeMs::now() > timeoutAt)
         return errReadTimeout;
   }

   if (readData() != success)
      return fail;

   return convert();
}

//***************************************************************************
// Read Data
//***************************************************************************

int Dht20::readData()
{
   const size_t dataByteCount {7};
   bool allZero {true};
   uint8_t buf[dataByteCount] {};

   if (::read(fd, buf, dataByteCount) != dataByteCount)
   {
      tell(eloAlways, "Error: '%s' communication failed at reading", chipName());
      return fail;
   }

   for (size_t i = 0; i < dataByteCount; ++i)
   {
      bytes[i] = buf[i];
      allZero = allZero && (bytes[i] == 0);
   }

   if (allZero)
      return errBytesAllZero;

   lastReadMs = cTimeMs::now();

   return success;
}

//***************************************************************************
// Convert
//***************************************************************************

int Dht20::convert()
{
   mStatus = bytes[0];

   uint32_t raw {};

   raw = (bytes[1] << 8) + bytes[2];
   raw = (raw << 4) + (bytes[3] >> 4);
   humidity = raw * 9.5367431640625e-5;          // => / 1048576.0 * 100%;

   raw = ((bytes[3] & 0x0F) << 8) + bytes[4];
   raw = (raw << 8) + bytes[5];
   temperature = raw * 1.9073486328125e-4 - 50;  // => / 1048576.0 * 200 - 50;

   uint8_t crc = crc8(bytes, 6);

   if (crc != bytes[6])
   {
      tell(eloAlways, "Errror: CRC check failed");
      return errChecksum;
   }

   return success;
}

//***************************************************************************
// CRC
//***************************************************************************

uint8_t Dht20::crc8(uint8_t* ptr, uint8_t len)
{
   uint8_t crc {0xFF};

   while (len--)
   {
      crc ^= *ptr++;

      for (uint8_t i = 0; i < 8; i++)
      {
         if (crc & 0x80)
         {
            crc <<= 1;
            crc ^= 0x31;
         }
         else
         {
            crc <<= 1;
         }
      }
   }

   return crc;
}

//***************************************************************************
// Read Status
//***************************************************************************

int Dht20::readStatus(uint8_t& statusByte)
{
   usleep(1000);

   if (readByte(statusByte) != success)
   {
      tell(eloAlways, "Error: Read status failed, can't read byte");
      return fail;
   }

   return success;
}

//***************************************************************************
// Reset Sensor
//  see datasheet 7.4 Sensor Reading Process
//***************************************************************************

int Dht20::resetSensor()
{
   uint8_t statusByte {0};

   if (readStatus(statusByte) != success)
      return fail;

   if ((statusByte & 0x18) != 0x18)
   {
      if (resetRegister(0x1B) != success)
         return fail;
      if (resetRegister(0x1C) != success)
         return fail;
      if (resetRegister(0x1E) != success)
         return fail;

      usleep(10000);
   }

   return success;
}

//***************************************************************************
// Reset Register
//***************************************************************************

int Dht20::resetRegister(uint8_t reg)
{
   if (writeRegister(reg, 0x00, 0x00) != success)
   {
      tell(eloAlways, "Error: Reset register failed");
      return fail;
   }

   usleep(5000);

   uint8_t buf[3] {};

   if (::read(fd, buf, 3) != 3)
   {
      tell(eloAlways, "Error: Reset register failed");
      return fail;
   }

   usleep(10000);
   writeRegister(0xB0|reg, buf[1], buf[2]);
   usleep(5000);

   return success;
}
