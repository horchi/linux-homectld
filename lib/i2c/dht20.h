//***************************************************************************
// i2c Interface
// File dht20.h
// This code is distributed under the terms and conditions of the
// GNU GENERAL PUBLIC LICENSE. See the file LICENSE for details.
// Date 2024-2024 - Jörg Wendel
//***************************************************************************

#pragma once

#include <stdint.h>

#include "i2c.h"

//***************************************************************************
// Class - Dht20
//***************************************************************************

class Dht20 : public I2C
{
   public:

      enum Errors
      {
         errChecksum = -99,
         errBytesAllZero,
         errReadTimeout,
         errLastRead
      };

      Dht20() {};
      Dht20(const Dht20& source);
      Dht20(Dht20&& source);
      ~Dht20() {};

      const char* chipName() override { return "DHT"; }
      int init(const char* aDevice, uint8_t aAddress, uint8_t aTcaAddress = 0xFF) override;

      int read();
      int getHumidity()    { return (int)humidity + humOffset; }
      float getTemperature() { return temperature + tempOffset; };

      void setHumOffset(float offset = 0)  { humOffset = offset; };
      void setTempOffset(float offset = 0) { tempOffset = offset; };
      float getHumOffset()                 { return humOffset; };
      float getTempOffset()                { return tempOffset; };

      int messurementStatus()     { return mStatus; }
      uint64_t getLastReadMs()    { return lastReadMs; }
      uint64_t getLastRequestMs() { return lastRequestMs; }

   private:

      int convert();
      int readData();
      int readStatus(uint8_t& statusByte);
      int resetSensor();

      bool isMeasuring();
      // bool isCalibrated()      { return (readStatus() & 0x08) == 0x08; }
      // bool isIdle()            { return (readStatus() & 0x80) == 0x00; }

      float humidity {0};
      float temperature {0};
      float humOffset {0};
      float tempOffset {0};

      uint8_t mStatus {success};
      uint64_t lastRequestMs {0};
      uint64_t lastReadMs {0};
      uint8_t bytes[7] {0};

      uint8_t crc8(uint8_t* ptr, uint8_t len);
      int resetRegister(uint8_t reg);
};
