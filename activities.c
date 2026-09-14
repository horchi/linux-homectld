/*
 *  activities.c
 *
 *  (c) 2026 Jörg Wendel
 *
 * This code is distributed under the terms and conditions of the
 * GNU GENERAL PUBLIC LICENSE. See the file COPYING for details.
 *
 *  Garmin Connect activities
 *
 *  The activities of a Garmin account are fetched on demand (button in the WEBIF,
 *  tab 'Aktivitäten') via the script garmin.py (python module 'garminconnect',
 *  tokens in <confDir>/garmin, see garmin/README.md) and stored in table
 *  'activities'. Nothing is polled, nothing lands in 'samples' or 'valuefacts'.
 *  The type and the name of an activity can be changed, the change is done at
 *  Garmin first and mirrored into the table on success.
 */

#include <sys/stat.h>
#include <set>

#include "daemon.h"

//***************************************************************************
// Call the garmin.py script (stderr is dropped, stdout is JSON)
//***************************************************************************

json_t* Daemon::garminCall(const char* args, int timeout, std::string& error, const char* input)
{
   // input (the bulk commands read their lines from stdin) is passed via a temporary file

   char inputFile[64] {};

   if (input)
   {
      strcpy(inputFile, "/tmp/garmin-XXXXXX");
      int fd {mkstemp(inputFile)};

      if (fd < 0 || write(fd, input, strlen(input)) != (ssize_t)strlen(input))
      {
         error = "Temporäre Datei konnte nicht geschrieben werden";
         tell(eloAlways, "Error: Garmin: %s (%s)", error.c_str(), strerror(errno));

         if (fd >= 0) { close(fd); unlink(inputFile); }

         return nullptr;
      }

      close(fd);
   }

   std::string cmd {std::string("garmin.py --tokens '") + confDir + "/garmin' " + args + " 2>/dev/null"};

   if (input)
      cmd += std::string(" < ") + inputFile;

   tell(eloDetail, "Detail: Garmin: calling '%s'", cmd.c_str());

   std::string result {executeCommand(timeout, "%s", cmd.c_str())};

   if (input)
      unlink(inputFile);

   json_t* jResult {jsonLoad(result.c_str(), 0, true)};

   if (!jResult)
   {
      error = result.empty() ? "garmin.py returned nothing (installed? see garmin/README.md)" : result.substr(0, 200);
      tell(eloAlways, "Error: Garmin: %s", error.c_str());
      return nullptr;
   }

   const char* err {getStringFromJson(jResult, "error")};

   if (!isEmpty(err))
   {
      error = err;
      tell(eloAlways, "Error: Garmin: %s", err);
      json_decref(jResult);
      return nullptr;
   }

   return jResult;
}

bool Daemon::garminConfigured()
{
   char* path {};
   asprintf(&path, "%s/garmin", confDir);
   struct stat st {};
   bool exists {stat(path, &st) == 0 && S_ISDIR(st.st_mode)};
   free(path);

   return exists;
}

//***************************************************************************
// Store one activity (insert or update)
//***************************************************************************

int Daemon::activityStore(json_t* jAct)
{
   long id {getLongFromJson(jAct, "id", 0)};

   if (!id)
      return ignore;

   tableActivities->clear();
   tableActivities->setBigintValue("GARMINID", id);
   bool exists {tableActivities->find() != 0};

   tableActivities->setValue("START", getLongFromJson(jAct, "start", 0));
   tableActivities->setValue("TYPE", getStringFromJson(jAct, "type", "other"));
   tableActivities->setValue("TYPEID", getLongFromJson(jAct, "typeid", 0));
   tableActivities->setValue("PARENTTYPEID", getLongFromJson(jAct, "parenttypeid", 0));
   tableActivities->setValue("NAME", getStringFromJson(jAct, "name", ""));
   tableActivities->setValue("LOCATION", getStringFromJson(jAct, "location", ""));
   tableActivities->setValue("DURATION", getLongFromJson(jAct, "duration", 0));
   tableActivities->setValue("MOVING", getLongFromJson(jAct, "moving", 0));
   tableActivities->setValue("ELAPSED", getLongFromJson(jAct, "elapsed", 0));
   tableActivities->setValue("DISTANCE", getDoubleFromJson(jAct, "distance", 0.0));
   tableActivities->setValue("ELEVGAIN", getDoubleFromJson(jAct, "elevgain", 0.0));
   tableActivities->setValue("ELEVLOSS", getDoubleFromJson(jAct, "elevloss", 0.0));
   tableActivities->setValue("AVGSPEED", getDoubleFromJson(jAct, "avgspeed", 0.0));
   tableActivities->setValue("MAXSPEED", getDoubleFromJson(jAct, "maxspeed", 0.0));
   tableActivities->setValue("AVGHR", getLongFromJson(jAct, "avghr", 0));
   tableActivities->setValue("MAXHR", getLongFromJson(jAct, "maxhr", 0));
   tableActivities->setValue("CALORIES", getLongFromJson(jAct, "calories", 0));
   tableActivities->setValue("STEPS", getLongFromJson(jAct, "steps", 0));
   tableActivities->setValue("LAT", getDoubleFromJson(jAct, "lat", 0.0));
   tableActivities->setValue("LON", getDoubleFromJson(jAct, "lon", 0.0));

   if (exists)
   {
      // the activity may have been changed at Garmin -> drop the cached details

      tableActivities->getValue("DETAILS")->setNull();
      tableActivities->update();
   }
   else
      tableActivities->insert();

   tableActivities->reset();

   return exists ? done : success;    // success -> new
}

