/*
 * MiniOS XGUI Desktop and Panel
 *
 * Desktop background and taskbar panel.
 */

#include "xgui/desktop.h"
#include "xgui/display.h"
#include "xgui/wm.h"
#include "xgui/event.h"
#include "xgui/theme.h"
#include "xgui/xgui.h"
#include "timer.h"
#include "string.h"
#include "serial.h"
#include "rtc.h"
#include "vfs.h"
#include "shell.h"
#include "heap.h"
#include "stdio.h"
#include "mouse.h"

/* Desktop state */
static uint32_t desktop_color = XGUI_DESKTOP_BG;
static int screen_width = 0;
static int screen_height = 0;

/* Wallpaper image (NULL = solid color) */
static uint32_t* wallpaper_pixels = NULL;
static int wallpaper_w = 0;
static int wallpaper_h = 0;
static wp_mode_t wallpaper_mode = WP_MODE_CENTER;

/* Panel state */
static int panel_y = 0;

/* Start menu state */
static bool start_menu_open = false;

/* XP-style two-column menu */
static int menu_hover_item = -1;   /* Hovered item index (-1 = none) */

/* Menu layout */
#define SMENU_W         300    /* Total menu width */
#define SMENU_LEFT_W    155    /* Left column width */
#define SMENU_RIGHT_W   145    /* Right column width */
#define SMENU_ITEM_H    24     /* Item row height */
#define SMENU_BANNER_H  32     /* Top banner height */
#define SMENU_BOTTOM_H  28     /* Bottom bar height */
#define SMENU_PAD       6      /* Padding */

/* Left column: Programs (indices 0..10) */
#define LEFT_COUNT  11
static const char* left_labels[LEFT_COUNT] = {
    "Terminal", "File Explorer", "Text Editor", "Spreadsheet",
    "Calculator", "Paint", "Sticky Note", "Calendar",
    "Clock", "Ski Game", "Flappy Cat"
};

/* Right column: System (indices 10..14) */
#define RIGHT_COUNT 5
#define RIGHT_BASE  LEFT_COUNT
static const char* right_labels[RIGHT_COUNT] = {
    "Control Panel", "Task Manager", "Disk Utility", "About MiniOS", "Clock Settings"
};

/* Shutdown = special index */
#define MENU_SHUTDOWN_IDX  (RIGHT_BASE + RIGHT_COUNT)

/*
 * Color helpers for modern panel rendering
 */
static uint32_t blend_color(uint32_t c1, uint32_t c2, int alpha) {
    /* alpha 0..255: 0 = all c1, 255 = all c2 */
    int r1 = XGUI_GET_R(c1), g1 = XGUI_GET_G(c1), b1 = XGUI_GET_B(c1);
    int r2 = XGUI_GET_R(c2), g2 = XGUI_GET_G(c2), b2 = XGUI_GET_B(c2);
    int r = r1 + ((r2 - r1) * alpha) / 255;
    int g = g1 + ((g2 - g1) * alpha) / 255;
    int b = b1 + ((b2 - b1) * alpha) / 255;
    return XGUI_RGB(r, g, b);
}

static uint32_t lighten(uint32_t c, int amount) {
    int r = XGUI_GET_R(c) + amount; if (r > 255) r = 255;
    int g = XGUI_GET_G(c) + amount; if (g > 255) g = 255;
    int b = XGUI_GET_B(c) + amount; if (b > 255) b = 255;
    return XGUI_RGB(r, g, b);
}

static uint32_t darken(uint32_t c, int amount) {
    int r = XGUI_GET_R(c) - amount; if (r < 0) r = 0;
    int g = XGUI_GET_G(c) - amount; if (g < 0) g = 0;
    int b = XGUI_GET_B(c) - amount; if (b < 0) b = 0;
    return XGUI_RGB(r, g, b);
}

static bool is_dark_color(uint32_t c) {
    int lum = XGUI_GET_R(c) * 299 + XGUI_GET_G(c) * 587 + XGUI_GET_B(c) * 114;
    return lum < 128000;
}

static uint32_t panel_text_color(void) {
    return is_dark_color(xgui_theme_current()->panel_bg) ? XGUI_WHITE : XGUI_BLACK;
}

/*
 * Draw a drop shadow around a rectangle.
 * Draws semi-transparent dark strips on the right and bottom edges.
 */
static void draw_drop_shadow(int x, int y, int w, int h, int depth) {
    for (int d = 1; d <= depth; d++) {
        int alpha = 80 - (d * 60) / depth; /* Fade out */
        if (alpha < 10) alpha = 10;
        /* Right edge shadow */
        for (int row = y + d; row < y + h + d; row++) {
            uint32_t bg = xgui_display_get_pixel(x + w + d - 1, row);
            xgui_display_pixel(x + w + d - 1, row, blend_color(bg, XGUI_BLACK, alpha));
        }
        /* Bottom edge shadow */
        for (int col = x + d; col < x + w + d; col++) {
            uint32_t bg = xgui_display_get_pixel(col, y + h + d - 1);
            xgui_display_pixel(col, y + h + d - 1, blend_color(bg, XGUI_BLACK, alpha));
        }
    }
}

