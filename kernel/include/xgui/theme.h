/*
 * MiniOS XGUI Theme System
 *
 * Runtime-configurable color themes for the desktop environment.
 */

#ifndef _XGUI_THEME_H
#define _XGUI_THEME_H

#include "types.h"

/* Theme color set */
typedef struct {
    uint32_t desktop_bg;
    uint32_t panel_bg;
    uint32_t title_active;
    uint32_t title_inactive;
    uint32_t window_bg;
    uint32_t button_bg;
    uint32_t button_pressed;
    uint32_t button_text;
    uint32_t selection;
    uint32_t border;
} xgui_theme_t;

/* Preset theme IDs */
typedef enum {
    XGUI_THEME_CLASSIC,
    XGUI_THEME_DARK,
    XGUI_THEME_OCEAN,
    XGUI_THEME_OLIVE,
    XGUI_THEME_HIGH_CONTRAST,
    XGUI_THEME_ROSE,
    XGUI_THEME_SLATE,
    XGUI_THEME_LAVENDER,
    XGUI_THEME_MINT,
    XGUI_THEME_SUNSET,
    XGUI_THEME_Y2K,
    XGUI_THEME_COUNT
} xgui_theme_id_t;

/* Initialize theme system (applies Classic theme) */
void xgui_theme_init(void);

/* Get current theme colors */
const xgui_theme_t* xgui_theme_current(void);

/* Get current theme ID */
xgui_theme_id_t xgui_theme_current_id(void);

/* Apply a preset theme */
void xgui_theme_apply(xgui_theme_id_t id);

/* Get preset theme (without applying) */
const xgui_theme_t* xgui_theme_get_preset(xgui_theme_id_t id);

/* Get human-readable theme name */
const char* xgui_theme_name(xgui_theme_id_t id);

/* Set just the desktop wallpaper color (independent of theme) */
void xgui_theme_set_desktop_bg(uint32_t color);

/* Save wallpaper BMP path to config */
void xgui_theme_save_wallpaper_path(const char* path);

/* Save wallpaper display mode to config */
void xgui_theme_save_wallpaper_mode(int mode);

/* Save timezone offset (minutes from UTC) to config */
void xgui_theme_save_timezone(int offset_minutes);

#endif /* _XGUI_THEME_H */
