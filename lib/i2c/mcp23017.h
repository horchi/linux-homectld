/*
 * Ported from Arduino MCP23017 library sources
 *   wich was published under the MIT License
 */

#pragma once

#include <stdint.h>

#include "../common.h"
#include "i2c.h"

enum class Mcp23017Port : uint8_t
{
   A = 0,
   B = 1
};

/**
 * Controls if the two interrupt pins mirror each other.
 * See "3.6 Interrupt Logic".
 */
enum class Mcp23017InterruptMode : uint8_t
{
   Separated = 0b00000000,  ///< Interrupt pins are kept independent
   Combined  = 0b01000000   ///< Interrupt pins are mirrored
};

/**
 * Registers addresses.
 * The library use addresses for IOCON.BANK = 0.
 * See "3.2.1 Byte mode and Sequential mode".
 */
enum class Mcp23017Register : uint8_t
{
   IODIR_A    = 0x00,  ///< Controls the direction of the data I/O for port A.
   IODIR_B    = 0x01,  ///< Controls the direction of the data I/O for port B.
   IPOL_A     = 0x02,  ///< Configures the polarity on the corresponding GPIO_ port bits for port A.
   IPOL_B     = 0x03,  ///< Configures the polarity on the corresponding GPIO_ port bits for port B.
   GPINTEN_A  = 0x04,  ///< Controls the interrupt-on-change for each pin of port A.
   GPINTEN_B  = 0x05,  ///< Controls the interrupt-on-change for each pin of port B.
   DEFVAL_A   = 0x06,  ///< Controls the default comparaison value for interrupt-on-change for port A.
   DEFVAL_B   = 0x07,  ///< Controls the default comparaison value for interrupt-on-change for port B.
   INTCON_A   = 0x08,  ///< Controls how the associated pin value is compared for the interrupt-on-change for port A.
   INTCON_B   = 0x09,  ///< Controls how the associated pin value is compared for the interrupt-on-change for port B.
   IOCON      = 0x0A,  ///< Controls the device.
   GPPU_A     = 0x0C,  ///< Controls the pull-up resistors for the port A pins.
   GPPU_B     = 0x0D,  ///< Controls the pull-up resistors for the port B pins.
   INTF_A     = 0x0E,  ///< Reflects the interrupt condition on the port A pins.
   INTF_B     = 0x0F,  ///< Reflects the interrupt condition on the port B pins.
   INTCAP_A   = 0x10,  ///< Captures the port A value at the time the interrupt occured.
   INTCAP_B   = 0x11,  ///< Captures the port B value at the time the interrupt occured.
   GPIO_A     = 0x12,  ///< Reflects the value on the port A.
   GPIO_B     = 0x13,  ///< Reflects the value on the port B.
   OLAT_A     = 0x14,  ///< Provides access to the port A output latches.
   OLAT_B     = 0x15,  ///< Provides access to the port B output latches.
};

inline Mcp23017Register operator+(Mcp23017Register a, Mcp23017Port b)
{
   return static_cast<Mcp23017Register>(static_cast<uint8_t>(a) + static_cast<uint8_t>(b));
};

class Mcp23017 : public I2C
{
   public:

      enum InterruptTrigger
      {
         itChange  = 1,
         itFalling = 2,
         itRising  = 3
      };

      Mcp23017() {};
      ~Mcp23017() {}

      const char* chipName() override { return "MCP"; }

      int writeRegister(Mcp23017Register reg, uint8_t value)                { return I2C::writeRegister((uint8_t)reg, value); }
      int writeRegister(Mcp23017Register reg, uint8_t byte1, uint8_t byte2) { return I2C::writeRegister((uint8_t)reg, byte1, byte2); }

