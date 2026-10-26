//***************************************************************************
// HomeMatic (BidCos-RF) to MQTT
// File hmmqtt.c
// This code is distributed under the terms and conditions of the
// GNU GENERAL PUBLIC LICENSE. See the file LICENSE for details.
// Date 26.10.2026 - Jörg Wendel
//
// MQTT (base topic default 'homematic', channel = <serial>:<channel>):
//
//  service -> clients (retained)
//    <base>/status            {"state":"online","cul":"V 1.67 CUL868","hmId":"4F5EE7"}
//    <base>/devices           [{"serial","type","model","rfAddress","firmware","channels":[{"channel","kind"}]}]
//    <base>/state/<channel>   {"level":76.0,"working":true,"direction":"down","rssi":-68,"ts":...}
//    <base>/pairing           {"active":true,"remaining":45}
//  service -> clients
//    <base>/event/paired      {"serial","type","rfAddress","result":"ok|unsupported|failed","text"}
//    <base>/event/removed     {"serial","result":"ok|failed"}
//  clients -> service
//    <base>/set/<channel>     {"level":50} | {"stop":true}
//    <base>/get/<channel>     status request at the device
//    <base>/pair              {"enable":true,"duration":60} | {"enable":false}
//    <base>/remove            {"serial":"OEQ1853240"}
//
// The service takes the address (hmId) of the former central (CCU), the devices
// paired to it keep working without pairing them again.
//***************************************************************************

#include <signal.h>

#include "hmmqtt.h"

using namespace BidCos;

bool HmMqtt::shutdown {false};

static constexpr int ackTimeoutMs {800};
static constexpr int maxTries {3};

//***************************************************************************
// Object
//***************************************************************************

HmMqtt::HmMqtt(const char* aDevice, const char* aMqttUrl, const char* aTopic, uint32_t aHmId,
               const char* aDevicesFile, bool aSniff)
   : device(aDevice),
     mqttUrl(aMqttUrl),
     topic(aTopic),
     hmId(aHmId),
     devicesFile(aDevicesFile),
     sniff(aSniff)
{
   counter = time(0) & 0xFF;
}

HmMqtt::~HmMqtt()
{
   publishStatus("offline");
   mqttDisconnect();

   for (auto& d : devices)
      delete d.second;
}

//***************************************************************************
// Init
//***************************************************************************

int HmMqtt::init()
{
   if (!hmId)
   {
      tell(eloAlways, "Error: Missing address (hmId) of the central");
      return fail;
   }

   tell(eloAlways, "Central address (hmId) %s%s", addr(hmId).c_str(), sniff ? ", sniff mode (receive only)" : "");

   if (!sniff)
      loadDevices();

   return success;
}

//***************************************************************************
// Loop
//***************************************************************************

int HmMqtt::loop()
{
   while (!doShutDown())
   {
      if (!sniff)
      {
         mqttConnection();
         mqttRead();
      }

      if (culConnection() != success)
      {
         sleep(1);
         continue;
      }

      std::string line;
      int res {success};

      while ((res = cul.readLine(line, 20)) == success)
         dispatchLine(line);

      if (!sniff)
      {
         processQueue();
         pairingCheck();
      }
   }

   return done;
}

//***************************************************************************
// CUL Connection
//***************************************************************************

int HmMqtt::culConnection()
{
   if (cul.isOpen())
      return success;

   if (time(0) < culRetryAt)
      return fail;

   culRetryAt = time(0) + 10;

   if (cul.open(device.c_str()) != success || cul.init() != success)
   {
      cul.close();
      publishStatus("offline");
      return fail;
   }

   publishStatus("online");

   // the current state of all devices (the answers come one by one)

   if (!sniff)
   {
      for (auto& d : devices)
      {
         std::vector<Frame> frames;

         for (int c = 1; c <= d.second->channels(); c++)
            d.second->statusRequest(c, frames);

         for (auto& f : frames)
            enqueue(f);
      }
   }

   return success;
}

