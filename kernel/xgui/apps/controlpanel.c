/*
 * MiniOS XGUI Control Panel
 *
 * Tabbed settings: Appearance (wallpaper, themes) and Font selection.
 */

#include "xgui/xgui.h"
#include "xgui/wm.h"
#include "xgui/display.h"
#include "xgui/widget.h"
#include "xgui/theme.h"
#include "xgui/desktop.h"
#include "xgui/font.h"
#include "string.h"
#include "stdio.h"
#include "keyboard.h"
#include "conf.h"
#include "vesa.h"

/* Singleton window */
static xgui_window_t* cp_window = NULL;

/* Tab system */
#define CP_TAB_APPEARANCE  0
#define CP_TAB_FONT        1
#define CP_TAB_DISPLAY     2
#define CP_TAB_COUNT       3
static int cp_active_tab = CP_TAB_APPEARANCE;
static const char* cp_tab_names[CP_TAB_COUNT] = { "Appearance", "Font", "Display" };

/* Display tab: resolution options */
typedef struct {
    int width;
    int height;
    const char* label;
} res_option_t;

static const res_option_t res_options[] = {
    { 800,  600,  "800 x 600" },
    { 1024, 768,  "1024 x 768" },
    { 1280, 1024, "1280 x 1024" },
};
#define RES_OPTION_COUNT 3

static xgui_widget_t* res_buttons[RES_OPTION_COUNT];

/* Reboot prompt state */
static bool reboot_prompt_visible = false;
static int reboot_prompt_selection = -1;  /* -1=none, 0=selected res index */

#define TAB_BAR_H     24
#define TAB_BTN_W     75
#define TAB_BTN_H     22
#define CONTENT_Y     (TAB_BAR_H + 4)

/* Theme buttons (Appearance tab) */
static xgui_widget_t* theme_buttons[XGUI_THEME_COUNT];

/* Font buttons (Font tab) */
static xgui_widget_t* font_buttons[XGUI_FONT_COUNT];

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

/* Appearance tab layout (relative to CONTENT_Y) */
#define SWATCH_SIZE   24
#define SWATCH_GAP    4
#define SWATCH_COLS   8
#define SWATCH_ROWS   4
#define SWATCH_X      10
#define SWATCH_Y      (CONTENT_Y + 20)
#define WP_FILE_Y     (CONTENT_Y + 138)
#define THEME_Y       (CONTENT_Y + 240)
#define THEME_BTN_W   100
#define THEME_BTN_H   24

/* Font tab layout */
#define FONT_BTN_W    140
#define FONT_BTN_H    28

/* Forward declarations */
static void cp_destroy_tab_widgets(void);
static void cp_create_tab_widgets(void);

/*
 * Wallpaper Set button click
 */
