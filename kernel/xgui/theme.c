/*
 * MiniOS XGUI Theme System
 *
 * Runtime-configurable color themes for the desktop environment.
 */

#include "xgui/theme.h"
#include "xgui/display.h"
#include "xgui/desktop.h"
#include "conf.h"
#include "rtc.h"
#include "serial.h"
#include "string.h"

/* GUI config section */
static conf_section_t gui_conf;
static bool conf_ready = false;

/* Current active theme */
static xgui_theme_t current_theme;
static xgui_theme_id_t current_id = XGUI_THEME_CLASSIC;

/* Preset theme definitions */
static const xgui_theme_t presets[XGUI_THEME_COUNT] = {
    /* Classic — matches the original hardcoded colors exactly */
    [XGUI_THEME_CLASSIC] = {
        .desktop_bg     = XGUI_RGB(70, 130, 180),   /* Steel Blue */
        .panel_bg       = XGUI_RGB(192, 192, 192),
        .title_active   = XGUI_RGB(0, 0, 128),
        .title_inactive = XGUI_RGB(128, 128, 128),
        .window_bg      = XGUI_RGB(255, 255, 255),
        .button_bg      = XGUI_RGB(212, 208, 200),
        .button_pressed = XGUI_RGB(128, 128, 128),
        .button_text    = XGUI_RGB(0, 0, 0),
        .selection      = XGUI_RGB(51, 153, 255),
        .border         = XGUI_RGB(127, 157, 185),
    },
    /* Dark */
    [XGUI_THEME_DARK] = {
        .desktop_bg     = XGUI_RGB(30, 30, 30),
        .panel_bg       = XGUI_RGB(48, 48, 48),
        .title_active   = XGUI_RGB(60, 60, 60),
        .title_inactive = XGUI_RGB(45, 45, 45),
        .window_bg      = XGUI_RGB(40, 40, 40),
        .button_bg      = XGUI_RGB(100, 100, 100),
        .button_pressed = XGUI_RGB(130, 130, 130),
        .button_text    = XGUI_RGB(255, 255, 255),
        .selection      = XGUI_RGB(0, 120, 215),
        .border         = XGUI_RGB(70, 70, 70),
    },
    /* Ocean */
    [XGUI_THEME_OCEAN] = {
        .desktop_bg     = XGUI_RGB(0, 80, 120),
        .panel_bg       = XGUI_RGB(180, 210, 230),
        .title_active   = XGUI_RGB(0, 100, 160),
        .title_inactive = XGUI_RGB(120, 160, 180),
        .window_bg      = XGUI_RGB(240, 248, 255),
        .button_bg      = XGUI_RGB(200, 220, 235),
        .button_pressed = XGUI_RGB(140, 180, 210),
        .button_text    = XGUI_RGB(0, 0, 0),
        .selection      = XGUI_RGB(0, 140, 200),
        .border         = XGUI_RGB(100, 150, 190),
    },
    /* Olive */
    [XGUI_THEME_OLIVE] = {
        .desktop_bg     = XGUI_RGB(85, 107, 47),
        .panel_bg       = XGUI_RGB(200, 200, 180),
        .title_active   = XGUI_RGB(80, 100, 40),
        .title_inactive = XGUI_RGB(140, 140, 120),
        .window_bg      = XGUI_RGB(250, 250, 240),
        .button_bg      = XGUI_RGB(210, 210, 190),
        .button_pressed = XGUI_RGB(160, 160, 140),
        .button_text    = XGUI_RGB(0, 0, 0),
        .selection      = XGUI_RGB(120, 150, 60),
        .border         = XGUI_RGB(140, 160, 110),
    },
    /* High Contrast */
    [XGUI_THEME_HIGH_CONTRAST] = {
        .desktop_bg     = XGUI_RGB(0, 0, 0),
        .panel_bg       = XGUI_RGB(0, 0, 0),
        .title_active   = XGUI_RGB(0, 0, 200),
        .title_inactive = XGUI_RGB(80, 80, 80),
        .window_bg      = XGUI_RGB(0, 0, 0),
        .button_bg      = XGUI_RGB(110, 110, 110),
        .button_pressed = XGUI_RGB(140, 140, 140),
        .button_text    = XGUI_RGB(255, 255, 255),
        .selection      = XGUI_RGB(255, 255, 0),
        .border         = XGUI_RGB(255, 255, 255),
    },
    /* Rose */
    [XGUI_THEME_ROSE] = {
        .desktop_bg     = XGUI_RGB(180, 80, 100),
        .panel_bg       = XGUI_RGB(240, 210, 215),
        .title_active   = XGUI_RGB(180, 60, 90),
        .title_inactive = XGUI_RGB(180, 150, 155),
        .window_bg      = XGUI_RGB(255, 245, 248),
        .button_bg      = XGUI_RGB(230, 200, 210),
        .button_pressed = XGUI_RGB(200, 160, 175),
        .button_text    = XGUI_RGB(0, 0, 0),
        .selection      = XGUI_RGB(200, 60, 100),
        .border         = XGUI_RGB(180, 130, 145),
    },
    /* Slate */
    [XGUI_THEME_SLATE] = {
        .desktop_bg     = XGUI_RGB(40, 50, 65),
        .panel_bg       = XGUI_RGB(60, 70, 85),
        .title_active   = XGUI_RGB(70, 90, 120),
        .title_inactive = XGUI_RGB(55, 60, 70),
        .window_bg      = XGUI_RGB(50, 58, 70),
        .button_bg      = XGUI_RGB(110, 120, 135),
        .button_pressed = XGUI_RGB(140, 150, 165),
        .button_text    = XGUI_RGB(255, 255, 255),
        .selection      = XGUI_RGB(80, 140, 210),
        .border         = XGUI_RGB(90, 100, 115),
    },
    /* Lavender */
    [XGUI_THEME_LAVENDER] = {
        .desktop_bg     = XGUI_RGB(100, 80, 160),
        .panel_bg       = XGUI_RGB(220, 210, 240),
        .title_active   = XGUI_RGB(110, 80, 170),
        .title_inactive = XGUI_RGB(160, 150, 180),
        .window_bg      = XGUI_RGB(248, 245, 255),
        .button_bg      = XGUI_RGB(210, 200, 230),
        .button_pressed = XGUI_RGB(170, 160, 200),
        .button_text    = XGUI_RGB(0, 0, 0),
        .selection      = XGUI_RGB(130, 90, 200),
        .border         = XGUI_RGB(150, 130, 180),
    },
    /* Mint */
    [XGUI_THEME_MINT] = {
        .desktop_bg     = XGUI_RGB(40, 140, 110),
        .panel_bg       = XGUI_RGB(200, 235, 225),
        .title_active   = XGUI_RGB(30, 130, 100),
        .title_inactive = XGUI_RGB(130, 170, 160),
        .window_bg      = XGUI_RGB(240, 255, 250),
        .button_bg      = XGUI_RGB(190, 225, 215),
        .button_pressed = XGUI_RGB(150, 200, 185),
        .button_text    = XGUI_RGB(0, 0, 0),
        .selection      = XGUI_RGB(30, 160, 120),
        .border         = XGUI_RGB(100, 170, 150),
    },
    /* Sunset */
    [XGUI_THEME_SUNSET] = {
        .desktop_bg     = XGUI_RGB(50, 30, 50),
        .panel_bg       = XGUI_RGB(70, 45, 60),
        .title_active   = XGUI_RGB(180, 80, 50),
        .title_inactive = XGUI_RGB(80, 60, 70),
        .window_bg      = XGUI_RGB(55, 40, 50),
        .button_bg      = XGUI_RGB(120, 90, 100),
        .button_pressed = XGUI_RGB(150, 110, 120),
        .button_text    = XGUI_RGB(255, 255, 255),
        .selection      = XGUI_RGB(220, 100, 50),
        .border         = XGUI_RGB(100, 70, 80),
    },
    /* Y2K */
    [XGUI_THEME_Y2K] = {
        .desktop_bg     = XGUI_RGB(58, 110, 165),
        .panel_bg       = XGUI_RGB(24, 60, 130),
        .title_active   = XGUI_RGB(0, 50, 160),
        .title_inactive = XGUI_RGB(100, 120, 150),
        .window_bg      = XGUI_RGB(255, 255, 255),
        .button_bg      = XGUI_RGB(212, 208, 200),
        .button_pressed = XGUI_RGB(128, 128, 128),
        .button_text    = XGUI_RGB(0, 0, 0),
        .selection      = XGUI_RGB(30, 150, 50),
        .border         = XGUI_RGB(60, 90, 160),
    },
};

