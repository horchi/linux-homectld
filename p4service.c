/*
 *  p4service.c
 *
 *  (c) 2026 Jörg Wendel
 *
 * This code is distributed under the terms and conditions of the
 * GNU GENERAL PUBLIC LICENSE. See the file COPYING for details.
 *
 *  Fröling S 3200 via the service p4mqttd (see p4mqttd/README.md),
 *  config 'p4Enabled', MQTT base topic 'p4Topic' (default 'p4'):
 *
 *    <base>/status     (retained, from the service) online / comm state
 *    <base>/state      (retained) state and mode of the control, a change of the state sends the state mail
 *    <base>/errors     (retained) the error list of the control, merged into the table 'p4errors', new errors are mailed
 *    <base>/cmd        (to the service) {"cmd":"menu","rid":"m1","parent":1}
 *    <base>/reply      (from the service) the answer to a command, same 'rid'
 *    <base>/sensor     the sensors P4VA, P4DI, P4DO, P4AO, P4UD (dispatchOther, like any other sensor)
 *    <base>/config     (retained, to the service) intervals, time synchronization and the active sensors,
 *                      the service polls only these (the first sweep after its start covers all sensors,
 *                      so they get known here and can be activated in the sensor setup)
 */

#include "daemon.h"

//***************************************************************************
// P4 Read Config
//   returns true if the connection to the broker has to be renewed (subscriptions)
//***************************************************************************

bool HomeCtl::p4ReadConfig()
{
   bool enabled {p4Enabled};
   std::string topic {p4Topic};

   getConfigItem("p4Enabled", p4Enabled, false);
   getConfigItem("p4Topic", p4Topic, "p4");
   getConfigItem("p4PageTitle", p4PageTitle, "S3200");
   getConfigItem("p4HeatingType", p4HeatingType, "P4");
   getConfigItem("p4Mail", p4Mail, false);
   getConfigItem("p4ConsumptionPerHour", p4ConsumptionPerHour, 0.0);
   getConfigItem("p4StateMailStates", p4StateMailStates, "0,1,3,19");
   getConfigItem("p4PollInterval", p4PollInterval, 60);
   getConfigItem("p4StateInterval", p4StateInterval, 10);
   getConfigItem("p4TimeSync", p4TimeSync, false);
   getConfigItem("p4MaxTimeLeak", p4MaxTimeLeak, 10);

   // the config is sent (again) to the service with the next check

   p4ConfigSent.clear();

   if (!p4Enabled)
      return enabled != p4Enabled;

   tell(eloAlways, "Adding the topics of the S 3200 service p4mqttd ('%s/...')", p4Topic.c_str());

   // '/cmd' and '/reply/#' (the answers for scripts) only to remove retained messages, see dispatchP4Mqtt()

   for (const char* sub : { "/sensor", "/status", "/reply", "/reply/#", "/cmd", "/state", "/errors" })
      mqttSensorTopics.push_back(p4Topic + sub);

   return enabled != p4Enabled || topic != p4Topic;
}

//***************************************************************************
// P4 Config 2 Json
//***************************************************************************

std::string HomeCtl::p4Config2Json()
{
   json_t* jConfig {json_object()};
   json_t* jActive {json_object()};

   json_object_set_new(jConfig, "pollInterval", json_integer(p4PollInterval));
   json_object_set_new(jConfig, "stateInterval", json_integer(p4StateInterval));
   json_object_set_new(jConfig, "tsync", json_boolean(p4TimeSync));
   json_object_set_new(jConfig, "maxTimeLeak", json_integer(p4MaxTimeLeak));
   json_object_set_new(jConfig, "iconSet", json_string(iconSet.empty() ? "light" : iconSet.c_str()));     // the images of the state of the control
   json_object_set_new(jConfig, "heatingType", json_string(p4HeatingType.c_str()));
   json_object_set_new(jConfig, "active", jActive);

   // the types always in the message, also if no sensor of it is active (then nothing is polled)

   for (const char* type : { "P4VA", "P4DI", "P4DO", "P4AO" })
   {
      json_t* jList {json_array()};
      json_object_set_new(jActive, type, jList);

      const auto it {sensors.find(type)};

      if (it == sensors.end())
         continue;

      for (const auto& [address, sensor] : it->second)
         if (sensor.active)
            json_array_append_new(jList, json_integer(address));
   }

   char* message {json_dumps(jConfig, JSON_COMPACT)};
   std::string s {message};

   free(message);
   json_decref(jConfig);

   return s;
}

//***************************************************************************
// P4 Check
//   publish the config (retained) to the service at the start, after a reconnect
//   and whenever it changed (e.g. a sensor activated)
//***************************************************************************

int HomeCtl::p4Check()
{
   static time_t lastCheck {0};

   p4CheckPending();

   // the duration of the states of today, every minute

   if (p4Enabled && time(0) >= p4StateDurationAt + tmeSecondsPerMinute)
   {
      p4StateDurationAt = time(0);
      p4CalcStateDuration();
   }

   if (!p4Enabled || !mqttWriter || !mqttWriter->isConnected())
      return done;

   if (time(0) < lastCheck + 5)
      return done;

   lastCheck = time(0);

   std::string config {p4Config2Json()};

   if (config == p4ConfigSent)
      return done;

   tell(eloAlways, "Info: Sending the config to p4mqttd [%s]", config.c_str());

   if (mqttWriter->writeRetained((p4Topic + "/config").c_str(), config.c_str()) == success)
      p4ConfigSent = config;

   return done;
}

//***************************************************************************
// P4 Answer
//   an error to the WEBIF page of the service menu (it shows the message instead of the items)
//***************************************************************************

int HomeCtl::p4Answer(long client, const char* error, const char* event)
{
   json_t* oJson {json_object()};

   json_object_set_new(oJson, "error", json_string(error));
   pushOutMessage(oJson, event, client);

   return done;
}

//***************************************************************************
// P4 Raw Answer
//   an error to the tab 'Kommandos' of the WEBIF (event 'p4raw')
//***************************************************************************

int HomeCtl::p4RawAnswer(long client, const char* tag, const char* error)
{
   json_t* oJson {json_object()};

   json_object_set_new(oJson, "tag", json_string(tag));
   json_object_set_new(oJson, "error", json_string(error));
   pushOutMessage(oJson, "p4raw", client);

   return done;
}

//***************************************************************************
// P4 Check Pending
//   requests the service didn't answer
//***************************************************************************

int HomeCtl::p4CheckPending()
{
   for (auto it = p4Pending.begin(); it != p4Pending.end(); )
   {
      if (time(0) - it->second.at > it->second.timeout)
      {
         tell(eloAlways, "Warning: p4mqttd didn't answer '%s' (%s)", it->second.cmd.c_str(), it->first.c_str());
         if (it->second.raw)
            p4RawAnswer(it->second.client, it->second.tag.c_str(), "Keine Antwort des Dienstes p4mqttd");
         else
            p4Answer(it->second.client, "Keine Antwort des Dienstes p4mqttd", it->second.cmd == "errors" ? "p4errors" : "p4menu");
         it = p4Pending.erase(it);
      }
      else
         ++it;
   }

   return done;
}

//***************************************************************************
// Dispatch P4 MQTT (status and replies of the service)
//***************************************************************************

