//***************************************************************************
// Test of the decision logic of the dynamic pellet filling (p4fill.h)
//   g++ -std=c++23 -I.. -x c++ p4fill-test.c -o /tmp/p4fill-test && /tmp/p4fill-test
//***************************************************************************

#include <stdio.h>
#include "p4fill.h"

using namespace P4Fill;

static int failed {0};

static void check(bool ok, const char* what)
{
   printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
   failed += !ok;
}

static Input input(time_t now, const char* day, int minute, const char* state, double level, int t1, int t2)
{
   Input in;
   in.now = now; in.today = day; in.minute = minute; in.stateInfo = state; in.level = level;
   in.times[0] = t1; in.times[1] = t2;
   return in;
}

int main()
{
   Config cfg;
   cfg.enabled = true;
   time_t t0 {1800000000};

   // daily standard times

   {
      State st; st.lastZero = t0;
      Result r {decide(cfg, input(t0, "2026-11-16", 600, "Heizen", 50, 0, 0), st)};
      check(r.actions.size() == 1 && r.actions[0].slot == 0 && r.actions[0].minutes == 960, "standard time 16:00 written once a day");
      check(st.resetDay == "2026-11-16" && r.persist, "reset day persisted");
      r = decide(cfg, input(t0 + 60, "2026-11-16", 601, "Heizen", 50, 960, 0), st);
      check(r.actions.empty(), "same day: nothing again");
   }

   // unknown times -> nothing

   {
      State st;
      Result r {decide(cfg, input(t0, "2026-11-16", 600, "Heizen", 50, -1, 0), st)};
      check(r.actions.empty() && !r.persist, "times unknown: no decision");
   }

   // fill now after the hold time

   {
      State st; st.lastZero = t0; st.resetDay = "2026-11-16";
      Result r {decide(cfg, input(t0, "2026-11-16", 600, "Betriebsbereit", 15, 960, 0), st)};
      check(r.actions.empty() && st.lowSince == t0, "low level: waiting for the hold time");
      r = decide(cfg, input(t0 + 240, "2026-11-16", 604, "Betriebsbereit", 15, 960, 0), st);
      check(r.actions.empty(), "low level: 4 minutes are not enough");
      r = decide(cfg, input(t0 + 300, "2026-11-16", 605, "Betriebsbereit", 15, 960, 0), st);
      check(r.actions.size() == 1 && r.actions[0].slot == 0 && r.actions[0].minutes == 610, "fill now: 1. time -> 10:10");
      r = decide(cfg, input(t0 + 900, "2026-11-16", 615, "Betriebsbereit", 15, 610, 0), st);
      check(r.actions.empty(), "no second 'fill now' within 30 minutes");
   }

   // fill now in the afternoon -> 2. time

   {
      State st; st.lastZero = t0; st.resetDay = "2026-11-16"; st.lowSince = t0 - 600;
      Result r {decide(cfg, input(t0, "2026-11-16", 900, "Betriebsbereit", 10, 960, 0), st)};
      check(r.actions.size() == 1 && r.actions[0].slot == 1 && r.actions[0].minutes == 905, "fill now 15:05 -> 2. time");
   }

   // the condition breaks -> hold time again

   {
      State st; st.lastZero = t0; st.resetDay = "2026-11-16";
      decide(cfg, input(t0, "2026-11-16", 600, "Betriebsbereit", 15, 960, 0), st);
      decide(cfg, input(t0 + 120, "2026-11-16", 602, "Vorbereitung", 15, 960, 0), st);
      check(st.lowSince == 0, "state changed: hold time restarts");
   }

   // postpone while heating

   {
      State st; st.lastZero = t0; st.resetDay = "2026-11-16";
      Result r {decide(cfg, input(t0, "2026-11-16", 950, "Heizen", 50, 960, 0), st)};
      check(r.actions.size() == 1 && r.actions[0].minutes == 980, "heating, filling in 10 min: postponed to 16:20");
      r = decide(cfg, input(t0, "2026-11-16", 940, "Heizen", 50, 960, 0), st);
      check(r.actions.empty(), "heating, filling in 20 min: not yet");
      r = decide(cfg, input(t0, "2026-11-16", 950, "Heizen", 85, 960, 0), st);
      check(r.actions.empty(), "level above 80%: not postponed");
      r = decide(cfg, input(t0, "2026-11-16", 950, "Betriebsbereit", 50, 960, 0), st);
      check(r.actions.empty(), "not heating: not postponed");
      r = decide(cfg, input(t0, "2026-11-16", 1435, "Zünden", 50, 5, 0), st);
      check(r.actions.size() == 1 && r.actions[0].minutes == 25, "over midnight: 23:55, filling 00:05 -> 00:25");
      r = decide(cfg, input(t0, "2026-11-16", 1430, "Heizen", 50, 0, 0), st);
      check(r.actions.empty(), "00:00 (off) is never postponed");
   }

   // run empty

   {
      State st; st.lastZero = t0 - 8 * 86400; st.resetDay = "2026-11-16";
      Result r {decide(cfg, input(t0, "2026-11-16", 600, "Betriebsbereit", 10, 960, 0), st)};
      check(st.toZero && r.actions.size() == 1 && r.actions[0].slot == 0 && r.actions[0].minutes == 0, "8 days: run empty, 1. time -> 00:00");
      r = decide(cfg, input(t0 + 86400, "2026-11-17", 600, "Betriebsbereit", 5, 0, 0), st);
      check(r.actions.empty(), "run empty: no standard times, no 'fill now'");
      r = decide(cfg, input(t0 + 86400 + 60, "2026-11-17", 601, "Betriebsbereit", 0.3, 0, 0), st);
      check(!st.toZero && st.lastZero == t0 + 86400 + 60, "empty: run empty ends, last zero set");
      check(r.actions.size() == 1 && r.actions[0].minutes == 960, "after running empty: the standard times at once");
   }

   // running empty without reaching 0 -> given up after 3 more days

   {
      State st; st.lastZero = t0 - 11 * 86400; st.toZero = true; st.resetDay = "2026-11-16";
      Result r {decide(cfg, input(t0, "2026-11-16", 600, "Heizen", 3, 0, 0), st)};
      check(!st.toZero && st.lastZero == t0 && r.actions.size() == 1 && r.actions[0].minutes == 960, "run empty given up after 8+3 days");
      st.lastZero = t0 - 10 * 86400; st.toZero = true;
      r = decide(cfg, input(t0, "2026-11-16", 600, "Heizen", 3, 0, 0), st);
      check(st.toZero && r.actions.empty(), "run empty still on after 10 days");
   }

   // empty in normal operation, days 0 = never

   {
      State st; st.lastZero = t0 - 30 * 86400; st.resetDay = "2026-11-16";
      Config c {cfg}; c.daysToZero = 0;
      Result r {decide(c, input(t0, "2026-11-16", 600, "Heizen", 50, 960, 0), st)};
      check(!st.toZero && r.actions.empty(), "days 0: never run empty");
   }

   // time strings

   check(str2Minutes("16:00") == 960 && str2Minutes("00:00") == 0 && str2Minutes("9:05") == 545, "str2Minutes");
   check(str2Minutes("960") == -1 && str2Minutes("16:00x") == -1 && str2Minutes("25:00") == -1, "str2Minutes wrong format");
   check(minutes2Str(545) == "09:05", "minutes2Str");

   printf("%s\n", failed ? "FAILED" : "all passed");

   return failed ? 1 : 0;
}
