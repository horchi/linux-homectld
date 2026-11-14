/*
 *  sun.c
 *
 *  (c) 2026 Jörg Wendel
 *
 * This code is distributed under the terms and conditions of the
 * GNU GENERAL PUBLIC LICENSE. See the file COPYING for details.
 *
 *  Sun times
 *
 *  Dawn (civil, -6°), sunrise, sunset (-0.833°) and dusk (civil) of the current
 *  day, calculated once a day, on a change of the configuration and if the
 *  position moved more than 10 km. The position is the configured one
 *  ('latitude' / 'longitude'), with 'gpsLocation' the one of the GPS receiver
 *  (GPS:0x0a) as long as one is known - also the last known one if the receiver
 *  delivers no data anymore.
 *
 *  The times are shown by the text sensor SUN:0x01 and are the base of the
 *  time ranges of the outputs (SETTINGS.times), the start and the end of a range
 *  are either a fixed time 'HH:MM' or one of 'dawn', 'sunrise', 'sunset', 'dusk',
 *  each with an offset in minutes:
 *
 *    { "start": "sunset", "startOffset": -30, "end": "06:30", "endOffset": 0 }
 *
 *  Optional a range is valid only on some days of the year ("DD.MM", also over the turn
 *  of the year), e.g. later in summer:
 *
 *    { "start": "dusk", "startOffset": 60, "end": "sunrise", "dateFrom": "01.05", "dateTo": "30.09" }
 *
 *  The day of a range is the day of its start, the end of a range over midnight
 *  belongs to the day before.
 *
 *  Without dawn / dusk (summer in the north) sunrise / sunset are used instead.
 *  If even these are missing the sun is up (midnight sun) or down (polar night)
 *  the whole day: sunrise / dawn resolve to 00:00 (sun up) or 24:00 (sun down),
 *  sunset / dusk the other way round, the offset is ignored - a range from
 *  sunrise to sunset is active all day long at midnight sun, never at polar night.
 */

#include <cmath>
#include <sstream>
#include <iomanip>
#include <sys/stat.h>

#include "daemon.h"

static constexpr double zenithOfficial {90.833};   // sunrise / sunset (refraction and radius of the sun)
static constexpr double zenithCivil {96.0};        // civil twilight

//***************************************************************************
// Sun Event (sunrise equation of the 'Almanac for Computers', accurate to about a minute)
//   returns false if the sun doesn't reach the zenith this day,
//   alwaysUp tells then if it stays above (true) or below (false)
//***************************************************************************

static bool sunEvent(time_t day, double lat, double lng, double zenith, bool rising, time_t& at, bool& alwaysUp)
{
   constexpr double toRad {M_PI / 180.0};
   constexpr double toDeg {180.0 / M_PI};

   struct tm tm {};
   localtime_r(&day, &tm);

   int dayOfYear {tm.tm_yday + 1};
   double lngHour {lng / 15.0};
   double t {dayOfYear + ((rising ? 6.0 : 18.0) - lngHour) / 24.0};

   double M {0.9856 * t - 3.289};                                       // mean anomaly
   double L {fmod(M + 1.916 * sin(M * toRad) + 0.020 * sin(2 * M * toRad) + 282.634 + 360.0, 360.0)};   // true longitude

   double RA {fmod(atan(0.91764 * tan(L * toRad)) * toDeg + 360.0, 360.0)};   // right ascension
   RA += floor(L / 90.0) * 90.0 - floor(RA / 90.0) * 90.0;              // same quadrant as L
   RA /= 15.0;

   double sinDec {0.39782 * sin(L * toRad)};
   double cosDec {cos(asin(sinDec))};
   double cosH {(cos(zenith * toRad) - sinDec * sin(lat * toRad)) / (cosDec * cos(lat * toRad))};

   if (cosH > 1.0 || cosH < -1.0)
   {
      alwaysUp = cosH < -1.0;
      return false;
   }

   double H {(rising ? 360.0 - acos(cosH) * toDeg : acos(cosH) * toDeg) / 15.0};
   double T {H + RA - 0.06571 * t - 6.622};                             // local mean time
   double UT {fmod(T - lngHour + 48.0, 24.0)};

   // UTC midnight of the (local) day plus UT, moved to the local day if it fell to the day before / after

   struct tm utc {};
   utc.tm_year = tm.tm_year;
   utc.tm_mon = tm.tm_mon;
   utc.tm_mday = tm.tm_mday;

   at = timegm(&utc) + (time_t)lround(UT * 3600.0);

   time_t midnight {midnightOf(day)};

   if (at < midnight)
      at += tmeSecondsPerDay;
   else if (at >= midnight + tmeSecondsPerDay)
      at -= tmeSecondsPerDay;

   return true;
}