int HomeCtl::dispatchP4Mqtt(const char* topic, const char* message)
{
   std::string sub {topic};
   sub = sub.substr(p4Topic.length() + 1);

   // Commands and answers are never retained. If a client published one retained (by mistake) it would stay
   // at the broker for ever, so it is removed there. Anything else on these topics isn't for us: the commands
   // (of us and of scripts) and the answers on the topics of scripts ('replyTo') are read and dropped.

   bool isCmd {sub == "cmd"};
   bool isReply {sub == "reply" || sub.rfind("reply/", 0) == 0};

   if ((isCmd || isReply) && mqttReader && mqttReader->isRetained())
   {
      tell(eloAlways, "Warning: p4mqttd: Removing the retained message of '%s' at the broker [%s]", topic, message);

      if (mqttWriter && mqttWriter->isConnected())
         mqttWriter->writeRetained(topic, nullptr);

      return done;
   }

   if (isCmd || (isReply && sub != "reply"))
      return done;

   json_t* jMsg {jsonLoad(message, 0, true)};

   if (!jMsg)
   {
      // an answer which isn't JSON can't be assigned to a request, the others are messages of the service

      if (isReply)
         tell(eloDebug, "Debug: p4mqttd: dropped '%s', no JSON [%s]", topic, message);
      else
         tell(eloAlways, "Error: p4mqttd: Can't parse '%s' [%s]", topic, message);

      return isReply ? done : fail;
   }

   if (sub == "status")
   {
      bool online {getBoolFromJson(jMsg, "online", false) && std::string(getStringFromJson(jMsg, "comm", "")) == "ok"};

      if (online != p4Online)
         tell(eloAlways, "Info: The S 3200 service p4mqttd is %s (comm '%s')", online ? "online" : "not available",
              getStringFromJson(jMsg, "comm", ""));

      p4Online = online;
   }

   else if (sub == "state")
   {
      int state {getIntFromJson(jMsg, "state", -1)};
      std::string info {getStringFromJson(jMsg, "stateinfo", "")};

      // the first message after our start is no change (it's retained)

      if (p4LastState != -1 && state != -1 && state != p4LastState)
      {
         tell(eloAlways, "Info: The state of the S 3200 changed from %d to %d '%s'", p4LastState, state, info.c_str());

         if (p4Mail && sendMails && p4IsMailState(state))
            p4SendStateMail(info.c_str(), state);
      }

      p4LastState = state;
      p4StateInfo = info;
   }

   else if (sub == "errors")
   {
      p4MergeErrors(getObjectFromJson(jMsg, "errors"));

      if (p4Mail && sendMails)
         p4SendErrorMail();
   }

   else if (sub == "reply")
   {
      std::string rid {getStringFromJson(jMsg, "rid", "")};
      auto it {p4Pending.find(rid)};

      if (it == p4Pending.end())
      {
         // not our request (a script, a missing 'rid') or already timed out: dropped

         tell(eloDebug, "Debug: p4mqttd: dropped the reply for the unknown request '%s'", rid.c_str());
      }
      else
      {
         long client {it->second.client};
         std::string cmd {it->second.cmd};
         P4Pending pending {it->second};

         p4Pending.erase(it);

         std::string status {getStringFromJson(jMsg, "status", "")};
         std::string text {getStringFromJson(jMsg, "message", "")};

         if (pending.raw)
         {
            // the tab 'Kommandos': the answer as it is, with the message which was sent

            if (cmd == "parset" || cmd == "timeset")
               tell(eloAlways, "P4: '%s' by '%s' address 0x%x: '%s' -> '%s', result '%s'", cmd.c_str(), pending.user.c_str(),
                    getIntFromJson(jMsg, "address", 0), getStringFromJson(jMsg, "old", ""), getStringFromJson(jMsg, "value", ""), status.c_str());

            json_t* oJson {json_object()};
            json_t* jRequest {jsonLoad(pending.request.c_str())};

            json_object_set_new(oJson, "tag", json_string(pending.tag.c_str()));
            json_object_set_new(oJson, "request", jRequest ? jRequest : json_null());
            json_object_set_new(oJson, "reply", json_deep_copy(jMsg));
            json_object_set_new(oJson, "duration", json_integer(time(0) - pending.at));
            pushOutMessage(oJson, "p4raw", client);
         }
         else if (cmd == "menu")
         {
            if (status != "ok")
               p4Answer(client, text.empty() ? ("Fehler: " + status).c_str() : text.c_str());
            else
            {
               json_t* oJson {json_deep_copy(jMsg)};

               json_object_del(oJson, "rid");
               json_object_del(oJson, "cmd");
               json_object_del(oJson, "status");
               pushOutMessage(oJson, "p4menu", client);
            }
         }
         else if (cmd == "errors")
         {
            if (status == "ok")
            {
               p4MergeErrors(getObjectFromJson(jMsg, "errors"));

               if (p4Mail && sendMails)
                  p4SendErrorMail();

               json_t* oJson {json_object()};
               p4Errors2Json(oJson);
               pushOutMessage(oJson, "p4errors", client);
            }
            else
               p4Answer(client, text.empty() ? "Fehler beim Lesen der Fehlerliste" : text.c_str());
         }
         else
         {
            // a change or a command: tell the result and show the menu level again (this also ends the progress dialog)

            const char* user {pending.user.c_str()};

            if (cmd == "parset" || cmd == "timeset")
            {
               std::string old {getStringFromJson(jMsg, "old", "")};
               std::string now {getStringFromJson(jMsg, "value", "")};

               tell(eloAlways, "P4: '%s' by '%s' address 0x%x: '%s' -> '%s', result '%s'", cmd.c_str(), user,
                    getIntFromJson(jMsg, "address", 0), old.c_str(), now.c_str(), status.c_str());

               if (status == "ok")
                  replyResult(success, client, "Gespeichert: %s -> %s", old.c_str(), now.c_str());
               else if (status == "nochange")
                  replyResult(success, client, "Wert unverändert");
               else
                  replyResult(fail, client, "%s", text.empty() ? ("Fehler: " + status).c_str() : text.c_str());
            }
            else if (cmd == "initmenu")
            {
               if (status == "ok")
                  replyResult(success, client, "Menü und Sensoren neu gelesen (%d Menüeinträge, %d Sensoren)",
                              getIntFromJson(jMsg, "menuItems", 0), getIntFromJson(jMsg, "sensors", 0));
               else
                  replyResult(fail, client, "%s", text.empty() ? ("Fehler: " + status).c_str() : text.c_str());
            }
            else if (cmd == "inittimes")
            {
               if (status == "ok")
                  replyResult(success, client, "Zeitprogramme neu gelesen (%d)", getIntFromJson(jMsg, "times", 0));
               else
                  replyResult(fail, client, "%s", text.empty() ? ("Fehler: " + status).c_str() : text.c_str());
            }

            json_t* oMenu {json_object()};

            json_object_set_new(oMenu, "parent", json_integer(pending.parent));
            performP4Menu(oMenu, client);
            json_decref(oMenu);
         }
      }
   }

   json_decref(jMsg);

   return done;
}

//***************************************************************************
// Perform P4 Menu (WEBIF)
//   { "parent": 1 }  -> the items of this level of the service menu with their values,
//   the answer comes with the event 'p4menu' ({ "items": [...], "parent", "last", "title" } or { "error" })
//***************************************************************************

int HomeCtl::performP4Menu(json_t* obj, long client)
{
   json_t* oCmd {json_object()};

   json_object_set_new(oCmd, "cmd", json_string("menu"));
   json_object_set_new(oCmd, "parent", json_integer(getIntFromJson(obj, "parent", 1)));

   return p4SendCommand(oCmd, client, "menu", getIntFromJson(obj, "parent", 1), 30);
}

//***************************************************************************
// Perform P4 Raw (WEBIF, tab 'Kommandos')
//   a test platform for the command topic of the service: the command is sent as it is typed
//   { "json": "{\"cmd\":\"parget\",\"address\":480}", "tag": "request" }
//   only the 'rid' (needed to assign the answer) and the 'user' (the one logged in) are set here,
//   a 'replyTo' is removed (the answer has to come to the homectld).
//   The answer comes with the event 'p4raw': { "tag", "request": {..as sent..}, "reply": {..as received..} }
//   or { "tag", "error" }. Commands which write to the heating control need the right urSettings.
//***************************************************************************