/*
 * Initialize the desktop
 */
void xgui_desktop_init(void) {
    screen_width = xgui_display_width();
    screen_height = xgui_display_height();
    desktop_color = XGUI_DESKTOP_BG;
}

/*
 * Draw the desktop background
 */
/*
 * Sample a pixel from the wallpaper using nearest-neighbor scaling.
 * sx, sy are in fixed-point 16.16 format source coordinates.
 */
static inline uint32_t wp_sample(int sx_fp, int sy_fp) {
    int sx = sx_fp >> 16;
    int sy = sy_fp >> 16;
    if (sx < 0) sx = 0;
    if (sy < 0) sy = 0;
    if (sx >= wallpaper_w) sx = wallpaper_w - 1;
    if (sy >= wallpaper_h) sy = wallpaper_h - 1;
    return wallpaper_pixels[sy * wallpaper_w + sx];
}

void xgui_desktop_draw(void) {
    /* Use live display dimensions in case init order was wrong */
    int w = xgui_display_width();
    int h = xgui_display_height();
    if (w == 0 || h == 0) {
        serial_write_string("DESKTOP: draw skipped, w=0 or h=0\n");
        return;
    }
    int desk_h = h - XGUI_PANEL_HEIGHT;

    if (wallpaper_pixels && wallpaper_w > 0 && wallpaper_h > 0) {
        if (wallpaper_mode == WP_MODE_FILL) {
            /* Stretch to fill entire desktop (ignores aspect ratio) */
            int sx_step = (wallpaper_w << 16) / w;
            int sy_step = (wallpaper_h << 16) / desk_h;
            for (int dy = 0; dy < desk_h; dy++) {
                int sy_fp = dy * sy_step;
                for (int dx = 0; dx < w; dx++) {
                    xgui_display_pixel(dx, dy, wp_sample(dx * sx_step, sy_fp));
                }
            }
        } else if (wallpaper_mode == WP_MODE_FIT) {
            /* Scale to fit preserving aspect ratio, letterbox with bg color */
            xgui_display_rect_filled(0, 0, w, desk_h, desktop_color);
            int scale_w, scale_h;
            /* Compare aspect ratios: img_w/img_h vs w/desk_h */
            if (wallpaper_w * desk_h > wallpaper_h * w) {
                /* Image is wider — fit to width */
                scale_w = w;
                scale_h = (wallpaper_h * w) / wallpaper_w;
            } else {
                /* Image is taller — fit to height */
                scale_h = desk_h;
                scale_w = (wallpaper_w * desk_h) / wallpaper_h;
            }
            int ox = (w - scale_w) / 2;
            int oy = (desk_h - scale_h) / 2;
            int sx_step = (wallpaper_w << 16) / scale_w;
            int sy_step = (wallpaper_h << 16) / scale_h;
            for (int dy = 0; dy < scale_h; dy++) {
                int sy_fp = dy * sy_step;
                for (int dx = 0; dx < scale_w; dx++) {
                    xgui_display_pixel(ox + dx, oy + dy, wp_sample(dx * sx_step, sy_fp));
                }
            }
        } else {
            /* WP_MODE_CENTER — original size, centered */
            xgui_display_rect_filled(0, 0, w, desk_h, desktop_color);
            int ox = (w - wallpaper_w) / 2;
            int oy = (desk_h - wallpaper_h) / 2;
            if (ox < 0) ox = 0;
            if (oy < 0) oy = 0;
            int blit_w = wallpaper_w < w ? wallpaper_w : w;
            int blit_h = wallpaper_h < desk_h ? wallpaper_h : desk_h;
            for (int row = 0; row < blit_h; row++) {
                int src_row = (wallpaper_h > desk_h) ? row + (wallpaper_h - desk_h) / 2 : row;
                if (src_row < 0 || src_row >= wallpaper_h) continue;
                int dst_y = oy + row;
                if (dst_y >= desk_h) break;
                uint32_t* src = &wallpaper_pixels[src_row * wallpaper_w];
                int src_x = (wallpaper_w > w) ? (wallpaper_w - w) / 2 : 0;
                for (int col = 0; col < blit_w; col++) {
                    xgui_display_pixel(ox + col, dst_y, src[src_x + col]);
                }
            }
        }
    } else {
        /* Solid color fill */
        xgui_display_rect_filled(0, 0, w, desk_h, desktop_color);
    }
}

/*
 * Set desktop wallpaper color (does NOT clear BMP wallpaper)
 */
void xgui_desktop_set_color(uint32_t color) {
    desktop_color = color;
}

/*
 * Clear BMP wallpaper (revert to solid color)
 */
void xgui_desktop_clear_wallpaper(void) {
    if (wallpaper_pixels) {
        kfree(wallpaper_pixels);
        wallpaper_pixels = NULL;
        wallpaper_w = 0;
        wallpaper_h = 0;
    }
}

/* BMP helper functions */
static uint16_t wp_get_le16(const uint8_t* p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}
static uint32_t wp_get_le32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static int32_t wp_get_le32s(const uint8_t* p) {
    return (int32_t)wp_get_le32(p);
}

/*
 * Set desktop wallpaper from a BMP file path
 * Returns 0 on success, -1 on failure
 */
