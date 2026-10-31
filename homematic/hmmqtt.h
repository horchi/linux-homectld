//***************************************************************************
// HomeMatic (BidCos-RF) to MQTT
// File hmmqtt.h
// This code is distributed under the terms and conditions of the
// GNU GENERAL PUBLIC LICENSE. See the file LICENSE for details.
// Date 26.10.2026 - Jörg Wendel
//
// Classic HomeMatic (BidCos) devices via a CUL (CC1101 USB, culfw) without CCU,
// controlled by MQTT. Layers:
//   Cul        - serial line of the culfw (commands 'V', 'X21', 'Ar', 'As<hex>')
//   BidCos     - frame, flags, message types, counter, ACK / repeat
//   HmDevice   - one class per device type (HmBlind: HM-LC-Bl1PBU-FM and relatives)
//   HmMqtt     - the service: device list, pairing, MQTT topics
//
// Protocol reference: FHEM 10_CUL_HM.pm / HMConfig.pm
//***************************************************************************

#pragma once

#include <deque>
#include <map>
#include <vector>
#include <string>

#include "../lib/common.h"
#include "../lib/json.h"
#include "../lib/mqtt.h"

class HmMqtt;

//***************************************************************************
// BidCos
//***************************************************************************

namespace BidCos
{
   enum Flags
   {
      fWakeUp   = 0x01,
      fWakeMeUp = 0x02,
      fConfig   = 0x04,      // broadcast / config
      fBurst    = 0x10,
      fBidi     = 0x20,      // an answer (ACK) is expected
      fRepeated = 0x40,
      fRptEn    = 0x80
   };

   enum MessageType
   {
      mtDeviceInfo = 0x00,   // pairing request of a device (its config button)
      mtConfig     = 0x01,   // config / status request (sub type in payload[1])
      mtAck        = 0x02,   // payload[0]: 0x00 ACK, 0x01 ACK with status, 0x80 NACK
      mtInfo       = 0x10,   // payload[0] 0x06: actuator status
      mtAction     = 0x11    // payload[0] 0x02: set level, 0x03: stop
   };

   enum ConfigType           // payload[1] of mtConfig
   {
      ctStart      = 0x05,
      ctEnd        = 0x06,
      ctWriteIndex = 0x08,
      ctStatusReq  = 0x0E
   };

   struct Frame
   {
      uint8_t cnt {0};
      uint8_t flags {0};
      uint8_t type {0};
      uint32_t src {0};
      uint32_t dst {0};
      std::vector<uint8_t> payload;
      int rssi {0};                                    // [dBm], received frames only

      std::string toHex() const;                       // len cnt flags type src dst payload
      static bool fromHex(const char* hex, Frame& frame);
      std::string describe() const;
   };

   std::string addr(uint32_t address);                 // "62B37C"
   uint32_t addr(const char* hex);
}

//***************************************************************************
// CUL (culfw)
//***************************************************************************

class Cul
{
   public:

      enum Result { wrnTimeout = -100 };

      ~Cul() { close(); }

      int open(const char* aDevice);
      int close();
      bool isOpen() { return fd >= 0; }

      int init();                                      // version, reporting, BidCos receive mode
      int send(const BidCos::Frame& frame);
      int readLine(std::string& line, int timeoutMs);  // success, wrnTimeout, fail

      const char* getVersion() { return version.c_str(); }

   protected:

      int write(const char* command);

      int fd {-1};
      std::string device;
      std::string buffer;
      std::string version;
};

//***************************************************************************
// Device Base
//***************************************************************************

class HmDevice
{
   public:

      struct Model
      {
         const char* name;
         const char* kind;     // kind of the channels (BLIND, ...)
         int channels;
      };

      virtual ~HmDevice() = default;

      static HmDevice* create(uint16_t model);         // nullptr if the type is not supported
      static const Model* modelOf(uint16_t model);
      static uint16_t modelOf(const char* typeName);

      virtual const char* kind() = 0;
      virtual int channels() = 0;

      // a frame of this device (status, ACK with status) -> state, true if a state changed

      virtual bool handle(const BidCos::Frame& frame) = 0;

      // MQTT 'set' of a channel -> frames to send