int HomeCtl::performP4Raw(json_t* obj, long client)
{
   std::string tag {getStringFromJson(obj, "tag", "")};
   const char* text {getStringFromJson(obj, "json", "")};
   json_t* oCmd {jsonLoad(text ? text : "", 0, true)};

   if (!oCmd || !json_is_object(oCmd))
   {
      if (oCmd)
         json_decref(oCmd);

      return p4RawAnswer(client, tag.c_str(), "Die Anfrage ist kein gültiges JSON Objekt");
   }

   std::string cmd {getStringFromJson(oCmd, "cmd", "")};
   std::string user {wsClients[(void*)client].user};
   bool writes {cmd == "parset"};

   json_object_del(oCmd, "replyTo");         // the answer has to come to us (topic <base>/reply)

   if (cmd != "parget" && cmd != "parset" && cmd != "parlist")
   {
      json_decref(oCmd);
      return p4RawAnswer(client, tag.c_str(), "Hier sind nur die Kommandos 'parget', 'parset' und 'parlist' erlaubt (Element \"cmd\")");
   }

   if (writes)
   {
      if (!(wsClients[(void*)client].rights & urSettings))
      {
         json_decref(oCmd);
         return p4RawAnswer(client, tag.c_str(), "Das Schreiben erfordert das Recht 'Einstellungen'");
      }

      // every change is logged with the user who is logged in

      json_object_set_new(oCmd, "user", json_string(user.c_str()));

      char* p {json_dumps(oCmd, JSON_COMPACT)};
      tell(eloAlways, "P4: user '%s' sends '%s' [%s]", user.c_str(), cmd.c_str(), p);
      free(p);
   }

   return p4SendCommand(oCmd, client, cmd.c_str(), 1, 60, tag.c_str());
}

//***************************************************************************
// P4 Send Command
//   to the service, the answer is handled in dispatchP4Mqtt(), takes the reference of oCmd
//   rawTag: a request of the tab 'Kommandos' (performP4Raw), answered with the event 'p4raw'
//***************************************************************************

int HomeCtl::p4SendCommand(json_t* oCmd, long client, const char* what, int parent, int timeout, const char* rawTag)
{
   const char* ev {std::string(what) == "errors" ? "p4errors" : "p4menu"};   // the page which waits for the answer

   auto failed = [&](const char* error)
   {
      json_decref(oCmd);
      return rawTag ? p4RawAnswer(client, rawTag, error) : p4Answer(client, error, ev);
   };

   if (!p4Enabled)
      return failed("Die Anbindung der Heizung ist nicht aktiviert (Konfiguration -> Heizung S 3200)");

   if (mqttCheckConnection() != success || !mqttWriter || !mqttWriter->isConnected())
      return failed("MQTT nicht verbunden");

   if (!p4Online)
      return failed("Der Dienst p4mqttd ist nicht erreichbar oder hat keine Verbindung zur Heizung");

   // the prefix is reserved for the homectld, so the 'rid' of a script can't collide with it

   char rid[40];
   snprintf(rid, sizeof(rid), "homectld-%u", ++p4Rid);

   json_object_set_new(oCmd, "rid", json_string(rid));

   char* message {json_dumps(oCmd, JSON_COMPACT)};
   int status {mqttWriter->write((p4Topic + "/cmd").c_str(), message)};

   tell(eloMqtt, "-> (p4mqttd) '%s/cmd' [%s]", p4Topic.c_str(), message);

   std::string sent {message};

   free(message);

   if (status != success)
      return failed("Senden an den Dienst p4mqttd fehlgeschlagen");

   json_decref(oCmd);

   P4Pending pending;

   pending.client = client;
   pending.at = time(0);
   pending.cmd = what;
   pending.parent = parent;
   pending.timeout = timeout;
   pending.raw = rawTag != nullptr;
   pending.tag = rawTag ? rawTag : "";
   pending.request = sent;
   pending.user = wsClients[(void*)client].user;
   p4Pending[rid] = pending;

   return done;
}

//***************************************************************************
// Perform P4 Command (WEBIF)
//   writes to the heating control, so the rights are checked (urSettings, wsactions.c) and every
//   change is logged with the user:
//   { "what": "parset",  "address": 480, "value": "72", "parent": 5 }
//   { "what": "timeset", "address": 3, "range": 1, "value": "06:00 - 22:00", "parent": 5 }
//   { "what": "initmenu" } | { "what": "inittimes" }      (read again from the control, takes a while)
//   the result comes as 'result' message followed by the menu level (event 'p4menu')
//***************************************************************************

int HomeCtl::performP4Cmd(json_t* obj, long client)
{
   std::string what {getStringFromJson(obj, "what", "")};
   int parent {getIntFromJson(obj, "parent", 1)};
   std::string user {wsClients[(void*)client].user};

   json_t* oCmd {json_object()};

   if (what == "parset" || what == "timeset")
   {
      std::string value {getStringFromJson(obj, "value", "")};

      // only what a number, a time or 'ja' / 'nein' consists of

      if (value.length() > 40 || value.find_first_not_of("0123456789:-,. janeinJANEIN") != std::string::npos)
      {
         json_decref(oCmd);
         return p4Answer(client, "Ungültiger Wert");
      }

      json_object_set_new(oCmd, "address", json_integer(getIntFromJson(obj, "address", -1)));
      json_object_set_new(oCmd, "value", json_string(value.c_str()));
      json_object_set_new(oCmd, "user", json_string(user.c_str()));

      if (what == "timeset")
         json_object_set_new(oCmd, "range", json_integer(getIntFromJson(obj, "range", 0)));

      tell(eloAlways, "P4: user '%s' writes '%s' to %s 0x%x%s", user.c_str(), value.c_str(), what.c_str(),
           getIntFromJson(obj, "address", -1), what == "timeset" ? (" range " + std::to_string(getIntFromJson(obj, "range", 0))).c_str() : "");
   }
   else if (what == "initmenu" || what == "inittimes")
   {
      tell(eloAlways, "P4: user '%s' requests '%s'", user.c_str(), what.c_str());
   }
   else
   {
      json_decref(oCmd);
      return p4Answer(client, "Unbekanntes Kommando");
   }

   json_object_set_new(oCmd, "cmd", json_string(what.c_str()));

   // reading the whole menu and sensors takes minutes

   int timeout {what == "initmenu" ? 900 : what == "inittimes" ? 180 : 60};

   return p4SendCommand(oCmd, client, what.c_str(), parent, timeout);
}

//***************************************************************************
// P4 Error Text of a state of an entry
//***************************************************************************

static const char* p4ErrState2Text(int state)
{
   switch (state)
   {
      case 1:  return "gekommen";
      case 4:  return "gegangen";
      case 2:  return "quittiert";
      default: return "unknown";
   }
}

//***************************************************************************
// P4 Merge Errors
//   the list of the control (oldest first) has up to three entries for one error: came, gone,
//   acknowledged - they are one row of the table 'p4errors' (key: number and time it came)
//***************************************************************************

int HomeCtl::p4MergeErrors(json_t* jErrors)
{
   if (!json_is_array(jErrors))
      return fail;

   // the first import (empty table) is not mailed, the errors are history

   bool initial {!selectAllP4Errors->find()};
   selectAllP4Errors->freeResult();

   time_t timeOne {0};
   size_t index {};
   json_t* jE {};
   int count {0};

   json_array_foreach(jErrors, index, jE)
   {
      int number {getIntFromJson(jE, "number", 0)};
      int info {getIntFromJson(jE, "info", 0)};
      int state {getIntFromJson(jE, "state", 0)};
      time_t t {(time_t)getIntFromJson(jE, "time", 0)};
      const char* text {getStringFromJson(jE, "text", "")};
      const char* timeField {state == 1 ? "TIME1" : state == 2 ? "TIME2" : state == 4 ? "TIME4" : nullptr};

      if (!timeField)
         continue;

      if (state == 1)
         timeOne = t;

      if (!timeOne)
         continue;

      tableP4Errors->clear();
      tableP4Errors->setValue("NUMBER", number);
      tableP4Errors->setValue("TIME1", timeOne);

      bool insert {!selectP4ErrorByKey->find()};

      tableP4Errors->clearChanged();

      if (insert
          || (state == 2 && !tableP4Errors->hasValue("STATE", p4ErrState2Text(2)))
          || (state == 4 && tableP4Errors->hasValue("STATE", p4ErrState2Text(1))))
      {
         tableP4Errors->setValue(timeField, t);
         tableP4Errors->setValue("STATE", p4ErrState2Text(state));
         tableP4Errors->setValue("NUMBER", number);
         tableP4Errors->setValue("INFO", info);
         tableP4Errors->setValue("TEXT", text);

         if (insert && initial)
            tableP4Errors->setValue("MAILCNT", 1);        // history, no mail
      }

      if (insert)
      {
         tableP4Errors->insert();
         count++;
      }
      else if (tableP4Errors->getChanges())
      {
         tableP4Errors->update();
         count++;
      }

      selectP4ErrorByKey->freeResult();

      if (state == 2)
         timeOne = 0;
   }

   if (count)
      tell(eloAlways, "Info: S 3200 error list: %d rows new or changed%s", count, initial ? " (first import)" : "");

   return done;
}

