//***************************************************************************
// Automation Control
// File vdr.c
// This code is distributed under the terms and conditions of the
// GNU GENERAL PUBLIC LICENSE. See the file LICENSE for details.
// Date 16.11.2026 - Jörg Wendel
//***************************************************************************

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <ifaddrs.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <net/if.h>
#include <net/ethernet.h>
#include <linux/if_packet.h>

#include "lib/json.h"
#include "daemon.h"

//***************************************************************************
// VDR List
//   config item 'vdr' (ctStringSet), one entry per VDR: 'Name;Host:Port;MAC'
//   the fields are recognized by their content, name and MAC are optional:
//   - MAC        -> 6 hex pairs separated by ':' or '-'
//   - Host:Port  -> the field with ':<number>' at the end (osd2web)
//   - OFF        -> the VDR may be switched off by the remote (power key), without
//                   the power key only wakes up (e.g. a VDR on the server itself)
//   - Name       -> the remaining field, missing -> name = host
//   without 'Host:Port' the first remaining field is taken as host
//   the host may be a name (DNS / hosts) or an IP
//***************************************************************************

static bool parseMac(const std::string& str, unsigned char mac[6]);

static bool isHostPort(const std::string& s)
{
   size_t pos = s.rfind(':');

   if (pos == std::string::npos || pos == 0 || pos == s.length() - 1)
      return false;

   return s.find_first_not_of("0123456789", pos + 1) == std::string::npos;
}

struct VdrEntry
{
   std::string name;
   std::string host;         // without port
   std::string url;          // host:port of osd2web
   std::string mac;
   bool powerOff {false};
};

static std::string trimmed(const std::string& s)
{
   size_t b = s.find_first_not_of(" \t");
   size_t e = s.find_last_not_of(" \t");
   return b == std::string::npos ? "" : s.substr(b, e - b + 1);
}

static VdrEntry parseVdrEntry(const std::string& entry)
{
   VdrEntry vdr;
   std::vector<std::string> rest;

   for (auto p : split(entry, ';'))
   {
      unsigned char hw[6];
      p = trimmed(p);

      if (p.empty())
         continue;

      if (strcasecmp(p.c_str(), "OFF") == 0)
         vdr.powerOff = true;
      else if (vdr.mac.empty() && parseMac(p, hw))
         vdr.mac = p;
      else if (vdr.url.empty() && isHostPort(p))
         vdr.url = p;
      else
         rest.push_back(p);
   }

   if (vdr.url.empty() && !rest.empty())
   {
      vdr.url = rest.front();
      rest.erase(rest.begin());
   }

   if (!rest.empty())
      vdr.name = rest.front();

   vdr.host = vdr.url.substr(0, vdr.url.find(':'));

   if (vdr.name.empty())
      vdr.name = vdr.host;

   return vdr;
}

static std::string vdrEntryString(const VdrEntry& vdr)
{
   return vdr.name + ";" + vdr.url + (vdr.mac.empty() ? "" : ";" + vdr.mac) + (vdr.powerOff ? ";OFF" : "");
}

//***************************************************************************
// MAC parse ('00:1e:06:43:48:8e' or '00-1e-06-43-48-8e')
//***************************************************************************

static bool parseMac(const std::string& str, unsigned char mac[6])
{
   unsigned int b[6];
   char sep1, sep2, sep3, sep4, sep5;

   if (sscanf(str.c_str(), "%2x%c%2x%c%2x%c%2x%c%2x%c%2x",
              &b[0], &sep1, &b[1], &sep2, &b[2], &sep3, &b[3], &sep4, &b[4], &sep5, &b[5]) != 11)
      return false;

   for (char s : { sep1, sep2, sep3, sep4, sep5 })
      if (s != ':' && s != '-')
         return false;

   for (int i = 0; i < 6; i++)
      mac[i] = b[i];

   return true;
}

//***************************************************************************
// Resolve host to IPv4
//***************************************************************************

static bool resolveIPv4(const std::string& host, in_addr& addr)
{
   addrinfo hints {};
   addrinfo* res {};

   hints.ai_family = AF_INET;

   if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res)
      return false;

   addr = ((sockaddr_in*)res->ai_addr)->sin_addr;
   freeaddrinfo(res);

   return true;
}

//***************************************************************************
// ARP Lookup - only possible if the host was reachable a short time ago
//***************************************************************************

