//***************************************************************************
// Automation Control
// File p4fill.h
// This code is distributed under the terms and conditions of the
// GNU GENERAL PUBLIC LICENSE. See the file LICENSE for details.
// Date 16.11.2026 - Jörg Wendel
//
// Dynamic pellet filling of the Fröling S 3200 - the decision logic
//
// The control fills the pellet container at two times of the day ('Start der 1. / 2. Pelletsbefüllung',
// parameters of the service menu, 00:00 = off). The suction turbine is loud and stops the burner, so:
//
//  - once a day the two times are set to the configured standard times
//  - is the state 'Betriebsbereit' and the level below the minimum for some minutes, a filling is
//    started now (one of the times is set to now + 5 minutes)
//  - is a filling due within 15 minutes while the boiler heats (preparation, ignition, heating ..),
//    it's postponed by 30 minutes
//  - after N days without an empty container both times are set to 00:00, the control empties the
//    container (the fines at the bottom) and fills on its own when it's empty, then the normal operation
//    starts again
//
// The replacement of the script DynamischePelletaustragung.py of the p4d. Without any dependency to the
// daemon, so it can be tested alone (contrib/p4fill-test.c), the glue is in p4fill.c.
//***************************************************************************

#pragma once

#include <time.h>
#include <math.h>
#include <string>
#include <vector>

namespace P4Fill
{
   static constexpr int minutesPerDay {1440};
   static constexpr int leadMinutes {5};             // a filling 'now' is set to now + 5 minutes
   static constexpr int postponeWindow {15};         // a filling due within this minutes while heating ..
   static constexpr int postponeBy {30};             //   .. is postponed to now + 30 minutes
   static constexpr int postponeMaxLevel {80};       //   .. if the level is below 80%
   static constexpr int refillGuard {30};            // [min] no second 'filling now' within this time
   static constexpr int maxZeroDays {3};             // [days] running empty ends after this time even if the level never was 0

   struct Config
   {
      bool enabled {false};
      int address[2] {60, 516};                      // the parameters 'Start der 1. / 2. Pelletsbefüllung'
      int stdTime[2] {16*60, 0};                     // [min of the day] standard times, 0 = off
      int minLevel {20};                             // [%] below it a filling is started in the ready state
      int holdMinutes {5};                           // .. if the condition holds this time
      int daysToZero {8};                            // run the container empty after this days, 0 = never
      std::string readyState {"Betriebsbereit"};
      std::vector<std::string> delayStates {"Vorbereitung", "Vorwärmen", "Zünden", "Heizen"};
   };

   struct State                                      // persistent (hidden config items)
   {
      time_t lastZero {0};                           // the container was empty the last time
      std::string resetDay;                          // the standard times were written this day (YYYY-MM-DD)
      bool toZero {false};                           // running the container empty
      time_t lowSince {0};                           // (not persistent) the filling condition holds since
      time_t fillSetAt {0};                          // (not persistent) the last 'filling now'
   };

   struct Input
   {
      time_t now {0};
      int minute {0};                                // minute of the day (local time)
      std::string today;                             // YYYY-MM-DD
      std::string stateInfo;                         // text of the state of the control
      double level {0.0};                            // [%] level of the container
      int times[2] {-1, -1};                         // the times of the control [min of the day], 0 = off
   };

   struct Action
   {
      int slot {0};                                  // 0 -> 1. filling, 1 -> 2. filling
      int minutes {0};                               // new time [min of the day], 0 = off
      std::string reason;
   };

   struct Result
   {
      std::vector<Action> actions;                   // only the times which differ from the ones of the control
      std::vector<std::string> log;                  // events worth to be logged (beside the actions)
      bool persist {false};                          // lastZero, resetDay or toZero changed
   };

   inline std::string minutes2Str(int minutes)
   {
      char buf[10];
      snprintf(buf, sizeof(buf), "%02d:%02d", (minutes / 60) % 24, minutes % 60);
      return buf;
   }

   // 'HH:MM' -> minutes of the day, -1 on a wrong format

   inline int str2Minutes(const std::string& s)
   {
      int h {0}, m {0};
      char c {};

      if (sscanf(s.c_str(), "%d:%d%c", &h, &m, &c) != 2 || h < 0 || h > 24 || m < 0 || m > 59)
         return -1;

      return (h * 60 + m) % minutesPerDay;
   }

   //***************************************************************************
   // Decide
   //   what to write to the control, 'state' is updated
   //***************************************************************************