//***************************************************************************
// Sun Position
//   azimuth (0° north, 90° east, 180° south, 270° west) and elevation above the
//   horizon [°] at time t (low precision formulas of the Astronomical Almanac, ~0.1°)
//***************************************************************************

void HomeCtl::sunPosition(time_t t, double lat, double lng, double& azimuth, double& elevation)
{
   constexpr double toRad {M_PI / 180.0};
   constexpr double toDeg {180.0 / M_PI};

   double n {t / 86400.0 + 2440587.5 - 2451545.0};             // days since J2000.0
   double L {fmod(280.460 + 0.9856474 * n, 360.0)};             // mean longitude
   double g {fmod(357.528 + 0.9856003 * n, 360.0) * toRad};     // mean anomaly
   double lambda {(L + 1.915 * sin(g) + 0.020 * sin(2 * g)) * toRad};
   double eps {(23.439 - 0.0000004 * n) * toRad};

   double ra {atan2(cos(eps) * sin(lambda), cos(lambda))};
   double dec {asin(sin(eps) * sin(lambda))};

   double gmst {fmod(18.697374558 + 24.06570982441908 * n, 24.0)};
   double H {(gmst * 15.0 + lng) * toRad - ra};                 // hour angle
   double phi {lat * toRad};

   elevation = asin(sin(phi) * sin(dec) + cos(phi) * cos(dec) * cos(H)) * toDeg;
   azimuth = fmod(atan2(-sin(H), tan(dec) * cos(phi) - sin(phi) * cos(H)) * toDeg + 360.0, 360.0);
}

//***************************************************************************
// Location (position for the sun times and the weather)
//***************************************************************************

HomeCtl::GpsCoordinate HomeCtl::location(bool* fromGps)
{
   if (fromGps)
      *fromGps = false;

   if (gpsLocation)
   {
      GpsCoordinate c {gpsCoordinate};

      // after a restart the last position is the restored text of the GPS sensor

      if (!c.latitude && !c.longitude && sensors.count("GPS") && sensors["GPS"].count(gpsCoordinateAddress))
         parseGpsText(sensors["GPS"][gpsCoordinateAddress].text.c_str(), c);

      if (c.latitude || c.longitude)
      {
         if (fromGps)
            *fromGps = true;

         return c;
      }
   }

   return { latitude, longitude };
}

//***************************************************************************
// Sun Times Of
//   the times of the day 'day' at the position of 'st' (also for other days than today,
//   e.g. the next actions of the time control)
//***************************************************************************

void HomeCtl::sunTimesOf(time_t day, SunTimes& st)
{
   GpsCoordinate pos {st.position};
   time_t noon {midnightOf(day) + 12 * tmeSecondsPerHour};
   bool upRise {false}, upSet {false}, dummy {false};

   st.day = midnightOf(day);

   if (!sunEvent(noon, pos.latitude, pos.longitude, zenithOfficial, true, st.sunrise, upRise))
      st.sunrise = 0;

   if (!sunEvent(noon, pos.latitude, pos.longitude, zenithOfficial, false, st.sunset, upSet))
      st.sunset = 0;

   if (!sunEvent(noon, pos.latitude, pos.longitude, zenithCivil, true, st.dawn, dummy))
      st.dawn = 0;

   if (!sunEvent(noon, pos.latitude, pos.longitude, zenithCivil, false, st.dusk, dummy))
      st.dusk = 0;

   st.sunUp = upRise || upSet;
}

