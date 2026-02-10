/*
 * MiniOS XGUI Desktop and Panel
 *
 * Desktop background and taskbar panel.
 */

#ifndef _XGUI_DESKTOP_H
#define _XGUI_DESKTOP_H

#include "types.h"
#include "xgui/wm.h"

/* Panel height */
#define XGUI_PANEL_HEIGHT 32

/* Panel button width */
#define XGUI_PANEL_BUTTON_WIDTH 100

/* Wallpaper display modes */
typedef enum {
    WP_MODE_CENTER = 0,
    WP_MODE_FILL   = 1,   /* Stretch to fill (ignores aspect ratio) */
    WP_MODE_FIT    = 2,   /* Scale to fit (preserves aspect ratio) */
    WP_MODE_COUNT  = 3
} wp_mode_t;

/*
 * Initialize the desktop
 */
void xgui_desktop_init(void);

/*
 * Draw the desktop background
 */
void xgui_desktop_draw(void);

/*
 * Set desktop wallpaper color
 */
void xgui_desktop_set_color(uint32_t color);

/*
 * Clear BMP wallpaper (revert to solid color)
 */
void xgui_desktop_clear_wallpaper(void);

/*
 * Initialize the panel (taskbar)
 */
void xgui_panel_init(void);

/*
 * Draw the panel
 */
void xgui_panel_draw(void);

/*
 * Handle panel click
 * Returns true if click was handled
 */
bool xgui_panel_click(int x, int y, int button);

/*
 * Update panel (e.g., clock)
 */
void xgui_panel_update(void);

/*
 * Draw the start menu (if open)
 */
void xgui_draw_start_menu(void);

/*
 * Get the usable desktop area (excluding panel)
 */
void xgui_desktop_get_work_area(int* x, int* y, int* width, int* height);

/*
 * Set desktop wallpaper from a BMP file path
 * Returns 0 on success, -1 on failure
 */
int xgui_desktop_set_wallpaper(const char* path);

/*
 * Set/get wallpaper display mode
 */
void xgui_desktop_set_wp_mode(wp_mode_t mode);
wp_mode_t xgui_desktop_get_wp_mode(void);

/*
 * Check if a BMP wallpaper is currently loaded
 */
bool xgui_desktop_has_wallpaper(void);

/*
 * Desktop icon management
 */
#define DICON_TYPE_APP  0
#define DICON_TYPE_FILE 1
bool xgui_desktop_add_icon(int type, const char* name, const char* path);
void xgui_desktop_draw_icons(void);
void xgui_desktop_draw_popups(void);
bool xgui_desktop_icon_click(int x, int y, int button);
void xgui_desktop_icon_mouse_move(int x, int y);
bool xgui_desktop_icon_mouse_up(int x, int y);
bool xgui_desktop_popup_visible(void);
bool xgui_desktop_dragging(void);

#endif /* _XGUI_DESKTOP_H */
