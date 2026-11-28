/* MIT License - Copyright (c) 2019-2024 Francis Van Roie
   For full license information read the LICENSE file in the project folder */

// homectld additions: board buzzer (custom/beep), a local countdown timer (custom/timer) and a
// setup screen for WLAN, hostname and MQTT server (custom/setup, at the first start instead of the OOBE),
// see my_custom.h for the commands

#include "hasplib.h"

#if defined(HASP_USE_CUSTOM) && HASP_USE_CUSTOM > 0

#include "hasp_debug.h"
#include "dev/device.h"
#include "hasp_config.h"
#include "sys/net/hasp_wifi.h"
#include "mqtt/hasp_mqtt.h"
#include "hasp/hasp_font.h"
#include <WiFi.h>

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

// ---------------------------------------------------------------------------
// Setup screen: WLAN (scanned networks + password), hostname and MQTT server with an on-screen
// keyboard. Own LVGL screen, independent of the pages: shown instead of the openHASP OOBE
// screen when no WLAN is configured (custom_setup_oobe(), haspSetup() does not run then) and on
// demand with 'custom/setup' (e.g. as 'action' of a button), 'Abbrechen' returns to the page.
// 'Speichern' validates the WLAN (blocking up to ~8 s), stores everything and reboots.
// ---------------------------------------------------------------------------

struct {
    lv_obj_t* screen = NULL;
    lv_obj_t* ssid   = NULL; // dropdown
    lv_obj_t* pass   = NULL;
    lv_obj_t* host   = NULL; // hostname
    lv_obj_t* mqtt   = NULL; // MQTT server
    lv_obj_t* status = NULL;
    lv_obj_t* kb     = NULL;
    bool oobe         = false; // no pages behind the screen
    uint8_t scanTries = 0;     // > 0 while a scan is wanted (retries while the station is connecting)
    bool scanRunning  = false;
    uint32_t scanStart = 0;
    char ssidConfigured[33] = ""; // preselected in the scan result
    uint32_t saveAt   = 0;     // millis when the (blocking) save starts, 0 = none
} setup;

void setup_status(const char* text, lv_color_t color)
{
    if(!setup.status) return;
    lv_label_set_text(setup.status, text);
    lv_obj_set_style_local_text_color(setup.status, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, color);
}

void setup_close()
{
    if(!setup.screen || setup.oobe) return;
    if(lv_obj_t* page = haspPages.get_obj(haspPages.get())) lv_scr_load(page);
    lv_obj_del_async(setup.screen);
    setup.screen = setup.ssid = setup.pass = setup.host = setup.mqtt = setup.status = setup.kb = NULL;
    setup.scanTries = 0;
    setup.saveAt    = 0;
}

void setup_scan()
{
    if(!setup.screen) return;
    setup.scanTries = 10;
    setup_status("Suche WLAN-Netze ...", LV_COLOR_SILVER);
}

void setup_scan_loop()
{
    if(!setup.screen) return;

    if(setup.scanRunning) {
        // connected to a network the scan takes longer than the timeout of scanComplete() (20 x 300 ms),
        // it reports 'failed' then, the result comes later anyway -> own timeout
        int16_t n = WiFi.scanComplete();
        if(n == WIFI_SCAN_RUNNING || (n < 0 && millis() - setup.scanStart < 20000)) return;
        setup.scanRunning = false;
        if(n < 0) return; // failed, again with the next tick

        setup.scanTries = 0;
        String options((char*)0);
        int count = 0;
        for(int i = 0; i < n; i++) {
            String ssid = WiFi.SSID(i);
            if(ssid.isEmpty() || ssid.indexOf('\n') >= 0) continue;
            bool dup = false;
            for(int j = 0; j < i && !dup; j++) dup = WiFi.SSID(j) == ssid;
            if(dup) continue;
            if(count++) options += '\n';
            options += ssid;
        }
        WiFi.scanDelete();

        lv_dropdown_set_options(setup.ssid, count ? options.c_str() : "");
        int32_t sel = *setup.ssidConfigured ? lv_dropdown_get_option_cnt(setup.ssid) : 0;
        char opt[33];
        while(sel-- > 0) {
            lv_dropdown_set_selected(setup.ssid, sel);
            lv_dropdown_get_selected_str(setup.ssid, opt, sizeof(opt));
            if(!strcmp(opt, setup.ssidConfigured)) break;
        }
        if(sel < 0) lv_dropdown_set_selected(setup.ssid, 0);
        char buf[48];
        snprintf_P(buf, sizeof(buf), count > 1 ? PSTR("%d Netze gefunden") : count ? PSTR("1 Netz gefunden") : PSTR("kein Netz gefunden"), count);
        setup_status(buf, LV_COLOR_SILVER);
        return;
    }

    if(!setup.scanTries) return;
    setup.scanTries--;
    int16_t r = WiFi.scanNetworks(true);
    if(r == WIFI_SCAN_RUNNING || r >= 0) {
        setup.scanRunning = true;
        setup.scanStart   = millis();
    }
    else if(!setup.scanTries)
        setup_status("Suche fehlgeschlagen", LV_COLOR_RED);
}