/* Theme names */
static const char* theme_names[XGUI_THEME_COUNT] = {
    "Classic",
    "Dark",
    "Ocean",
    "Olive",
    "High Contrast",
    "Rose",
    "Slate",
    "Lavender",
    "Mint",
    "Sunset",
    "Y2K",
};

/*
 * Initialize theme system — load saved config or apply Classic
 */
void xgui_theme_init(void) {
    /* Initialize gui_conf section name */
    memset(&gui_conf, 0, sizeof(gui_conf));
    strncpy(gui_conf.name, "gui", sizeof(gui_conf.name) - 1);

    /* Try to load saved GUI config */
    bool conf_ok = (conf_init() == 0);
    if (conf_ok) {
        conf_load(&gui_conf, "gui");
    }

    if (gui_conf.count > 0) {
        /* Restore saved settings */
        int theme_id = conf_get_int(&gui_conf, "theme", XGUI_THEME_CLASSIC);
        if (theme_id < 0 || theme_id >= (int)XGUI_THEME_COUNT) {
            theme_id = XGUI_THEME_CLASSIC;
        }
        current_theme = presets[theme_id];
        current_id = (xgui_theme_id_t)theme_id;

        uint32_t wallpaper = conf_get_uint32(&gui_conf, "wallpaper", current_theme.desktop_bg);
        current_theme.desktop_bg = wallpaper;
        xgui_desktop_set_color(wallpaper);

        /* Restore wallpaper display mode */
        int wp_mode = conf_get_int(&gui_conf, "wallpaper_mode", WP_MODE_CENTER);
        if (wp_mode >= 0 && wp_mode < WP_MODE_COUNT) {
            xgui_desktop_set_wp_mode((wp_mode_t)wp_mode);
        }

        /* Try to load saved BMP wallpaper (failsafe: ignore errors) */
        const char* wp_path = conf_get(&gui_conf, "wallpaper_path", "");
        if (wp_path[0]) {
            serial_write_string("THEME: loading wallpaper: ");
            serial_write_string(wp_path);
            serial_write_string("\n");
            if (xgui_desktop_set_wallpaper(wp_path) != 0) {
                serial_write_string("THEME: wallpaper load failed, using solid color\n");
                /* Clear the saved path so we don't retry on next boot */
                conf_set(&gui_conf, "wallpaper_path", "");
                conf_save(&gui_conf);
            }
        }

        /* Restore timezone offset */
        int tz_off = conf_get_int(&gui_conf, "timezone", 0);
        rtc_set_tz_offset(tz_off);

        conf_ready = true;
        serial_write_string("THEME: loaded config\n");
    } else {
        /* First boot — apply Classic and save defaults */
        current_theme = presets[XGUI_THEME_CLASSIC];
        current_id = XGUI_THEME_CLASSIC;
        xgui_desktop_set_color(current_theme.desktop_bg);
        conf_ready = true;

        if (conf_ok) {
            conf_set_int(&gui_conf, "theme", (int)XGUI_THEME_CLASSIC);
            conf_set_uint32(&gui_conf, "wallpaper", current_theme.desktop_bg);
            conf_set(&gui_conf, "wallpaper_path", "");
            conf_set_int(&gui_conf, "wallpaper_mode", WP_MODE_CENTER);
            conf_set_int(&gui_conf, "timezone", 0);
            conf_save(&gui_conf);
            serial_write_string("THEME: created default config\n");
        }
    }
}

