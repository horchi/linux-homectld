//***************************************************************************
// Automation Control
// File p4fill.c
// This code is distributed under the terms and conditions of the
// GNU GENERAL PUBLIC LICENSE. See the file LICENSE for details.
// Date 16.11.2026 - Jörg Wendel
//
// Dynamic pellet filling of the Fröling S 3200 - the glue to the daemon, the logic is in p4fill.h
//
//  - every minute (p4Check) the decision with the state of the control (<base>/state), the level
//    (sensor P4VA 'Füllstand im Pelletsbehälter') and the times of the control
//  - the times are read by 'parget' at the start, every 30 minutes and after an error (someone may
//    change them in the service menu), written by 'parset' ("HH:MM", user 'pellet-fill'), the requests
//    run beside the ones of the WEBIF (P4Pending.fill)
//  - the state (last empty, day of the standard times, run empty) and the last changes are kept in
//    hidden config items, the page Pellets shows them (p4Fill2Json)
//***************************************************************************

#include "daemon.h"

static constexpr int p4FillInterval {60};           // [s] the decision
static constexpr int p4FillReadTimes {30 * 60};     // [s] read the times of the control again
static constexpr int p4FillLevelMaxAge {300};       // [s] the level has to be younger
static constexpr size_t p4FillLogMax {1400};        // [bytes] of the log (config value)

//***************************************************************************
// P4 Fill Read Config
//***************************************************************************

int HomeCtl::p4FillReadConfig()
{
   P4Fill::Config& cfg {p4FillConfig};
   std::string t1, t2, states;
   long lastZero {0};

   getConfigItem("p4FillEnabled", cfg.enabled, false);
   getConfigItem("p4FillTime1", t1, "16:00");
   getConfigItem("p4FillTime2", t2, "00:00");
   getConfigItem("p4FillMinLevel", cfg.minLevel, 20);
   getConfigItem("p4FillHold", cfg.holdMinutes, 5);
   getConfigItem("p4FillDaysToZero", cfg.daysToZero, 8);
   getConfigItem("p4FillDelayStates", states, "Vorbereitung,Vorwärmen,Zünden,Heizen");
   getConfigItem("p4FillAddress1", cfg.address[0], 60);
   getConfigItem("p4FillAddress2", cfg.address[1], 516);
   getConfigItem("p4FillLevelAddress", p4FillLevelAddress, 0x71);

   getConfigItem("p4FillLastZero", lastZero, 0);
   getConfigItem("p4FillResetDay", p4FillState.resetDay, "");
   getConfigItem("p4FillToZero", p4FillState.toZero, false);
   getConfigItem("p4FillLog", p4FillLog, "[]");
   p4FillState.lastZero = lastZero;

   cfg.stdTime[0] = P4Fill::str2Minutes(t1);
   cfg.stdTime[1] = P4Fill::str2Minutes(t2);

   for (int slot = 0; slot < 2; slot++)
   {
      if (cfg.stdTime[slot] < 0)
      {
         tell(eloAlways, "Error: P4 fill: Invalid standard time '%s' of the %d. filling (HH:MM), filling disabled",
              slot ? t2.c_str() : t1.c_str(), slot + 1);
         cfg.enabled = false;
      }
   }

   cfg.delayStates.clear();

   for (const auto& s : split(states, ','))
   {
      size_t b {s.find_first_not_of(' ')};
      size_t e {s.find_last_not_of(' ')};

      if (b != std::string::npos)
         cfg.delayStates.push_back(s.substr(b, e - b + 1));
   }

   // the addresses may have changed

   p4FillTimes[0] = p4FillTimes[1] = -1;

   if (cfg.enabled)
      tell(eloAlways, "P4 fill: enabled, standard times %s / %s, fill below %d%%, run empty after %d days",
           P4Fill::minutes2Str(cfg.stdTime[0]).c_str(), P4Fill::minutes2Str(cfg.stdTime[1]).c_str(),
           cfg.minLevel, cfg.daysToZero);

   return done;
}

//***************************************************************************
// P4 Fill Check
//   called by p4Check(), the decision once a minute
//***************************************************************************