//***************************************************************************
// Sync - fetch the activities from Garmin ('since' YYYY-MM-DD or empty -> all)
//***************************************************************************

int Daemon::activitiesSync(const char* since, std::string& message)
{
   std::string args {"activities "};

   if (!isEmpty(since))
      args += std::string("--since ") + since;
   else
      args += "--all";

   std::string error;
   json_t* jResult {garminCall(args.c_str(), 600, error)};

   if (!jResult)
   {
      message = error;
      return fail;
   }

   json_t* jActs {getObjectFromJson(jResult, "activities")};
   int added {0};
   int updated {0};
   int removed {0};
   size_t i {0};
   json_t* jAct {};
   std::set<long> ids;

   connection->startTransaction();

   json_array_foreach(jActs, i, jAct)
   {
      int status {activityStore(jAct)};
      ids.insert(getLongFromJson(jAct, "id", 0));

      if (status == success)
         added++;
      else if (status == done)
         updated++;
   }

   // full sync: activities deleted at Garmin are removed here too (with their track)

   if (isEmpty(since) && json_array_size(jActs))
   {
      std::vector<long> gone;
      tableActivities->clear();

      for (int f = selectActivities->find(); f; f = selectActivities->fetch())
      {
         long id {(long)tableActivities->getBigintValue("GARMINID")};

         if (!ids.count(id))
            gone.push_back(id);
      }

      selectActivities->freeResult();

      for (long id : gone)
      {
         tableActivities->deleteWhere("garminid = %ld", id);
         tableActivityTracks->deleteWhere("garminid = %ld", id);
         removed++;
      }
   }

   connection->commit();
   json_decref(jResult);

   message = std::to_string(added) + " neue Aktivitäten, " + std::to_string(updated) + " aktualisiert";

   if (removed)
      message += ", " + std::to_string(removed) + " entfernt";
   tell(eloAlways, "Info: Garmin: sync %s -> %s", isEmpty(since) ? "all" : since, message.c_str());

   return success;
}

//***************************************************************************
// Activities to JSON (for the WEBIF)
//***************************************************************************

