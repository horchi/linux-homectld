//***************************************************************************
// Automation Control
// File freeathome.c
// This code is distributed under the terms and conditions of the
// GNU GENERAL PUBLIC LICENSE. See the file LICENSE for details.
// Date 2026 Jörg Wendel
//***************************************************************************

//***************************************************************************
// free@home Local API of the SysAP 2.0 (Busch-Jaeger / ABB)
//   enabled in the SysAP app: 'free@home settings -> Local API', a local API user
//
//   REST   https://<sysap>/fhapi/v1/api/rest/configuration                 all devices / channels
//          PUT  .../rest/datapoint/<sysap-uuid>/<serial>.<channel>.<idp>     body: the value as text
//   WS     wss://<sysap>/fhapi/v1/api/ws                                    the changes, no commands
//          { "<sysap-uuid>": { "datapoints": { "ABB7F500E17A/ch0003/odp0000": "1" },
//                              "devicesAdded": [ "<serial>" ], "devicesRemoved": [ ... ] } }
//   Both with HTTP basic auth, the SysAP has a self signed certificate (not checked).
//
//   The channels by their function id (hex string, e.g. "12"), the datapoints by their pairing id:
//
//     FHL  light   0x07 switch actuator, 0x12 dimming actuator, 0x2e RGBW, 0x2f RGB, 0x40 color temperature
//                  in:  1 on / off, 17 absolute dim value [%], 16 relative dimming, 22 color temperature [%] (0 = warm)
//                  out: 256 on / off, 272 dim value [%], 280 color temperature [%]
//     FHB  blind   0x09 shutter, 0x61 blind, 0x62 attic window
//                  in:  35 position [%] (0 = open!), 33 stop
//                  out: 289 position [%], 288 moving (2 up, 3 down)
//     FHS  sensor  0x00 switch sensor, 0x01 dimming sensor (wall switches) -> button events like deCONZ:
//                       out 1: "1" 1002, "0" 2002 - out 16 (long press): "9" 1001, "8" 1003, "1" 2001, "0" 2003
//                  0x11 movement detector (out 6: motion "1" / "0"),
//                  weather station 0x41 brightness (out 1027 [lx]), 0x42 rain (out 39), 0x43 temperature
//                  (out 1024 [°C]), 0x44 wind (out 1028 [m/s])
//
//   The address of a channel ('<serial>/<channel>') is kept in the table fahchannels.
//   After a (re)connect the SysAP sends the datapoints once more (about 60 s), no button events
//   or motions for the light rules from them.
//***************************************************************************

#include <curl/curl.h>
#include <cmath>

#include "lib/json.h"
#include "lib/base64.h"

#include "daemon.h"
#include "freeathome.h"

FreeAtHome* FreeAtHome::singleton {};
lws_context* FreeAtHome::context {};
lws* FreeAtHome::clientWsi {};
lws_sorted_usec_list_t FreeAtHome::sul {};
std::string FreeAtHome::wsHost;
int FreeAtHome::wsPort {443};
std::string FreeAtHome::wsAuth;
bool FreeAtHome::wsTls {true};
std::atomic<bool> FreeAtHome::connected {false};

cMyMutex FreeAtHome::messagesInMutex;
std::queue<std::string> FreeAtHome::messagesIn;

static constexpr const char* fahProtocolName {"freeathome"};
static constexpr time_t fahPrimingTime {60};     // [s] the SysAP repeats the datapoints after a connect
static std::atomic<time_t> fahConnectedAt {0};

// pairing ids

enum FahPairing
{
   fpSwitchOnOff           = 1,
   fpTimedStartStop        = 2,
   fpTimedMovement         = 6,
   fpRelativeSetValue      = 16,
   fpAbsoluteSetValue      = 17,
   fpColorTemperature      = 22,
   fpStopStepUpDown        = 33,
   fpSetPositionBlinds     = 35,
   fpRainAlarm             = 39,
   fpInfoOnOff             = 256,
   fpInfoDimValue          = 272,
   fpInfoColorTemperature  = 280,
   fpInfoMoveUpDown        = 288,
   fpInfoPositionBlinds    = 289,
   fpOutdoorTemperature    = 1024,
   fpBrightnessLevel       = 1027,
   fpWindSpeed             = 1028
};

