//***************************************************************************
// Home Automation Control
// File vecom.c
// This code is distributed under the terms and conditions of the
// GNU GENERAL PUBLIC LICENSE. See the file LICENSE for details.
// Date 24.02.2024 - Jörg Wendel
//***************************************************************************

#include "vecom.h"

//***************************************************************************
// ctob
//***************************************************************************

uint8_t ctob(char c)
{
   if (c <= '9')
      return c - '0';

   return c - 'A' + 0x0a;
}

//***************************************************************************
// VE Service
//***************************************************************************

std::map<uint8_t,VeService::RegisterDefinition> VeService::registerDefinitions
{
   { regInvNvmCommand,                   { "Invergter NVM Command",                rmWrite, rtByte,    1,  "" } },
   { regRestoreDefault,                  { "Restore NVM Default",                  rmWrite, rtNone,    1,  "" } },

   { regDeviceMode,                      { "Device Mode",                          rmWrite, rtByte,    1,  "" } },

   { regShutdownLowVoltageSet,           { "Shutdown Low Voltage Set",             rmWrite, rtWord,  100, "V" } },
   { regAlarmLowVoltageSet,              { "Alarm Low Voltage Set",                rmWrite, rtWord,  100, "V" } },
   { regAlarmLowVoltageClear,            { "Alarm Low Voltage Clear",              rmWrite, rtWord,  100, "V" } },
   { regVoltageRangeMin,                 { "Voltage Range Min",                    rmRead,  rtWord,  100, "V" } },
   { regVoltageRangeMax,                 { "Voltage Range Max",                    rmRead,  rtWord,  100, "V" } },
   { regInvProtUbatDynCutoffEnable,      { "Inv Prot Ubat Dyn Cutoff Enable",      rmWrite, rtByte,    1,  "" } },
   { regInvProtUbatDynCutoffFactor,      { "Inv Prot Ubat Dyn Cutoff Factor",      rmWrite, rtWord,    1,  "" } },
   { regInvProtUbatDynCutoffFactor2000,  { "Inv Prot Ubat Dyn Cutoff Factor 2000", rmWrite, rtWord,    1,  "" } },
   { regInvProtUbatDynCutoffFactor250,   { "Inv Prot Ubat Dyn Cutoff Factor 250",  rmWrite, rtWord,    1,  "" } },
   { regInvProtUbatDynCutoffFactor5,     { "Inv Prot Ubat Dyn Cutoff Factor 5",    rmWrite, rtWord,    1,  "" } },
   { regInvProtUbatDynCutoffVoltage,     { "Inv Prot Ubat Dyn Cutoff Voltage",     rmRead,  rtWord, 1000, "V" } }
};

//***************************************************************************
// VE Service
//***************************************************************************

const char* VeService::titleOf(uint16_t reg)
{
   if (registerDefinitions.find(reg) == registerDefinitions.end())
      return "<unknown>";

   return registerDefinitions[reg].title.c_str();
}

const char* VeService::unitOf(uint16_t reg)
{
   if (registerDefinitions.find(reg) == registerDefinitions.end())
      return "<unknown>";

   return registerDefinitions[reg].unit.c_str();
}

std::string VeService::toPretty(uint16_t reg, double value)
{
   if (registerDefinitions.find(reg) == registerDefinitions.end())
      return "<unknown>";

   const auto& def = registerDefinitions[reg];

   return def.title + "\t" + horchi::to_string(value) + " " + def.unit;
}

std::map<VeService::DeviceMode,std::string> VeService::deviceModes
{
   { dmUnknown,    "Unknown" },
   { dmInverterOn, "Inverter On" },
   { dmDeviceOn,   "Device On" },
   { dmDeviceOff,  "Device Off" },
   { dmEcoMode ,   "Eco" },
   { dmHibernate,  "Hibernate" }
};

VeService::DeviceMode VeService::toMode(const char* str)
{
   for (const auto& m : deviceModes)
   {
      if (m.second == str)
         return m.first;
   }

   return dmUnknown;
}

//***************************************************************************
// Read Char
//***************************************************************************

int VeCom::readChar(unsigned char& c, const char* expected)
{
   c = ' ';

   if (readByte(c) != success)
      return fail;

   if (expected && c != expected[0])
   {
      tell(eloAlways, "Error: Got unexpected response '%c' instead of '%c'", c, expected[0]);
      return fail;
   }

   return success;
}

//***************************************************************************
// Read Char As Byte
//***************************************************************************

int VeCom::readCharAsByte(byte& b)
{
   b = 0;

   if (readByte(b) != success)
      return fail;

   b = ctob(b);

   return success;
}

//***************************************************************************
// Read 2 Chars As Byte
//***************************************************************************

int VeCom::read2CharsAsByte(byte& b)
{
   unsigned char lChar {' '};
   unsigned char hChar {' '};

   if (readByte(hChar) != success || readByte(lChar) != success)
      return fail;

   // printf(":: '%c%c'\n", hChar, lChar);
   b = (ctob(hChar) << 4) + ctob(lChar);

   return success;
}

//***************************************************************************
// Read Word
//***************************************************************************

int VeCom::readWord(uint16_t& w)
{
   byte lByte {0};
   byte hByte {0};

   w = 0;

   if (read2CharsAsByte(lByte) != success || read2CharsAsByte(hByte) != success)
      return fail;

   w = (hByte << 8) + lByte;

   return success;
}

//***************************************************************************
// Send Command
//***************************************************************************

