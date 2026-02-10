/*
 * MiniOS XGUI Control Panel
 *
 * Appearance settings: wallpaper color swatches and color theme selection.
 */

#include "xgui/xgui.h"
#include "xgui/wm.h"
#include "xgui/display.h"
#include "xgui/widget.h"
#include "xgui/theme.h"
#include "xgui/desktop.h"
#include "string.h"
#include "stdio.h"
#include "keyboard.h"

/* Singleton window */
static xgui_window_t* cp_window = NULL;

/* Theme buttons */
static xgui_widget_t* theme_buttons[XGUI_THEME_COUNT];

/* Wallpaper color palette (32 colors, 4 rows x 8 cols) */
#define WALLPAPER_COLOR_COUNT 32
static const uint32_t wallpaper_colors[WALLPAPER_COLOR_COUNT] = {
    /* Row 1: Blues and teals */
    XGUI_RGB(70, 130, 180),   /* Steel Blue (default) */
    XGUI_RGB(0, 80, 120),     /* Dark Teal */
    XGUI_RGB(0, 100, 200),    /* Bright Blue */
    XGUI_RGB(30, 60, 120),    /* Navy */
    XGUI_RGB(0, 150, 200),    /* Cerulean */
    XGUI_RGB(100, 180, 220),  /* Sky Blue */
    XGUI_RGB(0, 128, 128),    /* Teal */
    XGUI_RGB(70, 180, 170),   /* Turquoise */
    /* Row 2: Greens, yellows, oranges */
    XGUI_RGB(50, 120, 50),    /* Forest Green */
    XGUI_RGB(85, 107, 47),    /* Olive */
    XGUI_RGB(34, 139, 34),    /* Green */
    XGUI_RGB(100, 160, 60),   /* Lime */
    XGUI_RGB(200, 180, 50),   /* Gold */
    XGUI_RGB(220, 140, 40),   /* Amber */
    XGUI_RGB(180, 80, 40),    /* Rust */
    XGUI_RGB(200, 60, 30),    /* Vermillion */
    /* Row 3: Reds, pinks, purples */
    XGUI_RGB(150, 50, 80),    /* Burgundy */
    XGUI_RGB(180, 40, 60),    /* Crimson */
    XGUI_RGB(200, 100, 120),  /* Rose */
    XGUI_RGB(180, 100, 160),  /* Orchid */
    XGUI_RGB(100, 50, 150),   /* Purple */
    XGUI_RGB(70, 50, 120),    /* Indigo */
    XGUI_RGB(140, 100, 180),  /* Wisteria */
    XGUI_RGB(100, 80, 160),   /* Lavender */
    /* Row 4: Neutrals */
    XGUI_RGB(30, 30, 30),     /* Near Black */
    XGUI_RGB(60, 60, 80),     /* Slate */
    XGUI_RGB(80, 80, 80),     /* Dark Gray */
    XGUI_RGB(128, 128, 128),  /* Gray */
    XGUI_RGB(160, 140, 120),  /* Tan */
    XGUI_RGB(192, 192, 192),  /* Silver */
    XGUI_RGB(210, 180, 140),  /* Wheat */
    XGUI_RGB(245, 245, 220),  /* Beige */
};

/* Wallpaper BMP file input */
#define WP_PATH_MAX 128
static char wp_path[WP_PATH_MAX] = "/mnt/";
static int wp_path_len = 5;
static int wp_cursor = 5;
static bool wp_input_active = false;
static char wp_status[32] = "";
static int wp_status_ticks = 0;
static xgui_widget_t* wp_set_btn = NULL;
static xgui_widget_t* wp_clear_btn = NULL;
static xgui_widget_t* wp_mode_btns[WP_MODE_COUNT];
static const char* wp_mode_names[WP_MODE_COUNT] = { "Center", "Fill", "Fit" };

/* Layout constants */
#define SWATCH_SIZE   24
#define SWATCH_GAP    4
#define SWATCH_COLS   8
#define SWATCH_ROWS   4
#define SWATCH_X      10
#define SWATCH_Y      30
#define WP_FILE_Y     148
#define THEME_Y       250
#define THEME_BTN_W   100
#define THEME_BTN_H   24

/*
 * Wallpaper Set button click
 */
