/*
 *  mqttconv.c
 *
 *  (c) 2026 Jörg Wendel
 *
 * This code is distributed under the terms and conditions of the
 * GNU GENERAL PUBLIC LICENSE. See the file COPYING for details.
 *
 *  MQTT converter scripts (<confDir>/mqtt.d, see dispatchOther()) - the WEBIF setup page
 *  'MQTT Konverter' (admin only, the scripts run as root):
 *
 *    <topic with '_' for '/'>.sh   script      called with the MQTT url, topic, message, call count
 *                                  alias       symbolic link to a script (one script for several topics)
 *                                  skip        empty file, the messages of the topic are dropped
 *
 *  The topics received by dispatchOther() (subscribed by 'mqttSensorTopics') are noted with their
 *  last message, the page offers them for a new converter and shows the last message of a topic.
 *
 *  WEBIF event 'mqttconverters', object { "action": "list" | "store" | "delete", ... }:
 *    store:   { "topic": "stat/bbqlight/POWER", "kind": "script" | "alias" | "skip",
 *               "code": "...", "target": "<file of the script>" (alias), "isNew": true }
 *    delete:  { "file": "stat_bbqlight_POWER.sh" }
 *    test:    { "topic": "...", "code": "...", "message": "...", "first": true }
 *             runs the code (also unsaved) with the message, a mosquitto_pub of the test dir in front of
 *             the PATH notes the messages instead of publishing them -> reply event 'mqttconvtest'
 *             { "published": [ "..." ], "output": "...", "status": 0 }
 *  reply (event 'mqttconverters'):
 *    { "dir": "...", "converters": [ { "file", "topic", "kind", "code", "target", "executable" } ],
 *      "subscriptions": [ "stat/bbqlight/+", ... ], "topics": [ { "topic", "message", "time" } ] }
 */

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include "daemon.h"

static constexpr size_t mqttSeenTopicsMax {200};       // noted topics (a '#' subscription may bring many)
static constexpr size_t mqttSeenMessageMax {2000};     // [bytes] of the noted last message
static constexpr off_t mqttConverterCodeMax {64 * 1024};

static std::string joinStrings(const std::vector<std::string>& strings, const char* delim)
{
   std::string result;

   for (const auto& s : strings)
      result += (result.empty() ? "" : delim) + s;

   return result;
}

//***************************************************************************
// Converter Dir / File of a Topic
//***************************************************************************

static std::string converterDir()
{
   return std::string(confDir) + "/mqtt.d";
}

std::string HomeCtl::mqttConverterFile(const std::string& topic)
{
   return strReplace("/", "_", topic) + ".sh";
}

//***************************************************************************
// Note a received Topic (dispatchOther)
//***************************************************************************

void HomeCtl::mqttConverterSeen(const char* topic, const char* message)
{
   if (strncmp(topic, INSTANCE "2mqtt/scripts", strlen(INSTANCE "2mqtt/scripts")) == 0)
      return;                     // the output of the converters (and script sensors)

   if (!mqttSeenTopics.count(topic) && mqttSeenTopics.size() >= mqttSeenTopicsMax)
      return;

   MqttSeenTopic& seen {mqttSeenTopics[topic]};
   seen.message = std::string(message).substr(0, mqttSeenMessageMax);
   seen.time = time(0);
}

//***************************************************************************
// Perform the Event 'mqttconverters'
//***************************************************************************

int HomeCtl::performMqttConverters(json_t* obj, long client)
{
   std::string action {getStringFromJson(obj, "action", "list")};
   int status {success};

   if (action == "store")
   {
      std::string error;

      if ((status = mqttConverterStore(obj, error)) != success)
         return replyResult(fail, client, "%s", error.c_str());

      replyResult(success, client, "Konverter für '%s' gespeichert", getStringFromJson(obj, "topic", ""));
   }
   else if (action == "delete")
   {
      std::string error;

      if ((status = mqttConverterDelete(getStringFromJson(obj, "file", ""), error)) != success)
         return replyResult(fail, client, "%s", error.c_str());

      replyResult(success, client, "Konverter '%s' gelöscht", getStringFromJson(obj, "file", ""));
   }
   else if (action == "test")
   {
      json_t* oJson {json_object()};
      mqttConverterTest(obj, oJson);
      return pushOutMessage(oJson, "mqttconvtest", client);
   }
   else if (action != "list")
      return replyResult(fail, client, "Unbekannte Aktion '%s'", action.c_str());

   json_t* oJson {json_object()};
   mqttConverters2Json(oJson);

   // only to the asking client - the code of the scripts is for admins only

   return pushOutMessage(oJson, "mqttconverters", client);
}

//***************************************************************************
// Converters 2 Json
//***************************************************************************

