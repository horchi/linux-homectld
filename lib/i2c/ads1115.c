//***************************************************************************
// ADS1115 Interface
// File ads1115.c
// This code is distributed under the terms and conditions of the
// GNU GENERAL PUBLIC LICENSE. See the file LICENSE for details.
// Date 2024-2024 - Jörg Wendel
//***************************************************************************

#include "ads1115.h"

//***************************************************************************
// Ads1115
//***************************************************************************

Ads1115::Ads1115(const Ads1115& source)
   : I2C{source}
{
}

Ads1115::Ads1115(Ads1115&& source)
   : I2C{std::move(source)}
{
}

//***************************************************************************
// Init
//***************************************************************************

int Ads1115::init(const char* aDevice, uint8_t aAddress, uint8_t aTcaAddress)
{
   int status {I2C::init(aDevice, aAddress, aTcaAddress)};

   if (status != success)
      return status;

   uint8_t hByte = omContinuous | pga1 | ai0Gnd | osNone;                               // high byte / bit 8-15
   uint8_t lByte = compQueueDisabled | compLatOff | cpActiveLow | cmHysteresis | dr128; // low byte  / bit 0-7
   uint16_t value = (hByte << 8) + lByte;

   writeRegister(registerConfig, value);

   return success;
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