//***************************************************************************
// FreeAtHome
//***************************************************************************

FreeAtHome::FreeAtHome()
{
}

FreeAtHome::~FreeAtHome()
{
   exitWsClient();
}

int FreeAtHome::init(HomeCtl* parent)
{
   daemon = parent;
   return success;
}

int FreeAtHome::exit()
{
   exitWsClient();
   exitDb();
   return success;
}

//***************************************************************************
// Init / Exit Db
//***************************************************************************

int FreeAtHome::initDb(cDbConnection* connection)
{
   int status {success};

   exitDb();

   tableChannels = new cDbTable(connection, "fahchannels");
   if (tableChannels->open() != success) return fail;

   selectChannelByUuid = new cDbStatement(tableChannels);

   selectChannelByUuid->build("select ");
   selectChannelByUuid->bindAllOut();
   selectChannelByUuid->build(" from %s where ", tableChannels->TableName());
   selectChannelByUuid->bind("UUID", cDBS::bndIn | cDBS::bndSet);

   status += selectChannelByUuid->prepare();

   return status;
}

int FreeAtHome::exitDb()
{
   delete selectChannelByUuid; selectChannelByUuid = nullptr;
   delete tableChannels;       tableChannels = nullptr;

   return success;
}

//***************************************************************************
// Set Access
//***************************************************************************

void FreeAtHome::setAccess(const char* aHost, const char* aUser, const char* aPassword)
{
   host = aHost ? aHost : "";
   user = aUser ? aUser : "";
   password = aPassword ? aPassword : "";
   tls = true;

   if (host.starts_with("https://"))
      host.erase(0, strlen("https://"));
   else if (host.starts_with("http://"))
   {
      host.erase(0, strlen("http://"));
      tls = false;
   }

   while (!host.empty() && host.back() == '/')
      host.pop_back();
}

//***************************************************************************
// Request (REST)
//***************************************************************************

static size_t fahWriteCallback(char* ptr, size_t size, size_t nmemb, void* userdata)
{
   static_cast<std::string*>(userdata)->append(ptr, size * nmemb);
   return size * nmemb;
}

int FreeAtHome::request(const char* method, const char* path, const char* body, std::string& response)
{
   response.clear();

   if (!configured())
      return fail;

   std::string url {std::string(tls ? "https://" : "http://") + host + "/fhapi/v1/api/rest/" + path};
   CURL* handle {curl_easy_init()};

   if (!handle)
      return fail;

   struct curl_slist* headers {};

   curl_easy_setopt(handle, CURLOPT_URL, url.c_str());
   curl_easy_setopt(handle, CURLOPT_CUSTOMREQUEST, method);
   curl_easy_setopt(handle, CURLOPT_HTTPAUTH, (long)CURLAUTH_BASIC);
   curl_easy_setopt(handle, CURLOPT_USERNAME, user.c_str());
   curl_easy_setopt(handle, CURLOPT_PASSWORD, password.c_str());
   curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, 0L);       // self signed certificate of the SysAP
   curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, 0L);
   curl_easy_setopt(handle, CURLOPT_TIMEOUT, 10L);
   curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);
   curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, fahWriteCallback);
   curl_easy_setopt(handle, CURLOPT_WRITEDATA, &response);

   if (body)
   {
      headers = curl_slist_append(headers, "Content-Type: text/plain");
      curl_easy_setopt(handle, CURLOPT_HTTPHEADER, headers);
      curl_easy_setopt(handle, CURLOPT_POSTFIELDS, body);
   }

   CURLcode res {curl_easy_perform(handle)};
   long httpCode {0};
   curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &httpCode);

   curl_slist_free_all(headers);
   curl_easy_cleanup(handle);

   tell(eloDetail, "FREEATHOME: -> %s '%s' '%s'; result (%ld) [%.200s]", method, url.c_str(), body ? body : "", httpCode, response.c_str());

   if (res != CURLE_OK || httpCode != 200)
   {
      tell(eloAlways, "FREEATHOME: REST call %s '%s' failed, %s (HTTP %ld)", method, url.c_str(),
           res != CURLE_OK ? curl_easy_strerror(res) : "error", httpCode);
      return fail;
   }

   return success;
}