//***************************************************************************
// Sun Calc
//   recalculates on a new day or if the position moved, returns true if done
//***************************************************************************

bool HomeCtl::sunCalc(bool force)
{
   bool fromGps {false};
   GpsCoordinate pos {location(&fromGps)};
   time_t today {midnightOf(time(0))};

   if (!force && sunTimes.day == today && fromGps == sunTimes.gps && gpsDistance(pos, sunTimes.position) < 10000.0)
      return false;

   sunTimes = {};
   sunTimes.position = pos;
   sunTimes.gps = fromGps;
   sunTimesOf(today, sunTimes);

   tell(eloDetail, "Sun: %s position %.4f/%.4f (%s): dawn %s, sunrise %s, sunset %s, dusk %s",
        l2pTime(today, "%d.%m.%Y").c_str(), pos.latitude, pos.longitude, fromGps ? "GPS" : "config",
        sunTimes.dawn ? l2pTime(sunTimes.dawn, "%H:%M").c_str() : "-",
        sunTimes.sunrise ? l2pTime(sunTimes.sunrise, "%H:%M").c_str() : "-",
        sunTimes.sunset ? l2pTime(sunTimes.sunset, "%H:%M").c_str() : "-",
        sunTimes.dusk ? l2pTime(sunTimes.dusk, "%H:%M").c_str() : "-");

   return true;
}

//***************************************************************************
// Sun Update
//   called by the main loop, recalculates if needed and updates the sensor SUN:0x01
//***************************************************************************

int HomeCtl::sunUpdate(bool force)
{
   if (!sunCalc(force) && sunTimes.published)
      return done;

   sunTimes.published = true;

   // the sensor - one text with all times (Plain Text widget shows the tabs as table)

   auto fmt = [](time_t t) { return t ? l2pTime(t, "%H:%M") : std::string("-"); };

   std::string text {"Morgendämmerung\t" + fmt(sunTimes.dawn) + "\n" +
                     "Sonnenaufgang\t" + fmt(sunTimes.sunrise) + "\n" +
                     "Sonnenuntergang\t" + fmt(sunTimes.sunset) + "\n" +
                     "Abenddämmerung\t" + fmt(sunTimes.dusk)};

   if (!sunTimes.sunrise && !sunTimes.sunset)
      text += std::string("\n") + (sunTimes.sunUp ? "Mitternachtssonne" : "Polarnacht");

   addValueFact(1, "SUN", 1, "sun", "txt", "Sonne");

   if (sensors["SUN"][1].type.empty())
      initSensorByFact("SUN", 1);

   sensors["SUN"][1].kind = "text";
   sensors["SUN"][1].setText(text);

   {
      json_t* ojData {json_object()};
      sensor2Json(ojData, "SUN", 1);
      json_object_set_new(ojData, "text", json_string(text.c_str()));

      char* tuple {};
      asprintf(&tuple, "%s:0x%02x", "SUN", 1);
      jsonSensorList[tuple] = ojData;
      free(tuple);

      pushDataUpdate("update", 0L);
   }

   return done;
}

//***************************************************************************
// Sun 2 Json (for the sensor SUN:0x01)
//***************************************************************************

int HomeCtl::sun2Json(json_t* obj)
{
   json_t* jSun {json_object()};

   json_object_set_new(jSun, "dawn", json_integer(sunTimes.dawn));
   json_object_set_new(jSun, "sunrise", json_integer(sunTimes.sunrise));
   json_object_set_new(jSun, "sunset", json_integer(sunTimes.sunset));
   json_object_set_new(jSun, "dusk", json_integer(sunTimes.dusk));
   json_object_set_new(jSun, "sunUp", json_boolean(sunTimes.sunUp));
   json_object_set_new(jSun, "latitude", json_real(sunTimes.position.latitude));
   json_object_set_new(jSun, "longitude", json_real(sunTimes.position.longitude));
   json_object_set_new(jSun, "gps", json_boolean(sunTimes.gps));

   double azimuth {0}, elevation {0};
   sunPosition(time(0), sunTimes.position.latitude, sunTimes.position.longitude, azimuth, elevation);
   json_object_set_new(jSun, "azimuth", json_real(round(azimuth * 10) / 10));
   json_object_set_new(jSun, "elevation", json_real(round(elevation * 10) / 10));

   json_object_set_new(obj, "sun", jSun);

   return done;
}