int HomeCtl::mqttConverters2Json(json_t* obj)
{
   std::string dir {converterDir()};
   json_t* jConverters {json_array()};
   std::map<std::string,std::string> topicOfFile;     // the noted topics, the file name is ambiguous ('_')

   for (const auto& [topic, seen] : mqttSeenTopics)
      topicOfFile[mqttConverterFile(topic)] = topic;

   json_object_set_new(obj, "dir", json_string(dir.c_str()));

   DIR* d {opendir(dir.c_str())};

   if (d)
   {
      std::vector<std::string> files;

      while (struct dirent* entry = readdir(d))
      {
         std::string name {entry->d_name};

         if (name.length() > 3 && name.compare(name.length() - 3, 3, ".sh") == 0)
            files.push_back(name);
      }

      closedir(d);
      std::sort(files.begin(), files.end());

      for (const auto& name : files)
      {
         std::string path {dir + "/" + name};
         struct stat st {};

         if (lstat(path.c_str(), &st) != 0)
            continue;

         json_t* jConv {json_object()};
         json_object_set_new(jConv, "file", json_string(name.c_str()));

         // the topic - a noted one if it matches, else from the file name (each '_' as '/')

         std::string topic {topicOfFile.count(name) ? topicOfFile[name] : strReplace("_", "/", name.substr(0, name.length() - 3))};
         json_object_set_new(jConv, "topic", json_string(topic.c_str()));
         json_object_set_new(jConv, "topicKnown", json_boolean(topicOfFile.count(name)));

         if (S_ISLNK(st.st_mode))
         {
            char target[PATH_MAX+1] {};
            ssize_t len {readlink(path.c_str(), target, PATH_MAX)};
            std::string t {len > 0 ? std::string(target, len) : ""};

            if (t.find('/') != std::string::npos)
               t = t.substr(t.rfind('/') + 1);

            json_object_set_new(jConv, "kind", json_string("alias"));
            json_object_set_new(jConv, "target", json_string(t.c_str()));
            json_object_set_new(jConv, "valid", json_boolean(stat(path.c_str(), &st) == 0));
         }
         else if (S_ISREG(st.st_mode) && st.st_size == 0)
         {
            json_object_set_new(jConv, "kind", json_string("skip"));
         }
         else if (S_ISREG(st.st_mode))
         {
            json_object_set_new(jConv, "kind", json_string("script"));
            json_object_set_new(jConv, "executable", json_boolean(st.st_mode & S_IXUSR));

            if (st.st_size <= mqttConverterCodeMax)
            {
               MemoryStruct data;

               if (loadFromFile(path.c_str(), &data) == success && data.memory)
                  json_object_set_new(jConv, "code", json_stringn(data.memory, data.size));
            }
            else
               json_object_set_new(jConv, "code", json_string("# file too large for the editor"));
         }
         else
         {
            json_decref(jConv);
            continue;
         }

         json_array_append_new(jConverters, jConv);
      }
   }

   json_object_set_new(obj, "converters", jConverters);

   // the subscribed topics of the configuration ('Zusätzliche sensor Topics')

   json_t* jSubscriptions {json_array()};

   for (const auto& t : split(sensorTopics, ','))
      if (!t.empty())
         json_array_append_new(jSubscriptions, json_string(t.c_str()));

   json_object_set_new(obj, "subscriptions", jSubscriptions);

   // the topics received (and not processed by an own dispatcher)

   json_t* jTopics {json_array()};

   for (const auto& [topic, seen] : mqttSeenTopics)
   {
      json_t* jTopic {json_object()};
      json_object_set_new(jTopic, "topic", json_string(topic.c_str()));
      json_object_set_new(jTopic, "message", json_string(seen.message.c_str()));
      json_object_set_new(jTopic, "time", json_integer(seen.time));
      json_array_append_new(jTopics, jTopic);
   }

   json_object_set_new(obj, "topics", jTopics);

   return done;
}

//***************************************************************************
// Store a Converter
//***************************************************************************