//***************************************************************************
// Dispatch Line (culfw)
//***************************************************************************

int HmMqtt::dispatchLine(const std::string& line)
{
   if (line == "LOVF")
   {
      tell(eloAlways, "Warning: CUL duty cycle limit reached (1%%), sending is blocked for a while");
      return done;
   }

   if (line[0] != 'A' || line.length() < 21)
      return done;                                     // not a BidCos frame (answers of commands, ...)

   Frame frame;

   if (!Frame::fromHex(line.c_str() + 1, frame))
   {
      tell(eloDetail, "Ignoring invalid frame '%s'", line.c_str());
      return done;
   }

   if (sniff)
   {
      tell(eloAlways, "%s", frame.describe().c_str());
      return done;
   }

   tell(eloDebug, "-> %s", frame.describe().c_str());

   return dispatchFrame(frame);
}

//***************************************************************************
// Dispatch Frame
//***************************************************************************

int HmMqtt::dispatchFrame(const Frame& frame)
{
   if (frame.src == hmId)
      return done;                                     // our own (repeated) frame

   // pairing request (config button of a device), broadcast or to us

   if (frame.type == mtDeviceInfo)
   {
      if (pairingUntil)
         pairDevice(frame);
      else
         tell(eloDetail, "Pairing request of %s ignored, pairing is not active", addr(frame.src).c_str());

      return done;
   }

   if (frame.dst != hmId)
      return done;                                     // not for us

   // the ACK expected by the device

   if ((frame.flags & fBidi) && frame.type != mtAck)
      sendAck(frame);

   HmDevice* d {deviceByAddress(frame.src)};

   // answer to the frame in flight?

   if (frame.type == mtAck && inFlight && !sendQueue.empty())
   {
      Pending pending {sendQueue.front()};

      if (pending.frame.dst == frame.src && pending.frame.cnt == frame.cnt)
      {
         sendQueue.pop_front();
         inFlight = false;
         pendingDone(pending, !frame.payload.size() || frame.payload[0] != 0x80, &frame);
      }
   }

   if (!d)
   {
      tell(eloDetail, "Frame of unknown device %s ignored", addr(frame.src).c_str());
      return done;
   }

   d->lastSeen = time(0);
   d->rssi = frame.rssi;

   if (d->handle(frame))
   {
      for (int c = 1; c <= d->channels(); c++)
         publishState(d, c);
   }

   return done;
}

//***************************************************************************
// Send ACK
//***************************************************************************

int HmMqtt::sendAck(const Frame& frame)
{
   Frame ack;
   ack.cnt = frame.cnt;
   ack.flags = fRptEn;
   ack.type = mtAck;
   ack.src = hmId;
   ack.dst = frame.src;
   ack.payload = { 0x00 };

   tell(eloDebug, "<- %s", ack.describe().c_str());

   return cul.send(ack);
}

//***************************************************************************
// Send Queue (one frame in flight, waiting for its ACK, repeated if missing)
//***************************************************************************

int HmMqtt::enqueue(Frame frame, PendingKind kind)
{
   frame.src = hmId;
   frame.cnt = ++counter;

   Pending pending;
   pending.frame = frame;
   pending.kind = kind;

   sendQueue.push_back(pending);

   return done;
}

int HmMqtt::processQueue()
{
   if (sendQueue.empty() || !cul.isOpen())
      return done;

   Pending& pending {sendQueue.front()};

   if (inFlight && cTimeMs::Now() < pending.sentAt + ackTimeoutMs)
      return done;

   if (inFlight && pending.tries >= maxTries)
   {
      Pending failed {pending};
      sendQueue.pop_front();
      inFlight = false;

      tell(eloAlways, "Warning: No answer of %s, giving up (%s)", addr(failed.frame.dst).c_str(),
           failed.frame.describe().c_str());

      pendingDone(failed, false, nullptr);
      return done;
   }

   pending.tries++;
   pending.sentAt = cTimeMs::Now();
   inFlight = true;

   tell(eloDebug, "<- %s%s", pending.frame.describe().c_str(),
        pending.tries > 1 ? (" (try " + std::to_string(pending.tries) + ")").c_str() : "");

   cul.send(pending.frame);

   return done;
}