//***************************************************************************
// Sun Resolve
//   "HH:MM" or dawn|sunrise|sunset|dusk plus offset [minutes] -> HHMM (0..2400)
//***************************************************************************

int HomeCtl::sunResolve(const char* ref, int offset, uint& hhmm, const SunTimes* times)
{
   if (isEmpty(ref))
      return fail;

   int minutes {0};

   if (strchr(ref, ':'))
   {
      int hh {0}, mm {0};

      if (sscanf(ref, "%d:%d", &hh, &mm) != 2)
         return fail;

      minutes = hh * 60 + mm;
   }
   else
   {
      if (!times)
      {
         sunCalc();     // a new day (the sensor follows by the main loop)
         times = &sunTimes;
      }

      const SunTimes& st {*times};
      bool rising {strcmp(ref, "dawn") == 0 || strcmp(ref, "sunrise") == 0};
      time_t t {0};

      if (strcmp(ref, "dawn") == 0)
         t = st.dawn ? st.dawn : st.sunrise;
      else if (strcmp(ref, "sunrise") == 0)
         t = st.sunrise;
      else if (strcmp(ref, "sunset") == 0)
         t = st.sunset;
      else if (strcmp(ref, "dusk") == 0)
         t = st.dusk ? st.dusk : st.sunset;
      else
         return fail;

      if (!t)
      {
         // midnight sun / polar night - the sun is up or down all day, offset ignored

         hhmm = rising == st.sunUp ? 0 : 2400;
         return success;
      }

      minutes = l2hhmm(t) / 100 * 60 + l2hhmm(t) % 100;
   }

   minutes = ((minutes + offset) % 1440 + 1440) % 1440;
   hhmm = minutes / 60 * 100 + minutes % 60;

   return success;
}

//***************************************************************************
// Time Control of Actors (DZL, DZLG, HMB)
//   unlike GPIO (state forced inside the range) an actor is switched only at the
//   limits of its ranges, switching by hand stays as it is until the next limit;
//   start or end of a range may be empty (e.g. only 'off' at 23:00)
//
//   SETTINGS: { "timesEnabled": true, "times": [ ... ],
//               "bri": 80,                         DZL / DZLG, brightness on switch on (optional)
//               "times": [ { ...,
//                            "levelIn": 0, "levelOut": 100,   HMB, position at the start / end of the range
//                                                  (without it the former "levelIn" / "levelOut" of
//                                                  the actor, else 0 / 100)
//                            "enabled": false } ] }   range switched off, stays configured (optional)
//***************************************************************************

static int actorRangeLevel(json_t* jRange, json_t* oSettings, bool in)
{
   const char* name {in ? "levelIn" : "levelOut"};
   return getIntFromJson(jRange, name, getIntFromJson(oSettings, name, in ? 0 : 100));
}

static time_t localTimeOf(time_t day, uint hhmm)
{
   struct tm tm {};
   localtime_r(&day, &tm);

   tm.tm_hour = hhmm / 100;
   tm.tm_min = hhmm % 100;
   tm.tm_sec = 0;
   tm.tm_isdst = -1;

   return mktime(&tm);
}

//***************************************************************************
// Range Date Valid
//   is the time range valid on the day of 't' ("dateFrom" / "dateTo" as "DD.MM",
//   without both all year)
//***************************************************************************

static int mmddOf(const char* date)      // "DD.MM" -> MMDD, -1 if not set or invalid
{
   int d {0}, m {0};

   if (isEmpty(date) || sscanf(date, "%d.%d", &d, &m) != 2 || d < 1 || d > 31 || m < 1 || m > 12)
      return -1;

   return m * 100 + d;
}

