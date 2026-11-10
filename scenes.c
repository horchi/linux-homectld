/*
 *  scenes.c
 *
 *  (c) 2026 Jörg Wendel
 *
 * This code is distributed under the terms and conditions of the
 * GNU GENERAL PUBLIC LICENSE. See the file COPYING for details.
 *
 *  Scenes (sensor type 'SCN')
 *
 *  A scene is a list of steps which are performed one after the other, e.g. a multi media
 *  automation: switch on the receiver, the TV, wait for the TV, select the inputs, ...
 *  It is a virtual actor 'SCN:0x01' - a symbol widget starts it by a click, a click on a
 *  running scene aborts it. 'working' (like the scripts, the widget is grayed) and the text
 *  show the state and progress. The steps are stored in the SETTINGS of the sensor:
 *
 *  { "steps": [ { "do": "set",     "key": "DENON:0x01", "value": 1 },
 *               { "do": "set",     "key": "SAMTV:0x04", "value": "Netflix" },
 *               { "do": "wait",    "sec": 2.5 },
 *               { "do": "waitfor", "key": "SAMTV:0x01", "op": "==", "value": 1,
 *                                  "timeout": 30, "continue": false },
 *               { "do": "set",     "key": "DZL:0x03", "value": 20, "if": { "key": "DZL:0x03", "op": ">", "value": 20 } },
 *               { "do": "scene",   "id": 2, "mode": "start" },
 *               { "do": "stop" } ] }
 *
 *  do 'set'      set a sensor / actor ('key' = "<type>:<address>"), the 'value':
 *                  missing / "toggle" -> toggle,  true / false / "on" / "off" / 1 / 0 -> state
 *                  number -> level (brightness, volume, ...), text -> choice (input, app, ...)
 *                  'action': "color" (hue, saturation, bri) or "ct" (ct, bri) for color lights
 *                  'continue': true -> a failing step doesn't abort the scene
 *  do 'wait'     pause of 'sec' seconds
 *  do 'waitfor'  wait until the condition (key, op, value) is true, at most 'timeout' seconds
 *                  (default 30), then abort the scene (or go on with 'continue': true)
 *  do 'stop'     end the scene
 *  do 'scene'    start (default) or stop ('mode') the scene 'id' (address of the SCN sensor)
 *  do 'shell'    run the command 'cmd' (e.g. "/usr/sbin/etherwake -i eno1 00:1e:06:43:48:8e") in the
 *                  background, the scene goes on at once; the output is logged. Not by a shell: the
 *                  line is split into arguments (quotes '..' ".." work, no pipes, ;, &&, $(), redirections),
 *                  the program (absolute path) must be in the whitelist
 *                  /etc/homectld/scene-commands-whitelist.conf:  [ "/usr/sbin/etherwake", "/usr/bin/ping" ]
 *
 *  All steps: 'if': { key, op, value } - the step is skipped if the condition is not true
 *             'title' - text of the step for the state text
 *
 *  Condition: key, op ( == != < > <= >= ), value (number, text or bool), optional 'field'
 *             (value | state | text), default: text for text values, else state for a
 *             status sensor (or bool values), else value.
 *
 *  The steps run in meanwhile() - no blocking, no thread.
 */

#include <thread>
#include <spawn.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include "daemon.h"

//***************************************************************************
// Parse Key
//   "DENON:0x01" -> type, address
//***************************************************************************

static bool sceneParseKey(const char* key, std::string& type, uint& address)
{
   if (isEmpty(key))
      return false;

   const char* colon {strchr(key, ':')};

   if (!colon || colon == key || !*(colon+1))
      return false;

   char* end {};
   long a {strtol(colon+1, &end, 0)};

   if (*end || a < 0)
      return false;

   type.assign(key, colon - key);
   address = (uint)a;

   return true;
}


//***************************************************************************
// Split Command Line into Arguments (no shell: no expansion, pipes, ...)
//***************************************************************************