//***************************************************************************
// Pending Done (ACK received or given up)
//***************************************************************************

int HmMqtt::pendingDone(const Pending& pending, bool succeeded, const Frame* answer)
{
   HmDevice* d {deviceByAddress(pending.frame.dst)};
   std::string target {d ? d->serial : addr(pending.frame.dst)};

   switch (pending.kind)
   {
      case pkCommand:
      {
         if (!succeeded)
            tell(eloAlways, "Warning: Command to '%s' failed", target.c_str());

         break;
      }

      case pkPairStart:
      case pkPairWrite:
      {
         if (succeeded)
            break;

         // the following frames of the pairing are useless now

         while (!sendQueue.empty() && sendQueue.front().frame.dst == pending.frame.dst &&
                sendQueue.front().kind != pkCommand)
            sendQueue.pop_front();

         [[fallthrough]];
      }

      case pkPairEnd:
      {
         json_t* obj {json_object()};
         json_object_set_new(obj, "serial", json_string(d ? d->serial.c_str() : ""));
         json_object_set_new(obj, "type", json_string(d ? d->type.c_str() : ""));
         json_object_set_new(obj, "rfAddress", json_string(addr(pending.frame.dst).c_str()));
         json_object_set_new(obj, "result", json_string(succeeded && pending.kind == pkPairEnd ? "ok" : "failed"));
         json_object_set_new(obj, "text", json_string(succeeded && pending.kind == pkPairEnd ? "angelernt"
                                                      : "keine Antwort beim Eintragen der Zentrale"));
         publish("event/paired", obj, false);

         tell(eloAlways, "Pairing of '%s' %s", target.c_str(), succeeded && pending.kind == pkPairEnd ? "succeeded" : "failed");

         if (succeeded && pending.kind == pkPairEnd && d)
         {
            saveDevices();
            publishDevices();

            std::vector<Frame> frames;

            for (int c = 1; c <= d->channels(); c++)
               d->statusRequest(c, frames);

            for (auto& f : frames)
               enqueue(f);
         }
         else if (d)
         {
            // not paired - forget it again (a device known before stays)

            if (pairingNewDevice)
            {
               devices.erase(d->rfAddress);
               delete d;
            }
         }

         pairingAddress = 0;
         break;
      }

      case pkRemoveEnd:
      {
         json_t* obj {json_object()};
         json_object_set_new(obj, "serial", json_string(removingSerial.c_str()));
         json_object_set_new(obj, "result", json_string(succeeded ? "ok" : "failed"));
         publish("event/removed", obj, false);

         tell(eloAlways, "Removing '%s' %s", removingSerial.c_str(), succeeded ? "succeeded" : "failed (removed from the list anyway)");
         removingSerial.clear();
         break;
      }
   }

   return done;
}

//***************************************************************************
// Pairing
//***************************************************************************

int HmMqtt::pairingStart(int duration)
{
   pairingUntil = time(0) + std::max(10, std::min(duration, 300));
   tell(eloAlways, "Pairing active for %d seconds", (int)(pairingUntil - time(0)));
   publishPairing();

   return done;
}

int HmMqtt::pairingStop()
{
   if (pairingUntil)
      tell(eloAlways, "Pairing stopped");

   pairingUntil = 0;
   publishPairing();

   return done;
}

int HmMqtt::pairingCheck()
{
   if (!pairingUntil)
      return done;

   if (time(0) >= pairingUntil)
      return pairingStop();

   if (time(0) - pairingPublishedAt >= 5)
      publishPairing();

   return done;
}