static std::string arpLookup(const in_addr& addr)
{
   FILE* fp = fopen("/proc/net/arp", "r");

   if (!fp)
      return "";

   char line[256];
   char ip[64], hwType[16], flags[16], mac[32], mask[16], dev[32];
   std::string result;

   if (!fgets(line, sizeof(line), fp))      // skip header
   {
      fclose(fp);
      return "";
   }

   while (fgets(line, sizeof(line), fp))
   {
      if (sscanf(line, "%63s %15s %15s %31s %15s %31s", ip, hwType, flags, mac, mask, dev) != 6)
         continue;

      in_addr a;

      if (inet_pton(AF_INET, ip, &a) != 1 || a.s_addr != addr.s_addr)
         continue;

      if (!(strtol(flags, nullptr, 16) & 0x2) || strcmp(mac, "00:00:00:00:00:00") == 0)    // ATF_COM
         continue;

      result = mac;
      break;
   }

   fclose(fp);

   return result;
}

//***************************************************************************
// Probe - TCP connect with timeout (fills the ARP table on the way)
//***************************************************************************

static bool probeTcp(const in_addr& addr, int port, int timeoutMs)
{
   int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);

   if (fd < 0)
      return false;

   sockaddr_in sa {};
   sa.sin_family = AF_INET;
   sa.sin_port = htons(port);
   sa.sin_addr = addr;

   bool ok {false};

   if (connect(fd, (sockaddr*)&sa, sizeof(sa)) == 0)
      ok = true;
   else if (errno == EINPROGRESS)
   {
      pollfd pfd { fd, POLLOUT, 0 };

      if (poll(&pfd, 1, timeoutMs) == 1)
      {
         int err {0};
         socklen_t len = sizeof(err);
         getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
         ok = err == 0 || err == ECONNREFUSED;    // refused: host is up, port closed
      }
   }

   close(fd);

   return ok;
}

//***************************************************************************
// Interface whose subnet contains 'addr' (name and broadcast address)
//***************************************************************************

static bool interfaceFor(const in_addr& addr, std::string& ifName, in_addr& bcast)
{
   bool found {false};
   ifaddrs* ifaddr {};

   bcast.s_addr = INADDR_BROADCAST;

   if (getifaddrs(&ifaddr) == -1)
      return false;

   for (ifaddrs* ifa = ifaddr; ifa; ifa = ifa->ifa_next)
   {
      if (!ifa->ifa_addr || !ifa->ifa_netmask || ifa->ifa_addr->sa_family != AF_INET)
         continue;

      in_addr ip = ((sockaddr_in*)ifa->ifa_addr)->sin_addr;
      in_addr mask = ((sockaddr_in*)ifa->ifa_netmask)->sin_addr;

      if ((ip.s_addr & mask.s_addr) == (addr.s_addr & mask.s_addr))
      {
         bcast.s_addr = (ip.s_addr & mask.s_addr) | ~mask.s_addr;
         ifName = ifa->ifa_name;
         found = true;
         break;
      }
   }

   freeifaddrs(ifaddr);

   return found;
}

//***************************************************************************
// Magic Packet
//   1. as ethernet frame with ethertype 0x0842 (like etherwake), some NICs
//      wake only on this one, needs CAP_NET_RAW and the interface
//   2. additional as UDP broadcast (port 9)
//***************************************************************************

static void magicPayload(const unsigned char mac[6], unsigned char packet[102])
{
   memset(packet, 0xff, 6);

   for (int i = 1; i <= 16; i++)
      memcpy(packet + i * 6, mac, 6);
}

static int sendMagicFrame(const unsigned char mac[6], const std::string& ifName)
{
   unsigned char packet[102];
   magicPayload(mac, packet);

   unsigned int ifIndex = if_nametoindex(ifName.c_str());

   if (!ifIndex)
   {
      tell(eloAlways, "Error: WOL, unknown interface '%s'", ifName.c_str());
      return fail;
   }

   int fd = socket(AF_PACKET, SOCK_DGRAM, htons(0x0842));     // kernel builds the ethernet header

   if (fd < 0)
   {
      tell(eloAlways, "Error: WOL, creating packet socket failed, %s", strerror(errno));
      return fail;
   }

   sockaddr_ll sll {};
   sll.sll_family = AF_PACKET;
   sll.sll_protocol = htons(0x0842);
   sll.sll_ifindex = ifIndex;
   sll.sll_halen = ETH_ALEN;
   memset(sll.sll_addr, 0xff, ETH_ALEN);                     // broadcast

   int status = sendto(fd, packet, sizeof(packet), 0, (sockaddr*)&sll, sizeof(sll)) == sizeof(packet) ? success : fail;

   if (status != success)
      tell(eloAlways, "Error: WOL, sending ethernet frame on '%s' failed, %s", ifName.c_str(), strerror(errno));

   close(fd);

   return status;
}