//***************************************************************************
// P4 Mails
//***************************************************************************

// the text goes into a shell command in single quotes

static std::string p4ShellEscape(const std::string& in)
{
   return strReplace("'", "'\\''", in);
}

static std::string p4HtmlEscape(const std::string& in)
{
   std::string out;

   for (char c : in)
   {
      switch (c)
      {
         case '&': out += "&amp;";  break;
         case '<': out += "&lt;";   break;
         case '>': out += "&gt;";   break;
         default:  out += c;
      }
   }

   return out;
}

// the look of a state of the control (the groups of the state images of the p4d)

struct P4StateLook
{
   const char* icon;
   const char* color;         // text and accent
   const char* tint;          // background of the badge
};

static P4StateLook p4StateLook(int state)
{
   if (state == 0)                                                   return { "⚠️", "#d32f2f", "#fdecea" };  // Störung
   if (state == 1 || state == 5)                                     return { "💤", "#6b7280", "#eef0f3" };  // Brenner aus
   if (state == 2 || state == 71 || state == 9)                      return { "🌡️", "#e8710a", "#fff1e3" };  // Anheizen
   if (state == 3)                                                   return { "🔥", "#e8590c", "#ffe8d9" };  // Heizen
   if (state == 4)                                                   return { "🔥", "#f0a04b", "#fff4e5" };  // Feuerhaltung
   if (state == 6)                                                   return { "🚪", "#b7791f", "#fef6e4" };  // Tür offen
   if (state == 7)                                                   return { "⚙️", "#2b7bbf", "#e8f2fb" };  // Vorbereitung
   if (state == 8)                                                   return { "♨️", "#e8710a", "#fff1e3" };  // Vorwärmen
   if (state == 15 || state == 70 || state == 69)                    return { "🧹", "#2b7bbf", "#e8f2fb" };  // Reinigen
   if ((state >= 10 && state <= 14) || state == 35 || state == 16)   return { "⏳", "#2b7bbf", "#e8f2fb" };  // Warten
   if (state == 60 || state == 61 || state == 72)                    return { "🔥", "#e8590c", "#ffe8d9" };  // Scheitholz
   if (state == 19)                                                  return { "✅", "#2e7d32", "#e8f5e9" };  // Betriebsbereit

   return { "ℹ️", "#2b7bbf", "#e8f2fb" };
}

int HomeCtl::p4SendErrorMail()
{
   if (!sendMails || mailScript.empty() || errorMailTo.empty())
      return done;

   std::vector<long> ids;
   std::string html {p4ErrorMailHtml(&ids)};

   if (ids.empty())
      return done;

   // they are mailed once (also if the mail fails, the error list is no queue)

   for (long id : ids)
   {
      tableP4Errors->clear();
      tableP4Errors->setValue("ID", id);

      if (tableP4Errors->find())
      {
         tableP4Errors->setValue("MAILCNT", tableP4Errors->getIntValue("MAILCNT") + 1);
         tableP4Errors->update();
      }
   }

   tell(eloAlways, "Info: Mail for %zu errors of the S 3200 to '%s'", ids.size(), errorMailTo.c_str());

   return sendMail(errorMailTo.c_str(), "Heizung: STÖRUNG", p4ShellEscape(html).c_str(), "text/html");
}

//***************************************************************************
// P4 Error Mail Html
//   the pending (not mailed) errors, their IDs go to 'ids' (nullptr -> the last 5 errors, for a test)
//***************************************************************************

std::string HomeCtl::p4ErrorMailHtml(std::vector<long>* ids)
{
   const P4StateLook look {p4StateLook(0)};
   const std::string muted {"color:#6b7280;font-size:12px;"};
   std::string rows;
   int n {0};

   auto addRow = [&]()
   {
      time_t t {std::max(std::max(tableP4Errors->getTimeValue("TIME1"), tableP4Errors->getTimeValue("TIME4")), tableP4Errors->getTimeValue("TIME2"))};
      std::string state {tableP4Errors->getStrValue("STATE")};
      std::string color {state == "gekommen" ? "#d32f2f" : state == "gegangen" ? "#e8710a" : "#6b7280"};
      std::string tint {state == "gekommen" ? "#fdecea" : state == "gegangen" ? "#fff1e3" : "#eef0f3"};
      std::string bg {n++ % 2 ? "#f8f9fb" : "#ffffff"};

      rows +=
         "<tr><td bgcolor=\"" + bg + "\" style=\"padding:8px 10px;background:" + bg + ";\">"
         "<div style=\"color:#222;font-weight:bold;\">" + p4HtmlEscape(tableP4Errors->getStrValue("TEXT")) + "</div>"
         "<div style=\"" + muted + "\">" + l2pTime(t, "%d.%m.%Y %H:%M") + " · Fehler " + std::to_string(tableP4Errors->getIntValue("NUMBER")) + "</div></td>"
         "<td bgcolor=\"" + bg + "\" align=\"right\" valign=\"middle\" style=\"padding:8px 10px;background:" + bg + ";\">"
         "<span style=\"display:inline-block;padding:3px 10px;border-radius:12px;background:" + tint + ";color:" + color +
         ";font-size:12px;font-weight:bold;white-space:nowrap;\">" + p4HtmlEscape(state) + "</span></td></tr>\n";
   };

   tableP4Errors->clear();

   if (ids)
   {
      for (bool f = selectP4PendingErrors->find(); f; f = selectP4PendingErrors->fetch())
      {
         addRow();
         ids->push_back(tableP4Errors->getIntValue("ID"));
      }

      selectP4PendingErrors->freeResult();
   }
   else
   {
      for (bool f = selectAllP4Errors->find(); f && n < 5; f = selectAllP4Errors->fetch())
         addRow();

      selectAllP4Errors->freeResult();
   }

   std::string top {
      "<tr><td style=\"padding:8px 20px;\">"
      "<table role=\"presentation\" width=\"100%\" cellpadding=\"0\" cellspacing=\"0\" border=\"0\" bgcolor=\"" + std::string(look.tint) + "\" "
      "style=\"background:" + look.tint + ";border-radius:12px;border-collapse:separate;\"><tr>"
      "<td width=\"64\" align=\"center\" valign=\"middle\" style=\"padding:14px 0 14px 14px;font-size:36px;\">" + look.icon + "</td>"
      "<td valign=\"middle\" style=\"padding:14px;\">"
      "<div style=\"font-size:18px;font-weight:bold;color:" + look.color + ";\">Störung an der Heizung</div>"
      "<div style=\"color:#444;font-size:13px;margin-top:2px;\">Aktueller Status: <b>" +
      p4HtmlEscape(p4StateInfo.empty() ? std::string("unbekannt") : p4StateInfo) + "</b></div>"
      "</td></tr></table></td></tr>\n" };

   std::string list;

   if (!rows.empty())
      list = "<tr><td colspan=\"2\" style=\"padding:14px 10px 4px 10px;font-size:11px;font-weight:bold;letter-spacing:0.08em;"
         "text-transform:uppercase;color:#9aa3ad;border-bottom:1px solid #e6e9ee;\">Fehler</td></tr>\n" + rows;

   return p4MailFrame(look, top, list, "Fehler Mail der Heizung");
}

