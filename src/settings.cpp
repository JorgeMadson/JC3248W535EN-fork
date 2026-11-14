#include "app_settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string>

#include "audio_player.h"
#include "display.h"
#include "sd_text.h"
#include "serial_log.h"
#include "weather.h"

/*
 * /settings.ini on SD:
 *   [settings]
 *   brightness=80
 *   units=auto        ; auto | celsius | fahrenheit
 *   clock=24h         ; 24h | 12h
 *   ambient=3         ; idle minutes on Home before the ambient screen: 0 (off), 1, 3, 5, 10
 *   volume=50         ; speaker volume %, 0..100 (Music app)
 *   shuffle=on        ; Music app plays in random order: on | off
 *   home_ordem=placar,freedoom,...   ; tela inicial: ids dos apps na ordem (home_apps.h)
 *   home_ocultos=relogio,air         ; tela inicial: apps escondidos
 * Timezone is kept separately in /timezone.ini (timezone_ini.cpp).
 */
static const char *k_path = "/settings.ini";
static const int k_min_brightness = 10; /* never let the slider black out the screen */

static AppSettings s_settings = {100, TempUnits::Auto, true, 3, 50, true};
/* Tela inicial (ver home_apps.h); vazio e sem "tem" = nada salvo. */
static std::string s_home_order, s_home_hidden;
static bool s_has_home_order = false, s_has_home_hidden = false;

static const char *units_name(TempUnits u) {
    return u == TempUnits::Celsius ? "celsius" : u == TempUnits::Fahrenheit ? "fahrenheit" : "auto";
}

/** Nearest allowed ambient timeout (0 = off). */
static int snap_ambient(int m) {
    return m <= 0 ? 0 : m <= 1 ? 1 : m <= 3 ? 3 : m <= 5 ? 5 : 10;
}

static int clamp_brightness(int v) {
    return v < k_min_brightness ? k_min_brightness : v > 100 ? 100 : v;
}

static int clamp_volume(int v) {
    return v < 0 ? 0 : v > 100 ? 100 : v;
}

static void save(void) {
    char base[200];
    snprintf(base, sizeof(base), "[settings]\nbrightness=%d\nunits=%s\nclock=%s\nambient=%d\nvolume=%d\nshuffle=%s\n",
             s_settings.brightness, units_name(s_settings.units), s_settings.clock_24h ? "24h" : "12h",
             s_settings.ambient_min, s_settings.volume, s_settings.shuffle ? "on" : "off");
    std::string text = base;
    if (s_has_home_order) {
        text += "home_ordem=" + s_home_order + "\n";
    }
    if (s_has_home_hidden) {
        text += "home_ocultos=" + s_home_hidden + "\n";
    }
    if (!sd_write_text(k_path, text.c_str())) {
        serial_log("[settings] cannot write %s", k_path);
        return;
    }
    serial_log("[settings] saved %s", k_path);
}

void settings_load(void) {
    std::string content;
    if (!sd_read_text(k_path, &content)) {
        serial_log("[settings] %s not found, using defaults", k_path);
        return;
    }
    ini_parse(content, [](const std::string &, const std::string &key, const std::string &val) {
        if (key == "brightness") {
            s_settings.brightness = clamp_brightness(atoi(val.c_str()));
        } else if (key == "units") {
            s_settings.units = str_ieq(val, "celsius")      ? TempUnits::Celsius
                               : str_ieq(val, "fahrenheit") ? TempUnits::Fahrenheit
                                                            : TempUnits::Auto;
        } else if (key == "clock") {
            s_settings.clock_24h = !str_ieq(val, "12h");
        } else if (key == "ambient") {
            s_settings.ambient_min = snap_ambient(atoi(val.c_str()));
        } else if (key == "volume") {
            s_settings.volume = clamp_volume(atoi(val.c_str()));
        } else if (key == "shuffle") {
            s_settings.shuffle = !str_ieq(val, "off");
        } else if (key == "home_ordem") {
            s_home_order = val;
            s_has_home_order = true;
        } else if (key == "home_ocultos") {
            s_home_hidden = val;
            s_has_home_hidden = true;
        }
    });
    serial_log("[settings] brightness=%d units=%s clock=%s ambient=%d min volume=%d shuffle=%s", s_settings.brightness,
               units_name(s_settings.units), s_settings.clock_24h ? "24h" : "12h", s_settings.ambient_min,
               s_settings.volume, s_settings.shuffle ? "on" : "off");
}

const AppSettings &settings_get(void) {
    return s_settings;
}

void settings_set_brightness(int percent, bool save_now) {
    s_settings.brightness = clamp_brightness(percent);
    (void)bsp_display_brightness_set(s_settings.brightness);
    if (save_now) {
        save();
    }
}

void settings_set_volume(int percent, bool save_now) {
    s_settings.volume = clamp_volume(percent);
    audio_set_volume(s_settings.volume);
    if (save_now) {
        save();
    }
}

void settings_set_shuffle(bool on) {
    s_settings.shuffle = on;
    audio_set_shuffle(on);
    save();
}

void settings_set_units(TempUnits units) {
    if (units == s_settings.units) {
        return;
    }
    s_settings.units = units;
    save();
    weather_refresh();
}

void settings_set_clock_24h(bool on) {
    if (on == s_settings.clock_24h) {
        return;
    }
    s_settings.clock_24h = on;
    save();
}

void settings_set_ambient_min(int minutes) {
    minutes = snap_ambient(minutes);
    if (minutes == s_settings.ambient_min) {
        return;
    }
    s_settings.ambient_min = minutes;
    save();
}

const char *settings_home_order(void) {
    return s_has_home_order ? s_home_order.c_str() : nullptr;
}

const char *settings_home_hidden(void) {
    return s_has_home_hidden ? s_home_hidden.c_str() : nullptr;
}

void settings_set_home(const char *order, const char *hidden) {
    s_home_order = order ? order : "";
    s_home_hidden = hidden ? hidden : "";
    s_has_home_order = s_has_home_hidden = true;
    save();
}

void settings_format_hm(char *buf, size_t n, const struct tm *ti, bool with_ampm) {
    if (s_settings.clock_24h) {
        snprintf(buf, n, "%02d:%02d", ti->tm_hour, ti->tm_min);
        return;
    }
    const int h12 = ti->tm_hour % 12 == 0 ? 12 : ti->tm_hour % 12;
    if (with_ampm) {
        snprintf(buf, n, "%d:%02d %s", h12, ti->tm_min, ti->tm_hour < 12 ? "AM" : "PM");
    } else {
        snprintf(buf, n, "%d:%02d", h12, ti->tm_min);
    }
}
