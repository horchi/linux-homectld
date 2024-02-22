//***************************************************************************
// ADS1115 Interface
// File ads1115.h
// This code is distributed under the terms and conditions of the
// GNU GENERAL PUBLIC LICENSE. See the file LICENSE for details.
// Date 2024-2024 - Jörg Wendel
//***************************************************************************

#include "../common.h"
#include "i2c.h"

#pragma once

//***************************************************************************
// Class - ADS 1115
//***************************************************************************

class Ads1115 : public I2C
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
         ai01   = 0b00000000,  // compares 0 with 1 (default)
         ai03   = 0b00010000,  // compares 0 with 3
         ai13   = 0b00100000,  // compares 1 with 3
         ai23   = 0b00110000,  // compares 2 with 3

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
      Ads1115(const Ads1115& source);
      Ads1115(Ads1115&& source);
      ~Ads1115() {}

      const char* chipName() override { return "ADS"; }

      int init(const char* aDevice, uint8_t aAddress, uint8_t aTcaAddress = 0xFF) override;
      int read(uint pin, int& milliVolt);
      void setChannel(Channel channel);

   protected:

      void delayAccToRate(DATA_RATE rate);
      int readRegister(uint8_t reg, int16_t& value);
      int writeRegister(uint8_t reg, uint16_t value);
};