int HomeCtl::mqttConverterStore(json_t* obj, std::string& error)
{
   std::string topic {getStringFromJson(obj, "topic", "")};
   std::string kind {getStringFromJson(obj, "kind", "script")};
   bool isNew {getBoolFromJson(obj, "isNew", false)};

   if (topic.empty() || topic.length() > 200)
   {
      error = "Topic fehlt oder ist zu lang";
      return fail;
   }

   if (topic.find_first_of("+#") != std::string::npos)
   {
      error = "Das Topic darf keine Platzhalter (+ #) enthalten, je Topic ein Konverter (ein Skript für mehrere Topics per Alias)";
      return fail;
   }

   for (unsigned char c : topic)
   {
      if (c < 0x20 || c == 0x7f || c == '\'' || c == '"' || c == '\\' || c == ' ')
      {
         error = "Das Topic enthält unzulässige Zeichen (Leerzeichen, Anführungszeichen, Steuerzeichen)";
         return fail;
      }
   }

   std::string dir {converterDir()};
   std::string file {mqttConverterFile(topic)};
   std::string path {dir + "/" + file};
   std::string tmp {dir + "/." + file + ".tmp"};
   struct stat st {};

   if (file[0] == '.')
   {
      error = "Das Topic darf nicht mit '.' beginnen";
      return fail;
   }

   if (isNew && lstat(path.c_str(), &st) == 0)
   {
      error = "Für das Topic '" + topic + "' gibt es schon einen Konverter (" + file + ")";
      return fail;
   }

   mkdir(dir.c_str(), 0755);
   unlink(tmp.c_str());

   if (kind == "alias")
   {
      std::string target {getStringFromJson(obj, "target", "")};
      std::string targetPath {dir + "/" + target};

      if (target.empty() || target.find('/') != std::string::npos || target == file)
      {
         error = "Alias: kein gültiges Skript gewählt";
         return fail;
      }

      if (lstat(targetPath.c_str(), &st) != 0 || !S_ISREG(st.st_mode) || st.st_size == 0)
      {
         error = "Alias: '" + target + "' ist kein Skript (ein Alias kann nur auf ein Skript zeigen)";
         return fail;
      }

      if (symlink(target.c_str(), tmp.c_str()) != 0)
      {
         error = "Anlegen des Links fehlgeschlagen: " + std::string(strerror(errno));
         return fail;
      }
   }
   else if (kind == "skip" || kind == "script")
   {
      std::string code {kind == "script" ? getStringFromJson(obj, "code", "") : ""};

      code = strReplace("\r", "", code);

      if (kind == "script")
      {
         if (code.find_first_not_of(" \t\n") == std::string::npos)
         {
            error = "Das Skript ist leer (zum Verwerfen der Nachrichten die Art 'Verwerfen' wählen)";
            return fail;
         }

         if (code.back() != '\n')
            code += "\n";
      }

      if (storeToFile(tmp.c_str(), code.c_str(), code.length()) != success)
      {
         error = "Schreiben von '" + tmp + "' fehlgeschlagen";
         return fail;
      }

      chmod(tmp.c_str(), kind == "script" ? 0750 : 0644);
   }
   else
   {
      error = "Unbekannte Art '" + kind + "'";
      return fail;
   }

   // a script with aliases may become an alias / skip -> the aliases would point to nothing

   if (kind != "script" && lstat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0)
   {
      std::vector<std::string> aliases {mqttConverterAliasesOf(file)};

      if (!aliases.empty())
      {
         unlink(tmp.c_str());
         error = "Auf das Skript zeigen noch Aliase: " + joinStrings(aliases, ", ");
         return fail;
      }
   }

   // replace (rename() replaces a symbolic link itself, doesn't write through it)

   if (rename(tmp.c_str(), path.c_str()) != 0)
   {
      error = "Speichern als '" + path + "' fehlgeschlagen: " + std::string(strerror(errno));
      unlink(tmp.c_str());
      return fail;
   }

   // the next call is the first one again (CALL_COUNT 1 -> init message of the sensors)

   mqttConverters.erase(path);

   if (kind == "script")
      for (const auto& alias : mqttConverterAliasesOf(file))
         mqttConverters.erase(dir + "/" + alias);

   tell(eloAlways, "MQTT converter '%s' (%s) stored as %s", topic.c_str(), file.c_str(), kind.c_str());

   return success;
}

//***************************************************************************
// Delete a Converter
//***************************************************************************

int HomeCtl::mqttConverterDelete(const std::string& file, std::string& error)
{
   if (file.empty() || file.find('/') != std::string::npos || file[0] == '.' ||
       file.length() < 4 || file.compare(file.length() - 3, 3, ".sh") != 0)
   {
      error = "Ungültiger Dateiname '" + file + "'";
      return fail;
   }

   std::string path {converterDir() + "/" + file};
   struct stat st {};

   if (lstat(path.c_str(), &st) != 0)
   {
      error = "'" + file + "' gibt es nicht";
      return fail;
   }

   if (S_ISREG(st.st_mode) && st.st_size > 0)
   {
      std::vector<std::string> aliases {mqttConverterAliasesOf(file)};

      if (!aliases.empty())
      {
         error = "Auf das Skript zeigen noch Aliase, diese zuerst löschen: " + joinStrings(aliases, ", ");
         return fail;
      }
   }

   if (unlink(path.c_str()) != 0)
   {
      error = "Löschen von '" + path + "' fehlgeschlagen: " + std::string(strerror(errno));
      return fail;
   }

   mqttConverters.erase(path);
   tell(eloAlways, "MQTT converter '%s' deleted", file.c_str());

   return success;
}

//***************************************************************************
// Aliases of a Script (symbolic links in the converter dir pointing to it)
//***************************************************************************