int xgui_desktop_set_wallpaper(const char* path) {
    if (!path || !path[0]) return -1;

    char resolved[256];
    shell_resolve_path(path, resolved, sizeof(resolved));

    vfs_node_t* node = vfs_lookup(resolved);
    if (!node) {
        serial_write_string("WALLPAPER: file not found: ");
        serial_write_string(resolved);
        serial_write_string("\n");
        return -1;
    }
    if (node->length < 54) {
        serial_write_string("WALLPAPER: file too small\n");
        return -1;
    }
    /* Sanity limit: 4MB max to avoid exhausting heap */
    if (node->length > 4 * 1024 * 1024) {
        serial_write_string("WALLPAPER: file too large\n");
        return -1;
    }

    uint8_t* buf = (uint8_t*)kmalloc(node->length);
    if (!buf) {
        serial_write_string("WALLPAPER: alloc failed for file buf\n");
        return -1;
    }

    int32_t rd = vfs_read(node, 0, node->length, buf);
    if (rd < 54 || buf[0] != 'B' || buf[1] != 'M') {
        serial_write_string("WALLPAPER: not a valid BMP\n");
        kfree(buf);
        return -1;
    }

    uint16_t bpp = wp_get_le16(buf + 28);
    uint32_t comp = wp_get_le32(buf + 30);
    /* Support 24-bit (uncompressed) and 32-bit (uncompressed or BI_BITFIELDS) */
    if (bpp == 24 && comp != 0) {
        serial_write_string("WALLPAPER: unsupported 24-bit compression\n");
        kfree(buf);
        return -1;
    }
    if (bpp == 32 && comp != 0 && comp != 3) {
        serial_write_string("WALLPAPER: unsupported 32-bit compression\n");
        kfree(buf);
        return -1;
    }
    if (bpp != 24 && bpp != 32) {
        serial_write_string("WALLPAPER: unsupported bpp (need 24 or 32)\n");
        kfree(buf);
        return -1;
    }

    int32_t bmp_w = wp_get_le32s(buf + 18);
    int32_t bmp_h_raw = wp_get_le32s(buf + 22);
    int bmp_h = bmp_h_raw < 0 ? -bmp_h_raw : bmp_h_raw;
    int bottom_up = bmp_h_raw > 0;

    /* Sanity check dimensions */
    if (bmp_w <= 0 || bmp_h <= 0 || bmp_w > 4096 || bmp_h > 4096) {
        serial_write_string("WALLPAPER: invalid dimensions\n");
        kfree(buf);
        return -1;
    }

    int bytes_per_pixel = bpp / 8;
    int row_bytes = bmp_w * bytes_per_pixel;
    int pad = (4 - (row_bytes % 4)) % 4;
    int padded_row = row_bytes + pad;
    uint32_t pix_offset = wp_get_le32(buf + 10);

    /* Validate pixel data fits within file */
    if (pix_offset + (uint32_t)(padded_row * bmp_h) > (uint32_t)rd) {
        serial_write_string("WALLPAPER: pixel data truncated\n");
        kfree(buf);
        return -1;
    }

    uint8_t* pix = buf + pix_offset;

    /* Allocate pixel buffer */
    uint32_t* pixels = (uint32_t*)kmalloc(bmp_w * bmp_h * sizeof(uint32_t));
    if (!pixels) {
        serial_write_string("WALLPAPER: alloc failed for pixels\n");
        kfree(buf);
        return -1;
    }

    /* Decode BMP to XGUI_RGB pixel buffer */
    for (int y = 0; y < bmp_h; y++) {
        int src_row = bottom_up ? (bmp_h - 1 - y) : y;
        uint8_t* row = pix + src_row * padded_row;
        for (int x = 0; x < bmp_w; x++) {
            uint8_t r, g, b;
            if (bpp == 32) {
                /* BGRA layout (standard for 32-bit BMP) */
                b = row[x * 4 + 0];
                g = row[x * 4 + 1];
                r = row[x * 4 + 2];
                /* alpha at row[x * 4 + 3] is ignored */
            } else {
                b = row[x * 3 + 0];
                g = row[x * 3 + 1];
                r = row[x * 3 + 2];
            }
            pixels[y * bmp_w + x] = XGUI_RGB(r, g, b);
        }
    }

    kfree(buf);

    /* Free old wallpaper */
    if (wallpaper_pixels) kfree(wallpaper_pixels);

    wallpaper_pixels = pixels;
    wallpaper_w = bmp_w;
    wallpaper_h = bmp_h;

    serial_write_string("WALLPAPER: loaded OK\n");
    return 0;
}

void xgui_desktop_set_wp_mode(wp_mode_t mode) {
    if (mode < WP_MODE_COUNT) wallpaper_mode = mode;
}

wp_mode_t xgui_desktop_get_wp_mode(void) {
    return wallpaper_mode;
}

bool xgui_desktop_has_wallpaper(void) {
    return wallpaper_pixels != NULL && wallpaper_w > 0 && wallpaper_h > 0;
}

/*
 * Initialize the panel
 */
