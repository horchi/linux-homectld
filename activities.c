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
#include <cmath>

#include "daemon.h"

//***************************************************************************
// Call the garmin.py script (stderr is dropped, stdout is JSON)
//***************************************************************************

json_t* HomeCtl::garminCall(const char* args, int timeout, std::string& error, const char* input)
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

bool HomeCtl::garminConfigured()
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

int HomeCtl::activityStore(json_t* jAct)
{
   long id {getLongFromJson(jAct, "id", 0)};

   if (!id)
      return ignore;

   tableActivities->clear();
   tableActivities->setBigintValue("GARMINID", id);
   bool exists {tableActivities->find()};

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

   // the values of the list entry become a preliminary version of the details ("full": false),
   //   the same format as 'garmin.py details' delivers; a changed activity gets them fresh, too.
   //   JSON_ENSURE_ASCII: the tables are utf8mb3, an emoji (4 byte) in the description broke the insert

   json_t* jSummary {json_object_get(jAct, "summary")};
   bool haveFull {exists && !tableActivities->getValue("DETAILS")->isNull() && activityDetailsFull(tableActivities->getStrValue("DETAILS"))};

   if (json_is_object(jSummary) && haveFull)
   {
      // full details cached: keep them, refresh name / type / location and the values the list has

      json_t* jDetails {jsonLoad(tableActivities->getStrValue("DETAILS"), 0, true)};

      if (jDetails)
      {
         json_object_set_new(jDetails, "name", json_string(getStringFromJson(jAct, "name", "")));
         json_object_set_new(jDetails, "type", json_string(getStringFromJson(jAct, "type", "other")));
         json_object_set_new(jDetails, "location", json_string(getStringFromJson(jAct, "location", "")));

         json_t* jOldSummary {json_object_get(jDetails, "summary")};

         if (json_is_object(jOldSummary))
            json_object_update(jOldSummary, jSummary);
         else
            json_object_set(jDetails, "summary", jSummary);

         char* dump {json_dumps(jDetails, JSON_COMPACT | JSON_ENSURE_ASCII)};
         tableActivities->setValue("DETAILS", dump);
         free(dump);
         json_decref(jDetails);
      }
   }
   else if (json_is_object(jSummary))
   {
      json_t* jDetails {json_object()};
      json_object_set_new(jDetails, "id", json_integer(id));
      json_object_set_new(jDetails, "name", json_string(getStringFromJson(jAct, "name", "")));
      json_object_set_new(jDetails, "description", json_string(getStringFromJson(jAct, "description", "")));
      json_object_set_new(jDetails, "type", json_string(getStringFromJson(jAct, "type", "other")));
      json_object_set_new(jDetails, "location", json_string(getStringFromJson(jAct, "location", "")));
      json_object_set(jDetails, "summary", jSummary);
      json_object_set_new(jDetails, "full", json_false());

      char* dump {json_dumps(jDetails, JSON_COMPACT | JSON_ENSURE_ASCII)};
      tableActivities->setValue("DETAILS", dump);
      free(dump);
      json_decref(jDetails);
   }
   else if (exists)
      tableActivities->getValue("DETAILS")->setNull();    // old script without the summary: drop the cached details

   if (exists)
      tableActivities->update();
   else
      tableActivities->insert();

   tableActivities->reset();

   return exists ? done : yes;        // yes -> new
}

// the cached details are the full ones (loaded with 'garmin.py details'), not the preliminary
//   version from the list entry (json_dumps writes the flag exactly like this)

bool HomeCtl::activityDetailsFull(const char* details)
{
   return !isEmpty(details) && !strstr(details, "\"full\":false");
}

//***************************************************************************
// Sync - fetch the activities from Garmin ('since' YYYY-MM-DD or empty -> all)
//***************************************************************************