static void wp_set_click(xgui_widget_t* widget) {
    (void)widget;
    if (wp_path_len == 0) return;
    int ret = xgui_desktop_set_wallpaper(wp_path);
    if (ret == 0) {
        strncpy(wp_status, "Wallpaper set!", sizeof(wp_status));
        /* Save path to config for persistence */
        xgui_theme_save_wallpaper_path(wp_path);
    } else {
        strncpy(wp_status, "Failed to load BMP", sizeof(wp_status));
    }
    wp_status_ticks = 80;
    if (cp_window) cp_window->dirty = true;
}

/*
 * Wallpaper Clear button click
 */
static void wp_clear_click(xgui_widget_t* widget) {
    (void)widget;
    xgui_desktop_clear_wallpaper();
    xgui_theme_save_wallpaper_path("");
    strncpy(wp_status, "Wallpaper cleared", sizeof(wp_status));
    wp_status_ticks = 80;
    if (cp_window) cp_window->dirty = true;
}

/*
 * Wallpaper mode button click
 */
static void wp_mode_click(xgui_widget_t* widget) {
    wp_mode_t mode = (wp_mode_t)(uint32_t)xgui_widget_get_userdata(widget);
    xgui_desktop_set_wp_mode(mode);
    xgui_theme_save_wallpaper_mode((int)mode);
    if (cp_window) cp_window->dirty = true;
}

/*
 * Theme button click callback
 */
static void theme_button_click(xgui_widget_t* widget) {
    xgui_theme_id_t id = (xgui_theme_id_t)(uint32_t)xgui_widget_get_userdata(widget);
    xgui_theme_apply(id);
    if (cp_window) cp_window->dirty = true;
}

/*
 * Paint callback — draws swatches manually, widgets drawn by toolkit
 */
static void cp_paint(xgui_window_t* win) {
    /* Section label: Wallpaper Color */
    xgui_win_text_transparent(win, 10, 10, "Wallpaper Color", XGUI_BLACK);

    /* Draw color swatches */
    uint32_t current_bg = xgui_theme_current()->desktop_bg;
    for (int i = 0; i < WALLPAPER_COLOR_COUNT; i++) {
        int col = i % SWATCH_COLS;
        int row = i / SWATCH_COLS;
        int x = SWATCH_X + col * (SWATCH_SIZE + SWATCH_GAP);
        int y = SWATCH_Y + row * (SWATCH_SIZE + SWATCH_GAP);

        /* Swatch fill */
        xgui_win_rect_filled(win, x, y, SWATCH_SIZE, SWATCH_SIZE, wallpaper_colors[i]);

        /* Highlight active swatch */
        if (wallpaper_colors[i] == current_bg) {
            xgui_win_rect(win, x - 2, y - 2, SWATCH_SIZE + 4, SWATCH_SIZE + 4, XGUI_RED);
            xgui_win_rect(win, x - 1, y - 1, SWATCH_SIZE + 2, SWATCH_SIZE + 2, XGUI_RED);
        } else {
            xgui_win_rect(win, x, y, SWATCH_SIZE, SWATCH_SIZE, XGUI_BLACK);
        }
    }

    /* Separator */
    xgui_win_hline(win, 10, WP_FILE_Y - 10, win->buf_width - 20, XGUI_DARK_GRAY);

    /* Section label: Wallpaper Image */
    xgui_win_text_transparent(win, 10, WP_FILE_Y - 5, "Wallpaper Image", XGUI_BLACK);

    /* File path input box */
    int input_x = 10;
    int input_y = WP_FILE_Y + 12;
    int input_w = win->buf_width - 20;
    int input_h = 18;
    xgui_win_rect_filled(win, input_x, input_y, input_w, input_h,
                         wp_input_active ? XGUI_WHITE : XGUI_RGB(240, 240, 240));
    xgui_win_rect(win, input_x, input_y, input_w, input_h,
                  wp_input_active ? XGUI_BLUE : XGUI_DARK_GRAY);

    /* Draw path text */
    char display_path[32];
    int max_chars = (input_w - 6) / 8;
    if (wp_path_len <= max_chars) {
        strncpy(display_path, wp_path, sizeof(display_path) - 1);
        display_path[sizeof(display_path) - 1] = '\0';
    } else {
        int start = wp_path_len - max_chars;
        strncpy(display_path, &wp_path[start], sizeof(display_path) - 1);
        display_path[sizeof(display_path) - 1] = '\0';
    }
    xgui_win_text(win, input_x + 3, input_y + 2, display_path, XGUI_BLACK, 
                  wp_input_active ? XGUI_WHITE : XGUI_RGB(240, 240, 240));

    /* Draw cursor if active */
    if (wp_input_active) {
        int vis_cursor = wp_cursor;
        if (wp_path_len > max_chars) vis_cursor -= (wp_path_len - max_chars);
        if (vis_cursor >= 0 && vis_cursor <= max_chars) {
            int cx = input_x + 3 + vis_cursor * 8;
            xgui_win_rect_filled(win, cx, input_y + 2, 2, 14, XGUI_BLACK);
        }
    }

    /* Status message */
    if (wp_status_ticks > 0) {
        xgui_win_text_transparent(win, 10, input_y + input_h + 4, wp_status,
                                  XGUI_RGB(0, 100, 0));
        wp_status_ticks--;
    }

    /* Display mode label */
    xgui_win_text_transparent(win, 10, WP_FILE_Y + 62, "Display:", XGUI_BLACK);

    /* Highlight active mode button */
    wp_mode_t cur_mode = xgui_desktop_get_wp_mode();
    for (int i = 0; i < (int)WP_MODE_COUNT; i++) {
        if (wp_mode_btns[i]) {
            int bx = wp_mode_btns[i]->x;
            int by = wp_mode_btns[i]->y;
            if ((wp_mode_t)i == cur_mode) {
                xgui_win_rect_filled(win, bx - 8, by + 4, 5, 14, XGUI_RGB(0, 160, 0));
            }
        }
    }

    /* Separator before themes */
    xgui_win_hline(win, 10, THEME_Y - 10, win->buf_width - 20, XGUI_DARK_GRAY);

    /* Section label: Color Theme */
    xgui_win_text_transparent(win, 10, THEME_Y - 5, "Color Theme", XGUI_BLACK);

    /* Highlight active theme button */
    xgui_theme_id_t cur_id = xgui_theme_current_id();
    for (int i = 0; i < (int)XGUI_THEME_COUNT; i++) {
        if (theme_buttons[i]) {
            /* Draw a colored indicator next to the active theme */
            int bx = theme_buttons[i]->x;
            int by = theme_buttons[i]->y;
            if ((xgui_theme_id_t)i == cur_id) {
                xgui_win_rect_filled(win, bx - 8, by + 6, 5, 12, XGUI_RGB(0, 160, 0));
            }
        }
    }

    /* Draw all widgets (theme buttons) */
    xgui_widgets_draw(win);
}