int Daemon::activities2Json(json_t* obj)
{
   json_t* oActs {json_array()};
   long newest {0};

   // the activities with a cached track (flag in the list)

   std::set<long> withTrack;
   tableActivityTracks->clear();

   for (int f = selectActivityTrackIds->find(); f; f = selectActivityTrackIds->fetch())
      withTrack.insert((long)tableActivityTracks->getBigintValue("GARMINID"));

   selectActivityTrackIds->freeResult();
   tableActivities->clear();

   for (int f = selectActivities->find(); f; f = selectActivities->fetch())
   {
      json_t* oAct {json_object()};
      long start {tableActivities->getTimeValue("START")};
      long id {(long)tableActivities->getBigintValue("GARMINID")};

      if (start > newest)
         newest = start;

      json_object_set_new(oAct, "id", json_integer(id));
      json_object_set_new(oAct, "hasdetails", json_boolean(!tableActivities->getValue("DETAILS")->isNull() && !isEmpty(tableActivities->getStrValue("DETAILS"))));
      json_object_set_new(oAct, "hastrack", json_boolean(withTrack.count(id) > 0));
      json_object_set_new(oAct, "start", json_integer(start));
      json_object_set_new(oAct, "type", json_string(tableActivities->getStrValue("TYPE")));
      json_object_set_new(oAct, "name", json_string(tableActivities->getStrValue("NAME")));
      json_object_set_new(oAct, "location", json_string(tableActivities->getStrValue("LOCATION")));
      json_object_set_new(oAct, "duration", json_integer(tableActivities->getIntValue("DURATION")));
      json_object_set_new(oAct, "moving", json_integer(tableActivities->getIntValue("MOVING")));
      json_object_set_new(oAct, "elapsed", json_integer(tableActivities->getIntValue("ELAPSED")));
      json_object_set_new(oAct, "distance", json_real(tableActivities->getFloatValue("DISTANCE")));
      json_object_set_new(oAct, "elevgain", json_real(tableActivities->getFloatValue("ELEVGAIN")));
      json_object_set_new(oAct, "elevloss", json_real(tableActivities->getFloatValue("ELEVLOSS")));
      json_object_set_new(oAct, "avgspeed", json_real(tableActivities->getFloatValue("AVGSPEED")));
      json_object_set_new(oAct, "maxspeed", json_real(tableActivities->getFloatValue("MAXSPEED")));
      json_object_set_new(oAct, "avghr", json_integer(tableActivities->getIntValue("AVGHR")));
      json_object_set_new(oAct, "maxhr", json_integer(tableActivities->getIntValue("MAXHR")));
      json_object_set_new(oAct, "calories", json_integer(tableActivities->getIntValue("CALORIES")));
      json_object_set_new(oAct, "steps", json_integer(tableActivities->getIntValue("STEPS")));
      json_object_set_new(oAct, "lat", json_real(tableActivities->getFloatValue("LAT")));
      json_object_set_new(oAct, "lon", json_real(tableActivities->getFloatValue("LON")));
      json_array_append_new(oActs, oAct);
   }

   selectActivities->freeResult();

   json_object_set_new(obj, "activities", oActs);
   json_object_set_new(obj, "newest", json_integer(newest));
   json_object_set_new(obj, "configured", json_boolean(garminConfigured()));

   return success;
}

int Daemon::activitiesPush(long client)
{
   json_t* oJson {json_object()};
   activities2Json(oJson);

   return pushOutMessage(oJson, "activities", client);
}

//***************************************************************************
// Details - cached in the table, fetched from Garmin on the first request
//***************************************************************************

json_t* Daemon::activityDetails(long id, bool force, std::string& error)
{
   tableActivities->clear();
   tableActivities->setBigintValue("GARMINID", id);

   if (!tableActivities->find())
   {
      tableActivities->reset();
      error = "Aktivität nicht gefunden";
      return nullptr;
   }

   json_t* jDetails {};

   if (!force && !tableActivities->getValue("DETAILS")->isNull() && !isEmpty(tableActivities->getStrValue("DETAILS")))
   {
      jDetails = jsonLoad(tableActivities->getStrValue("DETAILS"), 0, true);

      if (jDetails)
         json_object_set_new(jDetails, "cached", json_true());
   }

   if (!jDetails)
   {
      std::string args {"details " + std::to_string(id)};
      jDetails = garminCall(args.c_str(), 60, error);

      if (jDetails)
      {
         char* dump {json_dumps(jDetails, JSON_COMPACT)};
         tableActivities->setValue("DETAILS", dump);
         tableActivities->update();
         free(dump);
         json_object_set_new(jDetails, "cached", json_false());
      }
   }

   tableActivities->reset();

   return jDetails;
}

//***************************************************************************
// Track - simplified by Garmin to 1000 points, at least one point per 15 s,
//  cached in 'activitytracks'
//***************************************************************************