/*
 * Get current theme colors
 */
const xgui_theme_t* xgui_theme_current(void) {
    return &current_theme;
}

/*
 * Get current theme ID
 */
xgui_theme_id_t xgui_theme_current_id(void) {
    return current_id;
}

/*
 * Apply a preset theme
 */
void xgui_theme_apply(xgui_theme_id_t id) {
    if (id >= XGUI_THEME_COUNT) return;
    current_theme = presets[id];
    current_id = id;
    xgui_desktop_set_color(current_theme.desktop_bg);

    /* Persist to config */
    if (conf_ready) {
        conf_set_int(&gui_conf, "theme", (int)id);
        conf_set_uint32(&gui_conf, "wallpaper", current_theme.desktop_bg);
        conf_save(&gui_conf);
    }
}

/*
 * Get preset theme (without applying)
 */
const xgui_theme_t* xgui_theme_get_preset(xgui_theme_id_t id) {
    if (id >= XGUI_THEME_COUNT) return &presets[0];
    return &presets[id];
}

/*
 * Get human-readable theme name
 */
const char* xgui_theme_name(xgui_theme_id_t id) {
    if (id >= XGUI_THEME_COUNT) return "Unknown";
    return theme_names[id];
}

/*
 * Set just the desktop wallpaper color (independent of theme)
 */
void xgui_theme_set_desktop_bg(uint32_t color) {
    current_theme.desktop_bg = color;
    xgui_desktop_set_color(color);
    xgui_desktop_clear_wallpaper();

    /* Persist to config — clear wallpaper path since we're using solid color */
    if (conf_ready) {
        conf_set_uint32(&gui_conf, "wallpaper", color);
        conf_set(&gui_conf, "wallpaper_path", "");
        conf_save(&gui_conf);
    }
}

/*
 * Save wallpaper BMP path to config (called after successful set_wallpaper)
 */
void xgui_theme_save_wallpaper_path(const char* path) {
    if (conf_ready) {
        conf_set(&gui_conf, "wallpaper_path", path ? path : "");
        conf_save(&gui_conf);
    }
}

/*
 * Save wallpaper display mode to config
 */
void xgui_theme_save_wallpaper_mode(int mode) {
    if (conf_ready) {
        conf_set_int(&gui_conf, "wallpaper_mode", mode);
        conf_save(&gui_conf);
    }
}

/*
 * Save timezone offset (minutes from UTC) to config
 */
void xgui_theme_save_timezone(int offset_minutes) {
    if (conf_ready) {
        conf_set_int(&gui_conf, "timezone", offset_minutes);
        conf_save(&gui_conf);
    }
}