std::vector<std::string> HomeCtl::mqttConverterAliasesOf(const std::string& file)
{
   std::vector<std::string> aliases;
   std::string dir {converterDir()};
   DIR* d {opendir(dir.c_str())};

   if (!d)
      return aliases;

   while (struct dirent* entry = readdir(d))
   {
      std::string path {dir + "/" + entry->d_name};
      struct stat st {};

      if (lstat(path.c_str(), &st) != 0 || !S_ISLNK(st.st_mode))
         continue;

      char target[PATH_MAX+1] {};
      ssize_t len {readlink(path.c_str(), target, PATH_MAX)};
      std::string t {len > 0 ? std::string(target, len) : ""};

      if (t.find('/') != std::string::npos)
         t = t.substr(t.rfind('/') + 1);

      if (t == file)
         aliases.push_back(entry->d_name);
   }

   closedir(d);
   std::sort(aliases.begin(), aliases.end());

   return aliases;
}

//***************************************************************************
// Test a Converter
//   the code of the editor with a message, the messages the script publishes are noted
//   by a mosquitto_pub of the test dir (first in the PATH) instead of being sent; a script
//   calling /usr/bin/mosquitto_pub with the full path would publish for real
//***************************************************************************

static std::string shellQuote(const std::string& s)
{
   return "'" + strReplace("'", "'\\''", s) + "'";
}

int HomeCtl::mqttConverterTest(json_t* obj, json_t* oJson)
{
   static const char* testDir {"/var/cache/homectld/mqtttest"};
   static const char* separator {"\x1e"};                  // between the noted messages

   std::string topic {getStringFromJson(obj, "topic", "")};
   std::string message {getStringFromJson(obj, "message", "")};
   std::string code {strReplace("\r", "", getStringFromJson(obj, "code", ""))};
   bool first {getBoolFromJson(obj, "first", false)};

   std::string script {std::string(testDir) + "/converter.sh"};
   std::string publisher {std::string(testDir) + "/mosquitto_pub"};
   std::string outFile {std::string(testDir) + "/published"};

   json_t* jPublished {json_array()};
   json_object_set_new(oJson, "published", jPublished);

   mkdir("/var/cache/homectld", 0755);
   mkdir(testDir, 0700);
   unlink(outFile.c_str());

   // the stand-in of mosquitto_pub: notes the message (-m), the other options are ignored

   const char* stub {
      "#! /bin/bash\n"
      "# test of the MQTT converters (homectld, mqttconv.c) - notes the message instead of publishing it\n"
      "MSG=\"\"\n"
      "while [[ $# -gt 0 ]]; do\n"
      "   case \"$1\" in\n"
      "      -m) MSG=\"$2\"; shift 2;;\n"
      "      *) shift;;\n"
      "   esac\n"
      "done\n"
      "printf '%s\\x1e' \"${MSG}\" >> \"${MQTT_CONVERTER_TEST_OUT}\"\n"
   };

   if (storeToFile(publisher.c_str(), stub, strlen(stub)) != success ||
       storeToFile(script.c_str(), code.c_str(), code.length()) != success)
   {
      json_object_set_new(oJson, "status", json_integer(fail));
      json_object_set_new(oJson, "output", json_string("Schreiben der Testdateien fehlgeschlagen"));
      return fail;
   }

   chmod(publisher.c_str(), 0700);
   chmod(script.c_str(), 0700);

   std::string url {std::string("mqtt://") + mqttUrlPlain + "/" INSTANCE "2mqtt/scripts"};

   std::string output = executeCommand(5, "cd %s && PATH=%s:$PATH MQTT_CONVERTER_TEST_OUT=%s %s %s %s %s %d 2>&1; echo \"[exit $?]\"",
                                       testDir, testDir, outFile.c_str(), script.c_str(),
                                       shellQuote(url).c_str(), shellQuote(topic).c_str(), shellQuote(message).c_str(),
                                       first ? 1 : 2);

   MemoryStruct data;

   if (fileExists(outFile.c_str()) && loadFromFile(outFile.c_str(), &data) == success && data.memory)
   {
      std::string all(data.memory, data.size);
      size_t start {0}, end {0};

      while ((end = all.find(separator, start)) != std::string::npos)
      {
         json_array_append_new(jPublished, json_string(all.substr(start, end - start).c_str()));
         start = end + 1;
      }
   }

   unlink(outFile.c_str());
   unlink(script.c_str());

   json_object_set_new(oJson, "status", json_integer(success));
   json_object_set_new(oJson, "topic", json_string(topic.c_str()));
   json_object_set_new(oJson, "output", json_string(output.substr(0, 20000).c_str()));

   tell(eloAlways, "MQTT converter for '%s' tested, %zu message(s)", topic.c_str(), json_array_size(jPublished));

   return success;
}