json_t* Daemon::activityTrack(long id, bool force, std::string& error)
{
   tableActivities->clear();
   tableActivities->setBigintValue("GARMINID", id);

   if (!tableActivities->find())
   {
      tableActivities->reset();
      error = "Aktivität nicht gefunden";
      return nullptr;
   }

   long duration {std::max(tableActivities->getIntValue("ELAPSED"), tableActivities->getIntValue("DURATION"))};
   tableActivities->reset();

   long points {std::max(1000L, (duration + 14) / 15)};

   tableActivityTracks->clear();
   tableActivityTracks->setBigintValue("GARMINID", id);
   bool exists {tableActivityTracks->find() != 0};
   bool cached {exists && !isEmpty(tableActivityTracks->getStrValue("TRACK"))};
   json_t* jTrack {};

   if (cached && !force)
   {
      jTrack = json_object();
      json_object_set_new(jTrack, "id", json_integer(id));
      json_object_set_new(jTrack, "count", json_integer(tableActivityTracks->getIntValue("POINTS")));
      json_object_set_new(jTrack, "points", jsonLoad(tableActivityTracks->getStrValue("TRACK"), 0, true));
      json_object_set_new(jTrack, "cached", json_true());
   }
   else
   {
      std::string args {"track " + std::to_string(id) + " --points " + std::to_string(points)};
      jTrack = garminCall(args.c_str(), 120, error);

      if (jTrack)
      {
         char* dump {json_dumps(getObjectFromJson(jTrack, "points"), JSON_COMPACT)};
         tableActivityTracks->setValue("POINTS", getLongFromJson(jTrack, "count", 0));
         tableActivityTracks->setValue("TRACK", dump);

         if (exists)
            tableActivityTracks->update();
         else
            tableActivityTracks->insert();

         free(dump);
         json_object_set_new(jTrack, "cached", json_false());
         tell(eloAlways, "Info: Garmin: loaded track of activity %ld (%ld points requested, %ld received)", id, (long)points, getLongFromJson(jTrack, "count", 0));
      }
   }

   tableActivityTracks->reset();

   return jTrack;
}

// keep the cached details in sync with a changed type / name

void Daemon::activityDetailsPatch(long id, const char* key, const char* value)
{
   tableActivities->clear();
   tableActivities->setBigintValue("GARMINID", id);

   if (tableActivities->find() && !tableActivities->getValue("DETAILS")->isNull() && !isEmpty(tableActivities->getStrValue("DETAILS")))
   {
      json_t* jDetails {jsonLoad(tableActivities->getStrValue("DETAILS"), 0, true)};

      if (jDetails)
      {
         json_object_set_new(jDetails, key, json_string(value));
         char* dump {json_dumps(jDetails, JSON_COMPACT)};
         tableActivities->setValue("DETAILS", dump);
         tableActivities->update();
         free(dump);
         json_decref(jDetails);
      }
   }

   tableActivities->reset();
}

//***************************************************************************
// Change type / rename (at Garmin first, then in the table)
//***************************************************************************

int Daemon::activityChangeType(long id, const char* type, std::string& error)
{
   std::string args {"settype " + std::to_string(id) + " '" + type + "'"};
   json_t* jResult {garminCall(args.c_str(), 60, error)};

   if (!jResult)
      return fail;

   tableActivities->clear();
   tableActivities->setBigintValue("GARMINID", id);

   if (tableActivities->find())
   {
      tableActivities->setValue("TYPE", getStringFromJson(jResult, "type", type));
      tableActivities->setValue("TYPEID", getLongFromJson(jResult, "typeid", 0));
      tableActivities->setValue("PARENTTYPEID", getLongFromJson(jResult, "parenttypeid", 0));
      tableActivities->update();
   }

   tableActivities->reset();
   activityDetailsPatch(id, "type", getStringFromJson(jResult, "type", type));
   json_decref(jResult);

   return success;
}

int Daemon::activityRename(long id, const char* name, std::string& error)
{
   if (strchr(name, '\''))
   {
      error = "Kein Apostroph im Namen";
      return fail;
   }

   std::string args {"rename " + std::to_string(id) + " '" + name + "'"};
   json_t* jResult {garminCall(args.c_str(), 60, error)};

   if (!jResult)
      return fail;

   json_decref(jResult);
   tableActivities->clear();
   tableActivities->setBigintValue("GARMINID", id);

   if (tableActivities->find())
   {
      tableActivities->setValue("NAME", name);
      tableActivities->update();
   }

   tableActivities->reset();
   activityDetailsPatch(id, "name", name);

   return success;
}

int Daemon::activitySetLocation(long id, const char* location, std::string& error)
{
   if (strchr(location, '\''))
   {
      error = "Kein Apostroph im Ort";
      return fail;
   }

   std::string args {"setlocation " + std::to_string(id) + " '" + location + "'"};
   json_t* jResult {garminCall(args.c_str(), 60, error)};

   if (!jResult)
      return fail;

   json_decref(jResult);
   tableActivities->clear();
   tableActivities->setBigintValue("GARMINID", id);

   if (tableActivities->find())
   {
      tableActivities->setValue("LOCATION", location);
      tableActivities->update();
   }

   tableActivities->reset();
   activityDetailsPatch(id, "location", location);

   return success;
}

