/*
 *  gpstour.c
 *
 *  (c) 2026 Jörg Wendel
 *
 * This code is distributed under the terms and conditions of the
 * GNU GENERAL PUBLIC LICENSE. See the file COPYING for details.
 *
 *  Recording of GPS tours
 *
 *  A tour is started and stopped in the WEBIF (Map -> Touren). While a tour is
 *  active the coordinate of the GPS sensor (GPS:0x0a, text 'lat/lng') is stored
 *  into 'samples' whenever the position moved at least 'gpsTourMinDistance' meters
 *  since the last stored point. Without movement for 'gpsTourPauseAfter' minutes
 *  the tour is paused, on the next movement it continues automatically.
 *
 *  Only the tour itself (name, start, stop, distance, ...) is stored in table
 *  'gpstours', the points are the GPS samples between start and stop. With each
 *  point speed (GPS:0x03) and altitude (GPS:0x05) are stored with the same time
 *  stamp. Tour samples are marked with AGGREGATE 'T' and are never aggregated or
 *  deleted by the aggregation, regular samples ('S') are handled as before.
 *
 *  The active tour survives a restart: the tour row has no stop time and the
 *  RECORD flag of the GPS coordinate in 'valuefacts' is set while a tour is active.
 *
 *  Events of a tour (table 'gpstourevents'): fuel stops (liters, price, odometer), toll,
 *  pauses, night stops and notes, each with time and position. Together with the odometer
 *  at start and end of the tour (columns of 'gpstours') the costs and the fuel consumption
 *  are calculated (consumption per fuel stop: liters / km since the previous full tank,
 *  partial fills are added to the next full one and get no value of their own). The
 *  distance is taken from the odometer readings and, in parallel, from the GPS distance
 *  of the tour at the moment of the fuel stop (stored with the event).
 */

#include <cmath>
#include <sstream>
#include <iomanip>
#include <locale>
#include <algorithm>
#include <set>
#include <jansson.h>

#include "lib/json.h"
#include "daemon.h"

//***************************************************************************
// Helpers
//***************************************************************************

// text 'lat/lng', decimal separator '.' or ',' (the text of the sensor is
//  converted to the locale format at some places)

bool HomeCtl::parseGpsText(const char* text, GpsCoordinate& c)
{
   if (isEmpty(text))
      return false;

   std::string t {text};
   std::replace(t.begin(), t.end(), ',', '.');

   size_t pos {t.find('/')};

   if (pos == std::string::npos)
      return false;

   std::istringstream sLat(t.substr(0, pos));
   std::istringstream sLng(t.substr(pos+1));
   sLat.imbue(std::locale::classic());
   sLng.imbue(std::locale::classic());

   double lat {0.0};
   double lng {0.0};

   if (!(sLat >> lat) || !(sLng >> lng))
      return false;

   if ((lat == 0.0 && lng == 0.0) || std::fabs(lat) > 90.0 || std::fabs(lng) > 180.0)
      return false;   // no fix / invalid

   c.latitude = lat;
   c.longitude = lng;

   return true;
}

std::string HomeCtl::gpsCoordinateText(const GpsCoordinate& c)
{
   std::ostringstream o;

   o.imbue(std::locale::classic());
   o << std::fixed << std::setprecision(6) << c.latitude << '/' << c.longitude;

   return o.str();
}

// distance in meters (haversine)

double HomeCtl::gpsDistance(const GpsCoordinate& a, const GpsCoordinate& b)
{
   constexpr double earthRadius {6371000.0};
   constexpr double toRad {M_PI / 180.0};

   double dLat {(b.latitude - a.latitude) * toRad};
   double dLng {(b.longitude - a.longitude) * toRad};
   double h {std::sin(dLat/2) * std::sin(dLat/2) +
             std::cos(a.latitude * toRad) * std::cos(b.latitude * toRad) * std::sin(dLng/2) * std::sin(dLng/2)};

   return 2.0 * earthRadius * std::asin(std::sqrt(std::min(1.0, h)));
}

//***************************************************************************
// Init - resume the tour which was active at the last shutdown
//***************************************************************************

