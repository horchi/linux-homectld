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
// I2C - Copy Konstruktor
//***************************************************************************

I2C::I2C(const I2C& source)
{
   if (source.tca)
      tca = new Tca9548(*source.tca);
   tcaChannel = source.tcaChannel;
   device = source.device;
   address = source.address;
   fd = source.fd;
}

//***************************************************************************
// I2C - Move Konstruktor
//***************************************************************************

I2C::I2C(I2C&& source)
{
   tca = source.tca;
   source.tca = nullptr;
   tcaChannel = source.tcaChannel;
   device = source.device;
   address = source.address;
   fd = source.fd;
   source.fd = na;
}

I2C::~I2C()
{
   delete tca;
}

//***************************************************************************
// Init
//***************************************************************************

int I2C::init(const char* aDevice, uint8_t aAddress, uint8_t aTcaAddress)
{
   device = aDevice;
   address = aAddress;

   if (aTcaAddress != 0xFF)
   {
      tca = new Tca9548;
      tca->init(device.c_str(), aTcaAddress);
   }

   switchTcaChannel();

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

   if (getTcaChannel() != 0xff)
      tell(eloAlways, "Opened i2c '%s' device '%s' with address 0x%02x/%d", chipName(), device.c_str(), address, getTcaChannel());
   else
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
      tell(eloAlways, "Closing '0x%02x' device", address);
      ::close(fd);
      fd = na;
   }

   return done;
}

//***************************************************************************
// Get Address
//***************************************************************************

uint8_t I2C::getAddress() const
{
   if (tca)
      return tca->getAddress();

   return address;
}

//***************************************************************************
// Set TCA Channel
//***************************************************************************

int I2C::switchTcaChannel()
{
   if (tca && tcaChannel != 0xff)
   {
      tell(eloDebug, "Debug: Switching TCA channel to 0x%02x", tcaChannel);
      tca->setChannel(tcaChannel);
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

int I2C::writeByte(int fd, uint8_t byte)
{
   if (::write(fd, &byte, 1) != 1)
      return fail;

   return success;
}

//***************************************************************************
// Write Register (1 byte)
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

//***************************************************************************
// Write Register (2 bytes)
//***************************************************************************

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

//***************************************************************************
// Scan
//***************************************************************************

int I2C::scan(const char* device)
{
   int fd {na};

   if ((fd = ::open(device, O_RDWR)) < 0)
   {
      tell(eloAlways, "Error: Couldn't open i2c device '%s'", device);
      return fail;
   }

   tell(eloAlways, "start scan ..");

   for (uint addr = 1; addr < 128; ++addr)
   {
      if (ioctl(fd, I2C_SLAVE, addr) < 0)        // connect to i2c slave
      {
         tell(eloAlways, "Error: Couldn't find device on address 0x%02x", addr);
         continue;
      }

      if (writeByte(fd, addr) == success)
         tell(eloAlways, "0x%02x", addr);
   }

   tell(eloAlways, ".. done");

   return done;
}

//***************************************************************************
// TCA Multipexer
//***************************************************************************

int Tca9548::setChannel(uint8_t channel)
{
   if (writeByte(1 << channel) != success)
      return fail;

   return done;
}