int VeCom::sendCommand(uint8_t command, uint8_t data[], size_t size)
{
   char frame[100] {};
   uint8_t chkSum {0x55};
   byte c {' '};

   veFlush();

   // prepare / write frame

   sprintf(eos(frame), ":%01X", command);
   chkSum -= command;

   for (size_t i = 0; i < size; ++i)
   {
      sprintf(eos(frame), "%02X", data[i]);
      chkSum -= data[i];
   }

   sprintf(eos(frame), "%02X\n", chkSum);

   if (write(frame, strlen(frame)) != success)
   {
      tell(eloAlways, "Error: Writing command failed");
      return fail;
   }

   // get response ...

   // frame start

   if (readChar(c, ":") != success)
      return fail;

   // response code

   if (readCharAsByte(c) != success)
   {
      tell(eloAlways, "Error: Missing response");
      return fail;
   }

   // #TODO check response code ...
   //   check if error
   //   check if matches with request

   return done;
}

//***************************************************************************
// Send Ping
//***************************************************************************

int VeCom::sendPing()
{
   if (sendCommand(cPing, nullptr) != success)
      return onComError(fail);

   uint16_t w {0};

   if (readWord(w) != success)
      return onComError(fail);

   // version like 4124 => 1.24

   tell(eloAlways, "VICTRON FW version %01x.%02x", (w & 0x0f00) >> 8, (w & 0x00ff));

   uint8_t c {0};

   if (read2CharsAsByte(c) != success || readChar(c, "\n") != success)
      return onComError(fail);

   return done;
}

//***************************************************************************
// Read Register
//***************************************************************************

int VeCom::readRegister(uint16_t reg, double& value)
{
   if (registerDefinitions.find(reg) == registerDefinitions.end())
   {
      tell(eloAlways, "Error: Ignoring read request for unexpected register '0x%02x", reg);
      return onComError(fail);
   }

   value = 0.0;
   const auto& def = registerDefinitions[reg];

   uint8_t data[3] {};
   data[0] = reg & 0x00ff;
   data[1] = reg >> 8;
   data[2] = 0x00;

   if (sendCommand(cGet, data, 3) != success)
   {
      tell(eloAlways, "Error: Sending command failed");
      return onComError(fail);
   }

   uint16_t w {0};
   uint8_t b {0};
   uint8_t flags {0};

   readWord(w);               // read echo of address
   read2CharsAsByte(flags);   // flags

   if (flags != sgfSuccess)
      tell(eloAlways, "Read failed, flags are %d", flags);

   if (def.type == rtByte)
   {
      read2CharsAsByte(b);    // read byte value
      value = b / (double)def.factor;
   }
   else if (def.type == rtWord)
   {
      readWord(w);            // read word value
      value = w / (double)def.factor;
   }

   uint8_t c {0};

   if (read2CharsAsByte(c) != success || readChar(c, "\n") != success)
      return onComError(fail);

   // tell(eloAlways, "%s: %.2f %s", def.title.c_str(), value, def.unit.c_str());

   return success;
}

//***************************************************************************
// Write
//***************************************************************************

int VeCom::writeRegister(uint16_t reg, double& value)
{
   if (registerDefinitions.find(reg) == registerDefinitions.end())
   {
      tell(eloAlways, "Error: Ignoring write request for unexpected register '0x%02x", reg);
      return onComError(fail);
   }

   const auto& def = registerDefinitions[reg];

   uint16_t uiValue = (uint16_t)(value * def.factor); // calc value depending factor
   uint8_t data[5] {};
   size_t bytes {4};

   data[0] = reg & 0x00ff;
   data[1] = reg >> 8;
   data[2] = 0x00;
   data[3] = uiValue & 0x00ff;

   tell(eloDebug, "Debug: Set low byte to 0x%02x (%d) (%5.2f)", data[3], uiValue, value);

   if (def.type == rtWord)
   {
      tell(eloAlways, "Writing WORD");
      data[4] = uiValue >> 8;
      bytes++;
   }

   // tell(eloAlways, "Send value 0x%04x", uiValue);

   if (sendCommand(cSet, data, 5) != success)
   {
      tell(eloAlways, "Error: Sending command failed");
      return onComError(fail);
   }

   uint16_t w {0};
   uint8_t b {0};
   uint8_t flags {0};

   readWord(w);               // read echo of address
   read2CharsAsByte(flags);   // flags

   if (flags != sgfSuccess)
      tell(eloAlways, "Read failed, flags are %d", flags);

   if (def.type == rtByte)
   {
      read2CharsAsByte(b);    // read byte value
      value = b / (double)def.factor;
   }
   else if (def.type == rtWord)
   {
      readWord(w);            // read word value
      value = w / (double)def.factor;
   }

   uint8_t c {0};

   if (read2CharsAsByte(c) != success || readChar(c, "\n") != success)
      return onComError(fail);

   // tell(eloAlways, "Response: %s: %.2f %s", def.title.c_str(), value, def.unit.c_str());

   return success;
}

int VeCom::writeRegister(uint16_t reg, uint8_t& value)
{
   double dValue {(double)value};
   int res { writeRegister(reg, dValue)};
   value = dValue;
   return res;
}

//***************************************************************************
// Flush
//***************************************************************************

void VeCom::veFlush()
{
   uint8_t b {};

   while (readByte(b) == success)
      ;
}

//***************************************************************************
// On Com Error
//***************************************************************************

int VeCom::onComError(int result)
{
   veFlush();
   return result;
}