void setup_save()
{
    char ssid[33] = "";
    lv_dropdown_get_selected_str(setup.ssid, ssid, sizeof(ssid));
    if(!*ssid) {
        setup_status("erst ein WLAN auswaehlen", LV_COLOR_RED);
        return;
    }
    if(!*lv_textarea_get_text(setup.host)) {
        setup_status("Hostname fehlt", LV_COLOR_RED);
        return;
    }

    char buf[96];
    snprintf_P(buf, sizeof(buf), PSTR("Verbinde mit %s ..."), ssid);
    setup_status(strcmp(lv_textarea_get_text(setup.pass), D_PASSWORD_MASK) ? buf : "Speichere ...", LV_COLOR_YELLOW);
    setup.saveAt = millis() + 300; // let the GUI show the status first, the connect blocks
}

void setup_save_loop()
{
    if(!setup.screen || !setup.saveAt || (int32_t)(millis() - setup.saveAt) < 0) return;
    setup.saveAt = 0;

    char ssid[33] = "";
    char pass[65] = "";
    lv_dropdown_get_selected_str(setup.ssid, ssid, sizeof(ssid));
    strncpy(pass, lv_textarea_get_text(setup.pass), sizeof(pass) - 1);

    // the configured password is not readable (wifiGetConfig() gives the mask): same network with
    // the mask unchanged -> keep the WLAN settings, only hostname / MQTT change
    bool keepWifi = !strcmp(pass, D_PASSWORD_MASK);

    if(keepWifi && strcmp(ssid, setup.ssidConfigured)) {
        setup_status("Passwort fuer das neue WLAN eingeben", LV_COLOR_RED);
        return;
    }

    WiFi.scanDelete();
    if(!keepWifi && !wifiValidateSsid(ssid, pass)) {
        LOG_WARNING(TAG_CUSTOM, F("Setup: connect to %s failed"), ssid);
        setup_status("WLAN-Verbindung fehlgeschlagen - Passwort pruefen", LV_COLOR_RED);
        return; // wifiEvery5Seconds() reconnects to the configured network
    }

    if(!keepWifi) {
        StaticJsonDocument<256> settings;
        settings[FPSTR(FP_CONFIG_SSID)] = ssid;
        settings[FPSTR(FP_CONFIG_PASS)] = pass;
        wifiSetConfig(settings.as<JsonObject>());
    }
#if HASP_USE_MQTT > 0
    {
        StaticJsonDocument<256> settings;
        settings[FPSTR(FP_CONFIG_NAME)] = lv_textarea_get_text(setup.host);
        settings[FPSTR(FP_CONFIG_HOST)] = lv_textarea_get_text(setup.mqtt);
        mqttSetConfig(settings.as<JsonObject>());
    }
#endif
    LOG_INFO(TAG_CUSTOM, F("Setup: WLAN %s, hostname %s, MQTT %s saved, rebooting"), ssid,
             lv_textarea_get_text(setup.host), lv_textarea_get_text(setup.mqtt));
    setup_status("Gespeichert - Neustart ...", LV_COLOR_LIME);
    lv_refr_now(NULL);
    dispatch_reboot(true);
}