//***************************************************************************
// Kind / Unit of a Function
//***************************************************************************

FreeAtHome::Kind FreeAtHome::kindOf(uint functionId)
{
   switch (functionId)
   {
      case 0x07: case 0x12: case 0x2e: case 0x2f: case 0x40:
         return kLight;
      case 0x09: case 0x61: case 0x62:
         return kBlind;
      case 0x00: case 0x01: case 0x11: case 0x41: case 0x42: case 0x43: case 0x44:
         return kSensor;
      default:
         return kNone;
   }
}

const char* FreeAtHome::unitOf(uint functionId)
{
   switch (functionId)
   {
      case 0x00: case 0x01: return "btn";                  // wall switches, button events
      case 0x09: case 0x61: case 0x62: return "%";
      case 0x11: return "mov";
      case 0x41: return "lx";
      case 0x42: return "zst";
      case 0x43: return "°C";
      case 0x44: return "m/s";
      default:   return "";
   }
}

//***************************************************************************
// Address Of a Channel ('<serial>/<channel>', table fahchannels)
//***************************************************************************

long FreeAtHome::addressOf(const char* uuid, const char* type, const char* name)
{
   if (!tableChannels || !selectChannelByUuid)
      return na;

   tableChannels->clear();
   tableChannels->setValue("UUID", uuid);

   if (!selectChannelByUuid->find())
   {
      tableChannels->setValue("TYPE", type);
      tableChannels->insert();

      tell(eloAlways, "FREEATHOME: New channel '%s' (%s) '%s'", uuid, type, name);

      tableChannels->clear();
      tableChannels->setValue("UUID", uuid);
      selectChannelByUuid->find();
   }

   long address {tableChannels->getIntValue("ADDRESS")};

   if (address)
   {
      tableChannels->setValue("TYPE", type);
      tableChannels->setValue("NAME", name);
      tableChannels->store();
   }

   selectChannelByUuid->freeResult();
   tableChannels->reset();

   return address ? address : (long)na;
}

//***************************************************************************
// Read Configuration
//   all channels of the SysAP - creates the sensors, the current values as updates
//***************************************************************************

int FreeAtHome::readConfiguration(std::vector<Update>& updates)
{
   std::string response;

   if (request("GET", "configuration", nullptr, response) != success)
      return fail;

   json_t* jRoot {jsonLoad(response.c_str())};

   if (!jRoot)
   {
      tell(eloAlways, "FREEATHOME: Invalid configuration [%.200s]", response.c_str());
      return fail;
   }

   const char* uuid {};
   json_t* jSysap {};

   json_object_foreach(jRoot, uuid, jSysap)
   {
      json_t* jDevices {getObjectFromJson(jSysap, "devices")};

      if (!json_is_object(jDevices))
         continue;

      sysapUuid = uuid;
      channels.clear();
      addresses.clear();

      const char* serial {};
      json_t* jDevice {};

      json_object_foreach(jDevices, serial, jDevice)
         processDevice(serial, jDevice, updates);

      tell(eloAlways, "FREEATHOME: SysAP '%s' (%s), %zu channels", getStringFromJson(jSysap, "sysapName", ""),
           sysapUuid.c_str(), channels.size());

      break;      // one SysAP
   }

   json_decref(jRoot);

   return sysapUuid.empty() ? fail : success;
}