int HomeCtl::gpsTourInit()
{
   std::vector<long> openTours;

   gpsTour = GpsTour();
   tableGpsTours->clear();

   for (int f = selectGpsTours->find(); f; f = selectGpsTours->fetch())
   {
      if (tableGpsTours->getValue("STOP")->isNull())
         openTours.push_back(tableGpsTours->getIntValue("ID"));
   }

   selectGpsTours->freeResult();

   if (openTours.empty())
      return done;          // no active tour, the RECORD flag stays as configured by the user

   // only one tour can be active, close further ones (should not happen)

   for (size_t i = 1; i < openTours.size(); i++)
   {
      tableGpsTours->clear();
      tableGpsTours->setValue("ID", openTours[i]);

      if (tableGpsTours->find())
      {
         tell(eloAlways, "GPS: Closing additional open tour %ld '%s'", openTours[i], tableGpsTours->getStrValue("NAME"));
         tableGpsTours->setValue("STOP", (long)time(0));
         tableGpsTours->store();
      }

      tableGpsTours->reset();
   }

   tableGpsTours->clear();
   tableGpsTours->setValue("ID", openTours[0]);

   if (!tableGpsTours->find())
      return fail;

   time_t now {time(0)};

   gpsTour.id = tableGpsTours->getIntValue("ID");
   gpsTour.name = tableGpsTours->getStrValue("NAME");
   gpsTour.start = tableGpsTours->getTimeValue("START");
   gpsTour.distance = tableGpsTours->getFloatValue("DISTANCE");
   gpsTour.points = tableGpsTours->getIntValue("POINTS");
   gpsTour.pauseTime = tableGpsTours->getIntValue("PAUSETIME");
   gpsTour.odometer = tableGpsTours->getIntValue("ODOMETER");
   gpsTour.lastMoveAt = gpsTour.start;
   tableGpsTours->reset();

   // the last recorded point of the tour

   tableSamples->clear();
   tableSamples->setValue("ADDRESS", (long)gpsCoordinateAddress);
   tableSamples->setValue("TYPE", "GPS");
   tableSamples->setValue("AGGREGATE", gpsTourAggregate);
   gpsTourFrom.setValue((long)gpsTour.start);
   gpsTourTo.setValue((long)now);

   for (int f = selectGpsTourSamples->find(); f; f = selectGpsTourSamples->fetch())
   {
      GpsCoordinate c;

      if (parseGpsText(tableSamples->getStrValue("TEXT"), c))
      {
         gpsTour.lastPoint = c;
         gpsTour.hasLastPoint = true;
         gpsTour.lastMoveAt = tableSamples->getTimeValue("TIME");
      }
   }

   selectGpsTourSamples->freeResult();
   tableSamples->reset();

   gpsTourSetRecordFlag(true);

   tell(eloAlways, "GPS: Resuming tour %ld '%s' (started %s, %ld points, %.1f km)", gpsTour.id, gpsTour.name.c_str(),
        l2pTime(gpsTour.start).c_str(), gpsTour.points, gpsTour.distance / 1000.0);

   gpsTourCheckPause(now);

   return success;
}

//***************************************************************************
// Start / Stop / Delete / Rename
//***************************************************************************

int HomeCtl::gpsTourStart(const char* name, long odometer)
{
   if (gpsTour.id)
   {
      tell(eloAlways, "GPS: Can't start tour, tour '%s' is already active", gpsTour.name.c_str());
      return fail;
   }

   auto itGps {sensors.find("GPS")};

   if (itGps == sensors.end() || itGps->second.find(gpsCoordinateAddress) == itGps->second.end() ||
       !itGps->second.at(gpsCoordinateAddress).active)
   {
      tell(eloAlways, "GPS: Can't start tour, GPS sensor GPS:0x%02x is not active", gpsCoordinateAddress);
      return ignore;
   }

   time_t now {time(0)};
   std::string tourName {isEmpty(name) ? "Tour " + l2pTime(now, "%d.%m.%Y %H:%M") : name};

   if (tourName.length() > 100)
      tourName.resize(100);

   tableGpsTours->clear();
   tableGpsTours->setValue("NAME", tourName.c_str());
   tableGpsTours->setValue("START", (long)now);
   tableGpsTours->setValue("DISTANCE", 0.0);
   tableGpsTours->setValue("POINTS", 0L);
   tableGpsTours->setValue("PAUSETIME", 0L);
   tableGpsTours->setValue("ODOMETER", odometer > 0 ? odometer : 0L);
   tableGpsTours->store();

   gpsTour = GpsTour();
   gpsTour.id = tableGpsTours->getLastInsertId();
   gpsTour.name = tourName;
   gpsTour.start = now;
   gpsTour.odometer = odometer > 0 ? odometer : 0;
   gpsTour.lastMoveAt = now;
   tableGpsTours->reset();

   gpsTourSetRecordFlag(true);

   tell(eloAlways, "GPS: Started tour %ld '%s'", gpsTour.id, gpsTour.name.c_str());

   // the current position is the first point

   if (gpsCoordinate.latitude || gpsCoordinate.longitude)
      gpsTourStorePoint(now, gpsCoordinate);

   gpsTourPushState();

   return success;
}

// proposal for the end of the active tour: the time the position arrived within
// 'gpsTourEndTolerance' meters of the final position and stayed there (e.g. waiting at
// the reception of the camp site or a forgotten stop). fail -> no proposal (still moving,
// or not enough points)

int HomeCtl::gpsTourArrival(time_t& arrival, double& toleranceUsed)
{
   if (!gpsTour.id)
      return fail;

   struct Point { time_t time; GpsCoordinate c; };
   std::vector<Point> points;

   tableSamples->clear();
   tableSamples->setValue("ADDRESS", (long)gpsCoordinateAddress);
   tableSamples->setValue("TYPE", "GPS");
   tableSamples->setValue("AGGREGATE", gpsTourAggregate);
   gpsTourFrom.setValue((long)gpsTour.start);
   gpsTourTo.setValue((long)time(0));

   for (int f = selectGpsTourSamples->find(); f; f = selectGpsTourSamples->fetch())
   {
      GpsCoordinate c;

      if (parseGpsText(tableSamples->getStrValue("TEXT"), c))
         points.push_back({tableSamples->getTimeValue("TIME"), c});
   }

   selectGpsTourSamples->freeResult();
   tableSamples->reset();

   if (points.size() < 2)
      return fail;

   toleranceUsed = gpsTourEndTolerance;

   // walk back from the final position while the points stay within the tolerance

   const GpsCoordinate& end {points.back().c};
   size_t i {points.size() - 1};

   while (i > 0 && gpsDistance(points[i-1].c, end) <= toleranceUsed)
      i--;

   if (i == 0)
      return fail;                  // the whole tour is within the tolerance - nothing to propose

   arrival = points[i].time;       // first point of the trailing cluster around the final position

   return success;
}

