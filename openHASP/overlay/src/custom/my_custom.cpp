/* MIT License - Copyright (c) 2019-2024 Francis Van Roie
   For full license information read the LICENSE file in the project folder */

// homectld additions: board buzzer (custom/beep) and a local countdown timer (custom/timer),
// see my_custom.h for the commands

#include "hasplib.h"

#if defined(HASP_USE_CUSTOM) && HASP_USE_CUSTOM > 0

#include "hasp_debug.h"
#include "dev/device.h"

namespace {

// ---------------------------------------------------------------------------
// Buzzer: non blocking on/off pattern driven from custom_loop()
// ---------------------------------------------------------------------------

struct {
    uint16_t onMs  = 0;
    uint16_t offMs = 0;
    uint8_t left   = 0; // beeps still to do (incl. the running one)
    bool on        = false;
    uint32_t next  = 0; // millis of the next switch
} beep;

void beep_stop()
{
    beep.left = 0;
    if(beep.on) {
        haspDevice.set_buzzer(false);
        beep.on = false;
    }
}

void beep_start(uint16_t onMs, uint16_t offMs, uint8_t count)
{
    beep_stop();
    if(!count || !onMs) return;
    beep.onMs  = onMs;
    beep.offMs = offMs;
    beep.left  = count;
    beep.on    = true;
    beep.next  = millis() + onMs;
    haspDevice.set_buzzer(true);
}

void beep_loop()
{
    if(!beep.on && !beep.left) return;
    if((int32_t)(millis() - beep.next) < 0) return;

    if(beep.on) {
        haspDevice.set_buzzer(false);
        beep.on = false;
        beep.left--;
        beep.next = millis() + beep.offMs;
    } else if(beep.left) {
        haspDevice.set_buzzer(true);
        beep.on   = true;
        beep.next = millis() + beep.onMs;
    }
}

// ---------------------------------------------------------------------------
// Countdown timer
// ---------------------------------------------------------------------------

const int32_t timerMinSec   = 1;    // 'add' steps down as far as one step above zero
const int32_t timerMaxSec   = 3600;
const int32_t timerAlarmSec = 60; // the alarm beeps this long unless acknowledged

struct {
    uint8_t page    = 0; // objects to update (0 = none)
    uint8_t labelId = 0; // mm:ss
    uint8_t barId   = 0; // progress bar (0..total)
    uint8_t btnId   = 0; // start/stop button, the text follows the state
    int32_t defaultSec = 600;
    int32_t total      = 600; // the set time
    int32_t remaining  = 600;
    bool running       = false;
    int32_t alarm      = 0; // seconds of alarm left
    bool pendingShow   = false; // objects not there yet (config arrived before the page), retry every second
} timer;

void timer_attr(uint8_t objid, const char* attr, const char* value)
{
    if(!timer.page || !objid) return;

    if(lv_obj_t* obj = hasp_find_obj_from_page_id(timer.page, objid))
        hasp_process_obj_attribute(obj, attr, value, true);
    else
        timer.pendingShow = true;
}

void timer_publish(const char* state)
{
    char buf[96];
    snprintf_P(buf, sizeof(buf), PSTR("{\"state\":\"%s\",\"remaining\":%d,\"total\":%d}"), state, timer.remaining,
               timer.total);
    dispatch_state_subtopic("timer", buf);
}

void timer_show(bool full)
{
    char buf[16];

    timer.pendingShow = false;

    snprintf_P(buf, sizeof(buf), PSTR("%02d:%02d"), timer.remaining / 60, timer.remaining % 60);
    timer_attr(timer.labelId, "text", buf);
    snprintf_P(buf, sizeof(buf), PSTR("%d"), timer.total - timer.remaining);
    timer_attr(timer.barId, "val", buf);

    if(full) {
        timer_attr(timer.labelId, "text_color", timer.alarm ? "#ff5757" : timer.running ? "#ffffff" : "#a3a3a3");
        snprintf_P(buf, sizeof(buf), PSTR("%d"), timer.total);
        timer_attr(timer.barId, "max", buf);
        timer_attr(timer.barId, "bg_color10", timer.alarm ? "#ff5757" : "#2177AD");
        timer_attr(timer.barId, "bg_grad_color10", timer.alarm ? "#ff5757" : "#2177AD");
        timer_attr(timer.btnId, "text", timer.alarm ? "Aus" : timer.running ? "Stop" : "Start");
    }
}

void timer_ack()
{
    bool was = timer.alarm > 0;
    timer.alarm = 0;
    beep_stop();
    if(was) {
        timer.remaining = timer.total; // ready for the next run
        timer_show(true);
        timer_publish("stopped");
    }
}

void timer_set(int32_t sec)
{
    timer_ack();
    timer.running   = false;
    timer.total     = constrain(sec, timerMinSec, timerMaxSec);
    timer.remaining = timer.total;
    timer_show(true);
    timer_publish("stopped");
}

void timer_add(int32_t sec)
{
    if(timer.alarm) {
        timer_ack();
        return;
    }

    if(timer.remaining + sec <= 0) return; // one step above zero is the end

    if(timer.running) {
        timer.remaining = min(timer.remaining + sec, timerMaxSec);
        timer.total     = min(max(timer.total + sec, timer.remaining), timerMaxSec);
    } else {
        timer.remaining = min(timer.remaining + sec, timerMaxSec);
        timer.total     = timer.remaining;
    }
    timer_show(true);
    timer_publish(timer.running ? "running" : "stopped");
}

void timer_start(bool run)
{
    if(timer.alarm) {
        timer_ack();
        return;
    }

    if(run && timer.remaining <= 0) timer.remaining = timer.total;
    timer.running = run;
    timer_show(true);
    timer_publish(run ? "running" : "stopped");
}

void timer_tick()
{
    if(timer.pendingShow && hasp_find_obj_from_page_id(timer.page, timer.labelId)) timer_show(true);

    if(timer.running) {
        if(--timer.remaining <= 0) {
            timer.remaining = 0;
            timer.running   = false;
            timer.alarm     = timerAlarmSec;
            dispatch_text_line("idle off", TAG_CUSTOM); // wake the display
            timer_show(true);
            timer_publish("alarm");
            LOG_INFO(TAG_CUSTOM, F("Timer: done after %d s"), timer.total);
        } else {
            timer_show(false);
        }
    }

    if(timer.alarm > 0) {
        if(timer.alarm % 2 == 0) beep_start(150, 150, 3);
        if(--timer.alarm <= 0) timer_ack(); // nobody came
    }
}

void timer_command(const char* payload)
{
    char cmd[12] = "";
    int a = 0, b = 0, c = 0, d = 0, e = 0;
    int n = sscanf(payload, "%11s %d %d %d %d %d", cmd, &a, &b, &c, &d, &e);

    if(n < 1) return;

    if(!strcasecmp_P(cmd, PSTR("config")) && n >= 5) {
        timer.page    = a;
        timer.labelId = b;
        timer.barId   = c;
        timer.btnId   = d;
        if(n >= 6 && e > 0) timer.defaultSec = constrain(e, timerMinSec, timerMaxSec);
        if(!timer.running && !timer.alarm) timer.total = timer.remaining = timer.defaultSec;
        timer_show(true);
    } else if(!strcasecmp_P(cmd, PSTR("set")) && n >= 2) {
        timer_set(a);
    } else if(!strcasecmp_P(cmd, PSTR("add")) && n >= 2) {
        timer_add(a);
    } else if(!strcasecmp_P(cmd, PSTR("start"))) {
        timer_start(true);
    } else if(!strcasecmp_P(cmd, PSTR("stop"))) {
        timer_start(false);
    } else if(!strcasecmp_P(cmd, PSTR("toggle"))) {
        timer_start(!timer.running);
    } else if(!strcasecmp_P(cmd, PSTR("reset"))) {
        timer_set(timer.defaultSec);
    } else if(!strcasecmp_P(cmd, PSTR("ack"))) {
        timer_ack();
    } else if(!strcasecmp_P(cmd, PSTR("state"))) {
        timer_publish(timer.alarm ? "alarm" : timer.running ? "running" : "stopped");
    } else {
        LOG_WARNING(TAG_CUSTOM, F("Timer: unknown command '%s'"), payload);
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Hooks
// ---------------------------------------------------------------------------

void custom_setup()
{}

void custom_loop()
{
    beep_loop();
}

void custom_every_second()
{
    timer_tick();
}

void custom_every_5seconds()
{}

bool custom_pin_in_use(uint8_t pin)
{
    return false;
}

void custom_get_sensors(JsonDocument& doc)
{}

void custom_topic_payload(const char* topic, const char* payload, uint8_t source)
{
    if(!strcasecmp_P(topic, PSTR("beep"))) {
        int onMs = 200, offMs = 150, count = 1;
        sscanf(payload, "%d %d %d", &onMs, &offMs, &count);
        if(onMs <= 0) {
            beep_stop();
        } else {
            beep_start(constrain(onMs, 1, 5000), constrain(offMs, 0, 5000), constrain(count, 1, 50));
        }
    } else if(!strcasecmp_P(topic, PSTR("timer"))) {
        timer_command(payload);
    } else {
        LOG_WARNING(TAG_CUSTOM, F("Unknown custom topic '%s'"), topic);
    }
}

void custom_state_subtopic(const char* subtopic, const char* payload)
{}

#endif // HASP_USE_CUSTOM