void setup_ta_cb(lv_obj_t* ta, lv_event_t event)
{
    if(event == LV_EVENT_CLICKED && setup.kb) lv_keyboard_set_textarea(setup.kb, ta);
}

void setup_kb_cb(lv_obj_t* kb, lv_event_t event)
{
    if(event == LV_EVENT_APPLY) {
        setup_save();
        return;
    }
    if(event == LV_EVENT_CANCEL) return; // keep the keyboard
    lv_keyboard_def_event_cb(kb, event);
}

void setup_btn_cb(lv_obj_t* btn, lv_event_t event)
{
    if(event != LV_EVENT_CLICKED) return;
    switch(lv_obj_get_user_data(btn).id) {
        case 1: setup_scan(); break;
        case 2: setup_save(); break;
        case 3: setup_close(); break;
    }
}

lv_obj_t* setup_label(lv_obj_t* parent, const char* text, lv_coord_t x, lv_coord_t y)
{
    lv_obj_t* label = lv_label_create(parent, NULL);
    lv_label_set_text(label, text);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_style_local_text_color(label, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_SILVER);
    return label;
}

lv_obj_t* setup_ta(lv_obj_t* parent, const char* text, lv_coord_t x, lv_coord_t y, lv_coord_t w, bool pwd)
{
    lv_obj_t* ta = lv_textarea_create(parent, NULL);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_text(ta, text);
    lv_textarea_set_pwd_mode(ta, pwd);
    lv_textarea_set_cursor_hidden(ta, true);
    lv_obj_set_pos(ta, x, y);
    lv_obj_set_width(ta, w);
    lv_obj_set_event_cb(ta, setup_ta_cb);
    return ta;
}

lv_obj_t* setup_btn(lv_obj_t* parent, const char* text, uint8_t id, lv_coord_t x, lv_coord_t y, lv_coord_t w)
{
    lv_obj_t* btn = lv_btn_create(parent, NULL);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, w, 40);
    lv_obj_user_data_t udata = {0, 0, 0};
    udata.id = id;
    lv_obj_set_user_data(btn, udata);
    lv_obj_set_event_cb(btn, setup_btn_cb);
    lv_obj_t* label = lv_label_create(btn, NULL);
    lv_label_set_text(label, text);
    return btn;
}