static bool sceneSplitArgs(const std::string& line, std::vector<std::string>& args, std::string& error)
{
   std::string cur;
   bool inToken {false};
   char quote {0};

   for (size_t i = 0; i < line.size(); i++)
   {
      char c {line[i]};

      if (quote)
      {
         if (c == quote)
            quote = 0;
         else if (c == '\\' && quote == '"' && i+1 < line.size() && (line[i+1] == '"' || line[i+1] == '\\'))
            cur += line[++i];
         else
            cur += c;
      }
      else if (c == '\'' || c == '"')
      {
         quote = c;
         inToken = true;
      }
      else if (isspace((unsigned char)c))
      {
         if (inToken)
            args.push_back(cur);

         cur.clear();
         inToken = false;
      }
      else
      {
         cur += c;
         inToken = true;
      }
   }

   if (quote)
   {
      error = "Anführungszeichen nicht geschlossen";
      return false;
   }

   if (inToken)
      args.push_back(cur);

   return true;
}

//***************************************************************************
// Whitelist of the programs
//***************************************************************************

bool HomeCtl::sceneShellAllowed(const std::string& program, std::string& error)
{
   std::string file {std::string(confDir) + "/scene-commands-whitelist.conf"};
   json_error_t jerr;
   json_t* list {json_load_file(file.c_str(), 0, &jerr)};

   if (!list)
   {
      error = "Whitelist " + file + " fehlt oder ist ungültig";
      return false;
   }

   bool allowed {false};

   if (!json_is_array(list))
      error = "Whitelist " + file + " muss eine Liste [ \"/pfad\", ... ] sein";
   else
   {
      size_t i {0};
      json_t* e {};

      json_array_foreach(list, i, e)
         if (json_is_string(e) && program == json_string_value(e))
            allowed = true;

      if (!allowed)
         error = "'" + program + "' ist nicht in der Whitelist (" + file + ")";
   }

   json_decref(list);

   return allowed;
}

//***************************************************************************
// Run a Command in the Background (thread), log the output
//***************************************************************************

static void sceneRunCommand(std::vector<std::string> args)
{
   extern char** environ;

   std::vector<char*> argv;

   for (auto& a : args)
      argv.push_back(a.data());

   argv.push_back(nullptr);

   int fds[2];

   if (pipe(fds) != 0)
   {
      tell(eloAlways, "Error: Scene: pipe() failed for '%s'", args[0].c_str());
      return;
   }

   posix_spawn_file_actions_t fa;
   posix_spawn_file_actions_init(&fa);
   posix_spawn_file_actions_adddup2(&fa, fds[1], STDOUT_FILENO);
   posix_spawn_file_actions_adddup2(&fa, fds[1], STDERR_FILENO);
   posix_spawn_file_actions_addclose(&fa, fds[0]);
   posix_spawn_file_actions_addclose(&fa, fds[1]);

   pid_t pid {};
   int res {posix_spawn(&pid, args[0].c_str(), &fa, nullptr, argv.data(), environ)};

   posix_spawn_file_actions_destroy(&fa);
   close(fds[1]);

   if (res != 0)
   {
      close(fds[0]);
      tell(eloAlways, "Error: Scene: starting '%s' failed: %s", args[0].c_str(), strerror(res));
      return;
   }

   std::string out;
   time_t until {time(0) + 15};
   bool timedOut {false};
   char buf[256];

   while (true)
   {
      if (time(0) >= until)
      {
         timedOut = true;
         kill(pid, SIGKILL);
         break;
      }

      struct pollfd pfd {fds[0], POLLIN, 0};
      int r {poll(&pfd, 1, 500)};

      if (r > 0)
      {
         ssize_t n {read(fds[0], buf, sizeof(buf)-1)};

         if (n <= 0)
            break;                   // EOF, the command is finished

         buf[n] = 0;
         out += buf;
      }
   }

   close(fds[0]);

   int status {};
   waitpid(pid, &status, 0);

   while (!out.empty() && (out.back() == '\n' || out.back() == '\r'))
      out.pop_back();

   if (timedOut)
      tell(eloAlways, "Warning: Scene: '%s' terminated after 15 seconds", args[0].c_str());
   else if (WIFEXITED(status) && WEXITSTATUS(status) != 0)
      tell(eloAlways, "Warning: Scene: '%s' exited with %d", args[0].c_str(), WEXITSTATUS(status));

   if (!out.empty())
      tell(eloAlways, "Scene: output of '%s': %s", args[0].c_str(), out.c_str());
}

//***************************************************************************
// Running
//***************************************************************************

