//***************************************************************************
// HomeMatic (BidCos-RF) to MQTT
// File devices.c
// This code is distributed under the terms and conditions of the
// GNU GENERAL PUBLIC LICENSE. See the file LICENSE for details.
// Date 26.10.2026 - Jörg Wendel
//
// The device types, more types (switch, dimmer, contact, key) as further
// classes derived from HmDevice, registered in the model table below.
//***************************************************************************

#include <cmath>

#include "hmmqtt.h"

using namespace BidCos;

//***************************************************************************
// Models (model id of the DEVICE_INFO, see FHEM HMConfig.pm)
//***************************************************************************

static const std::map<uint16_t,HmDevice::Model> models
{
   { 0x0005, { "HM-LC-Bl1-FM",     "BLIND", 1 } },
   { 0x0006, { "HM-LC-Bl1-SM",     "BLIND", 1 } },
   { 0x0053, { "HM-LC-Bl1-PB-FM",  "BLIND", 1 } },
   { 0x006A, { "HM-LC-Bl1PBU-FM",  "BLIND", 1 } }
};

const HmDevice::Model* HmDevice::modelOf(uint16_t model)
{
   auto it {models.find(model)};
   return it != models.end() ? &it->second : nullptr;
}

uint16_t HmDevice::modelOf(const char* typeName)
{
   for (const auto& m : models)
   {
      if (strcasecmp(m.second.name, typeName) == 0)
         return m.first;
   }

   return 0;
}

HmDevice* HmDevice::create(uint16_t model)
{
   const Model* m {modelOf(model)};

   if (!m)
      return nullptr;

   HmDevice* device {};

   if (strcmp(m->kind, "BLIND") == 0)
      device = new HmBlind;

   if (device)
   {
      device->model = model;
      device->type = m->name;
   }

   return device;
}

//***************************************************************************
// Status Request (CONFIG / 0x0E of the channel)
//***************************************************************************

int HmDevice::statusRequest(uint channel, std::vector<Frame>& frames)
{
   Frame f;
   f.flags = fBidi | fRptEn;
   f.type = mtConfig;
   f.dst = rfAddress;
   f.payload = { (uint8_t)channel, ctStatusReq };
   frames.push_back(f);

   return success;
}

//***************************************************************************
// To Json (entry of the device list)
//***************************************************************************

json_t* HmDevice::toJson()
{
   char modelHex[10];
   snprintf(modelHex, sizeof(modelHex), "%04X", model);

   json_t* obj {json_object()};
   json_object_set_new(obj, "serial", json_string(serial.c_str()));
   json_object_set_new(obj, "type", json_string(type.c_str()));
   json_object_set_new(obj, "model", json_string(modelHex));
   json_object_set_new(obj, "rfAddress", json_string(addr(rfAddress).c_str()));
   json_object_set_new(obj, "firmware", json_string(firmware.c_str()));

   if (lastSeen)
   {
      json_object_set_new(obj, "lastSeen", json_integer(lastSeen));
      json_object_set_new(obj, "rssi", json_integer(rssi));
   }

   json_t* jChannels {json_array()};

   for (int c = 1; c <= channels(); c++)
   {
      json_t* jCh {json_object()};
      json_object_set_new(jCh, "channel", json_string(channelId(c).c_str()));
      json_object_set_new(jCh, "kind", json_string(kind()));
      json_array_append_new(jChannels, jCh);
   }

   json_object_set_new(obj, "channels", jChannels);

   addJson(obj);

   return obj;
}

//***************************************************************************
// Blind - Handle
//   ACK_STATUS (02 01) and INFO_ACTUATOR_STATUS (10 06):
//   payload: sub type, channel, level (0..200), flags (bit 4/5 direction 1 up, 2 down)
//***************************************************************************

bool HmBlind::handle(const Frame& frame)
{
   bool status {(frame.type == mtAck && frame.payload.size() >= 4 && frame.payload[0] == 0x01) ||
                (frame.type == mtInfo && frame.payload.size() >= 4 && frame.payload[0] == 0x06)};

   if (!status || (frame.payload[1] & 0x3F) != 1)
      return false;

   level = frame.payload[2] / 2.0;
   direction = (frame.payload[3] >> 4) & 0x03;
   direction = direction == 3 ? 0 : direction;           // 3: error
   stateAt = time(0);

   tell(eloDetail, "Blind '%s' level %.1f%% %s", serial.c_str(), level,
        direction == 1 ? "up" : direction == 2 ? "down" : "");

   return true;
}

//***************************************************************************
// Blind - Set
//   {"level": 0..100} -> ACTION 02 <ch> <level*2>, {"stop": true} -> ACTION 03 <ch>
//***************************************************************************