int FreeAtHome::processDevice(const char* serial, json_t* jDevice, std::vector<Update>& updates)
{
   json_t* jChannels {getObjectFromJson(jDevice, "channels")};
   const char* deviceName {getStringFromJson(jDevice, "displayName", serial)};

   if (!json_is_object(jChannels))
      return done;

   const char* chId {};
   json_t* jCh {};

   json_object_foreach(jChannels, chId, jCh)
   {
      uint functionId {(uint)strtoul(getStringFromJson(jCh, "functionID", "ffff"), nullptr, 16)};
      Kind kind {kindOf(functionId)};

      if (kind == kNone)
         continue;

      const char* type {kind == kLight ? "FHL" : kind == kBlind ? "FHB" : "FHS"};
      std::string name {getStringFromJson(jCh, "displayName", "")};

      if (name.empty())
         name = std::string(deviceName) + " " + chId;

      std::string key {std::string(serial) + "/" + chId};
      long address {addressOf(key.c_str(), type, name.c_str())};

      if (address == na)
      {
         tell(eloAlways, "FREEATHOME: Error: Can't get the address of '%s' from the database", key.c_str());
         continue;
      }

      Channel ch;
      ch.type = type;
      ch.serial = serial;
      ch.channel = chId;
      ch.functionId = functionId;

      const char* dp {};
      json_t* jDp {};

      json_object_foreach(getObjectFromJson(jCh, "inputs"), dp, jDp)
         ch.inputs[getIntFromJson(jDp, "pairingID")] = dp;

      json_object_foreach(getObjectFromJson(jCh, "outputs"), dp, jDp)
         ch.outputs[dp] = getIntFromJson(jDp, "pairingID");

      channels[address] = ch;
      addresses[key] = address;

      int options {HomeCtl::soNone};

      if (kind == kLight)
      {
         options = HomeCtl::soSwitch;

         if (ch.inputs.count(fpAbsoluteSetValue))
            options |= HomeCtl::soDim;
      }

      daemon->addValueFact(address, type, 1, name.c_str(), unitOf(functionId), ""/*title*/,
                           cWebService::urControl, nullptr, (HomeCtl::SensorOptions)options);

      // range of the color temperature (the actors know 0..100 % only)

      if (ch.inputs.count(fpColorTemperature))
      {
         Update u;
         u.type = type;
         u.address = address;
         u.ctMin = ctMinDefault;
         u.ctMax = ctMaxDefault;
         updates.push_back(u);
      }

      // the current values - no button events of the wall switches

      json_object_foreach(getObjectFromJson(jCh, "outputs"), dp, jDp)
      {
         int pairing {getIntFromJson(jDp, "pairingID")};

         if (pairing != fpSwitchOnOff && pairing != fpRelativeSetValue && pairing != fpTimedStartStop)
            datapoint2Update(channels[address], address, pairing, getStringFromJson(jDp, "value", ""), updates);
      }

      for (auto& u : updates)
         u.init = true;
   }

   return done;
}

//***************************************************************************
// Datapoint 2 Update
//***************************************************************************

void FreeAtHome::datapoint2Update(Channel& ch, uint address, int pairing, const char* value, std::vector<Update>& updates)
{
   if (isEmpty(value))
      return;

   Update u;
   u.type = ch.type;
   u.address = address;

   double v {strtod(value, nullptr)};
   int i {(int)lround(v)};

   switch (pairing)
   {
      case fpInfoOnOff:
         if (ch.type != "FHL") return;
         u.state = i != 0;
         break;

      case fpInfoDimValue:
         if (ch.type != "FHL") return;
         u.bri = std::max(0, std::min(i, 100));
         break;

      case fpInfoColorTemperature:
         if (ch.type != "FHL") return;
         u.ct = ctMaxDefault - std::max(0, std::min(i, 100)) * (ctMaxDefault - ctMinDefault) / 100;
         break;

      case fpInfoPositionBlinds:
         if (ch.type != "FHB") return;
         u.level = 100 - std::max(0, std::min(i, 100));    // free@home 0 = open, homectld 100 = open
         break;

      case fpInfoMoveUpDown:
         if (ch.type != "FHB") return;
         u.moving = i == 2 ? 1 : i == 3 ? 2 : 0;
         ch.moving = u.moving > 0;
         break;

      case fpTimedMovement:
         if (ch.functionId != 0x11) return;
         u.state = i != 0;
         break;

      case fpRainAlarm:
         u.state = i != 0;
         break;

      case fpBrightnessLevel:
      case fpOutdoorTemperature:
      case fpWindSpeed:
         if (ch.functionId == 0x11) return;           // brightness of the movement detector, its value is the motion
         u.value = v;
         u.hasValue = true;
         break;

      case fpSwitchOnOff:
      case fpTimedStartStop:
         if (ch.type != "FHS") return;
         u.btnevent = i ? 1002 : 2002;
         break;

      case fpRelativeSetValue:
         if (ch.type != "FHS") return;
         u.btnevent = i == 9 ? 1001 : i == 8 ? 1003 : i == 1 ? 2001 : 2003;
         break;

      default:
         return;
   }

   updates.push_back(u);
}