bool HomeCtl::p4IsMailState(int state)
{
   if (p4StateMailStates.empty())
      return true;

   for (const auto& s : split(p4StateMailStates, ','))
      if (s.find_first_not_of(" ") != std::string::npos && atoi(s.c_str()) == state)
         return true;

   return false;
}

//***************************************************************************
// P4 State Mail
//   like the HTML widget 'Heizung S 3200': state, boiler, puffer and pellets, the tiles
//   and the list of all values; only tables and inline styles (that's what the mail
//   clients understand), no images - the state is an emoji in a colored badge
//***************************************************************************

// color of a temperature, blue (cold) -> orange -> red (hot), in steps (a mix of the RGB values gets grey in the middle)

static const char* p4TempColor(double t)
{
   if (t < 30) return "#3b8fd8";
   if (t < 40) return "#26a69a";
   if (t < 50) return "#e6a817";
   if (t < 65) return "#f0862b";
   if (t < 80) return "#e8590c";

   return "#d32f2f";
}

static std::string p4Num(double v, int digits)
{
   char buf[50];
   snprintf(buf, sizeof(buf), "%.*f", digits, v);
   return buf;
}

static std::string p4Num(double v)        // without decimals for whole numbers
{
   return p4Num(v, v == (long)v ? 0 : 1);
}

// a horizontal bar (0-100%)

static std::string p4MailBar(double percent, const std::string& color, int width = 120)
{
   int w {(int)(std::min(std::max(percent, 0.0), 100.0) * width / 100.0 + 0.5)};
   std::string html {"<table role=\"presentation\" cellpadding=\"0\" cellspacing=\"0\" border=\"0\" width=\"" + std::to_string(width) +
      "\" style=\"border-collapse:separate;\"><tr>"};

   if (w > 0)
      html += "<td width=\"" + std::to_string(w) + "\" height=\"8\" bgcolor=\"" + color + "\" style=\"background:" + color +
         ";height:8px;line-height:8px;font-size:0;border-radius:4px;\">&nbsp;</td>";

   if (w < width)
      html += "<td width=\"" + std::to_string(width - w) + "\" height=\"8\" bgcolor=\"#e6e9ee\" style=\"background:#e6e9ee;"
         "height:8px;line-height:8px;font-size:0;border-radius:4px;\">&nbsp;</td>";

   return html + "</tr></table>";
}