//***************************************************************************
// Bulk Edit - type, name and location for a list of activities, empty = unchanged
//   one garmin.py call (one login) per field, the ids via stdin
//***************************************************************************

int Daemon::activitiesBulkEdit(json_t* jIds, const char* type, const char* name, const char* location, std::string& message)
{
   size_t count {json_array_size(jIds)};

   if (!count)
   {
      message = "Keine Aktivitäten gewählt";
      return fail;
   }

   if (strchr(name, '\'') || strchr(location, '\''))
   {
      message = "Kein Apostroph in Name oder Ort";
      return fail;
   }

   if (isEmpty(type) && isEmpty(name) && isEmpty(location))
   {
      message = "Keine Änderung";
      return success;
   }

   int timeout {60 + (int)count * 2};        // garmin.py pauses 0.5 s per activity
   std::string ids;
   std::string lines;                        // "id<TAB>value" for renames / setlocations
   size_t i {0};
   json_t* jId {};

   json_array_foreach(jIds, i, jId)
      ids += std::to_string(json_integer_value(jId)) + "\n";

   struct Field { const char* key; const char* column; const char* command; const char* value; const char* label; };
   Field fields[] {{ "type", "TYPE", "settypes", type, "Typ" }, { "name", "NAME", "renames", name, "Name" }, { "location", "LOCATION", "setlocations", location, "Ort" }};
   int failed {0};

   for (const auto& field : fields)
   {
      if (isEmpty(field.value))
         continue;

      std::string error;
      std::string args {field.command};
      json_t* jResult {};

      if (strcmp(field.key, "type") == 0)
      {
         args += std::string(" '") + type + "'";
         jResult = garminCall(args.c_str(), timeout, error, ids.c_str());
      }
      else
      {
         lines.clear();
         json_array_foreach(jIds, i, jId)
            lines += std::to_string(json_integer_value(jId)) + "\t" + field.value + "\n";

         jResult = garminCall(args.c_str(), timeout, error, lines.c_str());
      }

      if (!jResult)
      {
         message = std::string(field.label) + ": " + error;
         return fail;
      }

      // update the succeeded ones locally

      json_t* jOk {json_object_get(jResult, "ok")};
      json_t* jItem {};
      int changed {0};

      json_array_foreach(jOk, i, jItem)
      {
         long id {json_is_integer(jItem) ? (long)json_integer_value(jItem) : getLongFromJson(jItem, "id", 0)};

         tableActivities->clear();
         tableActivities->setBigintValue("GARMINID", id);

         if (tableActivities->find())
         {
            tableActivities->setValue(field.column, field.value);

            if (strcmp(field.key, "type") == 0)
            {
               tableActivities->setValue("TYPEID", getLongFromJson(jResult, "typeid", 0));
               tableActivities->setValue("PARENTTYPEID", getLongFromJson(jResult, "parenttypeid", 0));
            }

            tableActivities->update();
            changed++;
         }

         tableActivities->reset();
         activityDetailsPatch(id, field.key, field.value);
      }

      failed += json_array_size(json_object_get(jResult, "failed"));
      message += (message.empty() ? "" : ", ") + std::string(field.label) + " bei " + std::to_string(changed) + " geändert";
      tell(eloAlways, "Info: Garmin: bulk %s of %zu activities, %d changed, %zu failed", field.key, count, changed, json_array_size(json_object_get(jResult, "failed")));
      json_decref(jResult);
   }

   if (failed)
      message += ", " + std::to_string(failed) + " fehlgeschlagen";

   return success;
}

//***************************************************************************
// Perform Activities (websocket event 'activities')
//***************************************************************************