//***************************************************************************
// Process Message (websocket)
//***************************************************************************

int FreeAtHome::processMessage(const char* msg, std::vector<Update>& updates, bool& reread)
{
   json_t* jRoot {jsonLoad(msg)};

   if (!jRoot)
      return fail;

   bool priming {time(0) - fahConnectedAt < fahPrimingTime};
   const char* uuid {};
   json_t* jSysap {};

   json_object_foreach(jRoot, uuid, jSysap)
   {
      json_t* jAdded {getObjectFromJson(jSysap, "devicesAdded")};
      json_t* jRemoved {getObjectFromJson(jSysap, "devicesRemoved")};

      if ((json_is_array(jAdded) && json_array_size(jAdded)) || (json_is_array(jRemoved) && json_array_size(jRemoved)))
         reread = true;

      const char* key {};
      json_t* jValue {};

      json_object_foreach(getObjectFromJson(jSysap, "datapoints"), key, jValue)
      {
         // 'ABB7F500E17A/ch0003/odp0000' - only the outputs (the inputs are the commands to the channel)

         const char* slash {strrchr(key, '/')};

         if (!slash || strncmp(slash + 1, "odp", 3) != 0 || !json_is_string(jValue))
            continue;

         std::string chKey {key, (size_t)(slash - key)};
         auto it {addresses.find(chKey)};

         if (it == addresses.end())
            continue;

         Channel& ch {channels[it->second]};
         auto itOut {ch.outputs.find(slash + 1)};

         if (itOut == ch.outputs.end())
            continue;

         size_t count {updates.size()};
         datapoint2Update(ch, it->second, itOut->second, json_string_value(jValue), updates);

         for (size_t n = count; n < updates.size(); n++)
            updates[n].init = priming;
      }
   }

   json_decref(jRoot);

   return success;
}

//***************************************************************************
// Set Datapoint
//***************************************************************************

const FreeAtHome::Channel* FreeAtHome::channelOf(const char* type, uint address) const
{
   auto it {channels.find(address)};

   if (it == channels.end() || it->second.type != type)
   {
      tell(eloAlways, "FREEATHOME: Unknown channel '%s:0x%02x'", type, address);
      return nullptr;
   }

   return &it->second;
}

int FreeAtHome::setDatapoint(const Channel* ch, int pairing, const char* value)
{
   auto it {ch->inputs.find(pairing)};

   if (it == ch->inputs.end())
   {
      tell(eloAlways, "FREEATHOME: Channel '%s/%s' has no input with pairing %d", ch->serial.c_str(), ch->channel.c_str(), pairing);
      return fail;
   }

   std::string path {"datapoint/" + sysapUuid + "/" + ch->serial + "." + ch->channel + "." + it->second};
   std::string response;

   if (request("PUT", path.c_str(), value, response) != success)
      return fail;

   // { "<sysap-uuid>": { "result": "OK" } }

   json_t* jRoot {jsonLoad(response.c_str())};
   json_t* jSysap {jRoot ? getObjectFromJson(jRoot, sysapUuid.c_str()) : nullptr};
   const char* result {jSysap ? getStringFromJson(jSysap, "result", "") : ""};
   int status {strcasecmp(result, "ok") == 0 ? success : fail};

   if (status != success)
      tell(eloAlways, "FREEATHOME: Setting '%s' to '%s' failed [%.200s]", path.c_str(), value, response.c_str());

   if (jRoot)
      json_decref(jRoot);

   return status;
}

//***************************************************************************
// Light Backend
//***************************************************************************