bool HomeCtl::rangeDateValid(json_t* jRange, time_t t)
{
   int from {mmddOf(getStringFromJson(jRange, "dateFrom"))};
   int to {mmddOf(getStringFromJson(jRange, "dateTo"))};

   if (from < 0 || to < 0)
      return true;

   struct tm tm {};
   localtime_r(&t, &tm);
   int mmdd {(tm.tm_mon + 1) * 100 + tm.tm_mday};

   return from <= to ? mmdd >= from && mmdd <= to : mmdd >= from || mmdd <= to;
}

int HomeCtl::actorTimesDue(const char* type, uint address, json_t* oSettings, time_t from, time_t to,
                           std::vector<ActorTimeAction>& actions)
{
   json_t* jTimes {getObjectFromJson(oSettings, "times")};

   if (!getBoolFromJson(oSettings, "timesEnabled", true) || !jTimes || !json_is_array(jTimes))
      return done;

   time_t today {midnightOf(to)};
   time_t yesterday {midnightOf(today - 1)};
   size_t index {0};
   json_t* jRange {};

   json_array_foreach(jTimes, index, jRange)
   {
      if (!getBoolFromJson(jRange, "enabled", true))      // switched off range, stays configured
         continue;

      // the limits, false if empty or not occurring this day (sun times at polar night / midnight sun)

      auto limitOf = [&](bool in, uint& hhmm) {
         const char* ref {getStringFromJson(jRange, in ? "start" : "end")};
         return !isEmpty(ref) && sunResolve(ref, getIntFromJson(jRange, in ? "startOffset" : "endOffset", 0), hhmm) == success && hhmm < 2400;
      };

      uint hhmmStart {0}, hhmmEnd {0};
      bool hasStart {limitOf(true, hhmmStart)};
      bool hasEnd {limitOf(false, hhmmEnd)};
      bool overnight {hasStart && hasEnd && hhmmEnd <= hhmmStart};    // the end belongs to the day before

      for (bool in : { true, false })
      {
         if (!(in ? hasStart : hasEnd))
            continue;

         uint hhmm {in ? hhmmStart : hhmmEnd};
         time_t dueAt {0};

         for (time_t day : { yesterday, today })
         {
            time_t at {localTimeOf(day, hhmm)};
            time_t dayOfRange {in || !overnight ? day : midnightOf(day - 1)};

            if (at > from && at <= to && rangeDateValid(jRange, dayOfRange))
               dueAt = std::max(dueAt, at);
         }

         if (!dueAt)
            continue;

         ActorTimeAction action;
         action.type = type;
         action.address = address;
         action.in = in;
         action.at = dueAt;

         if (strcmp(type, "HMB") == 0)
            action.level = actorRangeLevel(jRange, oSettings, in);
         else if (in)
            action.bri = getIntFromJson(oSettings, "bri", na);

         actions.push_back(action);
      }
   }

   return done;
}

int HomeCtl::actorTimeAction(const ActorTimeAction& action)
{
   const char* type {action.type.c_str()};

   tell(eloAlways, "Time control: '%s:0x%02x' (%s) %s", type, action.address,
        sensors[type][action.address].title.c_str(),
        action.type == "HMB" ? ("to " + std::to_string(action.level) + "%").c_str() : action.in ? "on" : "off");

   if (action.type == "HMB")
   {
      shadeEnd(action.address);      // the time control has priority


      // LEVEL also if the blind is moving (toggleIo() would send STOP then)

      if (!valueFactRowOf(type, action.address))
         return fail;

      sensors[type][action.address].state = action.level == 100;
      return mqttNodeRedPublishAction(sensors[type][action.address], action.level, false, true);
   }

   // DZL / DZLG - explicit state, a group would be toggled by toggleIo()

   int bri {action.bri > 0 ? std::min(action.bri, 100) : na};
   return lightCommand(type, action.address, action.in, bri, na);
}