std::string HomeCtl::p4StateMailHtml(const char* stateInfo, int state)
{
   // the sensors of the widget template 'Heizung S 3200', a missing (inactive / invalid) one hides its element

   auto get = [this](const char* type, int address) -> const SensorData*
   {
      auto t {sensors.find(type)};

      if (t == sensors.end())
         return nullptr;

      auto s {t->second.find(address)};

      if (s == t->second.end() || !s->second.active || !s->second.valid())
         return nullptr;

      return &s->second;
   };

   const SensorData* mode       {get("P4UD", 0x02)};
   const SensorData* clock      {get("P4UD", 0x03)};
   const SensorData* boiler     {get("P4VA", 0x00)};
   const SensorData* load       {get("P4VA", 0x12)};
   const SensorData* bufTop     {get("P4VA", 0x76)};
   const SensorData* bufBottom  {get("P4VA", 0x78)};
   const SensorData* outside    {get("P4VA", 0x04)};
   const SensorData* flow       {get("P4VA", 0x15)};
   const SensorData* flowSet    {get("P4VA", 0x16)};
   const SensorData* hcPump     {get("P4DO", 0x00)};
   const SensorData* floor      {get("P4VA", 0x19)};
   const SensorData* floorPump  {get("P4DO", 0x01)};
   const SensorData* exhaust    {get("P4VA", 0x01)};
   const SensorData* hours      {get("P4VA", 0x62)};
   const SensorData* heatToday  {get("P4SD", 0x03)};
   const SensorData* pellets    {get("P4VA", 0x71)};

   const double boilerMax {90.0};
   const double pelletsWarn {20.0};
   const P4StateLook look {p4StateLook(state)};
   const std::string muted {"color:#6b7280;font-size:12px;"};

   auto unit = [](const SensorData* s) { return s->unit == "°C" ? std::string("°") : " " + p4HtmlEscape(s->unit); };

   // -------------------------------------------------
   // hero: state | boiler | puffer | pellets

   std::string hero;

   hero +=
      "<td valign=\"top\" align=\"center\" style=\"padding:8px;\">"
      "<table role=\"presentation\" cellpadding=\"0\" cellspacing=\"0\" border=\"0\"><tr>"
      "<td width=\"84\" height=\"84\" align=\"center\" valign=\"middle\" bgcolor=\"" + std::string(look.tint) + "\" "
      "style=\"width:84px;height:84px;border-radius:42px;background:" + look.tint + ";border:3px solid " + look.color + ";"
      "font-size:40px;line-height:84px;\">" + look.icon + "</td></tr></table>"
      "<div style=\"margin-top:8px;font-size:17px;font-weight:bold;color:" + look.color + ";\">" + p4HtmlEscape(stateInfo) + "</div>"
      "<div style=\"" + muted + "margin-top:2px;\">seit " + l2pTime(time(0), "%H:%M") + " Uhr</div>"
      "</td>";

   if (boiler)
   {
      std::string color {p4TempColor(boiler->value)};

      hero +=
         "<td valign=\"top\" align=\"center\" style=\"padding:8px;\">"
         "<div style=\"" + muted + "\">Kessel</div>"
         "<div style=\"font-size:36px;font-weight:bold;line-height:44px;color:" + color + ";\">" + p4Num(boiler->value, 0) +
         "<span style=\"font-size:18px;font-weight:normal;\">" + unit(boiler) + "</span></div>" +
         p4MailBar(boiler->value / boilerMax * 100, color);

      if (load)
         hero +=
            "<div style=\"" + muted + "margin-top:10px;\">Leistung " + p4Num(load->value, 0) + " %</div>" +
            p4MailBar(load->value, "#f0a04b");

      hero += "</td>";
   }

   if (bufTop)
   {
      auto half = [&](const SensorData* s, const char* radius) {
         std::string color {p4TempColor(s->value)};
         return "<tr><td width=\"56\" height=\"40\" align=\"center\" valign=\"middle\" bgcolor=\"" + color + "\" "
            "style=\"width:56px;height:40px;background:" + color + ";color:#fff;font-weight:bold;font-size:14px;" + radius + "\">" +
            p4Num(s->value, 0) + "°</td></tr>";
      };

      hero +=
         "<td valign=\"top\" align=\"center\" style=\"padding:8px;\">"
         "<div style=\"" + muted + "margin-bottom:4px;\">Puffer</div>"
         "<table role=\"presentation\" cellpadding=\"0\" cellspacing=\"0\" border=\"0\" style=\"border-collapse:separate;\">" +
         half(bufTop, bufBottom ? "border-radius:14px 14px 0 0;" : "border-radius:14px;") +
         (bufBottom ? half(bufBottom, "border-radius:0 0 14px 14px;") : "") +
         "</table></td>";
   }

   if (pellets)
   {
      bool low {pellets->value < pelletsWarn};
      std::string color {low ? "#d32f2f" : "#a0703c"};
      int fill {(int)(std::min(std::max(pellets->value, 0.0), 100.0) * 0.8 + 0.5)};   // 80px

      hero +=
         "<td valign=\"top\" align=\"center\" style=\"padding:8px;\">"
         "<div style=\"" + muted + "margin-bottom:4px;\">Pellets</div>"
         "<table role=\"presentation\" cellpadding=\"0\" cellspacing=\"0\" border=\"0\" width=\"36\" "
         "style=\"border:2px solid #9aa3ad;border-radius:6px;border-collapse:separate;\">";

      if (fill < 80)
         hero += "<tr><td height=\"" + std::to_string(80 - fill) + "\" style=\"height:" + std::to_string(80 - fill) +
            "px;line-height:0;font-size:0;\">&nbsp;</td></tr>";

      if (fill > 0)
         hero += "<tr><td height=\"" + std::to_string(fill) + "\" bgcolor=\"" + color + "\" style=\"height:" + std::to_string(fill) +
            "px;background:" + color + ";line-height:0;font-size:0;border-radius:0 0 4px 4px;\">&nbsp;</td></tr>";

      hero +=
         "</table>"
         "<div style=\"font-size:13px;font-weight:bold;margin-top:4px;color:" + (low ? color : std::string("#333")) + ";\">" +
         p4Num(pellets->value, 0) + " %</div>"
         "</td>";
   }

   // -------------------------------------------------
   // tiles

   std::string tiles;

   auto tile = [&](const SensorData* s, const char* icon, const char* label, const std::string& extra)
   {
      if (!s)
         return;

      tiles +=
         "<td width=\"25%\" valign=\"top\" style=\"padding:4px;\">"
         "<table role=\"presentation\" width=\"100%\" cellpadding=\"0\" cellspacing=\"0\" border=\"0\" bgcolor=\"#f5f7fa\" "
         "style=\"background:#f5f7fa;border-radius:10px;border-collapse:separate;\"><tr><td style=\"padding:10px;\">"
         "<div style=\"font-size:18px;\">" + std::string(icon) + "</div>"
         "<div style=\"font-size:20px;font-weight:bold;color:#222;\">" + p4Num(s->value) +
         "<span style=\"font-size:12px;font-weight:normal;color:#6b7280;\">" + unit(s) + "</span></div>"
         "<div style=\"" + muted + "\">" + label + extra + "</div>"
         "</td></tr></table></td>";
   };

   auto pump = [](const SensorData* s) -> std::string {
      if (!s)
         return "";
      return s->state ? " · <span style=\"color:#2e7d32;font-weight:bold;\">Pumpe an</span>" : " · Pumpe aus";
   };

   tile(outside, outside && outside->value < 3 ? "❄️" : "☀️", "Außen", "");
   tile(flow, "🌡️", "Vorlauf", (flowSet ? " · Soll " + p4Num(flowSet->value, 0) + "°" : "") + pump(hcPump));
   tile(floor, "♨️", "Fußboden", pump(floorPump));
   tile(exhaust, "💨", "Abgas", "");

   // -------------------------------------------------
   // chips

   std::string chips;

   auto chip = [&](const std::string& text) {
      chips += "<span style=\"display:inline-block;margin:3px 4px;padding:4px 12px;border-radius:14px;background:#eef0f3;"
         "color:#333;font-size:12px;\">" + text + "</span>";
   };

   if (mode && !mode->text.empty())  chip("⚙️ " + p4HtmlEscape(mode->text));
   if (hours)                        chip("⏱️ " + p4Num(hours->value, 0) + " h Betrieb");
   if (heatToday)                    chip("🔥 " + p4Num(heatToday->value / 60, 1) + " h heute");
   if (clock && !clock->text.empty()) chip("🕒 Steuerung " + p4HtmlEscape(clock->text));

   // -------------------------------------------------
   // all values, grouped like the sensor types

   static const std::pair<const char*, const char*> groups[] {
      { "P4VA", "Messwerte" }, { "P4DO", "Digitale Ausgänge" }, { "P4AO", "Analoge Ausgänge" },
      { "P4DI", "Digitale Eingänge" }, { "P4SD", "Statistik" } };

   std::string list;

   for (const auto& [type, title] : groups)
   {
      auto t {sensors.find(type)};

      if (t == sensors.end())
         continue;

      std::string rows;
      int n {0};

      for (const auto& [address, sensor] : t->second)
      {
         if (!sensor.active || sensor.title.empty())
            continue;

         std::string value;

         // the text of an in/output is its mode (Auto / Manual), it comes after the state or value

         if (sensor.kind == "text")
            value = p4HtmlEscape(sensor.text);
         else if (sensor.kind == "status")
            value = sensor.state ? "<span style=\"color:#2e7d32;font-weight:bold;\">● an</span>" : "<span style=\"color:#9aa3ad;\">○ aus</span>";
         else
            value = p4Num(sensor.value) + " " + p4HtmlEscape(sensor.unit);

         if (sensor.kind != "text" && !sensor.text.empty())
            value += " <span style=\"color:#9aa3ad;\">(" + p4HtmlEscape(sensor.text) + ")</span>";

         std::string bg {n++ % 2 ? "#f8f9fb" : "#ffffff"};

         rows += "<tr><td bgcolor=\"" + bg + "\" style=\"padding:5px 10px;background:" + bg + ";color:#444;\">" + p4HtmlEscape(sensor.title) +
            "</td><td bgcolor=\"" + bg + "\" align=\"right\" style=\"padding:5px 10px;background:" + bg + ";color:#222;white-space:nowrap;\">" +
            value + "</td></tr>\n";
      }

      if (rows.empty())
         continue;

      list +=
         "<tr><td colspan=\"2\" style=\"padding:14px 10px 4px 10px;font-size:11px;font-weight:bold;letter-spacing:0.08em;"
         "text-transform:uppercase;color:#9aa3ad;border-bottom:1px solid #e6e9ee;\">" + std::string(title) + "</td></tr>\n" + rows;
   }

   // -------------------------------------------------
   // the mail

   std::string top {
      "<tr><td align=\"center\" style=\"padding:8px 12px;\">"
      "<table role=\"presentation\" cellpadding=\"0\" cellspacing=\"0\" border=\"0\"><tr>" + hero + "</tr></table>"
      "</td></tr>\n" };

   if (!tiles.empty())
      top += "<tr><td style=\"padding:4px 16px;\"><table role=\"presentation\" width=\"100%\" cellpadding=\"0\" cellspacing=\"0\" border=\"0\"><tr>" +
         tiles + "</tr></table></td></tr>\n";

   if (!chips.empty())
      top += "<tr><td align=\"center\" style=\"padding:10px 16px 4px 16px;\">" + chips + "</td></tr>\n";

   return p4MailFrame(look, top, list, "Status Mail der Heizung");
}

//***************************************************************************
// P4 Mail Frame
//   the card of the mails: accent of the state on top, title, the rows 'top', the button
//   to the web interface and the rows of the table 'list' (two columns)
//***************************************************************************

std::string HomeCtl::p4MailFrame(const P4StateLook& look, const std::string& top, const std::string& list, const char* footer)
{
   const std::string font {"font-family:-apple-system,'Segoe UI',Roboto,Helvetica,Arial,sans-serif;"};
   const std::string muted {"color:#6b7280;font-size:12px;"};

   return
      "<!DOCTYPE html>\n"
      "<html><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
      "<meta name=\"color-scheme\" content=\"light\"><meta name=\"supported-color-schemes\" content=\"light\"></head>\n"
      "<body style=\"margin:0;padding:0;background:#eef1f5;" + font + "\">\n"
      "<table role=\"presentation\" width=\"100%\" cellpadding=\"0\" cellspacing=\"0\" border=\"0\" bgcolor=\"#eef1f5\" style=\"background:#eef1f5;\">"
      "<tr><td align=\"center\" style=\"padding:20px 10px;\">\n"
      "<table role=\"presentation\" width=\"100%\" cellpadding=\"0\" cellspacing=\"0\" border=\"0\" bgcolor=\"#ffffff\" "
      "style=\"max-width:600px;background:#ffffff;border-radius:14px;border-collapse:separate;overflow:hidden;font-size:14px;" + font + "\">\n"

      // head, the accent of the state on top

      "<tr><td height=\"5\" bgcolor=\"" + look.color + "\" style=\"height:5px;line-height:5px;font-size:0;background:" + look.color +
      ";border-radius:14px 14px 0 0;\">&nbsp;</td></tr>\n"
      "<tr><td style=\"padding:16px 20px 4px 20px;\">"
      "<table role=\"presentation\" width=\"100%\" cellpadding=\"0\" cellspacing=\"0\" border=\"0\"><tr>"
      "<td style=\"font-size:18px;font-weight:bold;color:#222;\">" + look.icon + " Heizung S 3200</td>"
      "<td align=\"right\" style=\"" + muted + "\">" + l2pTime(time(0), "%d.%m.%Y %H:%M") + "</td>"
      "</tr></table></td></tr>\n" +

      top +

      (webUrl.empty() ? "" :
       "<tr><td align=\"center\" style=\"padding:16px;\">"
       "<a href=\"" + webUrl + "\" style=\"display:inline-block;padding:10px 22px;border-radius:20px;background:#004d8f;color:#ffffff;"
       "text-decoration:none;font-weight:bold;font-size:14px;\">Visualisierung öffnen</a></td></tr>\n") +

      (list.empty() ? "" :
       "<tr><td style=\"padding:0 16px 16px 16px;\">"
       "<table role=\"presentation\" width=\"100%\" cellpadding=\"0\" cellspacing=\"0\" border=\"0\" style=\"font-size:13px;\">\n" +
       list + "</table></td></tr>\n") +

      "<tr><td align=\"center\" bgcolor=\"#f5f7fa\" style=\"padding:12px;background:#f5f7fa;" + muted + "border-radius:0 0 14px 14px;\">" +
      p4HtmlEscape(instanceName) + " · " + footer + "</td></tr>\n"

      "</table>\n"
      "</td></tr></table>\n"
      "</body></html>\n";
}