      virtual int set(uint channel, json_t* jCommand, std::vector<BidCos::Frame>& frames) = 0;
      virtual int statusRequest(uint channel, std::vector<BidCos::Frame>& frames);

      virtual json_t* state(uint channel) = 0;
      virtual bool moving() { return false; }          // state changes by itself (blind running)
      json_t* toJson();                                // entry of 'devices'

      std::string channelId(uint channel) { return serial + ":" + std::to_string(channel); }

      std::string serial;
      std::string type;
      std::string firmware;
      uint16_t model {0};
      uint32_t rfAddress {0};
      time_t lastSeen {0};
      int rssi {0};
      uint64_t pollAt {0};                             // next status request while moving [ms], 0 not moving
};

//***************************************************************************
// Blind Actuator (HM-LC-Bl1PBU-FM, HM-LC-Bl1-FM, ...)
//***************************************************************************

class HmBlind : public HmDevice
{
   public:

      const char* kind() override   { return "BLIND"; }
      int channels() override       { return 1; }

      bool handle(const BidCos::Frame& frame) override;
      int set(uint channel, json_t* jCommand, std::vector<BidCos::Frame>& frames) override;
      json_t* state(uint channel) override;

      bool working() { return direction != 0; }
      bool moving() override { return working(); }

   protected:

      double level {-1};       // [%], -1 unknown
      int direction {0};       // 0 none, 1 up, 2 down
      time_t stateAt {0};
};

//***************************************************************************
// Service
//***************************************************************************

class HmMqtt
{
   public:

      HmMqtt(const char* aDevice, const char* aMqttUrl, const char* aTopic, uint32_t aHmId,
             const char* aDevicesFile, bool aSniff);
      ~HmMqtt();

      int init();
      int loop();

      static void downF(int aSignal) { shutdown = true; }
      static bool doShutDown()       { return shutdown; }

   protected:

      // a frame waiting for its ACK

      enum PendingKind { pkCommand, pkPairStart, pkPairWrite, pkPairEnd, pkRemoveEnd };

      struct Pending
      {
         BidCos::Frame frame;
         PendingKind kind {pkCommand};
         int tries {0};
         uint64_t sentAt {0};
      };

      // CUL

      int culConnection();
      int dispatchLine(const std::string& line);
      int dispatchFrame(const BidCos::Frame& frame);
      int sendAck(const BidCos::Frame& frame);
      int enqueue(BidCos::Frame frame, PendingKind kind = pkCommand);
      int processQueue();
      int pendingDone(const Pending& pending, bool success, const BidCos::Frame* answer);
      int trackMoving();
      int statusAll();

      // pairing

      int pairingStart(int duration);
      int pairingStop();
      int pairingCheck();
      int pairDevice(const BidCos::Frame& frame);
      int removeDevice(const char* serial);
      int configFrames(uint32_t dst, const std::vector<uint8_t>& registers, PendingKind endKind);

      // devices

      int loadDevices();
      int saveDevices();
      HmDevice* deviceByAddress(uint32_t rfAddress);
      HmDevice* deviceByChannel(const std::string& channelId, uint& channel);

      // MQTT

      int mqttConnection();
      int mqttDisconnect();
      int mqttRead();
      int dispatchMqttMessage(const std::string& topic, const char* message);
      int publish(const std::string& subTopic, json_t* obj, bool retained);
      int publishStatus(const char* state);
      int publishDevices();
      int publishState(HmDevice* device, uint channel);
      int publishPairing();

      std::string device;
      const char* mqttUrl {};
      std::string topic;
      uint32_t hmId {0};
      std::string devicesFile;
      bool sniff {false};

      Cul cul;
      time_t culRetryAt {0};
      uint64_t statusAllAt {0};                       // status request of all devices after the CUL opened [ms]
      uint8_t counter {0};

      std::map<uint32_t,HmDevice*> devices;          // rf address -> device
      std::deque<Pending> sendQueue;                  // the front one is in flight
      bool inFlight {false};

      time_t pairingUntil {0};
      time_t pairingPublishedAt {0};
      uint32_t pairingAddress {0};                    // device being paired
      bool pairingNewDevice {false};                  // not in the list before the pairing
      std::string removingSerial;

      Mqtt* mqttWriter {};
      Mqtt* mqttReader {};

      static bool shutdown;
};