int HomeCtl::gpsTourStop(time_t stopAt, long odometer)
{
   if (!gpsTour.id)
      return fail;

   time_t now {time(0)};

   if (stopAt <= gpsTour.start || stopAt > now)
      stopAt = now;


   if (gpsTour.paused)
   {
      if (stopAt > gpsTour.pausedAt)
         gpsTour.pauseTime += stopAt - gpsTour.pausedAt;

      gpsTour.paused = false;
   }

   tableGpsTours->clear();
   tableGpsTours->setValue("ID", gpsTour.id);

   if (tableGpsTours->find())
   {
      tableGpsTours->setValue("STOP", (long)stopAt);
      tableGpsTours->setValue("DISTANCE", gpsTour.distance);
      tableGpsTours->setValue("POINTS", gpsTour.points);
      tableGpsTours->setValue("PAUSETIME", gpsTour.pauseTime);

      if (odometer > 0)
         tableGpsTours->setValue("ODOMETEREND", odometer);

      tableGpsTours->store();
   }

   tableGpsTours->reset();

   tell(eloAlways, "GPS: Stopped tour %ld '%s' at %s (%ld points, %.1f km, %ld min paused)", gpsTour.id, gpsTour.name.c_str(),
        l2pTime(stopAt).c_str(), gpsTour.points, gpsTour.distance / 1000.0, gpsTour.pauseTime / 60);

   gpsTour = GpsTour();
   gpsTourSetRecordFlag(false);
   gpsTourPushState();

   return success;
}

// delete the tour and its samples (coordinate, speed, altitude)

int HomeCtl::gpsTourDelete(long id)
{
   if (id == gpsTour.id)
   {
      tell(eloAlways, "GPS: Can't delete the active tour, stop it first");
      return fail;
   }

   tableGpsTours->clear();
   tableGpsTours->setValue("ID", id);

   if (!tableGpsTours->find())
   {
      tableGpsTours->reset();
      return fail;
   }

   time_t start {tableGpsTours->getTimeValue("START")};
   time_t stop {tableGpsTours->getValue("STOP")->isNull() ? time(0) : tableGpsTours->getTimeValue("STOP")};
   std::string name {tableGpsTours->getStrValue("NAME")};
   tableGpsTours->reset();

   tableSamples->deleteWhere("type = 'GPS' and aggregate = '%s' and time >= from_unixtime(%ld) and time <= from_unixtime(%ld)",
                             gpsTourAggregate, (long)start, (long)stop);
   tableGpsTourEvents->deleteWhere("tourid = %ld", id);
   tableGpsTours->deleteWhere("%s = %ld", tableGpsTours->getField("ID")->getDbName(), id);

   tell(eloAlways, "GPS: Deleted tour %ld '%s'", id, name.c_str());
   gpsTourPushState();

   return success;
}

int HomeCtl::gpsTourRename(long id, const char* name, json_t* obj)
{
   if (isEmpty(name))
      return fail;

   tableGpsTours->clear();
   tableGpsTours->setValue("ID", id);

   if (!tableGpsTours->find())
   {
      tableGpsTours->reset();
      return fail;
   }

   std::string tourName {name};

   if (tourName.length() > 100)
      tourName.resize(100);

   tableGpsTours->setValue("NAME", tourName.c_str());

   // the odometer readings can be corrected / added later

   if (obj && json_object_get(obj, "odometer"))
      tableGpsTours->setValue("ODOMETER", getLongFromJson(obj, "odometer", 0));

   if (obj && json_object_get(obj, "odometerend"))
      tableGpsTours->setValue("ODOMETEREND", getLongFromJson(obj, "odometerend", 0));

   if (obj && json_object_get(obj, "comment"))
      tableGpsTours->setValue("COMMENT", getStringFromJson(obj, "comment", ""));

   long odometer {tableGpsTours->getIntValue("ODOMETER")};
   tableGpsTours->store();
   tableGpsTours->reset();

   if (id == gpsTour.id)
   {
      gpsTour.name = tourName;
      gpsTour.odometer = odometer;
   }

   gpsTourPushState();

   return success;
}

//***************************************************************************
// Update - called on every new GPS coordinate
//***************************************************************************

int HomeCtl::gpsTourUpdate(time_t now)
{
   if (!gpsTour.id)
      return done;

   if (!gpsTour.hasLastPoint)
      return gpsTourStorePoint(now, gpsCoordinate);

   double distance {gpsDistance(gpsTour.lastPoint, gpsCoordinate)};

   if (distance < gpsTourMinDistance)
      return gpsTourCheckPause(now);

   if (gpsTour.paused)
   {
      gpsTour.pauseTime += now - gpsTour.pausedAt;
      gpsTour.paused = false;
      tell(eloAlways, "GPS: Tour '%s' continued after %ld minutes pause", gpsTour.name.c_str(), (long)(now - gpsTour.pausedAt) / 60);
      gpsTourStorePoint(now, gpsCoordinate, distance);
      gpsTourPushState();

      return success;
   }

   return gpsTourStorePoint(now, gpsCoordinate, distance);
}