int HomeCtl::activitiesSync(const char* since, std::string& message)
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

      if (status == yes)
         added++;
      else if (status == done)
         updated++;
   }

   // full sync: activities deleted at Garmin are removed here too (with their track)

   if (isEmpty(since) && json_array_size(jActs))
   {
      std::vector<long> gone;
      tableActivities->clear();

      for (bool f = selectActivities->find(); f; f = selectActivities->fetch())
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

int HomeCtl::activities2Json(json_t* obj)
{
   json_t* oActs {json_array()};
   long newest {0};

   // the activities with a cached track (flag in the list)

   std::set<long> withTrack;
   tableActivityTracks->clear();

   for (bool f = selectActivityTrackIds->find(); f; f = selectActivityTrackIds->fetch())
      withTrack.insert((long)tableActivityTracks->getBigintValue("GARMINID"));

   selectActivityTrackIds->freeResult();
   activitiesFillTrackDistance(withTrack);
   tableActivities->clear();

   for (bool f = selectActivities->find(); f; f = selectActivities->fetch())
   {
      json_t* oAct {json_object()};
      long start {tableActivities->getTimeValue("START")};
      long id {(long)tableActivities->getBigintValue("GARMINID")};

      if (start > newest)
         newest = start;

      json_object_set_new(oAct, "id", json_integer(id));
      json_object_set_new(oAct, "hasdetails", json_boolean(!tableActivities->getValue("DETAILS")->isNull() && activityDetailsFull(tableActivities->getStrValue("DETAILS"))));
      json_object_set_new(oAct, "hastrack", json_boolean(withTrack.count(id) > 0));
      json_object_set_new(oAct, "start", json_integer(start));
      json_object_set_new(oAct, "type", json_string(tableActivities->getStrValue("TYPE")));
      json_object_set_new(oAct, "name", json_string(tableActivities->getStrValue("NAME")));
      json_object_set_new(oAct, "location", json_string(tableActivities->getStrValue("LOCATION")));
      json_object_set_new(oAct, "duration", json_integer(tableActivities->getIntValue("DURATION")));
      json_object_set_new(oAct, "moving", json_integer(tableActivities->getIntValue("MOVING")));
      json_object_set_new(oAct, "elapsed", json_integer(tableActivities->getIntValue("ELAPSED")));

      // Garmin's distance, or the one of the stored track if Garmin's deviates too much

      double garminDistance {tableActivities->getFloatValue("DISTANCE")};
      double trackDistance {tableActivities->getValue("TRACKDISTANCE")->isNull() ? 0.0 : tableActivities->getFloatValue("TRACKDISTANCE")};
      bool fromTrack {activityUseTrackDistance(garminDistance, trackDistance)};

      json_object_set_new(oAct, "distance", json_real(fromTrack ? trackDistance : garminDistance));
      json_object_set_new(oAct, "garmindistance", json_real(garminDistance));
      json_object_set_new(oAct, "trackdistance", json_real(trackDistance));
      json_object_set_new(oAct, "distancefromtrack", json_boolean(fromTrack));
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

int HomeCtl::activitiesPush(long client)
{
   json_t* oJson {json_object()};
   activities2Json(oJson);

   return pushOutMessage(oJson, "activities", client);
}

//***************************************************************************
// Details - cached in the table, fetched from Garmin on the first request
//***************************************************************************

json_t* HomeCtl::activityDetails(long id, bool force, std::string& error)
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
         // values only the list entry has (e.g. the training load) stay

         if (!tableActivities->getValue("DETAILS")->isNull() && !isEmpty(tableActivities->getStrValue("DETAILS")))
         {
            json_t* jOld {jsonLoad(tableActivities->getStrValue("DETAILS"), 0, true)};
            json_t* jOldSummary {json_object_get(jOld, "summary")};
            json_t* jSummary {json_object_get(jDetails, "summary")};

            if (json_is_object(jOldSummary) && json_is_object(jSummary))
               json_object_update_missing(jSummary, jOldSummary);

            json_decref(jOld);
         }

         json_object_set_new(jDetails, "full", json_true());
         char* dump {json_dumps(jDetails, JSON_COMPACT | JSON_ENSURE_ASCII)};
         tableActivities->setValue("DETAILS", dump);
         tableActivities->update();
         free(dump);
         json_object_set_new(jDetails, "cached", json_false());
      }
   }

   if (jDetails)
   {
      double garminDistance {tableActivities->getFloatValue("DISTANCE")};
      double trackDistance {tableActivities->getValue("TRACKDISTANCE")->isNull() ? 0.0 : tableActivities->getFloatValue("TRACKDISTANCE")};

      json_object_set_new(jDetails, "trackdistance", json_real(trackDistance));
      json_object_set_new(jDetails, "distancefromtrack", json_boolean(activityUseTrackDistance(garminDistance, trackDistance)));
   }

   tableActivities->reset();

   return jDetails;
}