int Daemon::performActivities(json_t* obj, long client)
{
   std::string action {getStringFromJson(obj, "action", "list")};

   if (action == "list")
      return activitiesPush(client);

   if (action == "details")
   {
      std::string error;
      json_t* jDetails {activityDetails(getLongFromJson(obj, "id", 0), getBoolFromJson(obj, "force", false), error)};

      if (!jDetails)
         return replyResult(fail, error.c_str(), client);

      return pushOutMessage(jDetails, "activitydetails", client, false, 8);
   }

   if (action == "track")
   {
      std::string error;
      json_t* jTrack {activityTrack(getLongFromJson(obj, "id", 0), getBoolFromJson(obj, "force", false), error)};

      if (!jTrack)
         return replyResult(fail, error.c_str(), client);

      return pushOutMessage(jTrack, "activitytrack", client, false, 10);   // coordinates
   }

   if (action == "types")
   {
      std::string error;
      json_t* jResult {garminCall("types", 60, error)};

      if (!jResult)
         return replyResult(fail, error.c_str(), client);

      return pushOutMessage(jResult, "activitytypes", client);
   }

   // modifying actions need control rights

   auto itClient {wsClients.find((void*)client)};

   if (itClient == wsClients.end() || !(itClient->second.rights & urControl))
      return replyResult(fail, "Keine Berechtigung", client);

   if (action == "sync")
   {
      std::string message;
      int status {activitiesSync(getStringFromJson(obj, "since", ""), message)};

      if (status == success)
         activitiesPush(0);      // all clients

      return replyResult(status, message.c_str(), client);
   }

   if (action == "settype")
   {
      std::string error;

      if (activityChangeType(getLongFromJson(obj, "id", 0), getStringFromJson(obj, "type", ""), error) != success)
         return replyResult(fail, error.c_str(), client);

      activitiesPush(0);
      return replyResult(success, "Typ geändert", client);
   }

   if (action == "rename")
   {
      std::string error;

      if (activityRename(getLongFromJson(obj, "id", 0), getStringFromJson(obj, "name", ""), error) != success)
         return replyResult(fail, error.c_str(), client);

      activitiesPush(0);
      return replyResult(success, "Umbenannt", client);
   }

   if (action == "delete")
   {
      // at Garmin first, then locally (with the track)

      long id {getLongFromJson(obj, "id", 0)};
      std::string error;
      std::string args {"delete " + std::to_string(id)};
      json_t* jResult {garminCall(args.c_str(), 60, error)};

      if (!jResult)
         return replyResult(fail, error.c_str(), client);

      json_decref(jResult);
      tableActivities->deleteWhere("garminid = %ld", id);
      tableActivityTracks->deleteWhere("garminid = %ld", id);
      tell(eloAlways, "Info: Garmin: deleted activity %ld", id);
      activitiesPush(0);

      return replyResult(success, "Aktivität gelöscht", client);
   }

   if (action == "edit")
   {
      // type, name and location from the edit dialog, only the changed ones are sent to Garmin
      //   'ids' (array) -> bulk edit of several activities, 'id' -> one

      const char* type {getStringFromJson(obj, "type", "")};
      const char* name {getStringFromJson(obj, "name", "")};
      const char* location {getStringFromJson(obj, "location", "")};
      json_t* jIds {json_object_get(obj, "ids")};
      std::string error;
      std::string message;

      if (json_is_array(jIds) && json_array_size(jIds) > 1)
      {
         int status {activitiesBulkEdit(jIds, type, name, location, message)};

         if (status == success)
            activitiesPush(0);

         return replyResult(status, message.c_str(), client);
      }

      long id {json_is_array(jIds) && json_array_size(jIds) == 1 ? (long)json_integer_value(json_array_get(jIds, 0)) : getLongFromJson(obj, "id", 0)};

      tableActivities->clear();
      tableActivities->setBigintValue("GARMINID", id);

      if (!tableActivities->find())
      {
         tableActivities->reset();
         return replyResult(fail, "Aktivität nicht gefunden", client);
      }

      bool typeChanged {!isEmpty(type) && strcmp(type, tableActivities->getStrValue("TYPE")) != 0};
      bool nameChanged {!isEmpty(name) && strcmp(name, tableActivities->getStrValue("NAME")) != 0};
      bool locationChanged {!isEmpty(location) && strcmp(location, tableActivities->getStrValue("LOCATION")) != 0};
      tableActivities->reset();

      if (typeChanged)
      {
         if (activityChangeType(id, type, error) != success)
            return replyResult(fail, error.c_str(), client);

         message = "Typ geändert";
      }

      if (nameChanged)
      {
         if (activityRename(id, name, error) != success)
            return replyResult(fail, error.c_str(), client);

         message += (message.empty() ? "" : ", ") + std::string("umbenannt");
      }

      if (locationChanged)
      {
         if (activitySetLocation(id, location, error) != success)
            return replyResult(fail, error.c_str(), client);

         message += (message.empty() ? "" : ", ") + std::string("Ort geändert");
      }

      if (!typeChanged && !nameChanged && !locationChanged)
         return replyResult(success, "Keine Änderung", client);

      activitiesPush(0);
      return replyResult(success, message.c_str(), client);
   }

   return replyResult(fail, "Unbekannte Aktion", client);
}