// pause the tour without movement for 'gpsTourPauseAfter' minutes

int HomeCtl::gpsTourCheckPause(time_t now)
{
   if (!gpsTour.id || gpsTour.paused || !gpsTour.lastMoveAt)
      return done;

   if (now - gpsTour.lastMoveAt < gpsTourPauseAfter * tmeSecondsPerMinute)
      return done;

   gpsTour.paused = true;
   gpsTour.pausedAt = gpsTour.lastMoveAt;
   tell(eloAlways, "GPS: Tour '%s' paused, no movement since %s", gpsTour.name.c_str(), l2pTime(gpsTour.lastMoveAt).c_str());
   gpsTourPushState();

   return success;
}

//***************************************************************************
// Store Point / State
//***************************************************************************

// the coordinate and, with the same time stamp, speed and altitude (if the sensors deliver data)

int HomeCtl::gpsTourStorePoint(time_t now, const GpsCoordinate& c, double distance)
{
   tableSamples->clear();
   tableSamples->setValue("TIME", (long)now);
   tableSamples->setValue("ADDRESS", (long)gpsCoordinateAddress);
   tableSamples->setValue("TYPE", "GPS");
   tableSamples->setValue("AGGREGATE", gpsTourAggregate);
   tableSamples->setValue("SAMPLES", 1);
   tableSamples->setValue("TEXT", gpsCoordinateText(c).c_str());
   tableSamples->store();
   tableSamples->reset();

   auto itGps {sensors.find("GPS")};

   if (itGps != sensors.end())
   {
      for (uint address : { gpsSpeedAddress, gpsAltitudeAddress })
      {
         auto itSensor {itGps->second.find(address)};

         if (itSensor == itGps->second.end() || !itSensor->second.active || !itSensor->second.valid() || isNan(itSensor->second.value))
            continue;

         tableSamples->clear();
         tableSamples->setValue("TIME", (long)now);
         tableSamples->setValue("ADDRESS", (long)address);
         tableSamples->setValue("TYPE", "GPS");
         tableSamples->setValue("AGGREGATE", gpsTourAggregate);
         tableSamples->setValue("SAMPLES", 1);
         tableSamples->setValue("VALUE", itSensor->second.value);
         tableSamples->store();
         tableSamples->reset();
      }
   }

   gpsTour.lastPoint = c;
   gpsTour.hasLastPoint = true;
   gpsTour.lastMoveAt = now;
   gpsTour.distance += distance;
   gpsTour.points++;

   tell(eloDetail, "GPS: Tour '%s' point %ld stored (%s, +%.0f m)", gpsTour.name.c_str(), gpsTour.points,
        gpsCoordinateText(c).c_str(), distance);

   return gpsTourStoreState();
}

int HomeCtl::gpsTourStoreState()
{
   if (!gpsTour.id)
      return done;

   tableGpsTours->clear();
   tableGpsTours->setValue("ID", gpsTour.id);

   if (tableGpsTours->find())
   {
      tableGpsTours->clearChanged();
      tableGpsTours->setValue("DISTANCE", gpsTour.distance);
      tableGpsTours->setValue("POINTS", gpsTour.points);
      tableGpsTours->setValue("PAUSETIME", gpsTour.pauseTime);

      if (tableGpsTours->getChanges())
         tableGpsTours->store();
   }

   tableGpsTours->reset();

   return success;
}

// the RECORD flag of the GPS coordinate in valuefacts shows (and persists) the recording

int HomeCtl::gpsTourSetRecordFlag(bool on)
{
   tableValueFacts->clear();
   tableValueFacts->setValue("TYPE", "GPS");
   tableValueFacts->setValue("ADDRESS", (long)gpsCoordinateAddress);

   if (tableValueFacts->find())
   {
      if (!tableValueFacts->hasValue("RECORD", on ? "A" : "D"))
      {
         tableValueFacts->setValue("RECORD", on ? "A" : "D");
         tableValueFacts->store();
      }

      auto itGps {sensors.find("GPS")};

      if (itGps != sensors.end() && itGps->second.find(gpsCoordinateAddress) != itGps->second.end())
         itGps->second[gpsCoordinateAddress].record = on;
   }

   tableValueFacts->reset();

   return success;
}

//***************************************************************************
// To JSON
//***************************************************************************