int HomeCtl::p4SendStateMail(const char* stateInfo, int state)
{
   if (!sendMails || mailScript.empty() || stateMailTo.empty())
      return done;

   std::string html {p4StateMailHtml(stateInfo, state)};
   std::string subject {std::string("Heizung - Status: ") + stateInfo};

   tell(eloAlways, "Info: State mail '%s' (%d) to '%s'", stateInfo, state, stateMailTo.c_str());

   return sendMail(stateMailTo.c_str(), p4ShellEscape(subject).c_str(), p4ShellEscape(html).c_str(), "text/html");
}

//***************************************************************************
// P4 Send Test Mail
//   from the setup page 'Commands': the state mail with the current state or the error mail
//   with the last 5 errors (they don't count as mailed), independent of 'p4Mail'
//***************************************************************************

int HomeCtl::p4SendTestMail(bool error, long client)
{
   const std::string& receiver {error ? errorMailTo : stateMailTo};

   if (!p4Enabled)
      return replyResult(fail, client, "S 3200 (p4mqttd) not enabled");

   if (mailScript.empty() || !fileExists(mailScript.c_str()))
      return replyResult(fail, client, "Missing mail script '%s'", mailScript.c_str());

   if (receiver.empty())
      return replyResult(fail, client, "Missing receiver, configure the %s", error ? "'Fehler Mail Empfänger'" : "'Status Mail Empfänger'");

   std::string html;
   std::string subject;

   if (error)
   {
      html = p4ErrorMailHtml(nullptr);
      subject = "TEST Heizung: STÖRUNG";
   }
   else
   {
      if (p4LastState == -1)
         return replyResult(fail, client, "The state of the S 3200 isn't known yet (p4mqttd online?)");

      html = p4StateMailHtml(p4StateInfo.c_str(), p4LastState);
      subject = "TEST Heizung - Status: " + p4StateInfo;
   }

   tell(eloAlways, "Info: Test %s mail of the S 3200 to '%s'", error ? "error" : "state", receiver.c_str());

   if (sendMail(receiver.c_str(), p4ShellEscape(subject).c_str(), p4ShellEscape(html).c_str(), "text/html") != success)
      return replyResult(fail, client, "Sending mail failed, see the syslog and '/var/log/msmtp.log'");

   return replyResult(success, client, "Mail sent to '%s'", receiver.c_str());
}

//***************************************************************************
// P4 Errors 2 Json (the table, newest first)
//***************************************************************************

int HomeCtl::p4Errors2Json(json_t* obj)
{
   json_t* jList {json_array()};

   tableP4Errors->clear();

   for (bool f = selectAllP4Errors->find(); f; f = selectAllP4Errors->fetch())
   {
      json_t* jE {json_object()};

      json_object_set_new(jE, "number", json_integer(tableP4Errors->getIntValue("NUMBER")));
      json_object_set_new(jE, "info", json_integer(tableP4Errors->getIntValue("INFO")));
      json_object_set_new(jE, "state", json_string(tableP4Errors->getStrValue("STATE")));
      json_object_set_new(jE, "text", json_string(tableP4Errors->getStrValue("TEXT")));
      json_object_set_new(jE, "time1", json_integer(tableP4Errors->getTimeValue("TIME1")));
      json_object_set_new(jE, "time4", json_integer(tableP4Errors->getValue("TIME4")->isNull() ? 0 : tableP4Errors->getTimeValue("TIME4")));
      json_object_set_new(jE, "time2", json_integer(tableP4Errors->getValue("TIME2")->isNull() ? 0 : tableP4Errors->getTimeValue("TIME2")));
      json_array_append_new(jList, jE);
   }

   selectAllP4Errors->freeResult();

   json_object_set_new(obj, "errors", jList);
   json_object_set_new(obj, "now", json_integer(time(0)));
   json_object_set_new(obj, "online", json_boolean(p4Online));

   return done;
}

//***************************************************************************
// Perform P4 Errors (WEBIF, tab 'Fehler')
//   { } -> the table, { "action": "refresh" } -> read the list from the control first
//***************************************************************************

int HomeCtl::performP4Errors(json_t* obj, long client)
{
   if (!p4Enabled)
      return p4Answer(client, "Die Anbindung der Heizung ist nicht aktiviert (Konfiguration -> Heizung S 3200)", "p4errors");

   if (std::string(getStringFromJson(obj, "action", "")) == "refresh")
   {
      json_t* oCmd {json_object()};

      json_object_set_new(oCmd, "cmd", json_string("errors"));

      return p4SendCommand(oCmd, client, "errors", 1, 90);
   }

   json_t* oJson {json_object()};

   p4Errors2Json(oJson);
   pushOutMessage(oJson, "p4errors", client);

   return done;
}

//***************************************************************************
// P4 Calc State Duration
//   the minutes the S 3200 spent in each state today, from the samples of the state sensor
//   (P4UD:0x01, has to be recorded), as sensors P4SD:<state> (like the SD of the p4d)
//***************************************************************************

int HomeCtl::p4CalcStateDuration()
{
   std::map<int,time_t> durations;
   std::map<int,std::string> texts;
   time_t lastTime {0};
   int lastState {-1};
   time_t now {time(0)};

   tableSamples->clear();

   for (bool f = selectP4StateSamples->find(); f; f = selectP4StateSamples->fetch())
   {
      if (tableSamples->getValue("VALUE")->isNull())
         continue;                         // a sample without the number of the state

      time_t t {tableSamples->getTimeValue("TIME")};
      int state {(int)tableSamples->getFloatValue("VALUE")};

      if (lastState != -1 && t > lastTime)
         durations[lastState] += t - lastTime;

      lastTime = t;
      lastState = state;
      texts[state] = tableSamples->getStrValue("TEXT");
   }

   selectP4StateSamples->freeResult();

   if (lastState == -1)
      return done;                       // no samples today (the state sensor isn't recorded?)

   durations[lastState] += now - lastTime;

   // the states known up to now but not seen today are zero

   for (const auto& [address, sensor] : sensors["P4SD"])
      durations.try_emplace(address, 0);

   bool changed {false};

   for (const auto& [state, seconds] : durations)
   {
      const auto it {texts.find(state)};
      std::string title {(it != texts.end() ? it->second : std::string("Zustand ") + std::to_string(state)) + " (Laufzeit/Tag)"};

      // the default widget: a chart

      addValueFact(state, "P4SD", 1, ("State_Duration_" + std::to_string(state)).c_str(), "min", title.c_str(),
                   urView, nullptr, soNone, "{\"widgettype\":1,\"showpeak\":true}");

      SensorData& sensor {sensors["P4SD"][state]};

      if (sensor.type.empty())
         initSensorByFact("P4SD", state);

      sensor.kind = "value";

      if (sensor.setValue(seconds / 60, now))
      {
         json_t* ojData {json_object()};

         sensor2Json(ojData, "P4SD", state);
         sensorValue2Json(ojData, "P4SD", state);

         char* tuple {};
         asprintf(&tuple, "%s:0x%02x", "P4SD", state);
         jsonSensorList[tuple] = ojData;
         free(tuple);
         changed = true;
      }
   }

   if (changed)
      pushDataUpdate("update", 0L);

   return done;
}