/*
 * Event handler — manual hit-test on swatches, widgets handle theme buttons
 */
static void cp_handler(xgui_window_t* win, xgui_event_t* event) {
    /* Let widgets handle events first (theme buttons, set/clear buttons) */
    if (xgui_widgets_handle_event(win, event)) {
        return;
    }

    /* Handle window close */
    if (event->type == XGUI_EVENT_WINDOW_CLOSE) {
        xgui_window_destroy(win);
        cp_window = NULL;
        wp_set_btn = NULL;
        wp_clear_btn = NULL;
        for (int i = 0; i < (int)WP_MODE_COUNT; i++) wp_mode_btns[i] = NULL;
        for (int i = 0; i < (int)XGUI_THEME_COUNT; i++) {
            theme_buttons[i] = NULL;
        }
        return;
    }

    /* Handle keyboard input for wallpaper path */
    if (wp_input_active && event->type == XGUI_EVENT_KEY_CHAR) {
        char c = event->key.character;
        if (c >= 32 && c < 127 && wp_path_len < WP_PATH_MAX - 1) {
            memmove(&wp_path[wp_cursor + 1], &wp_path[wp_cursor],
                    wp_path_len - wp_cursor + 1);
            wp_path[wp_cursor] = c;
            wp_path_len++;
            wp_cursor++;
            win->dirty = true;
        }
        return;
    }

    if (wp_input_active && event->type == XGUI_EVENT_KEY_DOWN) {
        uint8_t key = event->key.keycode;
        if (key == '\b' && wp_cursor > 0) {
            memmove(&wp_path[wp_cursor - 1], &wp_path[wp_cursor],
                    wp_path_len - wp_cursor + 1);
            wp_cursor--;
            wp_path_len--;
            win->dirty = true;
        } else if (key == '\n' || key == '\r') {
            wp_set_click(NULL);
        } else if (key == 0x1B) {
            wp_input_active = false;
            win->dirty = true;
        } else if (key == KEY_LEFT && wp_cursor > 0) {
            wp_cursor--;
            win->dirty = true;
        } else if (key == KEY_RIGHT && wp_cursor < wp_path_len) {
            wp_cursor++;
            win->dirty = true;
        } else if (key == KEY_HOME) {
            wp_cursor = 0;
            win->dirty = true;
        } else if (key == KEY_END) {
            wp_cursor = wp_path_len;
            win->dirty = true;
        }
        return;
    }

    /* Handle mouse clicks */
    if (event->type == XGUI_EVENT_MOUSE_DOWN) {
        int mx = event->mouse.x;
        int my = event->mouse.y;

        /* Check if click is on the wallpaper path input box */
        int input_x = 10;
        int input_y = WP_FILE_Y + 12;
        int input_w = win->buf_width - 20;
        int input_h = 18;
        if (mx >= input_x && mx < input_x + input_w &&
            my >= input_y && my < input_y + input_h) {
            wp_input_active = true;
            win->dirty = true;
            return;
        } else {
            if (wp_input_active) {
                wp_input_active = false;
                win->dirty = true;
            }
        }

        /* Check swatch clicks */
        for (int i = 0; i < WALLPAPER_COLOR_COUNT; i++) {
            int col = i % SWATCH_COLS;
            int row = i / SWATCH_COLS;
            int x = SWATCH_X + col * (SWATCH_SIZE + SWATCH_GAP);
            int y = SWATCH_Y + row * (SWATCH_SIZE + SWATCH_GAP);

            if (mx >= x && mx < x + SWATCH_SIZE &&
                my >= y && my < y + SWATCH_SIZE) {
                xgui_theme_set_desktop_bg(wallpaper_colors[i]);
                win->dirty = true;
                return;
            }
        }
    }
}

