//***************************************************************************
// i2c Interface
// File i2c.h
// This code is distributed under the terms and conditions of the
// GNU GENERAL PUBLIC LICENSE. See the file LICENSE for details.
// Date 2024-2024 - Jörg Wendel
//***************************************************************************

#pragma once

#include <stdint.h>

#include "../common.h"

//***************************************************************************
// Class - I2C
//***************************************************************************

class I2C
{
   public:

      virtual ~I2C() { exit(); }

      virtual const char* chipName() = 0;

      virtual int init(const char* aDevice, uint8_t aAddress);
      virtual int exit();

      virtual int readRegister(uint8_t reg, uint8_t& byte1, uint8_t& byte2);
      virtual int readRegister(uint8_t reg, uint16_t& word);
      virtual int writeRegister(uint8_t reg, uint8_t value);
      virtual int writeRegister(uint8_t reg, uint8_t byte1, uint8_t byte2);

   protected:

      virtual int readByte(uint8_t& byte);
      virtual int writeByte(uint8_t byte);

      std::string device;
      uint8_t address {};
      int fd {na};
};