//***************************************************************************
// Store / Load the Time of the last Check
//   { "checkedAt": 1794067200 } - the limits up to this time are done, the ones after it
//   are caught up at the next start (stop, crash, power off)
//***************************************************************************

static const char* actorTimesDir {"/var/cache/homectld"};
static const char* actorTimesFile {"/var/cache/homectld/actortimes.json"};

int HomeCtl::actorTimesStore()
{
   if (!actorTimesCheckedAt || actorTimesCatchUp)       // not yet checked since the start, keep the stored time
      return done;

   actorTimesStoredAt = time(0);

   char* p {};
   asprintf(&p, "{ \"checkedAt\": %ld }\n", (long)actorTimesCheckedAt);

   std::string tmp {std::string(actorTimesFile) + ".tmp"};
   mkdir(actorTimesDir, 0755);

   if (storeToFile(tmp.c_str(), p, strlen(p)) != success || rename(tmp.c_str(), actorTimesFile) != 0)
   {
      tell(eloAlways, "Error: Can't store the time of the time control to '%s'", actorTimesFile);
      free(p);
      return fail;
   }

   free(p);

   return success;
}

int HomeCtl::actorTimesLoad()
{
   actorTimesStartedAt = time(0);
   actorTimesCatchUp = true;
   actorTimesCheckedAt = 0;

   if (!fileExists(actorTimesFile))
      return done;

   json_error_t error;
   json_t* obj {json_load_file(actorTimesFile, 0, &error)};

   if (!obj)
   {
      tell(eloAlways, "Error: Can't load the time of the time control from '%s' (%s)", actorTimesFile, error.text);
      return fail;
   }

   time_t checkedAt = getLongFromJson(obj, "checkedAt", 0);
   json_decref(obj);

   if (checkedAt > 0 && checkedAt <= actorTimesStartedAt)
      actorTimesCheckedAt = checkedAt;

   return success;
}

//***************************************************************************
// Actor Times Ready
//   the actors can be switched: deCONZ connected and its devices read (if configured) and
//   MQTT connected (HomeMatic via Node-RED / hmmqtt), the time control waits for it after the
//   start (the commands would get lost), at most actorTimesReadyWait
//***************************************************************************

bool HomeCtl::actorTimesReady(time_t now)
{
   if (!actorTimesCatchUp)
      return true;

   bool deconzOk {isEmpty(deconz.getHttpUrl()) || isEmpty(deconz.getApiKey()) || deconzReady};
   bool mqttOk {mqttWriter && mqttWriter->isConnected()};

   if (deconzOk && mqttOk)
      return true;

   if (now - actorTimesStartedAt < actorTimesReadyWait)
      return false;

   tell(eloAlways, "Time control: Not ready after %ld s (deCONZ %s, MQTT %s), going on",
        (long)(now - actorTimesStartedAt), deconzOk ? "ok" : "not ready", mqttOk ? "ok" : "not connected");

   return true;
}

//***************************************************************************
// Next Time Actions 2 Json
//   the next actions of the time control of an output / actor for the info dialog,
//   with the sun times of the day of the action (the next days too):
//
//   { "type": "DZL", "address": 3,
//     "next": { "at": 1793599067, "in": true, "bri": 80 },        earliest action, missing if none
//     "ranges": [ { "at": ..., "in": false }, null, ... ],       next limit of each range (null: none within a week)
//     "note": "Handbetrieb",                                      why there is no automatic action (optional)
//     "timers": [ ... ] }                                         running off timers of light rules (see below)
//
//   actors (DZL, DZLG, HMB) switch at each limit, GPIO (out) are forced to the state of
//   their ranges (in auto mode) - for them the next change of this state is 'next'
//***************************************************************************

