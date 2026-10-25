/*
 *  lightrules.c
 *
 *  (c) 2026 Jörg Wendel
 *
 * This code is distributed under the terms and conditions of the
 * GNU GENERAL PUBLIC LICENSE. See the file COPYING for details.
 *
 *  Light rules
 *
 *  Switches and motion sensors of deCONZ (DZS) switch lights (DZL, DZLG) and
 *  outputs, configured without LUA in the setup dialog of the sensor, stored in
 *  its SETTINGS:
 *
 *  { "rules": [ { "trigger": "button",              button | motion
 *                 "button": 1002,                   button event (trigger 'button')
 *                 "action": "toggle",               toggle | on | off | follow
 *                 "targets": [ "DZL:0x03", ... ],
 *                 "bri": 80,                        brightness [%] on switching on (optional)
 *                 "transition": 15,                 transition time [s] (optional)
 *                 "offAfter": 180,                  off timer [s] (optional)
 *                 "condition": { "start": "dusk", "startOffset": -30,
 *                                "end": "sunrise", "endOffset": 0 } } ] }   switch on only inside (optional)
 *
 *  Each button event / motion of the sensor is an event, also the same value again
 *  (twice the same button toggles on and off, new motion retriggers the timer).
 *
 *  toggle  if one of the targets is on, all off - else all on
 *  on      all on, off     all off
 *  follow  on with motion, off when the sensor reports no motion anymore
 *
 *  Off timer: started / restarted by each event which switches on, at the end only the
 *  targets switched on by the rule are switched off (a light which was already on stays on),
 *  a target switched off by hand meanwhile is dropped. The timers are kept in memory only,
 *  they are lost on a restart.
 */

#include "daemon.h"

//***************************************************************************
// Target 'TYPE:0xNN'
//***************************************************************************

static bool parseTarget(const std::string& target, std::string& type, uint& address)
{
   auto pos {target.find(':')};

   if (pos == std::string::npos)
      return false;

   type = target.substr(0, pos);
   address = strtoul(target.c_str() + pos + 1, nullptr, 0);

   return !type.empty();
}

bool HomeCtl::lightTargetState(const std::string& target)
{
   std::string type;
   uint address {0};

   if (!parseTarget(target, type, address) || !sensors.count(type) || !sensors[type].count(address))
      return false;

   return sensors[type][address].state;
}

int HomeCtl::lightTargetSwitch(const std::string& target, bool on, int bri, int transition)
{
   std::string type;
   uint address {0};

   if (!parseTarget(target, type, address))
      return fail;

   tell(eloDetail, "Light rule: '%s' %s", target.c_str(), on ? "on" : "off");

   // deCONZ with explicit state (a group would be toggled by toggleIo()), transition in 1/10 s

   if (type == "DZL" || type == "DZLG")
      return deconz.toggle(type.c_str(), address, on, on && bri > 0 ? std::min(bri, 100) : na,
                           transition > 0 ? transition * 10 : na, true);

   return toggleIo(address, type.c_str(), on);
}

//***************************************************************************
// Perform Light Rules
//   called by meanwhile(): the events collected by dispatchDeconz() and the timers
//***************************************************************************

int HomeCtl::performLightRules()
{
   while (!lightRuleEvents.empty())
   {
      LightRuleEvent event {lightRuleEvents.front()};
      lightRuleEvents.erase(lightRuleEvents.begin());

      cDbRow* fact {valueFactRowOf(event.type, event.address)};

      if (!fact || !fact->hasValue("STATE", "A") || fact->getValue("SETTINGS")->isEmpty())
         continue;

      json_t* oSettings {jsonLoad(fact->getStrValue("SETTINGS"), 0, true)};

      if (!oSettings)
         continue;

      json_t* jRules {getObjectFromJson(oSettings, "rules")};

      if (jRules && json_is_array(jRules))
      {
         size_t index {0};
         json_t* jRule {};

         json_array_foreach(jRules, index, jRule)
         {
            char* key {};
            asprintf(&key, "%s:0x%02x#%zu", event.type.c_str(), event.address, index);
            lightRuleApply(key, jRule, event);
            free(key);
         }
      }

      json_decref(oSettings);
   }

   static time_t lastCheckAt {0};

   if (lastCheckAt != time(0))
   {
      lastCheckAt = time(0);
      lightRuleCheckTimers();
   }

   return done;
}