void xgui_panel_init(void) {
    panel_y = screen_height - XGUI_PANEL_HEIGHT;
    start_menu_open = false;
}

/*
 * Draw a 3D gradient-filled rectangle (glossy button look)
 * Top half: bright highlight fading to base color
 * Bottom half: base color fading to darker shade
 */
static void draw_gradient_button(int x, int y, int w, int h, uint32_t base, bool pressed) {
    uint32_t top_color, mid_color, bot_color;
    if (pressed) {
        top_color = darken(base, 20);
        mid_color = darken(base, 10);
        bot_color = darken(base, 30);
    } else {
        top_color = lighten(base, 50);
        mid_color = base;
        bot_color = darken(base, 35);
    }
    
    int mid = h / 2;
    /* Top half: highlight to base */
    for (int row = 0; row < mid; row++) {
        int t = (row * 255) / mid;
        uint32_t c = blend_color(top_color, mid_color, t);
        xgui_display_hline(x + 1, y + row, w - 2, c);
    }
    /* Bottom half: base to dark */
    for (int row = mid; row < h; row++) {
        int t = ((row - mid) * 255) / (h - mid);
        uint32_t c = blend_color(mid_color, bot_color, t);
        xgui_display_hline(x + 1, y + row, w - 2, c);
    }
    
    /* 1px border for definition */
    uint32_t border_col = darken(base, 50);
    xgui_display_hline(x + 1, y, w - 2, pressed ? border_col : lighten(base, 70)); /* top */
    xgui_display_hline(x + 1, y + h - 1, w - 2, border_col);                       /* bottom */
    xgui_display_vline(x, y + 1, h - 2, pressed ? border_col : lighten(base, 30));  /* left */
    xgui_display_vline(x + w - 1, y + 1, h - 2, border_col);                        /* right */
}

/*
 * Draw the start button (3D gradient with accent color)
 */
static void draw_start_button(bool pressed) {
    int x = 2;
    int y = panel_y + 4;
    int w = 60;
    int h = XGUI_PANEL_HEIGHT - 8;
    
    uint32_t accent = xgui_theme_current()->selection;
    draw_gradient_button(x, y, w, h, accent, pressed);
    
    /* "Go" text, centered, auto-contrast */
    uint32_t btn_text = is_dark_color(accent) ? XGUI_WHITE : XGUI_BLACK;
    int text_w = 2 * 8; /* "Go" = 2 chars * 8px */
    xgui_display_text_transparent(x + (w - text_w) / 2, y + (h - 16) / 2, "Go", btn_text);
}

/*
 * Draw a task button for a window (3D gradient style)
 */
static void draw_task_button(xgui_window_t* win, int x, int y, int w, int h) {
    bool focused = (win->flags & XGUI_WINDOW_FOCUSED);
    bool minimized = (win->flags & XGUI_WINDOW_MINIMIZED);
    uint32_t panel_bg = xgui_theme_current()->panel_bg;
    uint32_t accent = xgui_theme_current()->selection;
    
    if (focused) {
        /* Focused: full 3D gradient button */
        uint32_t btn_base = lighten(panel_bg, is_dark_color(panel_bg) ? 30 : -15);
        draw_gradient_button(x, y, w, h, btn_base, false);
        /* Accent bar at bottom (inside border) */
        xgui_display_rect_filled(x + 2, y + h - 4, w - 4, 3, accent);
    } else if (minimized) {
        /* Minimized: very subtle, blends with panel */
        uint32_t btn_base = panel_bg;
        uint32_t border_col = is_dark_color(panel_bg) ? lighten(panel_bg, 20) : darken(panel_bg, 20);
        xgui_display_rect_filled(x, y, w, h, btn_base);
        xgui_display_rect(x, y, w, h, border_col);
    } else {
        /* Non-focused, non-minimized: subtle 3D gradient */
        uint32_t btn_base = lighten(panel_bg, is_dark_color(panel_bg) ? 15 : -8);
        draw_gradient_button(x, y, w, h, btn_base, false);
    }
    
    /* Window title (truncated) */
    char title[16];
    strncpy(title, win->title, 15);
    title[15] = '\0';
    
    int max_chars = (w - 8) / 8;
    if (max_chars < 15 && max_chars > 3) {
        title[max_chars - 2] = '.';
        title[max_chars - 1] = '.';
        title[max_chars] = '\0';
    }
    
    uint32_t text_col = panel_text_color();
    if (minimized) text_col = blend_color(text_col, panel_bg, 100); /* Dimmed */
    xgui_display_text_transparent(x + 6, y + (h - 16) / 2, title, text_col);
}

/*
 * Draw the clock
 */