int HomeCtl::nextTimeActions2Json(const char* type, uint address, json_t* obj)
{
   constexpr int dayCount {9};     // yesterday (morning part of a range over midnight) and the next 8 days

   json_object_set_new(obj, "type", json_string(type));
   json_object_set_new(obj, "address", json_integer(address));

   // running off timers of the light rules (lightrules.c) which switched this light on,
   //   "timers": [ { "at": ..., "rule": "DZS:0x21#0", "motion": true }, ... ] - with motion the
   //   timer is extended as long as the motion sensor reports it

   char* key {};
   asprintf(&key, "%s:0x%02x", type, address);
   json_t* jTimers {json_array()};

   for (const auto& [ruleKey, timer] : lightRuleTimers)
   {
      if (!timer.switchedOn.count(key) || !timer.offAt)
         continue;

      json_t* jTimer {json_object()};
      json_object_set_new(jTimer, "at", json_integer(timer.offAt));
      json_object_set_new(jTimer, "rule", json_string(ruleKey.c_str()));
      json_object_set_new(jTimer, "motion", json_boolean(!timer.motionSensor.empty() && lightMotionPresent(timer.motionSensor)));
      json_array_append_new(jTimers, jTimer);
   }

   free(key);
   json_object_set_new(obj, "timers", jTimers);

   bool actor {strcmp(type, "DZL") == 0 || strcmp(type, "DZLG") == 0 || strcmp(type, "HMB") == 0};
   bool output {strcmp(type, "GPIO") == 0 && sensors[type][address].fct == "out"};
   cDbRow* fact {(actor || output) ? valueFactRowOf(type, address) : nullptr};

   if (!fact || fact->getValue("SETTINGS")->isEmpty())
      return done;

   json_t* oSettings {jsonLoad(fact->getStrValue("SETTINGS"), 0, true)};
   json_t* jTimes {oSettings ? getObjectFromJson(oSettings, "times") : nullptr};

   if (!jTimes || !json_is_array(jTimes) || !json_array_size(jTimes))
   {
      if (oSettings)
         json_decref(oSettings);
      return done;
   }

   const char* note {};

   if (!fact->hasValue("STATE", "A") || !sensors[type][address].active)
      note = "Sensor nicht aktiv";
   else if (actor && !getBoolFromJson(oSettings, "timesEnabled", true))
      note = "Zeitsteuerung ausgeschaltet";
   else if (output && sensors[type][address].mode != omAuto)
      note = "Handbetrieb";

   if (note)
   {
      json_object_set_new(obj, "note", json_string(note));
      json_decref(oSettings);
      return done;
   }

   // sun times of the days

   sunCalc();

   time_t now {time(0)};
   time_t today {midnightOf(now)};
   std::vector<SunTimes> days(dayCount);

   for (int d = 0; d < dayCount; d++)
   {
      days[d].position = sunTimes.position;
      sunTimesOf(today + (d - 1) * tmeSecondsPerDay + 12 * tmeSecondsPerHour, days[d]);
   }

   auto dayIndexOf = [&](time_t t) {
      time_t midnight {midnightOf(t)};

      for (int d = 0; d < dayCount; d++)
         if (days[d].day == midnight)
            return d;

      return -1;
   };

   // the action as JSON

   auto action2Json = [&](time_t at, bool in, json_t* jR) {
      json_t* jAction {json_object()};
      json_object_set_new(jAction, "at", json_integer(at));
      json_object_set_new(jAction, "in", json_boolean(in));

      if (strcmp(type, "HMB") == 0)
         json_object_set_new(jAction, "level", json_integer(actorRangeLevel(jR, oSettings, in)));
      else if (actor && in && getIntFromJson(oSettings, "bri", na) > 0)
         json_object_set_new(jAction, "bri", json_integer(getIntFromJson(oSettings, "bri", na)));

      return jAction;
   };

   // next limit of each range (like actorTimesDue(): a limit of a day with the sun times of this day,
   //   the end of a range over midnight belongs to the day before)

   json_t* jRanges {json_array()};
   time_t nextAt {0};
   bool nextIn {false};
   json_t* nextRange {};
   size_t index {0};
   json_t* jRange {};

   json_array_foreach(jTimes, index, jRange)
   {
      time_t rangeAt {0};
      bool rangeIn {false};
      bool enabled {getBoolFromJson(jRange, "enabled", true)};    // switched off: no next action (null)

      for (int d = 1; enabled && d < dayCount && !rangeAt; d++)
      {
         auto limitOf = [&](bool in, uint& hhmm) {
            const char* ref {getStringFromJson(jRange, in ? "start" : "end")};
            return !isEmpty(ref) && sunResolve(ref, getIntFromJson(jRange, in ? "startOffset" : "endOffset", 0), hhmm, &days[d]) == success && hhmm < 2400;
         };

         uint hhmmStart {0}, hhmmEnd {0};
         bool hasStart {limitOf(true, hhmmStart)};
         bool hasEnd {limitOf(false, hhmmEnd)};
         bool overnight {hasStart && hasEnd && hhmmEnd <= hhmmStart};

         if (output && (!hasStart || !hasEnd))   // GPIO need both limits
            continue;

         for (bool in : { true, false })
         {
            if (!(in ? hasStart : hasEnd))
               continue;

            time_t at {localTimeOf(days[d].day, in ? hhmmStart : hhmmEnd)};
            time_t dayOfRange {in || !overnight ? days[d].day : days[d - 1].day};

            if (at > now && rangeDateValid(jRange, dayOfRange) && (!rangeAt || at < rangeAt))
            {
               rangeAt = at;
               rangeIn = in;
            }
         }
      }

      json_array_append_new(jRanges, rangeAt ? action2Json(rangeAt, rangeIn, jRange) : json_null());

      if (actor && rangeAt && (!nextAt || rangeAt < nextAt))
      {
         nextAt = rangeAt;
         nextIn = rangeIn;
         nextRange = jRange;
      }
   }

   json_object_set_new(obj, "ranges", jRanges);

   // GPIO: the next change of the state the ranges force (like process(), overlapping ranges
   //   and days without a valid range don't switch)

   if (output)
   {
      auto stateAt = [&](time_t t, bool& active) {
         int d {dayIndexOf(t)};
         uint hhmm {(uint)l2hhmm(t)};
         bool hasRanges {false};
         size_t i {0};
         json_t* jR {};

         active = false;

         if (d < 0)
            return false;

         json_array_foreach(jTimes, i, jR)
         {
            Range r;

            if (!getBoolFromJson(jR, "enabled", true))
               continue;

            if (sunResolve(getStringFromJson(jR, "start"), getIntFromJson(jR, "startOffset", 0), r.from, &days[d]) != success ||
                sunResolve(getStringFromJson(jR, "end"), getIntFromJson(jR, "endOffset", 0), r.to, &days[d]) != success)
               continue;

            bool morningPart {r.from > r.to && hhmm < r.to};

            if (!rangeDateValid(jR, morningPart ? days[d].day - 1 : t))
               continue;

            hasRanges = true;

            if (r.inRange(hhmm))
               active = true;
         }

         return hasRanges;
      };

      // candidates: the limits of all ranges and the midnights (date ranges, sun times of the next day)

      std::set<time_t> candidates;

      for (int d = 1; d < dayCount; d++)
      {
         candidates.insert(days[d].day);

         json_array_foreach(jTimes, index, jRange)
         {
            for (bool in : { true, false })
            {
               uint hhmm {0};

               if (sunResolve(getStringFromJson(jRange, in ? "start" : "end"), getIntFromJson(jRange, in ? "startOffset" : "endOffset", 0), hhmm, &days[d]) == success && hhmm < 2400)
                  candidates.insert(localTimeOf(days[d].day, hhmm));
            }
         }
      }

      bool state {sensors[type][address].state};
      bool active {false};

      if (stateAt(now, active))
         state = active;

      for (time_t t : candidates)
      {
         if (t <= now || !stateAt(t, active))
            continue;

         if (active != state)
         {
            nextAt = t;
            nextIn = active;
            break;
         }
      }
   }

   if (nextAt)
      json_object_set_new(obj, "next", action2Json(nextAt, nextIn, nextRange));

   json_decref(oSettings);

   return done;
}
