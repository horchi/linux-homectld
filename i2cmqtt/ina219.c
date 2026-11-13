//***************************************************************************
// INA219 Interface
// File ina219.c
// This code is distributed under the terms and conditions of the
// GNU GENERAL PUBLIC LICENSE. See the file LICENSE for details.
// Date 2024-2026 - Jörg Wendel
//***************************************************************************

#include <unistd.h>

#include "ina219.h"

//***************************************************************************
// Copy / Move Konstruktor
//***************************************************************************

Ina219::Ina219(const Ina219& source)
   : I2C{source}
{
   shuntOhms = source.shuntOhms;
   gain = source.gain;
   busVoltage = source.busVoltage;
   shuntVoltage = source.shuntVoltage;
   current = source.current;
   power = source.power;
}

Ina219::Ina219(Ina219&& source)
   : I2C{std::move(source)}
{
   shuntOhms = source.shuntOhms;
   gain = source.gain;
   busVoltage = source.busVoltage;
   shuntVoltage = source.shuntVoltage;
   current = source.current;
   power = source.power;
}

//***************************************************************************
// Init
//***************************************************************************

int Ina219::init(const char* aDevice, uint8_t aAddress, uint8_t aTcaAddress)
{
   int status {I2C::init(aDevice, aAddress, aTcaAddress)};

   if (status != success)
      return status;

   if (reset() != success)
      return fail;

   // 32V bus range, full shunt range (±320mV) and 128 times averaging on both
   //   ADCs, this results in a conversion time of ~68ms per channel

   if (configure(brng32V, gainDiv8, adcSamples128, adcSamples128, mShuntBusContinuous) != success)
      return fail;

   tell(eloDetail, "Info: INA219 at 0x%02x initialized, shunt is %.4f Ohm", address, shuntOhms);

   return success;
}

//***************************************************************************
// Reset
//***************************************************************************

int Ina219::reset()
{
   switchTcaChannel();

   if (writeWord(registerConfig, 0x8000) != success)    // bit 15 triggers the reset
      return fail;

   usleep(10000);

   return success;
}

//***************************************************************************
// Configure
//***************************************************************************

int Ina219::configure(BusRange range, Gain aGain, Adc busAdc, Adc shuntAdc, Mode mode)
{
   gain = aGain;

   uint16_t value = (range << 13) | (gain << 11) | (busAdc << 7) | (shuntAdc << 3) | mode;

   switchTcaChannel();

   if (writeWord(registerConfig, value) != success)
      return fail;

   // wait for the first conversion of both channels to be finished

   usleep(150000);

   return success;
}

//***************************************************************************
// Read
//***************************************************************************
// current and power are calculated in software from the measured shunt and
//   bus voltage, therefore the calibration register isn't needed at all and
//   the rounding error of the chip internal current LSB is avoided
//***************************************************************************

int Ina219::read()
{
   uint16_t raw {0};

   switchTcaChannel();

   if (readWord(registerShuntVoltage, raw) != success)
      return fail;

   shuntVoltage = (int16_t)raw * 0.01;         // LSB is 10uV -> [mV]

   if (readWord(registerBusVoltage, raw) != success)
      return fail;

   if (raw & 0x0001)                           // OVF - math overflow
      tell(eloDetail, "Info: INA219 at 0x%02x reports a math overflow", address);

   busVoltage = (raw >> 3) * 0.004;            // LSB is 4mV -> [V]

   current = (shuntVoltage / 1000.0) / shuntOhms;
   power = busVoltage * current;

   tell(eloDebug, "Debug: INA219 (0x%02x) bus %.3f V, shunt %.3f mV, %.3f A, %.3f W",
        address, busVoltage, shuntVoltage, current, power);

   return success;
}

//***************************************************************************
// Read / Write Word - the INA219 registers are MSB first
//***************************************************************************

int Ina219::readWord(uint8_t reg, uint16_t& value)
{
   uint8_t hByte {0};
   uint8_t lByte {0};

   if (readRegister(reg, hByte, lByte) != success)
      return fail;

   value = (hByte << 8) | lByte;

   return success;
}

int Ina219::writeWord(uint8_t reg, uint16_t value)
{
   return writeRegister(reg, value >> 8, value & 0xFF);
}