bool HomeCtl::sceneRunning(uint address)
{
   return sceneRuns.find(address) != sceneRuns.end();
}

//***************************************************************************
// Status (state, working and text of the sensor, push to the clients)
//***************************************************************************

void HomeCtl::sceneStatus(uint address, bool running, const std::string& text, int step, const char* result)
{
   SensorData& sensor {sensors["SCN"][address]};
   SceneInfo& info {sceneInfo[address]};

   info.step = step;
   info.result = result ? result : (running ? "running" : "ok");

   auto itRun {sceneRuns.find(address)};

   if (running && itRun != sceneRuns.end())
      info.warnings = itRun->second.warnings;

   sensor.working = running;
   sensor.setState(running);
   sensor.setText(text);

   json_t* ojData {json_object()};
   sensor2Json(ojData, "SCN", address);
   sensorValue2Json(ojData, "SCN", address);

   if (text.empty())
      json_object_set_new(ojData, "text", json_string(""));   // clears the old text at the clients

   // progress for the setup dialog: { "step": 3, "result": "error", "warnings": [ { "step": 2, "message": ".." } ] }

   json_t* ojScene {json_object()};
   json_t* ojWarnings {json_array()};

   json_object_set_new(ojScene, "step", json_integer(info.step));
   json_object_set_new(ojScene, "result", json_string(info.result.c_str()));

   for (const auto& [wStep, wMessage] : info.warnings)
   {
      json_t* ojWarning {json_object()};
      json_object_set_new(ojWarning, "step", json_integer(wStep));
      json_object_set_new(ojWarning, "message", json_string(wMessage.c_str()));
      json_array_append_new(ojWarnings, ojWarning);
   }

   json_object_set_new(ojScene, "warnings", ojWarnings);
   json_object_set_new(ojData, "scene", ojScene);

   char* key {};
   asprintf(&key, "SCN:0x%02x", address);
   jsonSensorList[key] = ojData;
   free(key);

   pushDataUpdate("update", 0L);
}

//***************************************************************************
// Describe a Step (for the state text)
//***************************************************************************

std::string HomeCtl::sceneDescribe(json_t* step)
{
   std::string title {getStringFromJson(step, "title", "")};

   if (!title.empty())
      return title;

   std::string what {getStringFromJson(step, "do", "")};
   std::string type;
   uint address {0};
   std::string key {getStringFromJson(step, "key", "")};

   if (sceneParseKey(key.c_str(), type, address))
   {
      SensorData* sensor {getSensor(type.c_str(), address)};

      if (sensor && !sensor->title.empty())
         key = sensor->title;
   }

   if (what == "set")     return "setze " + key;
   if (what == "waitfor") return "warte auf " + key;
   if (what == "wait")    return "warte " + std::to_string((int)json_number_value(json_object_get(step, "sec"))) + " s";
   if (what == "scene")   return std::string(getStringFromJson(step, "mode", "start")) == "stop" ? "Szene beenden" : "Szene starten";
   if (what == "shell")   return std::string("Befehl ") + getStringFromJson(step, "cmd", "");

   return what;
}

//***************************************************************************
// Condition
//***************************************************************************

bool HomeCtl::sceneCondition(json_t* cond)
{
   std::string type;
   uint address {0};

   if (!sceneParseKey(getStringFromJson(cond, "key", ""), type, address))
      return false;

   SensorData* sensor {getSensor(type.c_str(), address)};

   if (!sensor || !sensor->valid())
      return false;

   std::string op {getStringFromJson(cond, "op", "==")};
   std::string field {getStringFromJson(cond, "field", "")};
   json_t* jv {json_object_get(cond, "value")};

   if (!jv)
      return false;

   if (field.empty())
   {
      if (json_is_string(jv))
         field = "text";
      else if (sensor->kind == "status")
         field = "state";
      else
         field = "value";
   }

   // on / off (bool): the sensor is on if its state is set (status) or its value is not 0

   if (json_is_boolean(jv))
   {
      bool on {field == "state" ? sensor->state : sensor->value != 0};
      return op == "!=" ? on != json_is_true(jv) : on == json_is_true(jv);
   }

   if (field == "text")
   {
      std::string value {json_is_string(jv) ? json_string_value(jv) : ""};

      if (!json_is_string(jv))
         return false;

      std::string text {sensor->text};
      bool equal {strcasecmp(text.c_str(), value.c_str()) == 0};

      if (op == "!=")
         return !equal;

      return equal;
   }

   double current {field == "state" ? (double)sensor->state : sensor->value};
   double wanted {json_is_boolean(jv) ? (double)json_is_true(jv) : json_is_number(jv) ? json_number_value(jv) : 0.0};

   if (json_is_string(jv))       // "1" or "on" / "off" for a numeric field
   {
      const char* s {json_string_value(jv)};
      wanted = !strcasecmp(s, "on") ? 1 : !strcasecmp(s, "off") ? 0 : atof(s);
   }

   if (op == "!=") return current != wanted;
   if (op == "<")  return current <  wanted;
   if (op == ">")  return current >  wanted;
   if (op == "<=") return current <= wanted;
   if (op == ">=") return current >= wanted;

   return current == wanted;
}