//***************************************************************************
// P4 Pellets 2 Json
//   the deliveries with the stoker hours between them and the consumption per stoker hour
//   (port of P4d::performPellets), the stoker hours are the sample of P4VA:0xad at the date of the delivery
//***************************************************************************

static constexpr int p4StokerHoursAddress {0xad};      // 'Betriebsstunden Stokerschnecke'

int HomeCtl::p4Pellets2Json(json_t* obj)
{
   json_t* jList {json_array()};
   uint stokerHhLast {0};
   time_t timeLast {0};
   double tAmount {0.0};
   double tPrice {0.0};
   double consumptionHLast {0.0};

   // the rows first, the queries for the stoker hours need the connection

   struct Delivery { long id; time_t time; int amount; double price; std::string comment; };
   std::vector<Delivery> deliveries;

   tableP4Pellets->clear();

   for (bool f = selectAllP4Pellets->find(); f; f = selectAllP4Pellets->fetch())
      deliveries.push_back({ tableP4Pellets->getIntValue("ID"), tableP4Pellets->getTimeValue("TIME"),
                             (int)tableP4Pellets->getIntValue("AMOUNT"), tableP4Pellets->getFloatValue("PRICE"),
                             tableP4Pellets->getStrValue("COMMENT") });

   selectAllP4Pellets->freeResult();

   for (const auto& d : deliveries)
   {
      json_t* oData {json_object()};
      json_array_append_new(jList, oData);

      json_object_set_new(oData, "id", json_integer(d.id));
      json_object_set_new(oData, "time", json_integer(d.time));
      json_object_set_new(oData, "amount", json_integer(d.amount));
      json_object_set_new(oData, "price", json_real(d.price));
      json_object_set_new(oData, "comment", json_string(d.comment.c_str()));

      tAmount += d.amount;
      tPrice += d.price;

      // the stoker hours at the date of the delivery (the first sample after it)

      char where[200];
      int stokerHh {0};

      // the counter only grows, so the smallest value of the first week is the one after the delivery - this is
      //  much faster than the minimum of all samples after it (2.6 s against 0.07 s), the whole range is the fallback
      //  for a sensor which wasn't recorded then

      snprintf(where, sizeof(where), "type = 'P4VA' and address = %d and time > '%s' and time <= '%s'", p4StokerHoursAddress,
               l2pTime(d.time, "%Y-%m-%d %H:%M:%S").c_str(), l2pTime(d.time + 7 * tmeSecondsPerDay, "%Y-%m-%d %H:%M:%S").c_str());

      if (tableSamples->countWhere(where, stokerHh, "min(value)") != success || !stokerHh)
      {
         snprintf(where, sizeof(where), "type = 'P4VA' and address = %d and time > '%s'", p4StokerHoursAddress,
                  l2pTime(d.time, "%Y-%m-%d %H:%M:%S").c_str());

         if (tableSamples->countWhere(where, stokerHh, "min(value)") != success)
            stokerHh = 0;
      }

      if (!stokerHh)
      {
         tell(eloDetail, "Info: No sample of the stoker hours after %s", l2pTime(d.time).c_str());
         continue;
      }

      if (timeLast)
         json_object_set_new(oData, "duration", json_integer((d.time - timeLast) / tmeSecondsPerDay));

      if (stokerHhLast && (int)stokerHh - (int)stokerHhLast > 0)
      {
         json_object_set_new(oData, "stokerHours", json_integer(stokerHh - stokerHhLast));
         consumptionHLast = d.amount / (double)(stokerHh - stokerHhLast);
         json_object_set_new(oData, "consumptionH", json_real(consumptionHLast));
      }

      timeLast = d.time;
      stokerHhLast = stokerHh;
   }

   // the sum and the estimation since the last delivery

   double consumptionH {p4ConsumptionPerHour ? p4ConsumptionPerHour : consumptionHLast};
   double stokerNow {0.0};
   const auto it {sensors.find("P4VA")};

   if (it != sensors.end() && it->second.count(p4StokerHoursAddress))
      stokerNow = it->second.at(p4StokerHoursAddress).value;

   char hint[200];
   snprintf(hint, sizeof(hint), "consumption since the last delivery by %.2f kg per stoker hour", consumptionH);

   json_t* oSum {json_object()};
   json_array_append_new(jList, oSum);

   json_object_set_new(oSum, "id", json_integer(-1));
   json_object_set_new(oSum, "sum", json_true());
   json_object_set_new(oSum, "time", json_integer(time(0)));
   json_object_set_new(oSum, "amount", json_integer(tAmount));
   json_object_set_new(oSum, "price", json_real(tPrice));
   json_object_set_new(oSum, "comment", json_string("Total"));
   json_object_set_new(oSum, "consumptionHint", json_string(hint));

   double sinceLast {stokerHhLast && stokerNow > stokerHhLast ? stokerNow - stokerHhLast : 0.0};

   json_object_set_new(oSum, "stokerHours", json_integer(sinceLast));
   json_object_set_new(oSum, "consumptionDelta", json_integer(consumptionH * sinceLast));

   json_object_set_new(obj, "pellets", jList);

   return done;
}

int HomeCtl::performP4Pellets(json_t* obj, long client)
{
   json_t* oJson {json_object()};

   p4Pellets2Json(oJson);
   pushOutMessage(oJson, "p4pellets", client);

   return done;
}

//***************************************************************************
// Perform P4 Pellets Add (WEBIF)
//   { "id": -1 (new) | n, "time": <unix>, "amount": kg, "price": €, "comment": "..." } | { "id": n, "delete": true }
//***************************************************************************

int HomeCtl::performP4PelletsAdd(json_t* obj, long client)
{
   int id {getIntFromJson(obj, "id", -1)};
   bool del {getBoolFromJson(obj, "delete", false)};
   std::string user {wsClients[(void*)client].user};

   tableP4Pellets->clear();
   tableP4Pellets->setValue("ID", id);

   if (del)
   {
      if (id < 0 || !tableP4Pellets->find())
         return replyResult(fail, client, "Eintrag nicht gefunden");

      tableP4Pellets->deleteWhere("id = %d", id);
      tell(eloAlways, "P4: user '%s' deleted pellet delivery %d", user.c_str(), id);
      performP4Pellets(obj, client);

      return replyResult(success, client, "Eintrag gelöscht");
   }

   if (id >= 0 && !tableP4Pellets->find())
      return replyResult(fail, client, "Eintrag nicht gefunden");

   int amount {getIntFromJson(obj, "amount", 0)};
   double price {getDoubleFromJson(obj, "price", 0.0)};
   long when {getLongFromJson(obj, "time")};

   if (amount <= 0 || price <= 0.0 || when <= 0)
      return replyResult(fail, client, "Menge, Preis und Datum werden benötigt");

   tableP4Pellets->setValue("TIME", when);
   tableP4Pellets->setValue("AMOUNT", amount);
   tableP4Pellets->setValue("PRICE", price);
   tableP4Pellets->setValue("COMMENT", getStringFromJson(obj, "comment", ""));
   tableP4Pellets->store();

   tell(eloAlways, "P4: user '%s' stored pellet delivery %d: %d kg, %.2f, %s", user.c_str(), id, amount, price,
        l2pTime(when).c_str());

   performP4Pellets(obj, client);

   return replyResult(success, client, "Eintrag gespeichert");
}
