/*
 *  shade.c
 *
 *  (c) 2026 Jörg Wendel
 *
 * This code is distributed under the terms and conditions of the
 * GNU GENERAL PUBLIC LICENSE. See the file COPYING for details.
 *
 *  Shading of the blinds (HMB)
 *
 *  In summer the blinds are moved to a shading position while the sun shines on
 *  the window, configured without LUA in the dialog 'Zeitsteuerung' of the blind,
 *  stored in its SETTINGS:
 *
 *  { "shade": { "enabled": true,
 *               "level": 30,                       shading position [%] (100 = open)
 *               "azFrom": 180, "azTo": 290,        sun on the window, azimuth range [°]
 *               "minElevation": 10,                minimal elevation of the sun [°]
 *               "luxSensor": "DZS:0x30", "luxMin": 25000,     brightness (optional)
 *               "tempSensor": "P4VA:0x04", "tempMin": 22,     outside temperature (optional)
 *               "delayOn": 10, "delayOff": 20 } }  [min] conditions met / lost that long
 *
 *  - the shading moves only down to the shading position, a blind standing lower stays
 *  - at the end it moves back to the position before, only if it still stands at the
 *    shading position
 *  - moved by hand while shading: no shading for this blind until the next day
 *  - the time control (night) has priority: closing / opening by it ends the shading
 *    without moving back
 *
 *  The state is kept in memory only (a restart starts 'idle').
 */

#include <cmath>

#include "daemon.h"

//***************************************************************************
// Sensor of a Key 'TYPE:0xNN'
//***************************************************************************

HomeCtl::SensorData* HomeCtl::sensorOfKey(const char* key)
{
   if (isEmpty(key) || !strchr(key, ':'))
      return nullptr;

   std::string type {key, (size_t)(strchr(key, ':') - key)};
   uint address = strtoul(strchr(key, ':') + 1, nullptr, 0);

   if (!sensors.count(type) || !sensors[type].count(address))
      return nullptr;

   return &sensors[type][address];
}

//***************************************************************************
// Shade Conditions
//   all configured conditions met? text: the reason (for the info dialog / log)
//***************************************************************************

bool HomeCtl::shadeConditions(json_t* jShade, std::string& text)
{
   double azimuth {0}, elevation {0};
   sunPosition(time(0), sunTimes.position.latitude, sunTimes.position.longitude, azimuth, elevation);

   double azFrom {getDoubleFromJson(jShade, "azFrom", 0)};
   double azTo {getDoubleFromJson(jShade, "azTo", 360)};
   bool inRange {azFrom <= azTo ? azimuth >= azFrom && azimuth <= azTo : azimuth >= azFrom || azimuth <= azTo};

   char buf[100];
   snprintf(buf, sizeof(buf), "Sonne %.0f° / %.0f°", azimuth, elevation);
   text = buf;

   if (!inRange || elevation < getDoubleFromJson(jShade, "minElevation", 0))
   {
      text += " (nicht auf dem Fenster)";
      return false;
   }

   const char* luxSensor {getStringFromJson(jShade, "luxSensor", "")};

   if (!isEmpty(luxSensor))
   {
      SensorData* sensor {sensorOfKey(luxSensor)};

      if (!sensor || !sensor->valid())
      {
         text += ", Helligkeit unbekannt";
         return false;
      }

      snprintf(buf, sizeof(buf), ", %.0f lx", sensor->value);
      text += buf;

      if (sensor->value < getDoubleFromJson(jShade, "luxMin", 0))
         return false;
   }

   const char* tempSensor {getStringFromJson(jShade, "tempSensor", "")};

   if (!isEmpty(tempSensor))
   {
      SensorData* sensor {sensorOfKey(tempSensor)};

      if (!sensor || !sensor->valid())
      {
         text += ", Temperatur unbekannt";
         return false;
      }

      snprintf(buf, sizeof(buf), ", %.1f °C", sensor->value);
      text += buf;

      if (sensor->value < getDoubleFromJson(jShade, "tempMin", 0))
         return false;
   }

   return true;
}

//***************************************************************************
// Shade Move
//***************************************************************************