      /**
       * Initializes the chip with the default configuration.
       * Enables Byte mode (IOCON.BANK = 0 and IOCON.SEQOP = 1).
       * Enables pull-up resistors for all pins. This will only be effective for input pins.
       *
       * See "3.2.1 Byte mode and Sequential mode".
       */
      int init(const char* aDevice, uint8_t aAddress);
      /**
       * Controls the pins direction on a whole port at once.
       *
       * 1 = Pin is configured as an input.
       * 0 = Pin is configured as an output.
       *
       * See "3.5.1 I/O Direction register".
       */
      void portMode(Mcp23017Port port, uint8_t directions, uint8_t pullups = 0xFF, uint8_t inverted = 0x00);
      /**
       * Controls a single pin direction.
       * Pin 0-7 for port A, 8-15 fo port B.
       *
       * 1 = Pin is configured as an input.
       * 0 = Pin is configured as an output.
       *
       * See "3.5.1 I/O Direction register".
       *
       * Beware!
       * On Arduino platform, INPUT = 0, OUTPUT = 1, which is the inverse
       * of the Mcp23017 definition where a pin is an input if its IODIR bit is set to 1.
       * This library pinMode function behaves like Arduino's standard pinMode for consistency.
       * [ OUTPUT | INPUT | INPUT_PULLUP ]
       */
      void pinMode(uint8_t pin, uint8_t mode, bool inverted = false);
      /**
       * Writes a single pin state.
       * Pin 0-7 for port A, 8-15 for port B.
       *
       * 1 = Logic-high
       * 0 = Logic-low
       *
       * See "3.5.10 Port register".
       */
      void digitalWrite(uint8_t pin, uint8_t state);
      /**
       * Reads a single pin state.
       * Pin 0-7 for port A, 8-15 for port B.
       *
       * 1 = Logic-high
       * 0 = Logic-low
       *
       * See "3.5.10 Port register".
       */
      uint8_t digitalRead(uint8_t pin);

      /**
       * Writes pins state to a whole port.
       *
       * 1 = Logic-high
       * 0 = Logic-low
       *
       * See "3.5.10 Port register".
       */
      void writePort(Mcp23017Port port, uint8_t value);
      /**
       * Writes pins state to both ports.
       *
       * 1 = Logic-high
       * 0 = Logic-low
       *
       * See "3.5.10 Port register".
       */
      void write(uint16_t value);

      /**
       * Reads pins state for a whole port.
       *
       * 1 = Logic-high
       * 0 = Logic-low
       *
       * See "3.5.10 Port register".
       */
      uint8_t readPort(Mcp23017Port port);
      /**
       * Reads pins state for both ports.
       *
       * 1 = Logic-high
       * 0 = Logic-low
       *
       * See "3.5.10 Port register".
       */
      uint16_t read();

      /**
       * Reads a single register value.
       */
      uint8_t readRegister(Mcp23017Register reg);
      /**
       * Reads the values from a register pair.
       *
       * For portA and portB variable to effectively match the desired port,
       * you have to supply a portA register address to reg. Otherwise, values
       * will be reversed due to the way the Mcp23017 works in Byte mode.
       */
      int readRegister(Mcp23017Register reg, uint8_t& portA, uint8_t& portB);

      // interrupt stuff

      /**
       * Controls how the interrupt pins act with each other.
       * If intMode is SEPARATED, interrupt conditions on a port will cause its respective INT pin to active.
       * If intMode is OR, interrupt pins are OR'ed so an interrupt on one of the port will cause both pints to active.
       *
       * Controls the IOCON.MIRROR bit.
       * See "3.5.6 Configuration register".
       */
      void interruptMode(Mcp23017InterruptMode intMode);
      /**
       * Configures interrupt registers using an Arduino-like API.
       * mode can be one of CHANGE, FALLING or RISING.
       */
      void interrupt(Mcp23017Port port, uint8_t mode);
      /**
       * Disable interrupts for the specified port.
       */
      void disableInterrupt(Mcp23017Port port);
      /**
       * Reads which pin caused the interrupt.
       */
      void interruptedBy(uint8_t& portA, uint8_t& portB);
      /**
       * Clears interrupts on both ports.
       */
      void clearInterrupts();
      /**
       * Clear interrupts on both ports. Returns port values at the time the interrupt occured.
       */
      void clearInterrupts(uint8_t& portA, uint8_t& portB);
};