//***************************************************************************
// Pair Device
//   DEVICE_INFO payload: firmware, model (2), serial (10 ascii), class, ...
//   -> CONFIG_START list 0, WRITE_INDEX 02:01 0A:<id1> 0B:<id2> 0C:<id3>, CONFIG_END
//***************************************************************************

int HmMqtt::pairDevice(const Frame& frame)
{
   if (frame.payload.size() < 13)
      return fail;

   if (pairingAddress)
   {
      tell(eloDetail, "Pairing request of %s ignored, pairing of %s is running", addr(frame.src).c_str(),
           addr(pairingAddress).c_str());
      return done;
   }

   char fw[10];
   snprintf(fw, sizeof(fw), "%d.%d", frame.payload[0] >> 4, frame.payload[0] & 0x0F);
   uint16_t model = frame.payload[1] << 8 | frame.payload[2];
   std::string serial(frame.payload.begin() + 3, frame.payload.begin() + 13);

   tell(eloAlways, "Pairing request of '%s' (%s), model %04X, firmware %s", serial.c_str(),
        addr(frame.src).c_str(), model, fw);

   HmDevice* d {deviceByAddress(frame.src)};

   pairingNewDevice = !d;

   if (!d)
      d = HmDevice::create(model);

   if (!d)
   {
      json_t* obj {json_object()};
      json_object_set_new(obj, "serial", json_string(serial.c_str()));
      json_object_set_new(obj, "rfAddress", json_string(addr(frame.src).c_str()));
      json_object_set_new(obj, "type", json_string(""));
      json_object_set_new(obj, "result", json_string("unsupported"));

      char text[100];
      snprintf(text, sizeof(text), "Gerätetyp %04X wird noch nicht unterstützt", model);
      json_object_set_new(obj, "text", json_string(text));
      publish("event/paired", obj, false);

      tell(eloAlways, "Device type %04X of '%s' isn't supported (yet)", model, serial.c_str());
      return done;
   }

   d->serial = serial;
   d->rfAddress = frame.src;
   d->firmware = fw;
   d->lastSeen = time(0);
   devices[d->rfAddress] = d;

   pairingAddress = frame.src;
   pairingStop();                                     // one device per pairing

   std::vector<uint8_t> registers { 0x02, 0x01,
                                    0x0A, (uint8_t)(hmId >> 16), 0x0B, (uint8_t)(hmId >> 8), 0x0C, (uint8_t)hmId };

   return configFrames(frame.src, registers, pkPairEnd);
}

//***************************************************************************
// Remove Device (the central in the device is cleared, removed from the list)
//***************************************************************************

int HmMqtt::removeDevice(const char* serial)
{
   for (auto it = devices.begin(); it != devices.end(); it++)
   {
      if (it->second->serial != serial)
         continue;

      uint32_t address {it->first};

      tell(eloAlways, "Removing '%s' (%s)", serial, addr(address).c_str());

      removingSerial = serial;
      configFrames(address, { 0x0A, 0x00, 0x0B, 0x00, 0x0C, 0x00 }, pkRemoveEnd);

      delete it->second;
      devices.erase(it);

      saveDevices();
      publishDevices();

      return done;
   }

   json_t* obj {json_object()};
   json_object_set_new(obj, "serial", json_string(serial));
   json_object_set_new(obj, "result", json_string("failed"));
   publish("event/removed", obj, false);

   return fail;
}

//***************************************************************************
// Config Frames (list 0 of the device)
//***************************************************************************

int HmMqtt::configFrames(uint32_t dst, const std::vector<uint8_t>& registers, PendingKind endKind)
{
   Frame f;
   f.flags = fBidi | fRptEn;
   f.type = mtConfig;
   f.dst = dst;

   f.payload = { 0x00, ctStart, 0x00, 0x00, 0x00, 0x00, 0x00 };   // channel 0, peer 000000:00, list 0
   enqueue(f, endKind == pkPairEnd ? pkPairStart : pkCommand);

   f.payload = { 0x00, ctWriteIndex };
   f.payload.insert(f.payload.end(), registers.begin(), registers.end());
   enqueue(f, endKind == pkPairEnd ? pkPairWrite : pkCommand);

   f.payload = { 0x00, ctEnd };
   enqueue(f, endKind);

   return done;
}