//***************************************************************************
// Set (step 'set')
//***************************************************************************

int HomeCtl::sceneSet(json_t* step, std::string& error)
{
   std::string type;
   uint address {0};

   if (!sceneParseKey(getStringFromJson(step, "key", ""), type, address))
   {
      error = "ungültiger Sensor";
      return fail;
   }

   SensorData* sensor {getSensor(type.c_str(), address)};

   if (!sensor)
   {
      error = std::string("Sensor ") + getStringFromJson(step, "key", "") + " nicht gefunden";
      return fail;
   }

   std::string action {getStringFromJson(step, "action", "")};
   const char* topic {lookupCommandTopic(type.c_str(), address)};

   // color lights: same as the dialog of the WEBIF

   if (action == "color" || action == "ct")
   {
      json_t* obj {json_object()};

      json_object_set_new(obj, "type", json_string(type.c_str()));
      json_object_set_new(obj, "address", json_integer(address));
      json_object_set_new(obj, "action", json_string(action.c_str()));

      for (const char* field : { "hue", "saturation", "bri", "ct" })
         if (json_t* j = json_object_get(step, field))
            json_object_set(obj, field, j);

      int status {performToggleIo(obj, 0)};
      json_decref(obj);

      return status == fail ? fail : success;
   }

   // kind of the value

   enum { vkToggle, vkState, vkNumber, vkText } kind {vkToggle};
   double number {0};
   std::string text;
   json_t* jv {json_object_get(step, "value")};

   if (jv && json_is_boolean(jv))
   {
      kind = vkState;
      number = json_is_true(jv);
   }
   else if (jv && json_is_number(jv))
   {
      kind = vkNumber;
      number = json_number_value(jv);
   }
   else if (jv && json_is_string(jv))
   {
      text = json_string_value(jv);

      if (strcasecmp(text.c_str(), "toggle") == 0)
         kind = vkToggle;
      else if (sensor->choices.empty() && (strcasecmp(text.c_str(), "on") == 0 || strcasecmp(text.c_str(), "off") == 0))
      {
         kind = vkState;
         number = strcasecmp(text.c_str(), "on") == 0;
      }
      else
         kind = vkText;
   }

   tell(eloDetail, "Scene: set '%s:0x%02x' (%s)", type.c_str(), address, topic ? topic : "direct");

   // sensors with a command topic (MQTT: DENON, SAMTV, ...)

   if (!isEmpty(topic))
   {
      if (kind == vkToggle)
         switchCommand(type, address, "toggle", topic, sensor->state ? "0" : "1");
      else if (kind == vkText)
         switchCommand(type, address, "switch", topic, text.c_str());
      else
         switchCommand(type, address, kind == vkState ? "set" : "dim", topic, std::to_string((int)number).c_str());

      return success;
   }

   // the others by the function of the sensor

   if (kind == vkToggle)
      return toggleIo(address, type.c_str());

   if (kind == vkState)
      return toggleIo(address, type.c_str(), (int)number);

   if (kind == vkNumber)
   {
      if (number <= 0 && (type == "DZL" || type == "DZLG"))
         return toggleIo(address, type.c_str(), 0);

      return toggleIo(address, type.c_str(), true, (int)number);
   }

   error = "Text als Wert nur bei Sensoren mit Befehls-Topic möglich";
   return fail;
}

//***************************************************************************
// Start
//***************************************************************************