/*
 * Create the Control Panel window
 */
void xgui_controlpanel_create(void) {
    if (cp_window) {
        xgui_window_focus(cp_window);
        return;
    }

    int win_w = 250;
    int win_h = 590;
    cp_window = xgui_window_create("Control Panel", 200, 80, win_w, win_h,
                                    XGUI_WINDOW_DEFAULT);
    if (!cp_window) return;

    xgui_window_set_paint(cp_window, cp_paint);
    xgui_window_set_handler(cp_window, cp_handler);

    /* Wallpaper Set / Clear buttons */
    wp_set_btn = xgui_button_create(cp_window, 10, WP_FILE_Y + 34, 60, 22, "Set");
    if (wp_set_btn) xgui_widget_set_onclick(wp_set_btn, wp_set_click);
    wp_clear_btn = xgui_button_create(cp_window, 76, WP_FILE_Y + 34, 60, 22, "Clear");
    if (wp_clear_btn) xgui_widget_set_onclick(wp_clear_btn, wp_clear_click);

    /* Wallpaper display mode buttons */
    for (int i = 0; i < (int)WP_MODE_COUNT; i++) {
        int bx = 75 + i * 58;
        int by = WP_FILE_Y + 60;
        wp_mode_btns[i] = xgui_button_create(cp_window, bx, by, 54, 22, wp_mode_names[i]);
        if (wp_mode_btns[i]) {
            xgui_widget_set_onclick(wp_mode_btns[i], wp_mode_click);
            xgui_widget_set_userdata(wp_mode_btns[i], (void*)(uint32_t)i);
        }
    }

    /* Create theme buttons in two columns */
    for (int i = 0; i < (int)XGUI_THEME_COUNT; i++) {
        int col = i % 2;
        int row = i / 2;
        int bx = 15 + col * (THEME_BTN_W + 10);
        int by = THEME_Y + 10 + row * (THEME_BTN_H + 6);

        theme_buttons[i] = xgui_button_create(cp_window, bx, by,
                                               THEME_BTN_W, THEME_BTN_H,
                                               xgui_theme_name((xgui_theme_id_t)i));
        if (theme_buttons[i]) {
            xgui_widget_set_onclick(theme_buttons[i], theme_button_click);
            xgui_widget_set_userdata(theme_buttons[i], (void*)(uint32_t)i);
        }
    }
}