int HomeCtl::gpsTours2Json(json_t* obj)
{
   json_object_set_new(obj, "minDistance", json_integer(gpsTourMinDistance));
   json_object_set_new(obj, "pauseAfter", json_integer(gpsTourPauseAfter));
   json_object_set_new(obj, "endTolerance", json_integer(gpsTourEndTolerance));

   if (gpsTour.id)
   {
      json_t* oActive {json_object()};

      json_object_set_new(oActive, "id", json_integer(gpsTour.id));
      json_object_set_new(oActive, "name", json_string(gpsTour.name.c_str()));
      json_object_set_new(oActive, "start", json_integer(gpsTour.start));
      json_object_set_new(oActive, "paused", json_boolean(gpsTour.paused));
      json_object_set_new(oActive, "lastmove", json_integer(gpsTour.lastMoveAt));
      json_object_set_new(oActive, "distance", json_real(gpsTour.distance));
      json_object_set_new(oActive, "points", json_integer(gpsTour.points));
      json_object_set_new(oActive, "pausetime", json_integer(gpsTour.pauseTime + (gpsTour.paused ? time(0) - gpsTour.pausedAt : 0)));
      json_object_set_new(oActive, "odometer", json_integer(gpsTour.odometer));
      json_object_set_new(obj, "active", oActive);
   }
   else
   {
      json_object_set_new(obj, "active", json_null());
   }

   json_t* oTours {json_array()};
   tableGpsTours->clear();

   for (int f = selectGpsTours->find(); f; f = selectGpsTours->fetch())
   {
      json_t* oTour {json_object()};
      long id {tableGpsTours->getIntValue("ID")};
      bool isActive {id == gpsTour.id};

      json_object_set_new(oTour, "id", json_integer(id));
      json_object_set_new(oTour, "name", json_string(tableGpsTours->getStrValue("NAME")));
      json_object_set_new(oTour, "start", json_integer(tableGpsTours->getTimeValue("START")));

      if (tableGpsTours->getValue("STOP")->isNull())
         json_object_set_new(oTour, "stop", json_null());
      else
         json_object_set_new(oTour, "stop", json_integer(tableGpsTours->getTimeValue("STOP")));

      json_object_set_new(oTour, "distance", json_real(isActive ? gpsTour.distance : tableGpsTours->getFloatValue("DISTANCE")));
      json_object_set_new(oTour, "points", json_integer(isActive ? gpsTour.points : tableGpsTours->getIntValue("POINTS")));
      json_object_set_new(oTour, "pausetime", json_integer(isActive ? gpsTour.pauseTime : tableGpsTours->getIntValue("PAUSETIME")));
      json_object_set_new(oTour, "odometer", json_integer(tableGpsTours->getIntValue("ODOMETER")));
      json_object_set_new(oTour, "odometerend", json_integer(tableGpsTours->getIntValue("ODOMETEREND")));
      json_object_set_new(oTour, "comment", json_string(tableGpsTours->getStrValue("COMMENT")));
      json_array_append_new(oTours, oTour);
   }

   selectGpsTours->freeResult();

   // the costs / consumption per tour (own loop, the events statement resets the tours table)

   size_t i {0};
   json_t* oTour {};

   json_array_foreach(oTours, i, oTour)
   {
      json_t* oEvents {json_object()};
      gpsTourEvents2Json(oEvents, getLongFromJson(oTour, "id", 0), getLongFromJson(oTour, "odometer", 0), getLongFromJson(oTour, "odometerend", 0));
      json_object_set(oTour, "totals", json_object_get(oEvents, "totals"));
      json_decref(oEvents);
   }

   if (gpsTour.id)
   {
      json_t* oEvents {json_object()};
      gpsTourEvents2Json(oEvents, gpsTour.id, gpsTour.odometer, 0);
      json_object_set(json_object_get(obj, "active"), "totals", json_object_get(oEvents, "totals"));
      json_decref(oEvents);
   }

   json_object_set_new(obj, "tours", oTours);

   return success;
}

int HomeCtl::gpsTourPoints2Json(json_t* obj, long id)
{
   tableGpsTours->clear();
   tableGpsTours->setValue("ID", id);

   if (!tableGpsTours->find())
   {
      tableGpsTours->reset();
      return fail;
   }

   time_t start {tableGpsTours->getTimeValue("START")};
   time_t stop {tableGpsTours->getValue("STOP")->isNull() ? time(0) : tableGpsTours->getTimeValue("STOP")};
   bool isActive {id == gpsTour.id};
   long odometer {tableGpsTours->getIntValue("ODOMETER")};
   long odometerEnd {tableGpsTours->getIntValue("ODOMETEREND")};

   json_object_set_new(obj, "id", json_integer(id));
   json_object_set_new(obj, "name", json_string(tableGpsTours->getStrValue("NAME")));
   json_object_set_new(obj, "start", json_integer(start));
   json_object_set_new(obj, "stop", isActive ? json_null() : json_integer(stop));
   json_object_set_new(obj, "distance", json_real(isActive ? gpsTour.distance : tableGpsTours->getFloatValue("DISTANCE")));
   json_object_set_new(obj, "pausetime", json_integer(isActive ? gpsTour.pauseTime : tableGpsTours->getIntValue("PAUSETIME")));
   tableGpsTours->reset();

   json_t* oPoints {json_array()};

   tableSamples->clear();
   tableSamples->setValue("ADDRESS", (long)gpsCoordinateAddress);
   tableSamples->setValue("TYPE", "GPS");
   tableSamples->setValue("AGGREGATE", gpsTourAggregate);
   gpsTourFrom.setValue((long)start);
   gpsTourTo.setValue((long)stop);

   for (int f = selectGpsTourSamples->find(); f; f = selectGpsTourSamples->fetch())
   {
      GpsCoordinate c;

      if (!parseGpsText(tableSamples->getStrValue("TEXT"), c))
         continue;

      json_t* oPoint {json_array()};
      json_array_append_new(oPoint, json_real(c.latitude));
      json_array_append_new(oPoint, json_real(c.longitude));
      json_array_append_new(oPoint, json_integer(tableSamples->getTimeValue("TIME")));
      json_array_append_new(oPoints, oPoint);
   }

   selectGpsTourSamples->freeResult();
   tableSamples->reset();

   json_object_set_new(obj, "points", oPoints);
   gpsTourEvents2Json(obj, id, odometer, odometerEnd);

   return success;
}