void setup_show(bool oobe)
{
    if(setup.screen) return;

    lv_font_t* font = get_font("24");
    if(!font) font = (lv_font_t*)LV_FONT_DEFAULT;

    setup.oobe   = oobe;
    setup.screen = lv_obj_create(NULL, NULL);
    lv_obj_t* scr = setup.screen;
    lv_coord_t w  = lv_obj_get_width(scr);
    lv_coord_t h  = lv_obj_get_height(scr);
    lv_obj_set_style_local_bg_color(scr, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);
    lv_obj_set_style_local_bg_grad_dir(scr, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_GRAD_DIR_NONE);
    lv_obj_set_style_local_text_font(scr, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, font);
    lv_obj_set_style_local_text_color(scr, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);

    // current values
    char hostname[64] = "";
    char mqttHost[64] = "";
    char wifiPass[65] = "";
    {
        DynamicJsonDocument settings(512);
        wifiGetConfig(settings.to<JsonObject>());
        strncpy(setup.ssidConfigured, settings[FPSTR(FP_CONFIG_SSID)] | "", sizeof(setup.ssidConfigured) - 1);
        if(*setup.ssidConfigured) // the mask, the password itself is not readable
            strncpy(wifiPass, settings[FPSTR(FP_CONFIG_PASS)] | "", sizeof(wifiPass) - 1);
    }
    strncpy(hostname, haspDevice.get_hostname(), sizeof(hostname) - 1);
#if HASP_USE_MQTT > 0
    {
        DynamicJsonDocument settings(1024);
        mqttGetConfig(settings.to<JsonObject>());
        strncpy(mqttHost, settings[FPSTR(FP_CONFIG_HOST)] | "", sizeof(mqttHost) - 1);
    }
#endif

    lv_obj_t* title = lv_label_create(scr, NULL);
    lv_label_set_text(title, "Einrichtung");
    lv_obj_set_pos(title, 10, 14);

    setup.status = lv_label_create(scr, NULL);
    lv_label_set_long_mode(setup.status, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(setup.status, 160, 14);
    lv_obj_set_width(setup.status, w - 160 - (oobe ? 280 : 420));
    lv_label_set_text(setup.status, "");

    lv_coord_t bx = w - 10;
    if(!oobe) setup_btn(scr, "Abbrechen", 3, bx -= 140, 6, 135);
    setup_btn(scr, "Speichern", 2, bx -= 140, 6, 135);
    setup_btn(scr, "Suchen", 1, bx -= 130, 6, 125);

    const lv_coord_t c1 = 130, c2 = w / 2 + 130, fw = w / 2 - 140;
    setup_label(scr, "WLAN", 10, 66);
    setup.ssid = lv_dropdown_create(scr, NULL);
    lv_dropdown_set_options(setup.ssid, "");
    lv_obj_set_pos(setup.ssid, c1, 58);
    lv_obj_set_width(setup.ssid, fw);
    lv_obj_set_top(setup.ssid, true);
    setup_label(scr, "Passwort", w / 2 + 10, 66);
    setup.pass = setup_ta(scr, wifiPass, c2, 58, fw, true);

    setup_label(scr, "Hostname", 10, 120);
    setup.host = setup_ta(scr, hostname, c1, 112, fw, false);
    setup_label(scr, "MQTT", w / 2 + 10, 120);
    setup.mqtt = setup_ta(scr, mqttHost, c2, 112, fw, false);

    lv_coord_t kbTop = 166;
    if(oobe) {
        char ap[16] = ""; // the access point opened by wifiShowAP() in oobeSetup()
        byte mac[6];
        WiFi.macAddress(mac);
        snprintf_P(ap, sizeof(ap), PSTR("HASP-%02x%02x%02x"), mac[3], mac[4], mac[5]);
        char buf[128];
        snprintf_P(buf, sizeof(buf), PSTR("oder im Browser: WLAN %s, Passwort haspadmin, http://192.168.4.1"), ap);
        lv_obj_t* hint = setup_label(scr, buf, 10, kbTop);
        lv_obj_set_style_local_text_font(hint, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, get_font("16") ?: font);
        kbTop += 26;
    }

    setup.kb = lv_keyboard_create(scr, NULL);
    lv_obj_set_size(setup.kb, w, h - kbTop);
    lv_obj_align(setup.kb, NULL, LV_ALIGN_IN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_local_radius(setup.kb, LV_KEYBOARD_PART_BG, LV_STATE_DEFAULT, 0);
    lv_obj_set_event_cb(setup.kb, setup_kb_cb);
    lv_keyboard_set_textarea(setup.kb, setup.pass);
    lv_keyboard_set_cursor_manage(setup.kb, true);

    lv_scr_load(scr);
    setup_scan();
}

} // namespace

void custom_setup_oobe()
{
    setup_show(true);
}

// ---------------------------------------------------------------------------
// Hooks
// ---------------------------------------------------------------------------

void custom_setup()
{}

void custom_loop()
{
    beep_loop();
    setup_save_loop();
}

void custom_every_second()
{
    timer_tick();
    setup_scan_loop();
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
    } else if(!strcasecmp_P(topic, PSTR("setup"))) {
        if(!strcasecmp_P(payload, PSTR("scan")))
            setup_scan();
        else
            setup_show(false);
    } else {
        LOG_WARNING(TAG_CUSTOM, F("Unknown custom topic '%s'"), topic);
    }
}

void custom_state_subtopic(const char* subtopic, const char* payload)
{}

#endif // HASP_USE_CUSTOM