//***************************************************************************
// Track - simplified by Garmin to 1000 points, at least one point per 15 s,
//  cached in 'activitytracks'
//***************************************************************************

json_t* HomeCtl::activityTrack(long id, bool force, std::string& error)
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
   bool distanceKnown {!tableActivities->getValue("TRACKDISTANCE")->isNull()};
   double garminDistance {tableActivities->getFloatValue("DISTANCE")};
   double trackDistance {distanceKnown ? tableActivities->getFloatValue("TRACKDISTANCE") : 0.0};
   tableActivities->reset();

   long points {std::max(1000L, (duration + 14) / 15)};

   tableActivityTracks->clear();
   tableActivityTracks->setBigintValue("GARMINID", id);
   bool exists {tableActivityTracks->find()};
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

   if (jTrack)
   {
      // the track's distance, with the reply the list can update the row without a new list

      if (!cached || !distanceKnown)
         trackDistance = activityStoreTrackDistance(id, getObjectFromJson(jTrack, "points"));

      json_object_set_new(jTrack, "trackdistance", json_real(trackDistance));
      json_object_set_new(jTrack, "garmindistance", json_real(garminDistance));
      json_object_set_new(jTrack, "distancefromtrack", json_boolean(activityUseTrackDistance(garminDistance, trackDistance)));
   }

   return jTrack;
}

//***************************************************************************
// Track Distance - the watch sums up (almost) no distance in some water sport sessions
//   while the GPS track is complete; the sum of the track's segments is stored with the
//   activity and shown instead of Garmin's value when both differ by more than
//   'garminTrackDeviation' percent. Computed when a track is loaded (and once for the
//   tracks stored before), the sync of the list doesn't load tracks.
//***************************************************************************

double HomeCtl::activityTrackDistance(json_t* jPoints)
{
   // points: [lat, lon, time, alt, speed]

   double distance {0.0};
   GpsCoordinate last {};
   bool haveLast {false};
   size_t i {0};
   json_t* jPoint {};

   json_array_foreach(jPoints, i, jPoint)
   {
      if (!json_is_array(jPoint) || json_array_size(jPoint) < 2)
         continue;

      GpsCoordinate c {json_number_value(json_array_get(jPoint, 0)), json_number_value(json_array_get(jPoint, 1))};

      if (haveLast)
         distance += gpsDistance(last, c);

      last = c;
      haveLast = true;
   }

   return distance;
}

double HomeCtl::activityStoreTrackDistance(long id, json_t* jPoints)
{
   double distance {json_is_array(jPoints) ? activityTrackDistance(jPoints) : 0.0};

   tableActivities->clear();
   tableActivities->setBigintValue("GARMINID", id);

   if (tableActivities->find())
   {
      double garminDistance {tableActivities->getFloatValue("DISTANCE")};
      tableActivities->setValue("TRACKDISTANCE", distance);
      tableActivities->update();

      if (activityUseTrackDistance(garminDistance, distance))
         tell(eloAlways, "Info: Garmin: activity %ld: distance %.0f m (Garmin) vs. %.0f m (track), showing the track's", id, garminDistance, distance);
   }

   tableActivities->reset();

   return distance;
}

void HomeCtl::activitiesFillTrackDistance(const std::set<long>& ids)
{
   // tracks stored before the column TRACKDISTANCE existed; one pass per daemon run is enough,
   //   tracks loaded later store their distance directly

   static bool done {false};

   if (done)
      return;

   done = true;

   for (long id : ids)
   {
      tableActivities->clear();
      tableActivities->setBigintValue("GARMINID", id);
      bool missing {tableActivities->find() && tableActivities->getValue("TRACKDISTANCE")->isNull()};
      tableActivities->reset();

      if (!missing)
         continue;

      tableActivityTracks->clear();
      tableActivityTracks->setBigintValue("GARMINID", id);

      if (tableActivityTracks->find())
      {
         json_t* jPoints {jsonLoad(tableActivityTracks->getStrValue("TRACK"), 0, true)};
         activityStoreTrackDistance(id, jPoints);
         json_decref(jPoints);
      }

      tableActivityTracks->reset();
   }
}