int FreeAtHome::toggle(const char* type, uint address, bool state, int bri, int /*transitiontime*/, bool /*explicitState*/)
{
   const Channel* ch {channelOf(type, address)};

   if (!ch)
      return fail;

   // a dim value switches a dimmer on with this brightness (no flash of the last one),
   //   the transition time is a parameter of the actor

   if (state && bri > 0 && ch->inputs.count(fpAbsoluteSetValue))
      return setDatapoint(ch, fpAbsoluteSetValue, std::to_string(std::max(1, std::min(bri, 100))).c_str());

   return setDatapoint(ch, fpSwitchOnOff, state ? "1" : "0");
}

int FreeAtHome::dim(const char* type, uint address, int inc, int /*transitiontime*/, bool switchOn)
{
   const Channel* ch {channelOf(type, address)};

   if (!ch)
      return fail;

   if (inc > 0 && switchOn)
      setDatapoint(ch, fpAbsoluteSetValue, "1");

   // relative dimming as KNX (4 bit): 9 up, 1 down, 0 stop

   return setDatapoint(ch, fpRelativeSetValue, inc > 0 ? "9" : inc < 0 ? "1" : "0");
}

int FreeAtHome::color(const char* type, uint address, int /*hue*/, int /*saturation*/, int /*bri*/)
{
   tell(eloAlways, "FREEATHOME: Color of '%s:0x%02x' not supported", type, address);
   return fail;
}

int FreeAtHome::colorTemperature(const char* type, uint address, int ct, int bri)
{
   const Channel* ch {channelOf(type, address)};

   if (!ch)
      return fail;

   int pct {(ctMaxDefault - std::max(ctMinDefault, std::min(ct, ctMaxDefault))) * 100 / (ctMaxDefault - ctMinDefault)};
   int status {setDatapoint(ch, fpColorTemperature, std::to_string(pct).c_str())};

   if (bri > 0 && ch->inputs.count(fpAbsoluteSetValue))
      status += setDatapoint(ch, fpAbsoluteSetValue, std::to_string(std::max(1, std::min(bri, 100))).c_str());

   return status;
}

//***************************************************************************
// Blind Backend
//***************************************************************************

int FreeAtHome::blindMove(const char* type, uint address, int level, bool noStop)
{
   const Channel* ch {channelOf(type, address)};

   if (!ch)
      return fail;

   if (!noStop && ch->moving)
      return setDatapoint(ch, fpStopStepUpDown, "1");

   level = std::max(0, std::min(level, 100));

   return setDatapoint(ch, fpSetPositionBlinds, std::to_string(100 - level).c_str());   // free@home 0 = open
}

//***************************************************************************
// Websocket Client
//***************************************************************************

void FreeAtHome::connectClient(lws_sorted_usec_list_t* aSul)
{
   static lws_retry_bo_t retry {
      .secs_since_valid_ping = 30,
      .secs_since_valid_hangup = 70,
   };

   lws_client_connect_info ci;
   memset(&ci, 0, sizeof(ci));

   ci.context = context;
   ci.port = wsPort;
   ci.address = wsHost.c_str();
   ci.path = "/fhapi/v1/api/ws";
   ci.host = ci.address;
   ci.origin = ci.address;
   ci.local_protocol_name = fahProtocolName;   // no Sec-WebSocket-Protocol
   ci.alpn = "http/1.1";
   ci.pwsi = &clientWsi;
   ci.retry_and_idle_policy = &retry;

   if (wsTls)
      ci.ssl_connection = LCCSCF_USE_SSL | LCCSCF_ALLOW_SELFSIGNED | LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK | LCCSCF_ALLOW_INSECURE;

   if (!lws_client_connect_via_info(&ci))
      lws_sul_schedule(context, 0, aSul, connectClient, 10 * LWS_USEC_PER_SEC);
}