//***************************************************************************
// Events (fuel, toll, pause, night, note, odometer at start / end)
//***************************************************************************

// insert (no 'id') or update; missing time -> now, missing position -> the current GPS
//  position or the last point of the active tour, missing tour -> the active tour

int HomeCtl::gpsTourEventStore(json_t* obj, std::string& error)
{
   static const std::set<std::string> types {"fuel", "toll", "pause", "night", "note"};

   long id {getLongFromJson(obj, "id", 0)};
   std::string type {getStringFromJson(obj, "type", "")};

   if (!types.count(type))
   {
      error = "Unbekannter Ereignistyp '" + type + "'";
      return fail;
   }

   tableGpsTourEvents->clear();

   if (id)
   {
      tableGpsTourEvents->setValue("ID", id);

      if (!tableGpsTourEvents->find())
      {
         tableGpsTourEvents->reset();
         error = "Ereignis nicht gefunden";
         return fail;
      }
   }
   else
   {
      long tourId {getLongFromJson(obj, "tourid", gpsTour.id)};

      if (!tourId)
      {
         error = "Keine Tour aktiv";
         return fail;
      }

      tableGpsTourEvents->setValue("TOURID", tourId);
      tableGpsTourEvents->setValue("TIME", getLongFromJson(obj, "time", time(0)));

      double lat {getDoubleFromJson(obj, "lat", 0.0)};
      double lon {getDoubleFromJson(obj, "lon", 0.0)};

      if (!lat && !lon)
      {
         if (gpsCoordinate.latitude || gpsCoordinate.longitude)
         {
            lat = gpsCoordinate.latitude;
            lon = gpsCoordinate.longitude;
         }
         else if (tourId == gpsTour.id && gpsTour.hasLastPoint)
         {
            lat = gpsTour.lastPoint.latitude;
            lon = gpsTour.lastPoint.longitude;
         }
      }

      tableGpsTourEvents->setValue("LAT", lat);
      tableGpsTourEvents->setValue("LON", lon);
      tableGpsTourEvents->setValue("DISTANCE", tourId == gpsTour.id ? gpsTour.distance : 0.0);   // GPS distance so far
   }

   tableGpsTourEvents->setValue("TYPE", type.c_str());

   if (json_object_get(obj, "time") && id)
      tableGpsTourEvents->setValue("TIME", getLongFromJson(obj, "time", time(0)));

   tableGpsTourEvents->setValue("ODOMETER", getLongFromJson(obj, "odometer", 0));
   tableGpsTourEvents->setValue("AMOUNT", getDoubleFromJson(obj, "amount", 0.0));
   tableGpsTourEvents->setValue("FULL", getBoolFromJson(obj, "full", true) ? 1L : 0L);
   tableGpsTourEvents->setValue("PRICE", getDoubleFromJson(obj, "price", 0.0));

   std::string note {getStringFromJson(obj, "note", "")};

   if (note.length() > 200)
      note.resize(200);

   tableGpsTourEvents->setValue("NOTE", note.c_str());
   tableGpsTourEvents->store();

   tell(eloAlways, "GPS: Tour %ld event '%s' %s (%ld km, %.2f l, %.2f EUR)", tableGpsTourEvents->getIntValue("TOURID"), type.c_str(),
        id ? "updated" : "stored", tableGpsTourEvents->getIntValue("ODOMETER"), tableGpsTourEvents->getFloatValue("AMOUNT"),
        tableGpsTourEvents->getFloatValue("PRICE"));

   tableGpsTourEvents->reset();

   return success;
}

int HomeCtl::gpsTourEventDelete(long id)
{
   tableGpsTourEvents->clear();
   tableGpsTourEvents->setValue("ID", id);

   if (!tableGpsTourEvents->find())
   {
      tableGpsTourEvents->reset();
      return fail;
   }

   tableGpsTourEvents->reset();
   tableGpsTourEvents->deleteWhere("id = %ld", id);
   tell(eloAlways, "GPS: Deleted tour event %ld", id);

   return success;
}

// 'events' (in time order) and 'totals': costs by kind, liters, odometer km and the fuel
//  consumption - per fuel stop (liters since the previous odometer reading, the tour start
//  or the previous fuel stop) and the average