//***************************************************************************
// Devices (file of the service, the CCU devices are taken over once)
//
//   [ { "serial": "OEQ1853240", "type": "HM-LC-Bl1PBU-FM", "rfAddress": "62B37C", "firmware": "2.11" }, ... ]
//***************************************************************************

int HmMqtt::loadDevices()
{
   json_t* jArray {jsonLoadFile(devicesFile.c_str())};

   if (!jArray || !json_is_array(jArray))
   {
      tell(eloAlways, "No devices in '%s'", devicesFile.c_str());

      if (jArray)
         json_decref(jArray);

      return done;
   }

   size_t index {0};
   json_t* jDev {};

   json_array_foreach(jArray, index, jDev)
   {
      const char* modelHex {getStringFromJson(jDev, "model", "")};
      uint16_t model = !isEmpty(modelHex) ? strtoul(modelHex, nullptr, 16) : HmDevice::modelOf(getStringFromJson(jDev, "type", ""));
      HmDevice* d {HmDevice::create(model)};

      if (!d)
      {
         tell(eloAlways, "Warning: Device '%s' of the unknown type '%s' skipped", getStringFromJson(jDev, "serial", ""),
              getStringFromJson(jDev, "type", ""));
         continue;
      }

      d->serial = getStringFromJson(jDev, "serial", "");
      d->rfAddress = addr(getStringFromJson(jDev, "rfAddress", ""));
      d->firmware = getStringFromJson(jDev, "firmware", "");

      if (!d->rfAddress || d->serial.empty())
      {
         tell(eloAlways, "Warning: Device without serial / rfAddress in '%s' skipped", devicesFile.c_str());
         delete d;
         continue;
      }

      devices[d->rfAddress] = d;
      tell(eloAlways, "Device '%s' %s (%s)", d->serial.c_str(), d->type.c_str(), addr(d->rfAddress).c_str());
   }

   json_decref(jArray);

   return done;
}

int HmMqtt::saveDevices()
{
   json_t* jArray {json_array()};

   for (auto& d : devices)
   {
      json_t* obj {d.second->toJson()};
      json_object_del(obj, "lastSeen");
      json_object_del(obj, "rssi");
      json_object_del(obj, "channels");
      json_array_append_new(jArray, obj);
   }

   std::string tmp {devicesFile + ".tmp"};

   if (json_dump_file(jArray, tmp.c_str(), JSON_INDENT(3)) != 0 || rename(tmp.c_str(), devicesFile.c_str()) != 0)
      tell(eloAlways, "Error: Can't write '%s', %s", devicesFile.c_str(), strerror(errno));

   json_decref(jArray);

   return done;
}

HmDevice* HmMqtt::deviceByAddress(uint32_t rfAddress)
{
   auto it {devices.find(rfAddress)};
   return it != devices.end() ? it->second : nullptr;
}

HmDevice* HmMqtt::deviceByChannel(const std::string& channelId, uint& channel)
{
   auto pos {channelId.find(':')};
   std::string serial {channelId.substr(0, pos)};
   channel = pos != std::string::npos ? atoi(channelId.c_str() + pos + 1) : 1;

   for (auto& d : devices)
   {
      if (d.second->serial == serial && channel >= 1 && (int)channel <= d.second->channels())
         return d.second;
   }

   return nullptr;
}

//***************************************************************************
// MQTT Connection
//***************************************************************************