int HomeCtl::shadeMove(uint address, int level, const char* why)
{
   SensorData& sensor {sensors["HMB"][address]};

   tell(eloAlways, "Shading: 'HMB:0x%02x' (%s) to %d%% (%s)", address, sensor.title.c_str(), level, why);

   sensor.state = level == 100;
   return mqttNodeRedPublishAction(sensor, level, false, true);   // LEVEL also if moving
}

//***************************************************************************
// Shade Update
//   called by the main loop
//***************************************************************************

int HomeCtl::shadeUpdate()
{
   time_t now {time(0)};
   time_t today {midnightOf(now)};

   if (!sensors.count("HMB"))
      return done;

   for (auto& [address, sensor] : sensors["HMB"])
   {
      ShadeState& state {shadeStates[address]};
      cDbRow* fact {valueFactRowOf("HMB", address)};
      json_t* oSettings {fact && fact->hasValue("STATE", "A") && !fact->getValue("SETTINGS")->isEmpty() ?
                         jsonLoad(fact->getStrValue("SETTINGS"), 0, true) : nullptr};
      json_t* jShade {oSettings ? getObjectFromJson(oSettings, "shade") : nullptr};

      if (!jShade || !getBoolFromJson(jShade, "enabled", false))
      {
         if (state.phase != spIdle)
            tell(eloAlways, "Shading: 'HMB:0x%02x' (%s) disabled", address, sensor.title.c_str());

         state = {};                                     // no shading configured -> no state text

         if (oSettings)
            json_decref(oSettings);

         continue;
      }

      int level {getIntFromJson(jShade, "level", 30)};
      time_t delayOn {getIntFromJson(jShade, "delayOn", 10) * tmeSecondsPerMinute};
      time_t delayOff {getIntFromJson(jShade, "delayOff", 20) * tmeSecondsPerMinute};
      std::string why;
      bool cond {shadeConditions(jShade, why)};

      json_decref(oSettings);

      if (state.suspendedDay && state.suspendedDay != today)
         state.suspendedDay = 0;                            // a new day

      if (state.phase == spIdle)
      {
         state.lostSince = 0;
         state.condSince = cond ? (state.condSince ? state.condSince : now) : 0;

         if (state.suspendedDay)
            state.text = "für heute ausgesetzt (von Hand gefahren) – " + why;
         else if (!cond)
            state.text = "bereit – " + why;
         else if (now - state.condSince < delayOn)
            state.text = "wartet (" + std::to_string((delayOn - (now - state.condSince) + 59) / 60) + " min) – " + why;
         else if (sensor.value <= level + 1)
            state.text = "Rolladen steht schon tiefer – " + why;
         else
         {
            state.phase = spActive;
            state.levelBefore = lround(sensor.value);
            state.movedAt = now;
            state.text = "aktiv – " + why;
            shadeMove(address, level, why.c_str());
         }

         continue;
      }

      // active: moved by hand? (the blind needs some time to get there)

      if (!sensor.working && now - state.movedAt > 2 * tmeSecondsPerMinute && fabs(sensor.value - level) > 2)
      {
         tell(eloAlways, "Shading: 'HMB:0x%02x' (%s) moved by hand, suspended for today", address, sensor.title.c_str());
         state = {};
         state.suspendedDay = today;
         state.text = "für heute ausgesetzt (von Hand gefahren)";
         continue;
      }

      state.lostSince = cond ? 0 : (state.lostSince ? state.lostSince : now);

      if (state.lostSince && now - state.lostSince >= delayOff)
      {
         int before {state.levelBefore};
         state = {};
         state.text = "beendet – " + why;
         shadeMove(address, before, ("Ende, " + why).c_str());
         continue;
      }

      state.text = "aktiv seit " + l2pTime(state.movedAt, "%H:%M") + ", vorher " + std::to_string(state.levelBefore) + " %" +
         (state.lostSince ? " – endet in " + std::to_string((delayOff - (now - state.lostSince) + 59) / 60) + " min" : "") +
         " – " + why;
   }

   return done;
}

//***************************************************************************
// Shade End (by the time control - the night has priority)
//***************************************************************************

void HomeCtl::shadeEnd(uint address)
{
   if (shadeStates.count(address) && shadeStates[address].phase != spIdle)
   {
      tell(eloAlways, "Shading: 'HMB:0x%02x' ended by the time control", address);
      shadeStates[address] = {};
   }
}