int HmBlind::set(uint channel, json_t* jCommand, std::vector<Frame>& frames)
{
   Frame f;
   f.flags = fBidi | fRptEn;
   f.type = mtAction;
   f.dst = rfAddress;

   if (getBoolFromJson(jCommand, "stop", false))
      f.payload = { 0x03, (uint8_t)channel };
   else if (getObjectFromJson(jCommand, "level"))
   {
      double value {getDoubleFromJson(jCommand, "level", 0)};
      value = toActor(std::max(0.0, std::min(100.0, value)));
      f.payload = { 0x02, (uint8_t)channel, (uint8_t)lround(value * 2) };
   }
   else
   {
      tell(eloAlways, "Warning: Unknown command for blind '%s'", channelId(channel).c_str());
      return fail;
   }

   frames.push_back(f);

   return success;
}

//***************************************************************************
// Blind - State
//***************************************************************************

json_t* HmBlind::state(uint channel)
{
   json_t* obj {json_object()};

   if (level >= 0)
   {
      json_object_set_new(obj, "level", json_real(round(toVisible(level) * 10) / 10));

      if (half >= 0)
         json_object_set_new(obj, "actorLevel", json_real(level));
   }

   json_object_set_new(obj, "working", json_boolean(working()));
   json_object_set_new(obj, "direction", json_string(direction == 1 ? "up" : direction == 2 ? "down" : "none"));

   if (rssi)
      json_object_set_new(obj, "rssi", json_integer(rssi));

   json_object_set_new(obj, "ts", json_integer(stateAt));

   return obj;
}

//***************************************************************************
// Blind - Running Times
//   REFERENCE_RUNNING_TIME_TOP_BOTTOM (0B/0C) and _BOTTOM_TOP (0D/0E) of
//   list 1 of channel 1, [0.1 s], big endian; the actor calculates the
//   position from them (no position sensor, no end position detection)
//***************************************************************************

void HmBlind::addJson(json_t* obj)
{
   if (runtimeDown >= 0)
      json_object_set_new(obj, "runtimeDown", json_real(runtimeDown));

   if (runtimeUp >= 0)
      json_object_set_new(obj, "runtimeUp", json_real(runtimeUp));

   if (half >= 0)
      json_object_set_new(obj, "half", json_real(half));
}

void HmBlind::fromJson(json_t* obj)
{
   runtimeDown = getDoubleFromJson(obj, "runtimeDown", -1);
   runtimeUp = getDoubleFromJson(obj, "runtimeUp", -1);
   half = getDoubleFromJson(obj, "half", -1);
}

//***************************************************************************
// Blind - Characteristic (visible level <-> level of the actor)
//***************************************************************************

double HmBlind::toActor(double visible)
{
   if (half <= 0 || half >= 100)
      return visible;

   if (visible <= 50)
      return visible / 50 * half;

   return half + (visible - 50) / 50 * (100 - half);
}

double HmBlind::toVisible(double actor)
{
   if (half <= 0 || half >= 100)
      return actor;

   if (actor <= half)
      return actor / half * 50;

   return 50 + (actor - half) / (100 - half) * 50;
}

std::vector<uint8_t> HmBlind::runtimeRegisters(double down, double up)
{
   uint down10 = lround(std::max(0.1, std::min(600.0, down)) * 10);
   uint up10 = lround(std::max(0.1, std::min(600.0, up)) * 10);

   return { 0x0B, (uint8_t)(down10 >> 8), 0x0C, (uint8_t)down10,
            0x0D, (uint8_t)(up10 >> 8), 0x0E, (uint8_t)up10 };
}

bool HmBlind::handleParams(const Frame& frame)
{
   if (frame.type != mtInfo || frame.payload.size() < 2)
      return false;

   if (frame.payload[0] == 0x02)                       // pairs <reg> <value>, 00 00 ends the list
   {
      for (size_t i = 1; i + 1 < frame.payload.size(); i += 2)
      {
         if (!frame.payload[i] && !frame.payload[i+1])
            break;

         params[frame.payload[i]] = frame.payload[i+1];
      }
   }
   else if (frame.payload[0] == 0x03)                  // sequence <start reg> <value> <value> ...
   {
      uint8_t reg {frame.payload[1]};

      for (size_t i = 2; i < frame.payload.size(); i++)
         params[reg++] = frame.payload[i];
   }
   else
      return false;

   for (uint8_t reg : { 0x0B, 0x0C, 0x0D, 0x0E })
   {
      if (params.find(reg) == params.end())
         return false;
   }

   runtimeDown = ((params[0x0B] << 8) | params[0x0C]) / 10.0;
   runtimeUp = ((params[0x0D] << 8) | params[0x0E]) / 10.0;
   params.clear();

   tell(eloAlways, "Blind '%s' running times: down %.1f s, up %.1f s", serial.c_str(), runtimeDown, runtimeUp);

   return true;
}