int HmMqtt::mqttConnection()
{
   if (!mqttWriter)
      mqttWriter = new Mqtt();

   if (!mqttReader)
      mqttReader = new Mqtt();

   if (!mqttWriter->isConnected())
   {
      if (mqttWriter->connect(mqttUrl) != success)
      {
         tell(eloAlways, "Error: MQTT: Connecting publisher to '%s' failed", mqttUrl);
         return fail;
      }

      tell(eloAlways, "MQTT: Connecting publisher to '%s' succeeded", mqttUrl);

      publishStatus(cul.isOpen() ? "online" : "offline");
      publishDevices();
      publishPairing();
   }

   if (!mqttReader->isConnected())
   {
      if (mqttReader->connect(mqttUrl) != success)
      {
         tell(eloAlways, "Error: MQTT: Connecting subscriber to '%s' failed", mqttUrl);
         return fail;
      }

      for (const char* sub : { "set/#", "get/#", "pair", "remove" })
      {
         std::string t {topic + "/" + sub};

         if (mqttReader->subscribe(t.c_str()) == success)
            tell(eloAlways, "MQTT: Topic '%s' subscribed", t.c_str());
      }
   }

   return success;
}

int HmMqtt::mqttDisconnect()
{
   if (mqttReader) mqttReader->disconnect();
   if (mqttWriter) mqttWriter->disconnect();

   delete mqttReader; mqttReader = nullptr;
   delete mqttWriter; mqttWriter = nullptr;

   return done;
}

//***************************************************************************
// MQTT Read
//***************************************************************************

int HmMqtt::mqttRead()
{
   if (!mqttReader || !mqttReader->isConnected())
      return done;

   MemoryStruct message;

   while (mqttReader->read(&message, 10) == success)
   {
      std::string t {mqttReader->getLastReadTopic()};

      if (mqttReader->isRetained())
         continue;                                     // no old commands

      dispatchMqttMessage(t, message.memory ? message.memory : "");
   }

   return done;
}

int HmMqtt::dispatchMqttMessage(const std::string& t, const char* message)
{
   if (t.rfind(topic + "/", 0) != 0)
      return done;

   std::string sub {t.substr(topic.length() + 1)};
   json_t* jMsg {jsonLoad(message, 0, true)};

   tell(eloMqtt, "MQTT: <- '%s' '%s'", t.c_str(), message);

   if (sub.rfind("set/", 0) == 0 || sub.rfind("get/", 0) == 0)
   {
      uint channel {0};
      HmDevice* d {deviceByChannel(sub.substr(4), channel)};

      if (!d)
         tell(eloAlways, "Warning: Unknown channel '%s'", sub.substr(4).c_str());
      else
      {
         std::vector<Frame> frames;

         if (sub[0] == 'g')
            d->statusRequest(channel, frames);
         else if (jMsg)
            d->set(channel, jMsg, frames);

         for (auto& f : frames)
            enqueue(f);
      }
   }
   else if (sub == "pair")
   {
      if (jMsg && getBoolFromJson(jMsg, "enable", false))
         pairingStart(getIntFromJson(jMsg, "duration", 60));
      else
         pairingStop();
   }
   else if (sub == "remove")
   {
      if (jMsg && !isEmpty(getStringFromJson(jMsg, "serial", "")))
         removeDevice(getStringFromJson(jMsg, "serial", ""));
   }

   if (jMsg)
      json_decref(jMsg);

   return done;
}

//***************************************************************************
// Publish
//***************************************************************************

int HmMqtt::publish(const std::string& subTopic, json_t* obj, bool retained)
{
   if (!mqttWriter || !mqttWriter->isConnected())
   {
      json_decref(obj);
      return fail;
   }

   std::string t {topic + "/" + subTopic};
   char* message {json_dumps(obj, JSON_REAL_PRECISION(6) | JSON_COMPACT)};

   tell(eloMqtt, "MQTT: -> '%s' '%s'", t.c_str(), message);

   if (retained)
      mqttWriter->writeRetained(t.c_str(), message);
   else
      mqttWriter->write(t.c_str(), message);

   free(message);
   json_decref(obj);

   return success;
}