int HomeCtl::gpsTourEvents2Json(json_t* obj, long tourId, long odometerStart, long odometerEnd)
{
   json_t* oEvents {json_array()};
   json_t* oTotals {json_object()};

   double fuelCosts {0.0};
   double tollCosts {0.0};
   double otherCosts {0.0};
   double liters {0.0};
   double litersCounted {0.0};      // liters with a known odometer distance (for the average)
   double litersCountedGps {0.0};   // liters with a known GPS distance
   double pendingLiters {0.0};      // partial fills since the last full tank
   long kmCounted {0};
   double kmCountedGps {0.0};
   long previousOdometer {odometerStart};
   double previousDistance {0.0};   // GPS distance at the last full tank (0 = tour start)
   int fuelStops {0};

   tableGpsTourEvents->clear();
   tableGpsTourEvents->setValue("TOURID", tourId);

   for (int f = selectGpsTourEvents->find(); f; f = selectGpsTourEvents->fetch())
   {
      std::string type {tableGpsTourEvents->getStrValue("TYPE")};
      long odometer {tableGpsTourEvents->getIntValue("ODOMETER")};
      double distance {tableGpsTourEvents->getFloatValue("DISTANCE")};
      double amount {tableGpsTourEvents->getFloatValue("AMOUNT")};
      double price {tableGpsTourEvents->getFloatValue("PRICE")};
      bool full {tableGpsTourEvents->getValue("FULL")->isNull() || tableGpsTourEvents->getIntValue("FULL") != 0};
      json_t* oEvent {json_object()};

      json_object_set_new(oEvent, "id", json_integer(tableGpsTourEvents->getIntValue("ID")));
      json_object_set_new(oEvent, "time", json_integer(tableGpsTourEvents->getTimeValue("TIME")));
      json_object_set_new(oEvent, "type", json_string(type.c_str()));
      json_object_set_new(oEvent, "lat", json_real(tableGpsTourEvents->getFloatValue("LAT")));
      json_object_set_new(oEvent, "lon", json_real(tableGpsTourEvents->getFloatValue("LON")));
      json_object_set_new(oEvent, "odometer", json_integer(odometer));
      json_object_set_new(oEvent, "distance", json_real(distance));
      json_object_set_new(oEvent, "amount", json_real(amount));
      json_object_set_new(oEvent, "full", json_boolean(full));
      json_object_set_new(oEvent, "price", json_real(price));
      json_object_set_new(oEvent, "note", json_string(tableGpsTourEvents->getStrValue("NOTE")));

      if (type == "fuel")
      {
         fuelStops++;
         fuelCosts += price;
         liters += amount;

         // a partial fill counts to the next full tank, the distance runs from full tank to full tank

         if (!full)
         {
            pendingLiters += amount;
            json_array_append_new(oEvents, oEvent);
            continue;
         }

         if (odometer > 0 && previousOdometer > 0 && odometer > previousOdometer && amount + pendingLiters > 0)
         {
            long km {odometer - previousOdometer};
            json_object_set_new(oEvent, "km", json_integer(km));
            json_object_set_new(oEvent, "consumption", json_real((amount + pendingLiters) / km * 100.0));

            if (pendingLiters > 0)
               json_object_set_new(oEvent, "pendingLiters", json_real(pendingLiters));

            litersCounted += amount + pendingLiters;
            kmCounted += km;
         }

         // the same by the GPS distance of the tour

         if (distance > previousDistance + 1000.0 && amount + pendingLiters > 0)
         {
            double kmGps {(distance - previousDistance) / 1000.0};
            json_object_set_new(oEvent, "kmGps", json_real(kmGps));
            json_object_set_new(oEvent, "consumptionGps", json_real((amount + pendingLiters) / kmGps * 100.0));
            litersCountedGps += amount + pendingLiters;
            kmCountedGps += kmGps;
         }

         pendingLiters = 0.0;

         if (odometer > 0)
            previousOdometer = odometer;

         if (distance > 0)
            previousDistance = distance;
      }
      else if (type == "toll")
         tollCosts += price;
      else
         otherCosts += price;

      json_array_append_new(oEvents, oEvent);
   }

   selectGpsTourEvents->freeResult();
   tableGpsTourEvents->reset();

   json_object_set_new(oTotals, "events", json_integer(json_array_size(oEvents)));
   json_object_set_new(oTotals, "fuelStops", json_integer(fuelStops));
   json_object_set_new(oTotals, "fuelCosts", json_real(fuelCosts));
   json_object_set_new(oTotals, "tollCosts", json_real(tollCosts));
   json_object_set_new(oTotals, "otherCosts", json_real(otherCosts));
   json_object_set_new(oTotals, "totalCosts", json_real(fuelCosts + tollCosts + otherCosts));
   json_object_set_new(oTotals, "liters", json_real(liters));
   json_object_set_new(oTotals, "odometerStart", json_integer(odometerStart));
   json_object_set_new(oTotals, "odometerEnd", json_integer(odometerEnd));
   json_object_set_new(oTotals, "odometerKm", odometerStart && odometerEnd > odometerStart ? json_integer(odometerEnd - odometerStart) : json_null());
   json_object_set_new(oTotals, "consumption", kmCounted ? json_real(litersCounted / kmCounted * 100.0) : json_null());
   json_object_set_new(oTotals, "consumptionKm", json_integer(kmCounted));
   json_object_set_new(oTotals, "consumptionGps", kmCountedGps > 0 ? json_real(litersCountedGps / kmCountedGps * 100.0) : json_null());
   json_object_set_new(oTotals, "consumptionGpsKm", json_real(kmCountedGps));

   json_object_set_new(obj, "events", oEvents);
   json_object_set_new(obj, "totals", oTotals);

   return success;
}

// send the tour list including the state of the active tour (client 0 -> all clients)

int HomeCtl::gpsTourPushState(long client)
{
   json_t* oJson {json_object()};
   gpsTours2Json(oJson);

   return pushOutMessage(oJson, "gpstours", client);
}