bool HomeCtl::activityUseTrackDistance(double garminDistance, double trackDistance)
{
   if (garminTrackDeviation <= 0 || trackDistance <= 0.0)
      return false;

   return fabs(garminDistance - trackDistance) / trackDistance * 100.0 > garminTrackDeviation;
}

// keep the cached details in sync with a changed type / name

void HomeCtl::activityDetailsPatch(long id, const char* key, const char* value)
{
   tableActivities->clear();
   tableActivities->setBigintValue("GARMINID", id);

   if (tableActivities->find() && !tableActivities->getValue("DETAILS")->isNull() && !isEmpty(tableActivities->getStrValue("DETAILS")))
   {
      json_t* jDetails {jsonLoad(tableActivities->getStrValue("DETAILS"), 0, true)};

      if (jDetails)
      {
         json_object_set_new(jDetails, key, json_string(value));
         char* dump {json_dumps(jDetails, JSON_COMPACT | JSON_ENSURE_ASCII)};
         tableActivities->setValue("DETAILS", dump);
         tableActivities->update();
         free(dump);
         json_decref(jDetails);
      }
   }

   tableActivities->reset();
}

//***************************************************************************
// Edit - type, name and location of a list of activities, empty = unchanged
//   one garmin.py call (one login), one request per activity with all fields
//***************************************************************************

int HomeCtl::activitiesEdit(json_t* jIds, const char* type, const char* name, const char* location, std::string& message)
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

   bool setType {!isEmpty(type)};
   bool setName {!isEmpty(name)};
   bool setLocation {!isEmpty(location)};

   if (!setType && !setName && !setLocation)
   {
      message = "Keine Änderung";
      return success;
   }

   std::string args {"edit"};
   std::string what;                          // "Typ, Name, Ort" for the message

   if (setType)     { args += std::string(" --type '") + type + "'";         what += "Typ"; }
   if (setName)     { args += std::string(" --name '") + name + "'";         what += (what.empty() ? "" : ", ") + std::string("Name"); }
   if (setLocation) { args += std::string(" --location '") + location + "'"; what += (what.empty() ? "" : ", ") + std::string("Ort"); }

   std::string ids;
   size_t i {0};
   json_t* jId {};

   json_array_foreach(jIds, i, jId)
      ids += std::to_string(json_integer_value(jId)) + "\n";

   int timeout {60 + (int)count * 2};        // garmin.py pauses 0.5 s between the activities
   std::string error;
   json_t* jResult {garminCall(args.c_str(), timeout, error, ids.c_str())};

   if (!jResult)
   {
      message = what + ": " + error;
      return fail;
   }

   // update the succeeded ones locally

   json_t* jOk {json_object_get(jResult, "ok")};
   json_t* jItem {};
   int changed {0};

   json_array_foreach(jOk, i, jItem)
   {
      long id {(long)json_integer_value(jItem)};

      tableActivities->clear();
      tableActivities->setBigintValue("GARMINID", id);

      if (tableActivities->find())
      {
         if (setType)
         {
            tableActivities->setValue("TYPE", getStringFromJson(jResult, "type", type));
            tableActivities->setValue("TYPEID", getLongFromJson(jResult, "typeid", 0));
            tableActivities->setValue("PARENTTYPEID", getLongFromJson(jResult, "parenttypeid", 0));
         }

         if (setName)     tableActivities->setValue("NAME", name);
         if (setLocation) tableActivities->setValue("LOCATION", location);

         tableActivities->update();
         changed++;
      }

      tableActivities->reset();

      if (setType)     activityDetailsPatch(id, "type", getStringFromJson(jResult, "type", type));
      if (setName)     activityDetailsPatch(id, "name", name);
      if (setLocation) activityDetailsPatch(id, "location", location);
   }

   json_t* jFailed {json_object_get(jResult, "failed")};
   size_t failed {json_array_size(jFailed)};

   tell(eloAlways, "Info: Garmin: edit (%s) of %zu activities, %d changed, %zu failed", what.c_str(), count, changed, failed);

   if (count == 1)
   {
      if (failed)
      {
         message = what + ": " + getStringFromJson(json_array_get(jFailed, 0), "error", "fehlgeschlagen");
         json_decref(jResult);
         return fail;
      }

      message = what + " geändert";
   }
   else
   {
      message = what + " bei " + std::to_string(changed) + " von " + std::to_string(count) + " Aktivitäten geändert";

      if (failed)
         message += ", " + std::to_string(failed) + " fehlgeschlagen";
   }

   json_decref(jResult);

   return success;
}

