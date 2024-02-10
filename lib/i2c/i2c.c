//***************************************************************************
// i2c Interface
// File i2c.c
// This code is distributed under the terms and conditions of the
// GNU GENERAL PUBLIC LICENSE. See the file LICENSE for details.
// Date 2024-2024 - Jörg Wendel
//***************************************************************************

#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>

#include "i2c.h"

//***************************************************************************
// Init
//***************************************************************************

int I2C::init(const char* aDevice, uint8_t aAddress)
{
   device = aDevice;
   address = aAddress;

   if ((fd = ::open(device.c_str(), O_RDWR)) < 0)
   {
      tell(eloAlways, "Error: Couldn't open i2c device '%s'", device.c_str());
      return fail;
   }

   // connect to i2c slave

   if (ioctl(fd, I2C_SLAVE, address) < 0)
   {
      tell(eloAlways, "Error: Couldn't find device on address 0x%02x", address);
      return fail;
   }

   tell(eloAlways, "Opened i2c '%s' device '%s' with address 0x%02x", chipName(), device.c_str(), address);

   return success;
}

//***************************************************************************
// Exit
//***************************************************************************

int I2C::exit()
{
   if (fd >= 0)
   {
      tell(eloAlways, "Closing device");
      ::close(fd);
      fd = na;
   }

   return done;
}

//***************************************************************************
// Read Byte
//***************************************************************************

int I2C::readByte(uint8_t& byte)
{
   if (::read(fd, &byte, 1) != 1)
   {
      tell(eloAlways, "Error: '%s' communication failed, result was '%s' (%d)",
           chipName(), strerror(errno), fd);
      return fail;
   }

   tell(eloDebug, "Debug: i2c (0x%02x) <- 0x%02x (byte)", address, byte);

   return success;
}

//***************************************************************************
// Read Register
//***************************************************************************

int I2C::readRegister(uint8_t reg, uint8_t& byte1, uint8_t& byte2)
{
   writeByte(reg);

   uint8_t buf[2] {};

   if (::read(fd, buf, 2) != 2)
   {
      tell(eloAlways, "Error: '%s' communication failed at reading 2 bytes", chipName());
      return fail;
   }

   tell(eloDebug, "Debug: i2c (0x%02x) <- 0x%02x (byte)", address, buf[0]);
   tell(eloDebug, "Debug: i2c (0x%02x) <- 0x%02x (byte)", address, buf[1]);

   byte1 = buf[0];
   byte2 = buf[1];

   return success;
}

int I2C::readRegister(uint8_t reg, uint16_t& word)
{
   uint8_t buf[2] {};

   writeByte(reg);

   if (::read(fd, buf, 2) != 2)
   {
      tell(eloAlways, "Error: '%s' communication failed at reading word", chipName());
      return fail;
   }

   tell(eloDebug, "Debug: i2c (0x%02x) <- 0x%02x (byte)", address, buf[0]);
   tell(eloDebug, "Debug: i2c (0x%02x) <- 0x%02x (byte)", address, buf[1]);

   word = (buf[1] << 8) + buf[0];

   return success;
}

//***************************************************************************
// Write Byte
//***************************************************************************

int I2C::writeByte(uint8_t byte)
{
   tell(eloDebug, "Debug: i2c (0x%02x) -> 0x%02x (byte)", address, byte);

   if (::write(fd, &byte, 1) != 1)
   {
      tell(eloAlways, "Error: '%s' failed to write byte 0x%02x, result was '%s' (%d)",
           chipName(), byte, strerror(errno), fd);
      return fail;
   }

   return success;
}

//***************************************************************************
// Write Register
//***************************************************************************

int I2C::writeRegister(uint8_t reg, uint8_t value)
{
   int status {};
   uint8_t buf[2] {reg, value};

   if (::write(fd, buf, 2) != 2)
   {
      tell(eloAlways, "Error: '%s' failed to write register 0x%02x, byte [0x%02x], result was '%s' (%d)",
           chipName(), reg, value, strerror(errno), fd);
      return fail;
   }

   return status;
}

int I2C::writeRegister(uint8_t reg, uint8_t byte1, uint8_t byte2)
{
   int status {};
   uint8_t buf[3] {reg, byte1, byte2};

   if (::write(fd, buf, 3) != 3)
   {
      tell(eloAlways, "Error: '%s' failed to write register 0x%02x, bytes [0x%02x,0x%02x], result was '%s' (%d)",
           chipName(), reg, byte1, byte2, strerror(errno), fd);
      return fail;
   }

   return status;
}
