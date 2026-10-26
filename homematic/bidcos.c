//***************************************************************************
// HomeMatic (BidCos-RF) to MQTT
// File bidcos.c
// This code is distributed under the terms and conditions of the
// GNU GENERAL PUBLIC LICENSE. See the file LICENSE for details.
// Date 26.10.2026 - Jörg Wendel
//
// BidCos frame (hex as used by culfw):
//   len  cnt  flags  type  src(3)  dst(3)  payload(n)       len = 9 + n
//***************************************************************************

#include "hmmqtt.h"

namespace BidCos
{

//***************************************************************************
// Address
//***************************************************************************

std::string addr(uint32_t address)
{
   char buf[10];
   snprintf(buf, sizeof(buf), "%06X", address & 0xFFFFFF);
   return buf;
}

uint32_t addr(const char* hex)
{
   return isEmpty(hex) ? 0 : strtoul(hex, nullptr, 16) & 0xFFFFFF;
}

//***************************************************************************
// To Hex
//***************************************************************************

std::string Frame::toHex() const
{
   std::string hex;
   char buf[10];

   auto add = [&](uint8_t b) { snprintf(buf, sizeof(buf), "%02X", b); hex += buf; };

   add(9 + payload.size());
   add(cnt);
   add(flags);
   add(type);
   hex += addr(src);
   hex += addr(dst);

   for (auto b : payload)
      add(b);

   return hex;
}

//***************************************************************************
// From Hex
//   a culfw line without the leading 'A', with X21 the RSSI byte follows the frame
//***************************************************************************

bool Frame::fromHex(const char* hex, Frame& frame)
{
   std::vector<uint8_t> bytes;
   size_t n {strlen(hex)};

   for (size_t i = 0; i + 1 < n; i += 2)
   {
      if (!isxdigit(hex[i]) || !isxdigit(hex[i+1]))
         return false;

      char b[3] {hex[i], hex[i+1], 0};
      bytes.push_back(strtoul(b, nullptr, 16));
   }

   if (bytes.size() < 10 || bytes[0] < 9 || bytes.size() < (size_t)bytes[0] + 1)
      return false;

   size_t len {bytes[0]};

   frame.cnt = bytes[1];
   frame.flags = bytes[2];
   frame.type = bytes[3];
   frame.src = bytes[4] << 16 | bytes[5] << 8 | bytes[6];
   frame.dst = bytes[7] << 16 | bytes[8] << 8 | bytes[9];
   frame.payload.assign(bytes.begin() + 10, bytes.begin() + len + 1);
   frame.rssi = 0;

   if (bytes.size() > len + 1)                         // RSSI of the culfw
   {
      int r {bytes[len + 1]};
      frame.rssi = (r >= 128 ? r - 256 : r) / 2 - 74;
   }

   return true;
}

//***************************************************************************
// Describe (log)
//***************************************************************************

std::string Frame::describe() const
{
   const char* name {"?"};

   switch (type)
   {
      case mtDeviceInfo: name = "DEVICE_INFO"; break;
      case mtConfig:     name = "CONFIG";      break;
      case mtAck:        name = payload.size() && payload[0] == 0x80 ? "NACK" : "ACK"; break;
      case mtInfo:       name = "INFO";        break;
      case mtAction:     name = "ACTION";      break;
   }

   std::string p;
   char buf[10];

   for (auto b : payload)
   {
      snprintf(buf, sizeof(buf), "%02X", b);
      p += buf;
   }

   char text[300];
   snprintf(text, sizeof(text), "#%02X %s -> %s %s (%02X) flags %02X payload '%s'%s",
            cnt, addr(src).c_str(), addr(dst).c_str(), name, type, flags, p.c_str(),
            rssi ? (" rssi " + std::to_string(rssi)).c_str() : "");

   return text;
}

} // namespace BidCos