int HomeCtl::p4FillCheck()
{
   if (!p4FillConfig.enabled || !p4Online || time(0) < p4FillCheckAt + p4FillInterval)
      return done;

   // a request in work -> wait for it

   for (const auto& [rid, pending] : p4Pending)
      if (pending.fill)
         return done;

   p4FillCheckAt = time(0);

   // the times of the control

   if (p4FillTimes[0] < 0 || p4FillTimes[1] < 0 || time(0) > p4FillTimesAt + p4FillReadTimes)
   {
      tell(eloDetail, "P4 fill: Reading the times of the control");
      p4FillSend("parget", 0, -1, "");
      p4FillSend("parget", 1, -1, "");
      return done;
   }

   // state and level

   if (p4StateInfo.empty())
      return done;

   const auto it {sensors.find("P4VA")};
   const SensorData* level {it != sensors.end() && it->second.count(p4FillLevelAddress) ? &it->second.at(p4FillLevelAddress) : nullptr};

   if (!level || !level->valid() || time(0) - level->last() > p4FillLevelMaxAge)
   {
      if (!p4FillNoLevel)
         tell(eloAlways, "Warning: P4 fill: No current value of the level (P4VA 0x%x), is the sensor active? Waiting", p4FillLevelAddress);

      p4FillNoLevel = true;
      return done;
   }

   p4FillNoLevel = false;

   P4Fill::Input in;
   struct tm tm {};
   char today[20];

   in.now = time(0);
   localtime_r(&in.now, &tm);
   strftime(today, sizeof(today), "%Y-%m-%d", &tm);
   in.today = today;
   in.minute = tm.tm_hour * 60 + tm.tm_min;
   in.stateInfo = p4StateInfo;
   in.level = level->value;
   in.times[0] = p4FillTimes[0];
   in.times[1] = p4FillTimes[1];

   P4Fill::Result res {P4Fill::decide(p4FillConfig, in, p4FillState)};

   for (const auto& text : res.log)
      p4FillAddLog(text);

   if (res.persist)
      p4FillPersist();

   for (const auto& a : res.actions)
      p4FillSend("parset", a.slot, a.minutes, a.reason);

   return done;
}

//***************************************************************************
// P4 Fill Send
//   'parget' or 'parset' of the time of a filling, the answer comes to p4FillReply()
//***************************************************************************

int HomeCtl::p4FillSend(const char* cmd, int slot, int minutes, const std::string& reason)
{
   if (mqttCheckConnection() != success || !mqttWriter || !mqttWriter->isConnected())
      return fail;

   json_t* oCmd {json_object()};
   char rid[40];

   snprintf(rid, sizeof(rid), "homectld-%u", ++p4Rid);

   json_object_set_new(oCmd, "cmd", json_string(cmd));
   json_object_set_new(oCmd, "rid", json_string(rid));
   json_object_set_new(oCmd, "address", json_integer(p4FillConfig.address[slot]));

   if (strcmp(cmd, "parset") == 0)
   {
      json_object_set_new(oCmd, "value", json_string(P4Fill::minutes2Str(minutes).c_str()));
      json_object_set_new(oCmd, "user", json_string("pellet-fill"));
   }

   char* message {json_dumps(oCmd, JSON_COMPACT)};
   int status {mqttWriter->write((p4Topic + "/cmd").c_str(), message)};

   tell(eloMqtt, "-> (p4mqttd) '%s/cmd' [%s]", p4Topic.c_str(), message);

   P4Pending pending;

   pending.at = time(0);
   pending.cmd = cmd;
   pending.timeout = 60;
   pending.request = message;
   pending.user = "pellet-fill";
   pending.fill = true;
   pending.slot = slot;
   pending.reason = reason;

   free(message);
   json_decref(oCmd);

   if (status != success)
   {
      tell(eloAlways, "Error: P4 fill: Sending '%s' to p4mqttd failed", cmd);
      return fail;
   }

   p4Pending[rid] = pending;

   return done;
}

//***************************************************************************
// P4 Fill Reply
//***************************************************************************

int HomeCtl::p4FillReply(const std::string& cmd, int slot, const std::string& reason, json_t* jMsg)
{
   std::string status {getStringFromJson(jMsg, "status", "")};
   std::string value {getStringFromJson(jMsg, "value", "")};
   int minutes {P4Fill::str2Minutes(value)};

   if (slot < 0 || slot > 1)
      return fail;

   if (cmd == "parget")
   {
      if (status != "ok" || minutes < 0)
      {
         tell(eloAlways, "Error: P4 fill: Reading the %d. filling (address %d) failed, status '%s' value '%s' (%s)",
              slot + 1, p4FillConfig.address[slot], status.c_str(), value.c_str(), getStringFromJson(jMsg, "message", ""));
         p4FillTimes[slot] = -1;
         return fail;
      }

      if (p4FillTimes[slot] >= 0 && p4FillTimes[slot] != minutes)
         p4FillAddLog(std::to_string(slot + 1) + ". Befüllung an der Heizung geändert: "
                      + P4Fill::minutes2Str(p4FillTimes[slot]) + " -> " + value);

      p4FillTimes[slot] = minutes;
      p4FillTimesAt = time(0);

      return done;
   }

   // parset

   if ((status == "ok" || status == "nochange") && minutes >= 0)
   {
      p4FillTimes[slot] = minutes;
      p4FillAddLog(std::to_string(slot + 1) + ". Befüllung " + getStringFromJson(jMsg, "old", "?") + " -> " + value
                   + (reason.empty() ? "" : " (" + reason + ")"));
      return done;
   }

   p4FillAddLog(std::to_string(slot + 1) + ". Befüllung setzen fehlgeschlagen: " + status
                + " " + getStringFromJson(jMsg, "message", ""));
   p4FillTimes[slot] = -1;                       // read it again

   return fail;
}

