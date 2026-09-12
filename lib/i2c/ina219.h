//***************************************************************************
// INA219 Interface
// File ina219.h
// This code is distributed under the terms and conditions of the
// GNU GENERAL PUBLIC LICENSE. See the file LICENSE for details.
// Date 2024-2026 - Jörg Wendel
//***************************************************************************
/*
 https://www.ti.com/lit/ds/symlink/ina219.pdf
*/

#pragma once

#include <stdint.h>

#include "../common.h"
#include "i2c.h"

//***************************************************************************
// Class - INA 219 (bidirectional current/power monitor)
//***************************************************************************

class Ina219 : public I2C
{
   public:

      enum Register
      {
         registerConfig       = 0x00,   // configuration       (r/w)
         registerShuntVoltage = 0x01,   // shunt voltage       (r)
         registerBusVoltage   = 0x02,   // bus voltage         (r)
         registerPower        = 0x03,   // power               (r)
         registerCurrent      = 0x04,   // current             (r)
         registerCalibration  = 0x05    // calibration         (r/w)
      };

      enum Mode       // bit 0-2
      {
         mPowerDown          = 0b000,
         mShuntTriggered     = 0b001,
         mBusTriggered       = 0b010,
         mShuntBusTriggered  = 0b011,
         mAdcOff             = 0b100,
         mShuntContinuous    = 0b101,
         mBusContinuous      = 0b110,
         mShuntBusContinuous = 0b111    // default
      };

      enum Adc        // resolution/averaging; SADC bit 3-6, BADC bit 7-10
      {
         adc9Bit       = 0b0000,   //    84 us
         adc10Bit      = 0b0001,   //   148 us
         adc11Bit      = 0b0010,   //   276 us
         adc12Bit      = 0b0011,   //   532 us  (chip default)
         adcSamples2   = 0b1001,   //  1.06 ms
         adcSamples4   = 0b1010,   //  2.13 ms
         adcSamples8   = 0b1011,   //  4.26 ms
         adcSamples16  = 0b1100,   //  8.51 ms
         adcSamples32  = 0b1101,   // 17.02 ms
         adcSamples64  = 0b1110,   // 34.05 ms
         adcSamples128 = 0b1111    // 68.10 ms (our default, best noise suppression)
      };

      enum Gain       // PG bit 11-12, shunt voltage full scale range
      {
         gainDiv1  = 0b00,   // ±40 mV
         gainDiv2  = 0b01,   // ±80 mV
         gainDiv4  = 0b10,   // ±160 mV
         gainDiv8  = 0b11    // ±320 mV (default)
      };

      enum BusRange   // BRNG bit 13
      {
         brng16V = 0,
         brng32V = 1         // default
      };

      Ina219() {};
      Ina219(const Ina219& source);
      Ina219(Ina219&& source);
      ~Ina219() {};

      const char* chipName() override { return "INA"; }

      int init(const char* aDevice, uint8_t aAddress, uint8_t aTcaAddress = 0xFF) override;
      int reset();
      int configure(BusRange range, Gain aGain, Adc busAdc, Adc shuntAdc, Mode mode);

      // the shunt resistor, most breakout boards are equipped with 0.1 Ohm

      void setShunt(double ohms)       { if (ohms > 0.0) shuntOhms = ohms; }
      double getShunt() const          { return shuntOhms; }

      int read();

      double getBusVoltage() const     { return busVoltage; }     // [V]
      double getShuntVoltage() const   { return shuntVoltage; }   // [mV]
      double getVoltage() const        { return busVoltage + shuntVoltage/1000.0; }  // [V] at the load side
      double getCurrent() const        { return current; }        // [A]
      double getPower() const          { return power; }          // [W]

   protected:

      int readWord(uint8_t reg, uint16_t& value);
      int writeWord(uint8_t reg, uint16_t value);

      double shuntOhms {0.1};
      Gain gain {gainDiv8};

      double busVoltage {0.0};     // [V]
      double shuntVoltage {0.0};   // [mV]
      double current {0.0};        // [A]
      double power {0.0};          // [W]
};