static void wp_set_click(xgui_widget_t* widget) {
    (void)widget;
    if (wp_path_len == 0) return;
    int ret = xgui_desktop_set_wallpaper(wp_path);
    if (ret == 0) {
        strncpy(wp_status, "Wallpaper set!", sizeof(wp_status));
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
 * Font button click callback
 */
static void font_button_click(xgui_widget_t* widget) {
    int id = (int)(uint32_t)xgui_widget_get_userdata(widget);
    xgui_font_set(id);
    xgui_font_save_config();
    if (cp_window) cp_window->dirty = true;
}

/*
 * Resolution button click callback — switch resolution at runtime
 */
static void res_button_click(xgui_widget_t* widget) {
    int idx = (int)(uint32_t)xgui_widget_get_userdata(widget);
    /* Check if this is already the current resolution */
    int cur_w = xgui_display_width();
    int cur_h = xgui_display_height();
    if (res_options[idx].width == cur_w && res_options[idx].height == cur_h) {
        return;  /* Already at this resolution */
    }
    /* Switch resolution at runtime — this destroys all windows including ours */
    cp_window = NULL;
    wp_set_btn = NULL;
    wp_clear_btn = NULL;
    for (int i = 0; i < (int)WP_MODE_COUNT; i++) wp_mode_btns[i] = NULL;
    for (int i = 0; i < (int)XGUI_THEME_COUNT; i++) theme_buttons[i] = NULL;
    for (int i = 0; i < XGUI_FONT_COUNT; i++) font_buttons[i] = NULL;
    for (int i = 0; i < RES_OPTION_COUNT; i++) res_buttons[i] = NULL;
    xgui_set_resolution(res_options[idx].width, res_options[idx].height);
}

/*
 * Destroy all widgets for the current tab
 */
static void cp_destroy_tab_widgets(void) {
    if (!cp_window) return;
    xgui_widgets_destroy_all(cp_window);
    wp_set_btn = NULL;
    wp_clear_btn = NULL;
    for (int i = 0; i < (int)WP_MODE_COUNT; i++) wp_mode_btns[i] = NULL;
    for (int i = 0; i < (int)XGUI_THEME_COUNT; i++) theme_buttons[i] = NULL;
    for (int i = 0; i < XGUI_FONT_COUNT; i++) font_buttons[i] = NULL;
    for (int i = 0; i < RES_OPTION_COUNT; i++) res_buttons[i] = NULL;
    reboot_prompt_visible = false;
    reboot_prompt_selection = -1;
}

/*
 * Create widgets for the active tab
 */
static void cp_create_tab_widgets(void) {
    if (!cp_window) return;

    if (cp_active_tab == CP_TAB_APPEARANCE) {
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

        /* Theme buttons in two columns */
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
    } else if (cp_active_tab == CP_TAB_FONT) {
        /* Font buttons */
        for (int i = 0; i < XGUI_FONT_COUNT; i++) {
            int by = CONTENT_Y + 40 + i * (FONT_BTN_H + 8);
            font_buttons[i] = xgui_button_create(cp_window, 15, by,
                                                  FONT_BTN_W, FONT_BTN_H,
                                                  xgui_font_name(i));
            if (font_buttons[i]) {
                xgui_widget_set_onclick(font_buttons[i], font_button_click);
                xgui_widget_set_userdata(font_buttons[i], (void*)(uint32_t)i);
            }
        }
    } else if (cp_active_tab == CP_TAB_DISPLAY) {
        /* Resolution buttons */
        for (int i = 0; i < RES_OPTION_COUNT; i++) {
            int by = CONTENT_Y + 80 + i * 34;
            res_buttons[i] = xgui_button_create(cp_window, 15, by, 140, 28,
                                                 res_options[i].label);
            if (res_buttons[i]) {
                xgui_widget_set_onclick(res_buttons[i], res_button_click);
                xgui_widget_set_userdata(res_buttons[i], (void*)(uint32_t)i);
            }
        }
    }
}

/*
 * Switch to a different tab
 */
static void cp_switch_tab(int tab) {
    if (tab == cp_active_tab) return;
    if (tab < 0 || tab >= CP_TAB_COUNT) return;
    wp_input_active = false;
    cp_destroy_tab_widgets();
    cp_active_tab = tab;
    cp_create_tab_widgets();
    if (cp_window) cp_window->dirty = true;
}

/*
 * Draw tab bar at the top
 */
static void cp_draw_tabs(xgui_window_t* win) {
    /* Tab bar background */
    xgui_win_rect_filled(win, 0, 0, win->buf_width, TAB_BAR_H, XGUI_RGB(220, 220, 220));
    xgui_win_hline(win, 0, TAB_BAR_H, win->buf_width, XGUI_DARK_GRAY);

    for (int i = 0; i < CP_TAB_COUNT; i++) {
        int tx = 4 + i * (TAB_BTN_W + 4);
        int ty = 1;

        if (i == cp_active_tab) {
            /* Active tab: white background, no bottom border */
            xgui_win_rect_filled(win, tx, ty, TAB_BTN_W, TAB_BTN_H + 1, XGUI_WHITE);
            xgui_win_hline(win, tx, ty, TAB_BTN_W, XGUI_DARK_GRAY);
            xgui_win_rect_filled(win, tx, ty + 1, 1, TAB_BTN_H, XGUI_DARK_GRAY);
            xgui_win_rect_filled(win, tx + TAB_BTN_W - 1, ty + 1, 1, TAB_BTN_H, XGUI_DARK_GRAY);
        } else {
            /* Inactive tab: gray background */
            xgui_win_rect_filled(win, tx, ty + 2, TAB_BTN_W, TAB_BTN_H - 2, XGUI_RGB(200, 200, 200));
            xgui_win_rect(win, tx, ty + 2, TAB_BTN_W, TAB_BTN_H - 2, XGUI_DARK_GRAY);
        }

        /* Tab label centered */
        int tw = xgui_font_string_width(cp_tab_names[i]);
        int lx = tx + (TAB_BTN_W - tw) / 2;
        int ly = ty + (TAB_BTN_H - 16) / 2 + (i == cp_active_tab ? 0 : 2);
        xgui_win_text_transparent(win, lx, ly, cp_tab_names[i], XGUI_BLACK);
    }
}

/*
 * Paint: Appearance tab
 */
static void cp_paint_appearance(xgui_window_t* win) {
    /* Section label: Wallpaper Color */
    xgui_win_text_transparent(win, 10, CONTENT_Y + 4, "Wallpaper Color", XGUI_BLACK);

    /* Draw color swatches */
    uint32_t current_bg = xgui_theme_current()->desktop_bg;
    for (int i = 0; i < WALLPAPER_COLOR_COUNT; i++) {
        int col = i % SWATCH_COLS;
        int row = i / SWATCH_COLS;
        int x = SWATCH_X + col * (SWATCH_SIZE + SWATCH_GAP);
        int y = SWATCH_Y + row * (SWATCH_SIZE + SWATCH_GAP);

        xgui_win_rect_filled(win, x, y, SWATCH_SIZE, SWATCH_SIZE, wallpaper_colors[i]);

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
            int bx = theme_buttons[i]->x;
            int by = theme_buttons[i]->y;
            if ((xgui_theme_id_t)i == cur_id) {
                xgui_win_rect_filled(win, bx - 8, by + 6, 5, 12, XGUI_RGB(0, 160, 0));
            }
        }
    }
}

/*
 * Paint: Font tab
 */
static void cp_paint_font(xgui_window_t* win) {
    xgui_win_text_transparent(win, 10, CONTENT_Y + 8, "Select Font", XGUI_BLACK);

    /* Highlight active font button */
    int cur_font = xgui_font_get();
    for (int i = 0; i < XGUI_FONT_COUNT; i++) {
        if (font_buttons[i]) {
            int bx = font_buttons[i]->x;
            int by = font_buttons[i]->y;
            if (i == cur_font) {
                xgui_win_rect_filled(win, bx - 8, by + 8, 5, 12, XGUI_RGB(0, 160, 0));
            }
        }
    }

    /* Preview area */
    int preview_y = CONTENT_Y + 40 + XGUI_FONT_COUNT * (FONT_BTN_H + 8) + 16;
    xgui_win_hline(win, 10, preview_y - 8, win->buf_width - 20, XGUI_DARK_GRAY);
    xgui_win_text_transparent(win, 10, preview_y, "Preview:", XGUI_BLACK);
    xgui_win_rect_filled(win, 10, preview_y + 18, win->buf_width - 20, 70, XGUI_WHITE);
    xgui_win_rect(win, 10, preview_y + 18, win->buf_width - 20, 70, XGUI_DARK_GRAY);
    xgui_win_text(win, 14, preview_y + 22, "ABCDEFGHIJKLM", XGUI_BLACK, XGUI_WHITE);
    xgui_win_text(win, 14, preview_y + 38, "nopqrstuvwxyz", XGUI_BLACK, XGUI_WHITE);
    xgui_win_text(win, 14, preview_y + 54, "0123456789 !@#", XGUI_BLACK, XGUI_WHITE);
}

/*
 * Paint: Display tab
 */
static void cp_paint_display(xgui_window_t* win) {
    int cw = win->buf_width;

    /* Current resolution info */
    xgui_win_text_transparent(win, 10, CONTENT_Y + 8, "Current Resolution", XGUI_BLACK);

    int cur_w = xgui_display_width();
    int cur_h = xgui_display_height();
    char res_str[32];
    snprintf(res_str, sizeof(res_str), "%d x %d", cur_w, cur_h);

    xgui_win_rect_filled(win, 10, CONTENT_Y + 26, cw - 20, 24, XGUI_RGB(240, 240, 240));
    xgui_win_rect(win, 10, CONTENT_Y + 26, cw - 20, 24, XGUI_DARK_GRAY);
    xgui_win_text(win, 16, CONTENT_Y + 30, res_str, XGUI_BLACK, XGUI_RGB(240, 240, 240));

    /* BPP info */
    vesa_info_t* vesa = vesa_get_info();
    char bpp_str[32];
    snprintf(bpp_str, sizeof(bpp_str), "%d bpp", vesa->bpp);
    int bpp_x = cw - 10 - (int)strlen(bpp_str) * 8;
    xgui_win_text(win, bpp_x, CONTENT_Y + 30, bpp_str, XGUI_RGB(100, 100, 100),
                  XGUI_RGB(240, 240, 240));

    /* Separator */
    xgui_win_hline(win, 10, CONTENT_Y + 60, cw - 20, XGUI_DARK_GRAY);

    /* Resolution picker label */
    xgui_win_text_transparent(win, 10, CONTENT_Y + 66, "Change Resolution", XGUI_BLACK);

    /* Highlight current resolution button */
    for (int i = 0; i < RES_OPTION_COUNT; i++) {
        if (res_buttons[i]) {
            int bx = res_buttons[i]->x;
            int by = res_buttons[i]->y;
            if (res_options[i].width == cur_w && res_options[i].height == cur_h) {
                xgui_win_rect_filled(win, bx - 8, by + 8, 5, 12, XGUI_RGB(0, 160, 0));
            }
        }
    }

    /* Note */
    int note_y = CONTENT_Y + 80 + RES_OPTION_COUNT * 34 + 8;
    xgui_win_text_transparent(win, 10, note_y, "Changes apply instantly.",
                              XGUI_RGB(120, 120, 120));
    xgui_win_text_transparent(win, 10, note_y + 16, "All windows will close.",
                              XGUI_RGB(120, 120, 120));
    if (!vesa_bochs_available()) {
        xgui_win_text_transparent(win, 10, note_y + 36, "Bochs VBE not detected.",
                                  XGUI_RGB(180, 60, 60));
        xgui_win_text_transparent(win, 10, note_y + 52, "Use QEMU -vga std.",
                                  XGUI_RGB(180, 60, 60));
    }
}

/*
 * Main paint callback
 */
static void cp_paint(xgui_window_t* win) {
    cp_draw_tabs(win);

    if (cp_active_tab == CP_TAB_APPEARANCE) {
        cp_paint_appearance(win);
    } else if (cp_active_tab == CP_TAB_FONT) {
        cp_paint_font(win);
    } else if (cp_active_tab == CP_TAB_DISPLAY) {
        cp_paint_display(win);
    }

    xgui_widgets_draw(win);
}

/*
 * Event handler
 */
static void cp_handler(xgui_window_t* win, xgui_event_t* event) {
    /* Let widgets handle events first */
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
        for (int i = 0; i < (int)XGUI_THEME_COUNT; i++) theme_buttons[i] = NULL;
        for (int i = 0; i < XGUI_FONT_COUNT; i++) font_buttons[i] = NULL;
        return;
    }

    /* Handle keyboard input for wallpaper path (Appearance tab only) */
    if (cp_active_tab == CP_TAB_APPEARANCE && wp_input_active && event->type == XGUI_EVENT_KEY_CHAR) {
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

    if (cp_active_tab == CP_TAB_APPEARANCE && wp_input_active && event->type == XGUI_EVENT_KEY_DOWN) {
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

        /* Check tab bar clicks */
        if (my < TAB_BAR_H) {
            for (int i = 0; i < CP_TAB_COUNT; i++) {
                int tx = 4 + i * (TAB_BTN_W + 4);
                if (mx >= tx && mx < tx + TAB_BTN_W) {
                    cp_switch_tab(i);
                    return;
                }
            }
            return;
        }

        /* Appearance tab: wallpaper input + swatch clicks */
        if (cp_active_tab == CP_TAB_APPEARANCE) {
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
    int win_h = 490;
    cp_window = xgui_window_create("Control Panel", 200, 80, win_w, win_h,
                                    XGUI_WINDOW_DEFAULT);
    if (!cp_window) return;

    xgui_window_set_paint(cp_window, cp_paint);
    xgui_window_set_handler(cp_window, cp_handler);

    cp_active_tab = CP_TAB_APPEARANCE;
    cp_create_tab_widgets();
}