static int sendMagicUdp(const unsigned char mac[6], const in_addr& bcast)
{
   unsigned char packet[102];
   magicPayload(mac, packet);

   int fd = socket(AF_INET, SOCK_DGRAM, 0);

   if (fd < 0)
      return fail;

   int on {1};
   setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on));

   sockaddr_in sa {};
   sa.sin_family = AF_INET;
   sa.sin_port = htons(9);
   sa.sin_addr = bcast;

   int status = sendto(fd, packet, sizeof(packet), 0, (sockaddr*)&sa, sizeof(sa)) == sizeof(packet) ? success : fail;

   if (status != success)
      tell(eloAlways, "Error: WOL, sending UDP packet failed, %s", strerror(errno));

   close(fd);

   return status;
}

//***************************************************************************
// Perform VDR Wakeup
//   { "event" : "vdrwakeup", "object" : { "name" : "Wohnzimmer" } }
//   the power off goes from the WEBIF direct to osd2web, it is only reported for the log
//   { "event" : "vdrwakeup", "object" : { "name" : "Wohnzimmer", "action" : "poweroff" } }
//***************************************************************************

int HomeCtl::performVdrWakeup(json_t* obj, long client)
{
   std::string name {getStringFromJson(obj, "name", "")};
   std::string action {getStringFromJson(obj, "action", "wakeup")};
   std::string user {wsClients[(void*)client].user};
   std::vector<std::string> entries;

   if (action == "poweroff")
   {
      tell(eloAlways, "VDR: Power off of '%s' by user '%s'", name.c_str(), user.c_str());
      return done;
   }

   getConfigItem("vdr", entries);

   for (const auto& e : entries)
   {
      VdrEntry vdr {parseVdrEntry(e)};

      if (vdr.name != name)
         continue;

      in_addr addr {};
      bool resolved {resolveIPv4(vdr.host, addr)};
      std::string mac {vdr.mac};

      if (mac.empty() && resolved)
         mac = arpLookup(addr);

      unsigned char hw[6];

      if (!parseMac(mac, hw))
         return replyResult(fail, client, "Keine gültige MAC für VDR '%s' bekannt, bitte im Setup eintragen", name.c_str());

      std::string ifName;
      in_addr bcast;
      bcast.s_addr = INADDR_BROADCAST;

      if (resolved)
         interfaceFor(addr, ifName, bcast);

      tell(eloAlways, "VDR: Sending wakeup to '%s' (%s) via '%s' / %s, by user '%s'", name.c_str(), mac.c_str(),
           ifName.empty() ? "-" : ifName.c_str(), inet_ntoa(bcast), user.c_str());

      int frameStatus {ifName.empty() ? fail : sendMagicFrame(hw, ifName)};
      int udpStatus {sendMagicUdp(hw, bcast)};

      if (frameStatus != success && udpStatus != success)
         return replyResult(fail, client, "Wakeup an VDR '%s' fehlgeschlagen", name.c_str());

      return done;     // no dialog, the remote shows the connection state
   }

   return replyResult(fail, client, "VDR '%s' nicht konfiguriert", name.c_str());
}

//***************************************************************************
// VDR Learn MACs
//   called after the config 'vdr' was stored, for entries without MAC the
//   MAC is looked up (works if the VDR is running) and stored in the entry
//***************************************************************************

int HomeCtl::vdrLearnMacs(std::string& info)
{
   std::vector<std::string> entries;
   bool changed {false};

   getConfigItem("vdr", entries);

   for (auto& e : entries)
   {
      VdrEntry vdr {parseVdrEntry(e)};

      if (!vdr.mac.empty())
      {
         unsigned char hw[6];

         if (!parseMac(vdr.mac, hw))
            info += "<br/>VDR '" + vdr.name + "': MAC '" + vdr.mac + "' ungültig";

         continue;
      }

      in_addr addr {};

      if (!resolveIPv4(vdr.host, addr))
      {
         info += "<br/>VDR '" + vdr.name + "': Host '" + vdr.host + "' nicht auflösbar";
         continue;
      }

      std::string mac {arpLookup(addr)};

      if (mac.empty())
      {
         size_t pos = vdr.url.find(':');
         int port = pos != std::string::npos ? atoi(vdr.url.c_str() + pos + 1) : 80;

         if (probeTcp(addr, port > 0 ? port : 80, 500))
            mac = arpLookup(addr);
      }

      if (mac.empty())
      {
         info += "<br/>VDR '" + vdr.name + "': MAC nicht ermittelbar (VDR aus?), für den Wakeup bitte eintragen";
         continue;
      }

      tell(eloAlways, "VDR: Learned MAC '%s' of '%s'", mac.c_str(), vdr.name.c_str());
      info += "<br/>VDR '" + vdr.name + "': MAC " + mac + " ermittelt";
      vdr.mac = mac;
      e = vdrEntryString(vdr);
      changed = true;
   }

   if (changed)
      setConfigItem("vdr", entries);

   return done;
}