//***************************************************************************
// Perform Activities (websocket event 'activities')
//***************************************************************************

int HomeCtl::performActivities(json_t* obj, long client)
{
   std::string action {getStringFromJson(obj, "action", "list")};

   if (action == "list")
      return activitiesPush(client);

   if (action == "details")
   {
      std::string error;
      json_t* jDetails {activityDetails(getLongFromJson(obj, "id", 0), getBoolFromJson(obj, "force", false), error)};

      if (!jDetails)
         return replyResult(fail, client, "%s", error.c_str());

      return pushOutMessage(jDetails, "activitydetails", client, false, 8);
   }

   if (action == "track")
   {
      std::string error;
      json_t* jTrack {activityTrack(getLongFromJson(obj, "id", 0), getBoolFromJson(obj, "force", false), error)};

      if (!jTrack)
         return replyResult(fail, client, "%s", error.c_str());

      return pushOutMessage(jTrack, "activitytrack", client, false, 10);   // coordinates
   }

   if (action == "types")
   {
      std::string error;
      json_t* jResult {garminCall("types", 60, error)};

      if (!jResult)
         return replyResult(fail, client, "%s", error.c_str());

      return pushOutMessage(jResult, "activitytypes", client);
   }

   // modifying actions need control rights

   auto itClient {wsClients.find((void*)client)};

   if (itClient == wsClients.end() || !(itClient->second.rights & urControl))
      return replyResult(fail, client, "Keine Berechtigung");

   if (action == "sync")
   {
      std::string message;
      int status {activitiesSync(getStringFromJson(obj, "since", ""), message)};

      if (status == success)
         activitiesPush(0);      // all clients

      return replyResult(status, client, "%s", message.c_str());
   }

   if (action == "delete")
   {
      // at Garmin first, then locally (with the track)

      long id {getLongFromJson(obj, "id", 0)};
      std::string error;
      std::string args {"delete " + std::to_string(id)};
      json_t* jResult {garminCall(args.c_str(), 60, error)};

      if (!jResult)
         return replyResult(fail, client, "%s", error.c_str());

      json_decref(jResult);
      tableActivities->deleteWhere("garminid = %ld", id);
      tableActivityTracks->deleteWhere("garminid = %ld", id);
      tell(eloAlways, "Info: Garmin: deleted activity %ld", id);
      activitiesPush(0);

      return replyResult(success, client, "Aktivität gelöscht");
   }

   if (action == "edit")
   {
      // type, name and location from the edit dialog, empty = unchanged
      //   'ids' (array) -> several activities, 'id' -> one (only the fields which differ are sent)

      const char* type {getStringFromJson(obj, "type", "")};
      const char* name {getStringFromJson(obj, "name", "")};
      const char* location {getStringFromJson(obj, "location", "")};
      json_t* jIds {json_object_get(obj, "ids")};
      json_t* jOwnIds {};
      std::string message;

      if (!json_is_array(jIds))
      {
         jOwnIds = json_array();
         json_array_append_new(jOwnIds, json_integer(getLongFromJson(obj, "id", 0)));
         jIds = jOwnIds;
      }

      if (json_array_size(jIds) == 1)
      {
         tableActivities->clear();
         tableActivities->setBigintValue("GARMINID", json_integer_value(json_array_get(jIds, 0)));

         if (!tableActivities->find())
         {
            tableActivities->reset();
            if (jOwnIds) json_decref(jOwnIds);
            return replyResult(fail, client, "Aktivität nicht gefunden");
         }

         if (strcmp(type, tableActivities->getStrValue("TYPE")) == 0)         type = "";
         if (strcmp(name, tableActivities->getStrValue("NAME")) == 0)         name = "";
         if (strcmp(location, tableActivities->getStrValue("LOCATION")) == 0) location = "";

         tableActivities->reset();
      }

      int status {activitiesEdit(jIds, type, name, location, message)};

      if (jOwnIds)
         json_decref(jOwnIds);

      if (status == success)
         activitiesPush(0);

      return replyResult(status, client, "%s", message.c_str());
   }

   return replyResult(fail, client, "Unbekannte Aktion");
}
