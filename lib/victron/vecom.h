//***************************************************************************
// Home Automation Control
// File vecom.h
// This code is distributed under the terms and conditions of the
// GNU GENERAL PUBLIC LICENSE. See the file LICENSE for details.
// Date 24.02.2024 - Jörg Wendel
//***************************************************************************

#pragma once

#include "../serial.h"

//***************************************************************************
// Command Interface
//***************************************************************************

class VeService
{
   public:

      enum Command
      {
         cEnterBoot  = 0x00,  // boot loader
         cPing       = 0x01,
         cAppVersion = 0x03,
         cProductId  = 0x04,
         cRestart    = 0x06,
         cGet        = 0x07,
         cSet        = 0x08,
         cAsync      = 0x0a
      };

      enum Response
      {
         rDone    = 0x01,
         rUnknown = 0x03,
         rError   = 0x04,
         rPing    = 0x05,
         rGet     = 0x07,
         rSet     = 0x08
      };

      enum SetGetFlags
      {
         sgfSuccess        = 0x00,
         sgfUnkonwnId      = 0x01,
         sgfNotSupported   = 0x02,
         sgfParameterError = 0x04
      };

      enum RegisterMode
      {
         rmWrite,
         rmRead
      };

      enum RegisterType
      {
         rtNone,
         rtByte,
         rtWord
      };

      struct RegisterDefinition
      {
         std::string title;
         RegisterMode mode {rmRead};
         RegisterType type {rtWord};
         uint factor {0};
         std::string unit;
      };

      enum Register
      {
         regInvNvmCommand                  = 0xeb99,  // see NvmCommand below
         regRestoreDefault                 = 0x0004,

         regDeviceMode                     = 0x0200,

         regShutdownLowVoltageSet          = 0x2210,  // Low battery voltage shutdown threshold
         regAlarmLowVoltageSet             = 0x0320,  // Low battery warning threshold, also below this level the inverter will not start-up after a shutdown
         regAlarmLowVoltageClear           = 0x0321,  // Charge detect threshold after a long-term low voltage shut down. If the battery voltage
         //   exceeds this level the alarm is cleared, and the inverter will restart

         regVoltageRangeMin                = 0x2211,  // Limits for all battery user threshold settings, e.g. VE_REG_ALARM_LOW_VOLTAGE_SET.
         regVoltageRangeMax                = 0x2212,  //   Out of range input levels will be clamped to these levels

         regInvProtUbatDynCutoffEnable     = 0xebba,
         regInvProtUbatDynCutoffFactor     = 0xebb7,
         regInvProtUbatDynCutoffFactor2000 = 0xebb5,
         regInvProtUbatDynCutoffFactor250  = 0xebb3,
         regInvProtUbatDynCutoffFactor5    = 0xebb2,
         regInvProtUbatDynCutoffVoltage    = 0xebb1
      };

      enum NvmCommand       // none-volatile-memory commands
      {
         nvmNoCommand = 0,  // return value
         nvmSave      = 1,  // Save current user settings to NVM
         nvmRevert    = 2,  // Cancel modified settings. Load most recent saved user settings.
         nvmBackup    = 3,  // Undo last save. Load second last time saved settings.
                            //   After an update of the FW it is possible the backup is not
                            //   available. Then the settings are reverted instead.
         nvmDefault   = 4,  // Load the factory default values. Identical to VE_REG_RESTORE_DEFAULT 0x0004
      };

      enum DeviceMode
      {
         dmUnknown    = 0x00,
         dmInverterOn = 0x02,
         dmDeviceOn   = 0x03,   // (multi compliant)
         dmDeviceOff  = 0x04,   // VE.Direct is still enabled
         dmEcoMode    = 0x05,
         dmHibernate  = 0xfd    // VE.Direct is affected 2)
      };

      static std::map<uint8_t,RegisterDefinition> registerDefinitions;
      static const char* titleOf(uint16_t reg);
      static const char* unitOf(uint16_t reg);
      static std::string toPretty(uint16_t reg, double value);

      static std::map<DeviceMode,std::string> deviceModes;
      static DeviceMode toMode(const char* str);
};

//***************************************************************************
// VE Com
//***************************************************************************

class VeCom : public Serial, public VeService
{
   public:

      VeCom(int aBaud = B19200) : Serial(aBaud) {};
      virtual ~VeCom() {};

      int sendPing();
      int readRegister(uint16_t reg, double& value);
      int writeRegister(uint16_t reg, double& value);
      int writeRegister(uint16_t reg, uint8_t& value);

   private:

      int sendCommand(uint8_t command, uint8_t data[], size_t size = 0);
      int readChar(unsigned char& c, const char* expected = nullptr);
      int readCharAsByte(byte& b);
      int read2CharsAsByte(byte& b);
      int readWord(uint16_t& w);

      int onComError(int result);
      void veFlush();
};