//***************************************************************************
// P4 Fill Add Log
//   the last changes as JSON array in a hidden config item, newest first
//***************************************************************************

int HomeCtl::p4FillAddLog(const std::string& text)
{
   tell(eloAlways, "P4 fill: %s", text.c_str());

   json_t* jLog {jsonLoad(p4FillLog.c_str(), 0, true)};

   if (!jLog || !json_is_array(jLog))
   {
      if (jLog)
         json_decref(jLog);

      jLog = json_array();
   }

   json_t* jEntry {json_object()};
   json_object_set_new(jEntry, "time", json_integer(time(0)));
   json_object_set_new(jEntry, "text", json_string(text.c_str()));
   json_array_insert_new(jLog, 0, jEntry);

   char* p {json_dumps(jLog, JSON_COMPACT)};

   while (p && strlen(p) > p4FillLogMax && json_array_size(jLog) > 1)
   {
      free(p);
      json_array_remove(jLog, json_array_size(jLog) - 1);
      p = json_dumps(jLog, JSON_COMPACT);
   }

   p4FillLog = p ? p : "[]";
   free(p);
   json_decref(jLog);

   setConfigItem("p4FillLog", p4FillLog.c_str());

   return done;
}

//***************************************************************************
// P4 Fill Persist
//***************************************************************************

int HomeCtl::p4FillPersist()
{
   setConfigItem("p4FillLastZero", (long)p4FillState.lastZero);
   setConfigItem("p4FillResetDay", p4FillState.resetDay.c_str());
   setConfigItem("p4FillToZero", p4FillState.toZero);

   return done;
}

//***************************************************************************
// P4 Fill 2 Json
//   the state for the page Pellets
//***************************************************************************

int HomeCtl::p4Fill2Json(json_t* obj)
{
   const P4Fill::Config& cfg {p4FillConfig};
   json_t* oFill {json_object()};
   json_t* jTimes {json_array()};
   json_t* jStd {json_array()};

   json_object_set_new(obj, "fill", oFill);
   json_object_set_new(oFill, "enabled", json_boolean(cfg.enabled));

   for (int slot = 0; slot < 2; slot++)
   {
      json_array_append_new(jTimes, p4FillTimes[slot] < 0 ? json_null() : json_string(P4Fill::minutes2Str(p4FillTimes[slot]).c_str()));
      json_array_append_new(jStd, json_string(P4Fill::minutes2Str(cfg.stdTime[slot]).c_str()));
   }

   json_object_set_new(oFill, "times", jTimes);
   json_object_set_new(oFill, "stdTimes", jStd);
   json_object_set_new(oFill, "minLevel", json_integer(cfg.minLevel));
   json_object_set_new(oFill, "daysToZero", json_integer(cfg.daysToZero));
   json_object_set_new(oFill, "state", json_string(p4StateInfo.c_str()));
   json_object_set_new(oFill, "toZero", json_boolean(p4FillState.toZero));

   if (p4FillState.lastZero)
   {
      json_object_set_new(oFill, "lastZero", json_integer(p4FillState.lastZero));

      if (cfg.daysToZero > 0)
         json_object_set_new(oFill, "nextZero", json_integer(p4FillState.lastZero + (time_t)cfg.daysToZero * tmeSecondsPerDay));
   }

   const auto it {sensors.find("P4VA")};

   if (it != sensors.end() && it->second.count(p4FillLevelAddress) && it->second.at(p4FillLevelAddress).valid())
      json_object_set_new(oFill, "level", json_real(it->second.at(p4FillLevelAddress).value));

   json_t* jLog {jsonLoad(p4FillLog.c_str(), 0, true)};
   json_object_set_new(oFill, "log", jLog && json_is_array(jLog) ? jLog : json_array());

   if (jLog && !json_is_array(jLog))
      json_decref(jLog);

   return done;
}