//***************************************************************************
// Perform GPS Tour (WEBIF request)
//   { "action": "list" }                         -> 'gpstours'
//   { "action": "points", "id": <id> }           -> 'gpstourpoints'
//   { "action": "events", "id": <id> }           -> 'gpstourevents' (events and totals only, for the list)
//   { "action": "start", "name": "<name>", "odometer": <km> }
//   { "action": "stop", "stop": <time>, "odometer": <km> }
//   { "action": "delete", "id": <id> }
//   { "action": "rename", "id": <id>, "name": "<name>", "odometer": <km>, "odometerend": <km>, "comment": "<text>" }   (all but the name optional)
//   { "action": "event", "id": <id>, "tourid": <id>, "type": "fuel|toll|pause|night|note",
//                        "odometer": <km>, "amount": <l>, "full": true|false, "price": <EUR>, "note": "<text>" }   -> insert / update
//   { "action": "eventdelete", "id": <id> }
//***************************************************************************

int HomeCtl::performGpsTour(json_t* obj, long client)
{
   std::string action {getStringFromJson(obj, "action", "list")};

   if (action == "list")
      return gpsTourPushState(client);

   if (action == "points")
   {
      json_t* oJson {json_object()};

      if (gpsTourPoints2Json(oJson, getLongFromJson(obj, "id", 0)) != success)
      {
         json_decref(oJson);
         return replyResult(fail, "Tour nicht gefunden", client);
      }

      return pushOutMessage(oJson, "gpstourpoints", client, false, 10);   // coordinates
   }

   if (action == "events")
   {
      long id {getLongFromJson(obj, "id", 0)};

      tableGpsTours->clear();
      tableGpsTours->setValue("ID", id);

      if (!tableGpsTours->find())
      {
         tableGpsTours->reset();
         return replyResult(fail, "Tour nicht gefunden", client);
      }

      long odometer {tableGpsTours->getIntValue("ODOMETER")};
      long odometerEnd {tableGpsTours->getIntValue("ODOMETEREND")};
      tableGpsTours->reset();

      json_t* oJson {json_object()};
      json_object_set_new(oJson, "id", json_integer(id));
      gpsTourEvents2Json(oJson, id, odometer, odometerEnd);

      return pushOutMessage(oJson, "gpstourevents", client);
   }

   // modifying actions need control rights

   auto itClient {wsClients.find((void*)client)};

   if (itClient == wsClients.end() || !(itClient->second.rights & urControl))
      return replyResult(fail, "Keine Berechtigung", client);

   if (action == "start")
   {
      int status {gpsTourStart(getStringFromJson(obj, "name", ""), getLongFromJson(obj, "odometer", 0))};

      if (status == success)
         return replyResult(success, "Tour Aufzeichnung gestartet", client);

      return replyResult(fail, status == ignore ? "GPS Sensor (GPS:0x0a 'Coordinate') ist nicht aktiv" : "Es ist bereits eine Tour aktiv", client);
   }

   if (action == "stopinfo")
   {
      // the WEBIF asks before stopping: now or the arrival at the final position?

      if (!gpsTour.id)
         return replyResult(fail, "Keine Tour aktiv", client);

      json_t* oJson {json_object()};
      time_t arrival {0};
      double tolerance {(double)gpsTourEndTolerance};
      bool hasArrival {gpsTourArrival(arrival, tolerance) == success};

      json_object_set_new(oJson, "id", json_integer(gpsTour.id));
      json_object_set_new(oJson, "name", json_string(gpsTour.name.c_str()));
      json_object_set_new(oJson, "now", json_integer(time(0)));
      json_object_set_new(oJson, "arrival", hasArrival ? json_integer(arrival) : json_null());
      json_object_set_new(oJson, "tolerance", json_integer((long)tolerance));

      return pushOutMessage(oJson, "gpstourstopinfo", client);
   }

   if (action == "stop")
   {
      if (gpsTourStop(getLongFromJson(obj, "stop", 0), getLongFromJson(obj, "odometer", 0)) == success)
         return replyResult(success, "Tour Aufzeichnung beendet", client);

      return replyResult(fail, "Keine Tour aktiv", client);
   }

   if (action == "delete")
   {
      if (gpsTourDelete(getLongFromJson(obj, "id", 0)) == success)
         return replyResult(success, "Tour gelöscht", client);

      return replyResult(fail, "Tour kann nicht gelöscht werden (aktiv oder nicht gefunden)", client);
   }

   if (action == "rename")
   {
      if (gpsTourRename(getLongFromJson(obj, "id", 0), getStringFromJson(obj, "name", ""), obj) == success)
         return replyResult(success, "Tour gespeichert", client);

      return replyResult(fail, "Tour nicht gefunden", client);
   }

   if (action == "event")
   {
      std::string error;

      if (gpsTourEventStore(obj, error) != success)
         return replyResult(fail, error.c_str(), client);

      gpsTourPushState();
      return replyResult(success, "Ereignis gespeichert", client);
   }

   if (action == "eventdelete")
   {
      if (gpsTourEventDelete(getLongFromJson(obj, "id", 0)) != success)
         return replyResult(fail, "Ereignis nicht gefunden", client);

      gpsTourPushState();
      return replyResult(success, "Ereignis gelöscht", client);
   }

   return replyResult(fail, "Unbekannte Aktion", client);
}