int FreeAtHome::initWsClient()
{
   if (!configured() || syncThread)
      return done;

   singleton = this;

   wsHost = host.substr(0, host.find(':'));
   wsPort = host.find(':') != std::string::npos ? atoi(host.c_str() + host.find(':') + 1) : tls ? 443 : 80;
   wsTls = tls;
   wsAuth = "Basic " + macaron::Base64::Encode(user + ":" + password);

   tell(eloAlways, "FREEATHOME: Websocket at '%s://%s:%d/fhapi/v1/api/ws'", tls ? "wss" : "ws", wsHost.c_str(), wsPort);

   memset(&protocols, 0, sizeof(protocols));
   protocols[0].name = fahProtocolName;
   protocols[0].callback = callbackWs;
   protocols[0].rx_buffer_size = 0;

   lws_context_creation_info info;
   memset(&info, 0, sizeof(info));
   info.port = CONTEXT_PORT_NO_LISTEN;
   info.protocols = protocols;
   info.options = tls ? LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT : 0;
   info.fd_limit_per_thread = 1 + 1 + 1;

   context = lws_create_context(&info);

   if (!context)
   {
      tell(eloAlways, "FREEATHOME: Error: Websocket init failed");
      return fail;
   }

   lws_sul_schedule(context, 0, &sul, connectClient, 100);

   threadClose = false;

   if (pthread_create(&syncThread, nullptr, syncFct, this))
   {
      tell(eloAlways, "FREEATHOME: Error: Failed to start the websocket thread");
      syncThread = 0;
      return fail;
   }

   return success;
}

int FreeAtHome::exitWsClient()
{
   if (syncThread)
   {
      threadClose = true;
      lws_cancel_service(context);

      time_t endWait {time(0) + 2};

      while (threadActive && time(0) < endWait)
         usleep(1000);

      if (threadActive)
         pthread_cancel(syncThread);
      else
         pthread_join(syncThread, nullptr);

      syncThread = 0;
   }

   if (context)
      lws_context_destroy(context);

   context = nullptr;
   connected = false;

   return done;
}

void* FreeAtHome::syncFct(void* user)
{
   FreeAtHome* fah {static_cast<FreeAtHome*>(user)};
   fah->threadActive = true;

   while (!fah->threadClose)
      lws_service(context, 0);

   fah->threadActive = false;

   return nullptr;
}

int FreeAtHome::atInMessage(const char* data, size_t size)
{
   cMyMutexLock lock(&messagesInMutex);
   messagesIn.push(std::string(data, size));

   return done;
}

int FreeAtHome::callbackWs(lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len)
{
   switch (reason)
   {
      case LWS_CALLBACK_CLIENT_APPEND_HANDSHAKE_HEADER:
      {
         unsigned char** p {static_cast<unsigned char**>(in)};
         unsigned char* end {(*p) + len};

         if (lws_add_http_header_by_token(wsi, WSI_TOKEN_HTTP_AUTHORIZATION, (const unsigned char*)wsAuth.c_str(),
                                          (int)wsAuth.length(), p, end))
            return -1;

         break;
      }
      case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
      {
         tell(eloAlways, "FREEATHOME: Websocket connection error '%s'", in ? (char*)in : "(null)");
         connected = false;
         lws_sul_schedule(context, 0, &sul, connectClient, 10 * LWS_USEC_PER_SEC);
         break;
      }
      case LWS_CALLBACK_CLIENT_ESTABLISHED:
      {
         tell(eloAlways, "FREEATHOME: Websocket connected");
         connected = true;
         fahConnectedAt = time(0);
         singleton->rxBuffer.clear();
         singleton->atInMessage("WS CONNECTED", strlen("WS CONNECTED"));   // (re)read the configuration
         break;
      }
      case LWS_CALLBACK_CLIENT_RECEIVE:
      {
         // a message may come in fragments

         singleton->rxBuffer.append(static_cast<const char*>(in), len);

         if (lws_is_final_fragment(wsi) && !lws_remaining_packet_payload(wsi))
         {
            tell(eloDebug, "FREEATHOME: Rx [%.300s]", singleton->rxBuffer.c_str());
            singleton->atInMessage(singleton->rxBuffer.c_str(), singleton->rxBuffer.size());
            singleton->rxBuffer.clear();
         }

         break;
      }
      case LWS_CALLBACK_CLIENT_CLOSED:
      {
         tell(eloAlways, "FREEATHOME: Websocket closed");
         connected = false;
         lws_sul_schedule(context, 0, &sul, connectClient, 10 * LWS_USEC_PER_SEC);
         break;
      }

      default: break;
   }

   return lws_callback_http_dummy(wsi, reason, user, in, len);
}