int HmMqtt::publishStatus(const char* state)
{
   if (sniff)
      return done;

   json_t* obj {json_object()};
   json_object_set_new(obj, "state", json_string(state));
   json_object_set_new(obj, "cul", json_string(cul.getVersion()));
   json_object_set_new(obj, "hmId", json_string(addr(hmId).c_str()));

   return publish("status", obj, true);
}

int HmMqtt::publishDevices()
{
   json_t* jArray {json_array()};

   for (auto& d : devices)
      json_array_append_new(jArray, d.second->toJson());

   return publish("devices", jArray, true);
}

int HmMqtt::publishState(HmDevice* d, uint channel)
{
   return publish("state/" + d->channelId(channel), d->state(channel), true);
}

int HmMqtt::publishPairing()
{
   pairingPublishedAt = time(0);

   json_t* obj {json_object()};
   json_object_set_new(obj, "active", json_boolean(pairingUntil != 0));
   json_object_set_new(obj, "remaining", json_integer(pairingUntil ? pairingUntil - time(0) : 0));

   return publish("pairing", obj, true);
}

//***************************************************************************
// Usage
//***************************************************************************

void showUsage(const char* bin)
{
   printf("Usage: %s [options]\n", bin);
   printf("\n");
   printf("  options:\n");
   printf("     -d <device>     serial device of the CUL (default /dev/ttyACM0)\n");
   printf("     -i <hmId>       address of the central (hex, e.g. 4F5EE7 of the former CCU)\n");
   printf("     -f <file>       device list (default /var/lib/hmmqtt/devices.json)\n");
   printf("     -u <url>        MQTT url (default tcp://localhost:1883)\n");
   printf("     -T <topic>      MQTT base topic (default homematic)\n");
   printf("     -s              sniff: only log the received frames, no MQTT, no sending\n");
   printf("     -l <eloquence>  set eloquence\n");
   printf("     -t              log to terminal\n");
   printf("     -n              don't fork in background\n");
}

//***************************************************************************
// Main
//***************************************************************************

int main(int argc, char** argv)
{
   bool nofork {false};
   bool sniff {false};
   int _stdout {na};
   const char* device {"/dev/ttyACM0"};
   const char* mqttUrl {"tcp://localhost:1883"};
   const char* mqttTopic {"homematic"};
   const char* devicesFile {"/var/lib/hmmqtt/devices.json"};
   uint32_t hmId {0};

   for (int i = 1; argv[i]; i++)
   {
      if (argv[i][0] == '?' || strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0)
      {
         showUsage(argv[0]);
         return 0;
      }

      if (argv[i][0] != '-' || strlen(argv[i]) != 2)
         continue;

      switch (argv[i][1])
      {
         case 'l': if (argv[i+1]) eloquence = (Eloquence)strtol(argv[++i], nullptr, 0); break;
         case 'd': if (argv[i+1]) device = argv[++i];                    break;
         case 'i': if (argv[i+1]) hmId = addr(argv[++i]);                break;
         case 'f': if (argv[i+1]) devicesFile = argv[++i];               break;
         case 'u': if (argv[i+1]) mqttUrl = argv[++i];                   break;
         case 'T': if (argv[i+1]) mqttTopic = argv[++i];                 break;
         case 's': sniff = true;                                         break;
         case 't': _stdout = yes;                                        break;
         case 'n': nofork = true;                                        break;
      }
   }

   logstdout = _stdout != na ? _stdout : false;

   HmMqtt* job = new HmMqtt(device, mqttUrl, mqttTopic, hmId, devicesFile, sniff);

   if (job->init() != success)
   {
      printf("Initialization failed, see syslog for details\n");
      delete job;
      return 1;
   }

   if (!nofork)
   {
      int pid {fork()};

      if (pid < 0)
      {
         printf("Can't fork daemon, %s\n", strerror(errno));
         return 1;
      }

      if (pid != 0)
         return 0;
   }

   ::signal(SIGINT, HmMqtt::downF);
   ::signal(SIGTERM, HmMqtt::downF);

   job->loop();

   tell(eloAlways, "shutdown");
   delete job;

   return 0;
}