int HomeCtl::sceneStart(uint address)
{
   if (sceneRunning(address))
      return done;

   sceneInfo[address] = SceneInfo();

   cDbRow* fact {valueFactRowOf("SCN", address)};

   if (!fact)
   {
      tell(eloAlways, "Warning: Scene SCN:0x%02x not found", address);
      return fail;
   }

   std::string settings {fact->getValue("SETTINGS")->isEmpty() ? "" : fact->getStrValue("SETTINGS")};
   tableValueFacts->reset();

   json_t* jSettings {settings.empty() ? nullptr : jsonLoad(settings.c_str(), 0, true)};
   json_t* steps {jSettings ? json_object_get(jSettings, "steps") : nullptr};

   if (!json_is_array(steps) || !json_array_size(steps))
   {
      if (jSettings)
         json_decref(jSettings);

      tell(eloAlways, "Scene SCN:0x%02x has no steps", address);
      sceneStatus(address, false, "keine Schritte");
      return fail;
   }

   SceneRun& run {sceneRuns[address]};

   run.steps = json_incref(steps);
   run.index = 0;
   run.nextMs = 0;
   run.waitSince = 0;
   json_decref(jSettings);

   tell(eloAlways, "Scene: '%s' (SCN:0x%02x) started, %zu steps", sensors["SCN"][address].title.c_str(), address,
        json_array_size(run.steps));

   sceneStatus(address, true, "gestartet");

   return success;
}

//***************************************************************************
// Finish / Stop
//***************************************************************************

void HomeCtl::sceneFinish(uint address, const std::string& text, const char* result, int step)
{
   auto it {sceneRuns.find(address)};

   if (it == sceneRuns.end())
      return;

   sceneInfo[address].warnings = it->second.warnings;
   json_decref(it->second.steps);
   sceneRuns.erase(it);

   tell(eloAlways, "Scene: '%s' (SCN:0x%02x) %s", sensors["SCN"][address].title.c_str(), address, text.c_str());
   sceneStatus(address, false, text, step, result);
}

int HomeCtl::sceneStop(uint address, const char* text)
{
   if (!sceneRunning(address))
      return done;

   sceneFinish(address, text, "aborted");

   return success;
}

//***************************************************************************
// Toggle (click at the widget): running -> abort, else start
//***************************************************************************

int HomeCtl::sceneToggle(uint address, int state)
{
   bool running {sceneRunning(address)};
   bool wanted {state == na ? !running : state != 0};

   if (wanted && !running)
      return sceneStart(address);

   if (!wanted && running)
      return sceneStop(address, "abgebrochen");

   return done;
}

//***************************************************************************
// Run the steps of a scene until a pause
//***************************************************************************