   inline Result decide(const Config& cfg, const Input& in, State& state)
   {
      Result res;
      int times[2] {in.times[0], in.times[1]};
      std::string reasons[2];
      int level {(int)lround(in.level)};

      auto set = [&](int slot, int minutes, const std::string& reason)
      {
         times[slot] = minutes;
         reasons[slot] = reason;
      };

      auto finish = [&]()
      {
         for (int slot = 0; slot < 2; slot++)
            if (times[slot] != in.times[slot])
               res.actions.push_back({ slot, times[slot], reasons[slot] });

         return res;
      };

      if (!cfg.enabled || in.times[0] < 0 || in.times[1] < 0)
         return res;

      // the container is empty

      if (level < 1)
      {
         if (!state.lastZero || in.now - state.lastZero >= 600)   // not every minute to the database
         {
            if (!state.lastZero || in.now - state.lastZero > 3600)
               res.log.push_back("Pelletbehälter leer (" + std::to_string(level) + "%)");

            state.lastZero = in.now;
            res.persist = true;
         }

         if (state.toZero)
         {
            state.toZero = false;
            state.resetDay = "";                     // the standard times again, right now
            res.log.push_back("Leerfahrt beendet, zurück zum Normalbetrieb");
            res.persist = true;
         }
      }

      if (!state.lastZero)                           // first run, start counting
      {
         state.lastZero = in.now;
         res.persist = true;
      }

      // running empty, but the level never comes to 0 (sensor shows a rest) -> give up

      if (state.toZero && in.now - state.lastZero >= (time_t)(cfg.daysToZero + maxZeroDays) * 24 * 60 * 60)
      {
         res.log.push_back("Leerfahrt abgebrochen, Füllstand nach " + std::to_string(maxZeroDays)
                           + " Tagen nicht unter 1% (" + std::to_string(level) + "%)");
         state.toZero = false;
         state.lastZero = in.now;
         state.resetDay = "";
         res.persist = true;
      }

      // run the container empty

      if (cfg.daysToZero > 0 && in.now - state.lastZero >= (time_t)cfg.daysToZero * 24 * 60 * 60)
      {
         if (!state.toZero)
         {
            state.toZero = true;
            res.persist = true;
            res.log.push_back("Seit " + std::to_string((in.now - state.lastZero) / (24 * 60 * 60))
                              + " Tagen nicht leer, Leerfahrt beginnt");
         }

         for (int slot = 0; slot < 2; slot++)
            set(slot, 0, "Leerfahrt");

         state.lowSince = 0;
         return finish();
      }

      // the standard times, once a day

      if (state.resetDay != in.today)
      {
         for (int slot = 0; slot < 2; slot++)
            set(slot, cfg.stdTime[slot], "Standardzeit");

         state.resetDay = in.today;
         res.persist = true;
      }

      // ready and the level is low -> fill now

      if (in.stateInfo == cfg.readyState && level < cfg.minLevel)
      {
         if (!state.lowSince)
            state.lowSince = in.now;

         if (in.now - state.lowSince >= cfg.holdMinutes * 60 && in.now - state.fillSetAt >= refillGuard * 60)
         {
            int value {(in.minute + leadMinutes) % minutesPerDay};
            value = value ? value : 1;               // 00:00 is 'off'
            int slot {value <= 720 ? 0 : 1};

            set(slot, value, "Füllstand " + std::to_string(level) + "% und " + in.stateInfo);
            state.fillSetAt = in.now;
            state.lowSince = 0;
         }
      }
      else
         state.lowSince = 0;

      // a filling is due soon while the boiler heats -> postpone it

      bool heating {false};

      for (const auto& s : cfg.delayStates)
         if (!s.empty() && s == in.stateInfo)
            heating = true;

      if (heating && level > 1 && level < postponeMaxLevel)
      {
         for (int slot = 0; slot < 2; slot++)
         {
            if (!times[slot])
               continue;

            int distance {(times[slot] - in.minute + minutesPerDay) % minutesPerDay};

            if (distance > 0 && distance < postponeWindow)
            {
               int value {(in.minute + postponeBy) % minutesPerDay};
               value = value ? value : 1;

               set(slot, value, "verschoben, " + in.stateInfo + " und Befüllung in "
                   + std::to_string(distance) + " min (Füllstand " + std::to_string(level) + "%)");
            }
         }
      }

      return finish();
   }
}
