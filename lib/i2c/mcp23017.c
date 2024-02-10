/*
 * Ported from Arduino MCP23017 library sources
 *   wich was published under the MIT License
 */

#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

#include <fcntl.h>
#include <unistd.h>

#ifndef _NO_RASPBERRY_PI_
#  include <wiringPi.h>
#endif

#include "../../gpio.h"
#include "mcp23017.h"

//***************************************************************************
// Init
//***************************************************************************

int Mcp23017::init(const char* aDevice, uint8_t aAddress)
{
   int status {I2C::init(aDevice, aAddress)};

   if (status != success)
      return status;

   // BANK   = 0 - sequential register addresses
   // MIRROR = 0 - use configureInterrupt
   // SEQOP  = 1 - sequential operation disabled, address pointer does not increment
   // DISSLW = 0 - slew rate enabled
   // HAEN   = 0 - hardware address pin is always enabled on 23017
   // ODR    = 0 - open drain output
   // INTPOL = 0 - interrupt active low

   if (writeRegister(Mcp23017Register::IOCON, 0b00100000) != success)
   {
      tell(eloAlways, "Error: Failed to write 'IOCON' register");
      return fail;
   }

   // enable all pull up resistors (will be effective for input pins only)

   if (writeRegister(Mcp23017Register::GPPU_A, 0xFF, 0xFF) != success)
   {
      tell(eloAlways, "Error: Failed to write 'GPPU_A' register, %s", strerror(errno));
      return fail;
   }

   return success;
}

void Mcp23017::portMode(Mcp23017Port port, uint8_t directions, uint8_t pullups, uint8_t inverted)
{
   writeRegister(Mcp23017Register::IODIR_A + port, directions);
   writeRegister(Mcp23017Register::GPPU_A + port, pullups);
   writeRegister(Mcp23017Register::IPOL_A + port, inverted);
}

void Mcp23017::pinMode(uint8_t pin, uint8_t mode, bool inverted)
{
   Mcp23017Register iodirreg {Mcp23017Register::IODIR_A};
   Mcp23017Register pullupreg {Mcp23017Register::GPPU_A};
   Mcp23017Register polreg {Mcp23017Register::IPOL_A};

   if (pin > 7)
   {
      iodirreg = Mcp23017Register::IODIR_B;
      pullupreg = Mcp23017Register::GPPU_B;
      polreg = Mcp23017Register::IPOL_B;
      pin -= 8;
   }

   uint8_t iodir = readRegister(iodirreg);

   if (mode == INPUT || mode == INPUT_PULLUP)
      bitSet(iodir, pin);
   else
      bitClear(iodir, pin);

   uint8_t pull = readRegister(pullupreg);

   if (mode == INPUT_PULLUP)
      bitSet(pull, pin);
   else
      bitClear(pull, pin);

   uint8_t pol = readRegister(polreg);

   if (inverted)
      bitSet(pol, pin);
   else
      bitClear(pol, pin);

   writeRegister(iodirreg, iodir);
   writeRegister(pullupreg, pull);
   writeRegister(polreg, pol);
}

void Mcp23017::digitalWrite(uint8_t pin, uint8_t state)
{
   Mcp23017Register gpioreg {Mcp23017Register::GPIO_A};

   if (pin > 7)
   {
      gpioreg = Mcp23017Register::GPIO_B;
      pin -= 8;
   }

   uint8_t gpio = readRegister(gpioreg);

   if (state == HIGH)
      bitSet(gpio, pin);
   else
      bitClear(gpio, pin);

   writeRegister(gpioreg, gpio);
}

uint8_t Mcp23017::digitalRead(uint8_t pin)
{
   Mcp23017Register gpioreg {Mcp23017Register::GPIO_A};

   if (pin > 7)
   {
      gpioreg = Mcp23017Register::GPIO_B;
      pin -=8;
   }

   uint8_t gpio = readRegister(gpioreg);

   return bitRead(gpio, pin) ?  HIGH : LOW;
}

void Mcp23017::writePort(Mcp23017Port port, uint8_t value)
{
   writeRegister(Mcp23017Register::GPIO_A + port, value);
}

void Mcp23017::write(uint16_t value)
{
   writeRegister(Mcp23017Register::GPIO_A, lowByte(value), highByte(value));
}

uint8_t Mcp23017::readPort(Mcp23017Port port)
{
   return readRegister(Mcp23017Register::GPIO_A + port);
}

uint16_t Mcp23017::read()
{
   uint8_t a = readPort(Mcp23017Port::A);
   uint8_t b = readPort(Mcp23017Port::B);

   return a | b << 8;
}

uint8_t Mcp23017::readRegister(Mcp23017Register reg)
{
   ::write(fd, &reg, 1);

   uint8_t byte {};

   if (::read(fd, &byte, 1) != 1)
   {
      tell(eloAlways, "Error: MCP Communication failed");
      return 0;  // return fail :o
   }

   return byte;
}

int Mcp23017::readRegister(Mcp23017Register reg, uint8_t& portA, uint8_t& portB)
{
   ::write(fd, &reg, 1);

   uint8_t buf[2] {};

   if (::read(fd, buf, 2) != 2)
   {
      tell(eloAlways, "Error: MCP Communication failed");
      return fail;
   }

   portA = buf[0];
   portB = buf[1];

   return success;
}

void Mcp23017::interruptMode(Mcp23017InterruptMode intMode)
{
   uint8_t iocon = readRegister(Mcp23017Register::IOCON);

   if (intMode == Mcp23017InterruptMode::Combined)
      iocon |= static_cast<uint8_t>(Mcp23017InterruptMode::Combined);
   else
      iocon &= ~(static_cast<uint8_t>(Mcp23017InterruptMode::Combined));

   writeRegister(Mcp23017Register::IOCON, iocon);
}

void Mcp23017::interrupt(Mcp23017Port port, uint8_t mode)
{
   Mcp23017Register defvalreg = Mcp23017Register::DEFVAL_A + port;
   Mcp23017Register intconreg = Mcp23017Register::INTCON_A + port;

   // enable interrupt for port

   writeRegister(Mcp23017Register::GPINTEN_A + port, 0xFF);

   switch(mode)
   {
      case itChange:  // interrupt on change
         writeRegister(intconreg, 0);
         break;
      case itFalling: // interrupt falling, compared against defval, 0xff
         writeRegister(intconreg, 0xFF);
         writeRegister(defvalreg, 0xFF);
         break;
      case itRising:  // interrupt rising, compared against defval, 0x00
         writeRegister(intconreg, 0xFF);
         writeRegister(defvalreg, 0x00);
         break;
   }
}

void Mcp23017::interruptedBy(uint8_t& portA, uint8_t& portB)
{
   readRegister(Mcp23017Register::INTF_A, portA, portB);
}

void Mcp23017::disableInterrupt(Mcp23017Port port)
{
   writeRegister(Mcp23017Register::GPINTEN_A + port, 0x00);
}

void Mcp23017::clearInterrupts()
{
   uint8_t a {}, b {};
   clearInterrupts(a, b);
}

void Mcp23017::clearInterrupts(uint8_t& portA, uint8_t& portB)
{
   readRegister(Mcp23017Register::INTCAP_A, portA, portB);
}