static void draw_clock(void) {
    rtc_time_t now;
    rtc_get_adjusted_time(&now);

    /* Day of week (Sakamoto's algorithm) */
    static const char* dow_names[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
    static const int sak_t[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    int dy = now.year;
    if (now.month < 3) dy--;
    int dow = (dy + dy/4 - dy/100 + dy/400 + sak_t[now.month - 1] + now.day) % 7;

    /* Date string: "Sun 2/8/26" */
    char date_str[16];
    snprintf(date_str, sizeof(date_str), "%s %d/%d/%02d",
             dow_names[dow], now.month, now.day, now.year % 100);

    /* 12-hour format with AM/PM */
    uint8_t h = now.hours;
    const char* ampm = (h < 12) ? "AM" : "PM";
    if (h == 0) h = 12;
    else if (h > 12) h -= 12;

    char time_str[12];
    time_str[0] = (h >= 10) ? '0' + (h / 10) : ' ';
    time_str[1] = '0' + (h % 10);
    time_str[2] = ':';
    time_str[3] = '0' + (now.minutes / 10);
    time_str[4] = '0' + (now.minutes % 10);
    time_str[5] = ' ';
    time_str[6] = ampm[0];
    time_str[7] = ampm[1];
    time_str[8] = '\0';

    int time_w = 72;
    int date_w = 88;
    int total_w = date_w + time_w;
    int date_x = screen_width - total_w;
    int time_x = screen_width - time_w + 4;
    int text_y = panel_y + (XGUI_PANEL_HEIGHT - 16) / 2;

    uint32_t panel_bg = xgui_theme_current()->panel_bg;
    uint32_t text_col = panel_text_color();
    uint32_t sep_col = is_dark_color(panel_bg) ? lighten(panel_bg, 30) : darken(panel_bg, 30);

    /* Subtle left separator */
    xgui_display_vline(date_x - 2, panel_y + 6, XGUI_PANEL_HEIGHT - 12, sep_col);

    /* Thin separator between date and time */
    xgui_display_vline(date_x + date_w, panel_y + 8, XGUI_PANEL_HEIGHT - 16, sep_col);

    xgui_display_text_transparent(date_x + 4, text_y, date_str, text_col);
    xgui_display_text_transparent(time_x, text_y, time_str, text_col);
}

/*
 * Draw the panel
 */
void xgui_panel_draw(void) {
    uint32_t panel_bg = xgui_theme_current()->panel_bg;
    
    /* Gradient panel background: lighter at top, darker at bottom */
    for (int row = 0; row < XGUI_PANEL_HEIGHT; row++) {
        int t = (row * 255) / XGUI_PANEL_HEIGHT;
        uint32_t row_color = blend_color(lighten(panel_bg, 15), darken(panel_bg, 15), t);
        xgui_display_hline(0, panel_y + row, screen_width, row_color);
    }
    
    /* Thin highlight line at top edge */
    xgui_display_hline(0, panel_y, screen_width,
                       is_dark_color(panel_bg) ? lighten(panel_bg, 40) : lighten(panel_bg, 30));
    
    /* Subtle shadow above the panel (drawn into desktop area) */
    for (int s = 0; s < 4; s++) {
        int alpha = 40 - s * 10;
        if (alpha < 5) alpha = 5;
        for (int col = 0; col < screen_width; col++) {
            uint32_t bg = xgui_display_get_pixel(col, panel_y - 1 - s);
            xgui_display_pixel(col, panel_y - 1 - s, blend_color(bg, XGUI_BLACK, alpha));
        }
    }
    
    /* Draw start button */
    draw_start_button(start_menu_open);
    
    /* Draw task buttons for each window */
    int task_x = 68;
    int task_y = panel_y + 4;
    int task_h = XGUI_PANEL_HEIGHT - 8;
    int task_w = XGUI_PANEL_BUTTON_WIDTH;
    int max_tasks = (screen_width - 120 - 60) / (task_w + 2);
    
    int task_count = 0;
    /* Non-minimized visible windows */
    for (xgui_window_t* win = xgui_wm_get_top(); win && task_count < max_tasks; win = win->prev) {
        if ((win->flags & XGUI_WINDOW_VISIBLE) && !(win->flags & XGUI_WINDOW_MINIMIZED)) {
            draw_task_button(win, task_x, task_y, task_w, task_h);
            task_x += task_w + 2;
            task_count++;
        }
    }
    /* Minimized windows (shown with raised/unfocused style) */
    for (xgui_window_t* win = xgui_wm_get_top(); win && task_count < max_tasks; win = win->prev) {
        if ((win->flags & XGUI_WINDOW_MINIMIZED) && (win->flags & XGUI_WINDOW_VISIBLE)) {
            draw_task_button(win, task_x, task_y, task_w, task_h);
            task_x += task_w + 2;
            task_count++;
        }
    }
    
    /* Draw clock */
    draw_clock();
    
    /* Mark panel area as dirty */
    xgui_display_mark_dirty(panel_y, screen_height);
}

/* Forward declarations */
extern void xgui_calculator_create(void);
extern void xgui_about_create(void);
extern void xgui_notepad_create(void);
extern void xgui_paint_create(void);
extern void xgui_explorer_create(void);
extern void xgui_terminal_create(void);
extern void xgui_controlpanel_create(void);
extern void xgui_gui_editor_create(void);
extern void xgui_gui_spreadsheet_create(void);
extern void xgui_diskutil_create(void);
extern void xgui_skigame_create(void);
extern void xgui_flappycat_create(void);
extern void xgui_clock_settings_create(void);
extern void xgui_quit(void);

/*
 * Draw a hover-highlighted menu item row with gradient
 */
static void draw_menu_highlight(int x, int y, int w, int h, uint32_t accent) {
    for (int row = 0; row < h; row++) {
        int t = (row * 255) / h;
        uint32_t c = blend_color(lighten(accent, 30), darken(accent, 5), t);
        xgui_display_hline(x, y + row, w, c);
    }
    /* Subtle top highlight */
    xgui_display_hline(x, y, w, lighten(accent, 50));
}

/*
 * Draw the start menu — XP-style two-column layout
 */
void xgui_draw_start_menu(void) {
    if (!start_menu_open) {
        menu_hover_item = -1;
        return;
    }

    uint32_t accent = xgui_theme_current()->selection;
    uint32_t menu_bg = xgui_theme_current()->window_bg;
    bool dark = is_dark_color(menu_bg);
    uint32_t hover_text = XGUI_WHITE;
    uint32_t hover_bg = is_dark_color(accent) ? lighten(accent, 15) : accent;

    /* Right column background: slightly tinted */
    uint32_t right_bg = dark ? lighten(menu_bg, 12) : darken(menu_bg, 12);

    /* Compute body height: max of left/right item counts */
    int left_body = LEFT_COUNT * SMENU_ITEM_H;
    int right_body = RIGHT_COUNT * SMENU_ITEM_H;
    int body_h = left_body > right_body ? left_body : right_body;
    body_h += SMENU_PAD * 2; /* top/bottom padding in body */

    int menu_h = SMENU_BANNER_H + body_h + SMENU_BOTTOM_H;
    int menu_x = 2;
    int menu_y = panel_y - menu_h;

    /* Get mouse position */
    int mx, my;
    mouse_get_position(&mx, &my);

    /* --- Hover detection --- */
    menu_hover_item = -1;

    int body_top = menu_y + SMENU_BANNER_H;
    int body_bot = body_top + body_h;
    int left_x0 = menu_x;
    int right_x0 = menu_x + SMENU_LEFT_W;

    /* Left column hover */
    if (mx >= left_x0 && mx < left_x0 + SMENU_LEFT_W &&
        my >= body_top + SMENU_PAD && my < body_top + SMENU_PAD + LEFT_COUNT * SMENU_ITEM_H) {
        int idx = (my - body_top - SMENU_PAD) / SMENU_ITEM_H;
        if (idx >= 0 && idx < LEFT_COUNT) menu_hover_item = idx;
    }

    /* Right column hover (offset by 16px for "System" header) */
    int right_item_top = body_top + SMENU_PAD + 16;
    if (mx >= right_x0 && mx < right_x0 + SMENU_RIGHT_W &&
        my >= right_item_top && my < right_item_top + RIGHT_COUNT * SMENU_ITEM_H) {
        int idx = (my - right_item_top) / SMENU_ITEM_H;
        if (idx >= 0 && idx < RIGHT_COUNT) menu_hover_item = RIGHT_BASE + idx;
    }

    /* Bottom bar: small Shutdown button hover (right-aligned) */
    int bot_y = body_bot;
    int shut_btn_w = 76;
    int shut_btn_h = 22;
    int shut_btn_x = menu_x + SMENU_W - shut_btn_w - 6;
    int shut_btn_y = bot_y + (SMENU_BOTTOM_H - shut_btn_h) / 2;
    if (mx >= shut_btn_x && mx < shut_btn_x + shut_btn_w &&
        my >= shut_btn_y && my < shut_btn_y + shut_btn_h) {
        menu_hover_item = MENU_SHUTDOWN_IDX;
    }

    /* === DRAW === */

    /* --- Top banner: accent gradient with "MiniOS" --- */
    for (int row = 0; row < SMENU_BANNER_H; row++) {
        int t = (row * 255) / SMENU_BANNER_H;
        uint32_t c = blend_color(lighten(accent, 25), darken(accent, 15), t);
        xgui_display_hline(menu_x + 1, menu_y + row, SMENU_W - 2, c);
    }
    /* Banner highlight */
    xgui_display_hline(menu_x + 1, menu_y + 1, SMENU_W - 2, lighten(accent, 50));
    /* Banner text */
    uint32_t banner_text = is_dark_color(accent) ? XGUI_WHITE : XGUI_BLACK;
    xgui_display_text_transparent(menu_x + 12, menu_y + 8, "MiniOS", banner_text);

    /* --- Left column: white/light background --- */
    for (int row = 0; row < body_h; row++) {
        int t = (row * 40) / body_h;
        uint32_t c = dark ? blend_color(menu_bg, lighten(menu_bg, 8), t)
                          : blend_color(XGUI_WHITE, XGUI_RGB(245, 245, 245), t);
        xgui_display_hline(menu_x + 1, body_top + row, SMENU_LEFT_W - 1, c);
    }

    /* --- Right column: tinted background --- */
    for (int row = 0; row < body_h; row++) {
        int t = (row * 40) / body_h;
        uint32_t c = blend_color(right_bg, darken(right_bg, 5), t);
        xgui_display_hline(right_x0, body_top + row, SMENU_RIGHT_W - 1, c);
    }

    /* Vertical divider between columns (etched line) */
    uint32_t div_dark = dark ? lighten(menu_bg, 20) : darken(menu_bg, 30);
    uint32_t div_light = dark ? lighten(menu_bg, 40) : lighten(menu_bg, 10);
    xgui_display_vline(right_x0 - 1, body_top + 4, body_h - 8, div_dark);
    xgui_display_vline(right_x0, body_top + 4, body_h - 8, div_light);

    /* --- Left column items --- */
    uint32_t left_text = dark ? XGUI_WHITE : XGUI_RGB(20, 20, 20);
    for (int i = 0; i < LEFT_COUNT; i++) {
        int iy = body_top + SMENU_PAD + i * SMENU_ITEM_H;
        if (menu_hover_item == i) {
            draw_menu_highlight(menu_x + 2, iy, SMENU_LEFT_W - 4, SMENU_ITEM_H, hover_bg);
            xgui_display_text_transparent(menu_x + 12, iy + 4, left_labels[i], hover_text);
        } else {
            xgui_display_text_transparent(menu_x + 12, iy + 4, left_labels[i], left_text);
        }
    }

    /* Separator line in left column after main apps (before games) */
    int sep_after = 8; /* After "Clock" (index 8), before "Ski Game" */
    int sep_ly = body_top + SMENU_PAD + sep_after * SMENU_ITEM_H - 1;
    uint32_t lsep = dark ? lighten(menu_bg, 20) : XGUI_RGB(210, 210, 210);
    xgui_display_hline(menu_x + 10, sep_ly, SMENU_LEFT_W - 20, lsep);

    /* --- Right column items --- */
    /* Section header */
    uint32_t hdr_col = dark ? XGUI_RGB(180, 200, 255) : XGUI_RGB(60, 80, 140);
    xgui_display_text_transparent(right_x0 + 8, body_top + SMENU_PAD - 1, "System", hdr_col);
    int right_item_start = body_top + SMENU_PAD + 16;

    for (int i = 0; i < RIGHT_COUNT; i++) {
        int iy = right_item_start + i * SMENU_ITEM_H;
        if (menu_hover_item == RIGHT_BASE + i) {
            draw_menu_highlight(right_x0 + 1, iy, SMENU_RIGHT_W - 3, SMENU_ITEM_H, hover_bg);
            xgui_display_text_transparent(right_x0 + 10, iy + 4, right_labels[i], hover_text);
        } else {
            uint32_t rt = dark ? XGUI_RGB(220, 220, 220) : XGUI_RGB(40, 40, 40);
            xgui_display_text_transparent(right_x0 + 10, iy + 4, right_labels[i], rt);
        }
    }

    /* --- Bottom bar: neutral background --- */
    for (int row = 0; row < SMENU_BOTTOM_H; row++) {
        int t = (row * 255) / SMENU_BOTTOM_H;
        uint32_t c = blend_color(darken(accent, 10), darken(accent, 30), t);
        xgui_display_hline(menu_x + 1, bot_y + row, SMENU_W - 2, c);
    }
    xgui_display_hline(menu_x + 1, bot_y, SMENU_W - 2, lighten(accent, 20));

    /* Small red Shutdown button (right-aligned in bottom bar) */
    {
        uint32_t btn_base = XGUI_RGB(180, 30, 30);
        uint32_t btn_hover = XGUI_RGB(220, 50, 50);
        uint32_t btn_color = (menu_hover_item == MENU_SHUTDOWN_IDX) ? btn_hover : btn_base;

        /* Button body gradient */
        for (int row = 0; row < shut_btn_h; row++) {
            int t = (row * 60) / shut_btn_h;
            uint32_t c = darken(btn_color, t);
            xgui_display_hline(shut_btn_x, shut_btn_y + row, shut_btn_w, c);
        }
        /* Top highlight */
        xgui_display_hline(shut_btn_x, shut_btn_y, shut_btn_w, lighten(btn_color, 40));
        /* 3D border */
        xgui_display_hline(shut_btn_x, shut_btn_y + shut_btn_h - 1, shut_btn_w, darken(btn_color, 60));
        xgui_display_vline(shut_btn_x, shut_btn_y, shut_btn_h, lighten(btn_color, 30));
        xgui_display_vline(shut_btn_x + shut_btn_w - 1, shut_btn_y, shut_btn_h, darken(btn_color, 50));

        /* Centered text */
        int tw = xgui_display_text_width("Shutdown");
        int tx = shut_btn_x + (shut_btn_w - tw) / 2;
        int ty = shut_btn_y + (shut_btn_h - 12) / 2;
        xgui_display_text_transparent(tx, ty, "Shutdown", XGUI_WHITE);
    }

    /* --- Outer border (beveled) --- */
    uint32_t brd_light = dark ? lighten(menu_bg, 40) : darken(menu_bg, 15);
    uint32_t brd_dark = dark ? lighten(menu_bg, 15) : darken(menu_bg, 50);
    xgui_display_hline(menu_x, menu_y, SMENU_W, brd_light);
    xgui_display_vline(menu_x, menu_y, menu_h, brd_light);
    xgui_display_hline(menu_x, menu_y + menu_h - 1, SMENU_W, brd_dark);
    xgui_display_vline(menu_x + SMENU_W - 1, menu_y, menu_h, brd_dark);

    /* Drop shadow */
    draw_drop_shadow(menu_x, menu_y, SMENU_W, menu_h, 6);

    /* Mark dirty */
    xgui_display_mark_dirty(menu_y, panel_y);
}

/*
 * Handle panel click
 */
bool xgui_panel_click(int x, int y) {
    /* Check if click is in start menu area */
    if (start_menu_open) {
        int left_body = LEFT_COUNT * SMENU_ITEM_H;
        int right_body = RIGHT_COUNT * SMENU_ITEM_H;
        int body_h = (left_body > right_body ? left_body : right_body) + SMENU_PAD * 2;
        int menu_h = SMENU_BANNER_H + body_h + SMENU_BOTTOM_H;
        int menu_x = 2;
        int menu_y = panel_y - menu_h;

        /* Check if click is inside the menu */
        if (x >= menu_x && x < menu_x + SMENU_W && y >= menu_y && y < menu_y + menu_h) {
            int item = menu_hover_item;
            if (item < 0) return true; /* Click on empty area, keep open */

            start_menu_open = false;

            /* Left column: Programs */
            switch (item) {
                case 0: xgui_terminal_create(); break;
                case 1: xgui_explorer_create(); break;
                case 2: xgui_gui_editor_create(); break;
                case 3: xgui_gui_spreadsheet_create(); break;
                case 4: xgui_calculator_create(); break;
                case 5: xgui_paint_create(); break;
                case 6: xgui_notepad_create(); break;
                case 7: xgui_calendar_create(); break;
                case 8: xgui_analogclock_create(); break;
                case 9: xgui_skigame_create(); break;
                case 10: xgui_flappycat_create(); break;
                default: break;
            }

            /* Right column: System */
            if (item == RIGHT_BASE + 0) xgui_controlpanel_create();
            if (item == RIGHT_BASE + 1) xgui_taskmgr_create();
            if (item == RIGHT_BASE + 2) xgui_diskutil_create();
            if (item == RIGHT_BASE + 3) xgui_about_create();
            if (item == RIGHT_BASE + 4) xgui_clock_settings_create();

            /* Shutdown */
            if (item == MENU_SHUTDOWN_IDX) xgui_shutdown();

            return true;
        }

        /* Click outside menu — close */
        if (y < panel_y) {
            start_menu_open = false;
            return true;
        }
    }
    
    /* Check if click is in panel area */
    if (y < panel_y) {
        if (start_menu_open) {
            start_menu_open = false;
            return true;
        }
        return false;
    }
    
    /* Check start button */
    if (x >= 2 && x < 62 && y >= panel_y + 2 && y < panel_y + XGUI_PANEL_HEIGHT - 2) {
        start_menu_open = !start_menu_open;
        return true;
    }
    
    /* Check date/time area click */
    {
        int time_w = 72;
        int date_w = 88;
        int total_w = date_w + time_w;
        int date_x = screen_width - total_w;
        int time_x = screen_width - time_w;

        if (x >= date_x && x < screen_width && y >= panel_y + 2 && y < panel_y + XGUI_PANEL_HEIGHT - 2) {
            if (x < time_x) {
                /* Date area clicked — toggle calendar */
                xgui_calendar_toggle();
            } else {
                /* Time area clicked — open clock settings */
                xgui_clock_settings_create();
            }
            return true;
        }
    }

    /* Check task buttons */
    int task_x = 68;
    int task_w = XGUI_PANEL_BUTTON_WIDTH;
    int max_tasks = (screen_width - 120 - 60) / (task_w + 2);
    
    int task_count = 0;
    /* Non-minimized windows first */
    for (xgui_window_t* win = xgui_wm_get_top(); win && task_count < max_tasks; win = win->prev) {
        if ((win->flags & XGUI_WINDOW_VISIBLE) && !(win->flags & XGUI_WINDOW_MINIMIZED)) {
            if (x >= task_x && x < task_x + task_w) {
                if (win->flags & XGUI_WINDOW_FOCUSED) {
                    /* Clicking focused window minimizes it */
                    xgui_window_minimize(win);
                } else {
                    xgui_window_focus(win);
                }
                return true;
            }
            task_x += task_w + 2;
            task_count++;
        }
    }
    /* Minimized windows */
    for (xgui_window_t* win = xgui_wm_get_top(); win && task_count < max_tasks; win = win->prev) {
        if ((win->flags & XGUI_WINDOW_MINIMIZED) && (win->flags & XGUI_WINDOW_VISIBLE)) {
            if (x >= task_x && x < task_x + task_w) {
                xgui_window_restore(win);
                return true;
            }
            task_x += task_w + 2;
            task_count++;
        }
    }
    
    return true;  /* Click was in panel, consume it */
}

/*
 * Update panel
 */
void xgui_panel_update(void) {
    /* Redraw clock area */
    draw_clock();
}

/*
 * Get the usable desktop area
 */
void xgui_desktop_get_work_area(int* x, int* y, int* width, int* height) {
    if (x) *x = 0;
    if (y) *y = 0;
    if (width) *width = screen_width;
    if (height) *height = screen_height - XGUI_PANEL_HEIGHT;
}
