//***************************************************************************
// DS2484 I2C-to-1-Wire Master Interface
// File ds2484.h
// This code is distributed under the terms and conditions of the
// GNU GENERAL PUBLIC LICENSE. See the file LICENSE for details.
// Date 2026-2026 - Jörg Wendel
//***************************************************************************

#pragma once

#include <map>
#include <vector>
#include <string>

#include "../common.h"
#include "i2c.h"

//***************************************************************************
// Class - DS2484
//***************************************************************************

class Ds2484 : public I2C
{
   public:

      // DS2484 Funktionskommandos (Function Commands)

      enum Command : uint8_t
      {
         cmdDeviceReset          = 0xF0,
         cmdWriteDeviceConfig    = 0xD2,
         cmdReadDeviceConfig     = 0xC3,
         cmdAdjust1WirePort      = 0xB4,
         cmd1WireReset           = 0xB4, // Gleicher Opcode wie Adjust, Unterscheidung über Parameter
         cmd1WireSingleBit       = 0x87,
         cmd1WireWriteByte       = 0xA5,
         cmd1WireReadByte        = 0x96,
         cmd1WireTriplet         = 0x78
      };

      // Status Register Bits
      enum StatusBit : uint8_t
      {
         status1WB  = 0x01,  // 1-Wire Busy (1 = Operation läuft)
         statusPPD  = 0x02,  // Presence Pulse Detected (1 = Device antwortet)
         statusSD   = 0x04,  // Short Detected (1 = Kurzschluss auf der Leitung)
         statusLL   = 0x08,  // Logic Level der 1-Wire Leitung
         statusRST  = 0x10,  // Device Reset Indikator
         statusSBR  = 0x20,  // Single Bit Read Ergebnis
         statusTSB  = 0x40,  // Triplet Second Bit
         statusDIR  = 0x80   // Triplet Direction Search Result
      };

      // Konfigurations-Bits (Device Configuration)
      enum ConfigBit : uint8_t
      {
         cfgAPU = 0x01,  // Active Pullup (1 = Ein, 0 = Aus)
         cfgPDN = 0x02,  // Pull-Down (1 = Ein, 0 = Aus)
         cfgSPU = 0x04,  // Strong Pullup (1 = Ein, 0 = Aus)
         cfgWSB = 0x08   // 1-Wire Speed Bit (1 = Overdrive, 0 = Standard)
      };

      struct SensorData
      {
         double value {0.0};
         std::vector<double> values;
         bool active {false};
      };

      typedef std::map<std::string, SensorData> SensorList;

      Ds2484() {};
      Ds2484(const Ds2484& source);
      Ds2484(Ds2484&& source);
      ~Ds2484() {}

      const char* chipName() override { return "DS2484"; }

      int init(const char* aDevice, uint8_t aAddress = 0x18, uint8_t aTcaAddress = 0xFF) override;

      // 1-Wire Primitiven
      int wireReset(bool& presenceDetected);
      int wireWriteByte(uint8_t byte);
      int wireReadByte(uint8_t& byte);
      int wireWriteBit(bool bit);
      int wireReadBit(bool& bit);
      int wireTriplet(uint8_t searchDirection, uint8_t& status);

      // Konfiguration & Chipsteuerung
      int deviceReset();
      int writeConfig(uint8_t config);
      int readStatus(uint8_t& status);
      int searchRom(SensorList& foundSensors);

   protected:

      int waitOnBusy(uint8_t& status, int timeoutMs = 100);
};
