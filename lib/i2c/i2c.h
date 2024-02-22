//***************************************************************************
// i2c Interface
// File i2c.h
// This code is distributed under the terms and conditions of the
// GNU GENERAL PUBLIC LICENSE. See the file LICENSE for details.
// Date 2024-2024 - Jörg Wendel
//***************************************************************************
/*
 https://cdn-shop.adafruit.com/datasheets/mcp23017.pdf
 https://cdn-shop.adafruit.com/datasheets/ads1115.pdf
 https://wolles-elektronikkiste.de/tca9548a-i2c-multiplexer
 https://www.berrybase.de/media/pdf/fe/80/ab/produkt_downloads-Datenblatt_DatasheetMiNbUDgDdHRvM.pdf
*/

#pragma once

#include <stdint.h>

#include "../common.h"

class Tca9548;

//***************************************************************************
// Class - I2C
//***************************************************************************

class I2C
{
   public:

      I2C() {};
      I2C(const I2C& source);
      I2C(I2C&& source);
      virtual ~I2C();

      virtual const char* chipName() = 0;

      virtual int init(const char* aDevice, uint8_t aAddress, uint8_t aTcaAddress = 0xFF);
      virtual int exit();

      uint8_t getAddress() const;
      uint8_t getSensorAddress()  const { return address; }
      uint8_t getTcaChannel()     const { return tcaChannel; }
      void setTcaChannel(uint8_t c)     { tcaChannel = c; }

      int getFd() const { return fd; };

      virtual int switchTcaChannel();

      virtual int readRegister(uint8_t reg, uint8_t& byte1, uint8_t& byte2);
      virtual int readRegister(uint8_t reg, uint16_t& word);
      virtual int writeRegister(uint8_t reg, uint8_t value);
      virtual int writeRegister(uint8_t reg, uint8_t byte1, uint8_t byte2);

      static int scan(const char* device);

   protected:

      virtual int readByte(uint8_t& byte);
      virtual int writeByte(uint8_t byte);

      static int writeByte(int fd, uint8_t byte);

      Tca9548* tca {};
      int tcaChannel {0xff};
      std::string device;
      uint8_t address {};
      int fd {na};
};

//***************************************************************************
// Class - TCA I2C Bus Multipexer
//***************************************************************************

class Tca9548 : public I2C
{
   public:

      Tca9548() {}
      Tca9548(const Tca9548& source) : I2C{source} {};
      Tca9548(Tca9548&& source) : I2C{std::move(source)} {};

      const char* chipName() override { return "TCA"; }
      virtual int setChannel(uint8_t channel);
};