void HomeCtl::sceneRunSteps(uint address)
{
   while (true)
   {
      auto it {sceneRuns.find(address)};

      if (it == sceneRuns.end())
         return;

      SceneRun& run {it->second};
      uint64_t now {cTimeMs::Now()};

      if (run.nextMs > now)
         return;

      size_t count {json_array_size(run.steps)};

      if (run.index >= count)
      {
         sceneFinish(address, "fertig");
         return;
      }

      json_t* step {json_array_get(run.steps, run.index)};
      std::string what {getStringFromJson(step, "do", "")};
      int stepNo {(int)run.index + 1};
      std::string progress {"Schritt " + std::to_string(run.index+1) + "/" + std::to_string(count) + ": "};

      // condition of the step

      json_t* jIf {json_object_get(step, "if")};

      if (jIf && !sceneCondition(jIf))
      {
         tell(eloDetail, "Scene: SCN:0x%02x step %zu (%s) skipped, condition not met", address, run.index+1, what.c_str());
         run.waitSince = 0;
         run.index++;
         continue;
      }

      if (what == "set")
      {
         std::string error;

         if (sceneSet(step, error) != success)
         {
            if (error.empty())
               error = "fehlgeschlagen";

            if (!getBoolFromJson(step, "continue", false))
            {
               sceneFinish(address, "Fehler bei " + progress + error, "error", stepNo);
               return;
            }

            tell(eloAlways, "Scene: SCN:0x%02x step %d failed (%s), going on", address, stepNo, error.c_str());
            run.warnings.push_back({stepNo, error});
         }

         run.index++;
      }
      else if (what == "wait")
      {
         double sec {json_number_value(json_object_get(step, "sec"))};
         run.index++;

         if (sec > 0)
         {
            run.nextMs = now + (uint64_t)(sec * 1000);
            sceneStatus(address, true, progress + sceneDescribe(step), stepNo);
            return;
         }
      }
      else if (what == "waitfor")
      {
         if (sceneCondition(step))
         {
            run.waitSince = 0;
            run.index++;
            continue;
         }

         if (!run.waitSince)
         {
            run.waitSince = now;
            sceneStatus(address, true, progress + sceneDescribe(step), stepNo);
         }

         double timeout {getDoubleFromJson(step, "timeout", 30)};

         if (now - run.waitSince >= (uint64_t)(timeout * 1000))
         {
            tell(eloAlways, "Scene: SCN:0x%02x step %zu, timeout waiting for '%s'", address, run.index+1,
                 getStringFromJson(step, "key", ""));

            if (!getBoolFromJson(step, "continue", false))
            {
               sceneFinish(address, "Timeout bei " + progress + sceneDescribe(step), "timeout", stepNo);
               return;
            }

            tell(eloAlways, "Scene: SCN:0x%02x step %d, timeout, going on", address, stepNo);
            run.warnings.push_back({stepNo, "Timeout"});
            run.waitSince = 0;
            run.index++;
            continue;
         }

         return;     // check again with the next call
      }
      else if (what == "shell")
      {
         std::string cmd {getStringFromJson(step, "cmd", "")};
         std::vector<std::string> args;
         std::string error;
         run.index++;

         if (cmd.empty())
            error = "Befehl fehlt";
         else if (!sceneSplitArgs(cmd, args, error))
            ;
         else if (args.empty() || args[0].empty() || args[0][0] != '/')
            error = "Programm ohne absoluten Pfad";
         else
            sceneShellAllowed(args[0], error);

         if (!error.empty())
         {
            tell(eloAlways, "Scene: SCN:0x%02x step %zu, command '%s' rejected: %s", address, run.index, cmd.c_str(), error.c_str());

            if (!getBoolFromJson(step, "continue", false))
            {
               sceneFinish(address, "Fehler bei " + progress + error, "error", stepNo);
               return;
            }

            run.warnings.push_back({stepNo, error});
         }
         else
         {
            tell(eloAlways, "Scene: SCN:0x%02x step %zu, run '%s'", address, run.index, cmd.c_str());

            try
            {
               std::thread(sceneRunCommand, args).detach();
            }
            catch (const std::system_error& e)
            {
               tell(eloAlways, "Error: Scene: failed to start the thread for '%s'", cmd.c_str());
            }
         }
      }
      else if (what == "stop")
      {
         sceneFinish(address, "beendet bei Schritt " + std::to_string(run.index+1), "stopped", stepNo);
         return;
      }
      else if (what == "scene")
      {
         int other {getIntFromJson(step, "id", na)};
         std::string mode {getStringFromJson(step, "mode", "start")};

         run.index++;

         if (other == na)
         {
            sceneFinish(address, "Fehler bei " + progress + "Szene ohne 'id'", "error", stepNo);
            return;
         }

         if ((uint)other == address)
         {
            if (mode == "stop")        // stops itself
            {
               sceneFinish(address, "beendet bei Schritt " + std::to_string(run.index), "stopped", stepNo);
               return;
            }

            tell(eloAlways, "Scene: SCN:0x%02x step %zu, a scene doesn't start itself", address, run.index);
         }
         else if (mode == "stop")
            sceneStop(other, "abgebrochen");
         else
            sceneStart(other);         // a running scene is not started again, no recursion
      }
      else
      {
         sceneFinish(address, "Fehler bei " + progress + "unbekannter Schritt '" + what + "'", "error", stepNo);
         return;
      }
   }
}

//***************************************************************************
// Process (called by meanwhile())
//***************************************************************************

int HomeCtl::sceneProcess()
{
   if (sceneRuns.empty())
      return done;

   // copy, a step can start or stop other scenes

   std::vector<uint> addresses;

   for (const auto& [address, run] : sceneRuns)
      addresses.push_back(address);

   for (uint address : addresses)
      sceneRunSteps(address);

   return done;
}