//***************************************************************************
// Apply one Rule to an Event
//***************************************************************************

int HomeCtl::lightRuleApply(const char* key, json_t* jRule, const LightRuleEvent& event)
{
   std::string trigger {getStringFromJson(jRule, "trigger", "")};
   std::string action {getStringFromJson(jRule, "action", "")};

   if (trigger == "button")
   {
      if (event.button == na || getIntFromJson(jRule, "button", na) != event.button)
         return done;
   }
   else if (trigger == "motion")
   {
      if (event.presence == na || (!event.presence && action != "follow"))
         return done;
   }
   else
      return done;

   std::vector<std::string> targets;
   json_t* jTargets {getObjectFromJson(jRule, "targets")};
   size_t index {0};
   json_t* jTarget {};

   if (jTargets && json_is_array(jTargets))
      json_array_foreach(jTargets, index, jTarget)
         if (json_is_string(jTarget))
            targets.push_back(json_string_value(jTarget));

   if (targets.empty())
      return done;

   bool on {false};

   if (action == "toggle")
   {
      bool anyOn {false};

      for (const auto& t : targets)
         anyOn = anyOn || lightTargetState(t);

      on = !anyOn;
   }
   else if (action == "on")
      on = true;
   else if (action == "off")
      on = false;
   else if (action == "follow")
      on = event.presence;
   else
      return done;

   // condition (time range) - only for switching on

   json_t* jCond {getObjectFromJson(jRule, "condition")};

   if (on && jCond && json_is_object(jCond))
   {
      Range r;

      if (sunResolve(getStringFromJson(jCond, "start"), getIntFromJson(jCond, "startOffset", 0), r.from) == success &&
          sunResolve(getStringFromJson(jCond, "end"), getIntFromJson(jCond, "endOffset", 0), r.to) == success &&
          !r.inRange(l2hhmm(time(0))))
      {
         tell(eloDetail, "Light rule '%s': outside of the time range, ignored", key);
         return done;
      }
   }

   int bri {getIntFromJson(jRule, "bri", 0)};
   int transition {getIntFromJson(jRule, "transition", 0)};
   int offAfter {getIntFromJson(jRule, "offAfter", 0)};

   tell(eloDetail, "Light rule '%s' (%s): %s", key, action.c_str(), on ? "on" : "off");

   // the targets switched on now (they were off) - for the off timer

   std::vector<std::string> switchedOn;

   for (const auto& t : targets)
   {
      if (on && !lightTargetState(t))
         switchedOn.push_back(t);

      lightTargetSwitch(t, on, bri, transition);
   }

   if (on && offAfter > 0)
   {
      LightRuleTimer& timer {lightRuleTimers[key]};    // new or retriggered

      timer.offAt = time(0) + offAfter;
      timer.transition = transition;

      for (const auto& t : switchedOn)
         timer.switchedOn[t] = false;

      tell(eloDetail, "Light rule '%s': off at %s", key, l2pTime(timer.offAt, "%H:%M:%S").c_str());
   }
   else if (!on)
      lightRuleTimers.erase(key);

   return done;
}

//***************************************************************************
// Check the Off Timers (once per second)
//***************************************************************************

int HomeCtl::lightRuleCheckTimers()
{
   time_t now {time(0)};

   for (auto it = lightRuleTimers.begin(); it != lightRuleTimers.end(); )
   {
      LightRuleTimer& timer {it->second};

      // a target switched off by hand (seen 'on' before, the state follows the switching delayed) is dropped

      for (auto t = timer.switchedOn.begin(); t != timer.switchedOn.end(); )
      {
         if (lightTargetState(t->first))
            t->second = true;
         else if (t->second)
         {
            tell(eloDetail, "Light rule '%s': '%s' switched off meanwhile", it->first.c_str(), t->first.c_str());
            t = timer.switchedOn.erase(t);
            continue;
         }

         t++;
      }

      if (timer.switchedOn.empty())
      {
         it = lightRuleTimers.erase(it);      // nothing to switch off
         continue;
      }

      if (now >= timer.offAt)
      {
         tell(eloAlways, "Light rule '%s': off timer elapsed", it->first.c_str());

         for (const auto& t : timer.switchedOn)
            lightTargetSwitch(t.first, false, na, timer.transition);

         it = lightRuleTimers.erase(it);
         continue;
      }

      it++;
   }

   return done;
}
