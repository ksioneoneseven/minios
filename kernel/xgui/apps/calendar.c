/*
 * MiniOS XGUI Calendar
 *
 * A monthly calendar widget with event storage.
 * Events are persisted in /mnt/conf/calendar.conf with keys like "20260208".
 */

#include "xgui/xgui.h"
#include "xgui/wm.h"
#include "xgui/display.h"
#include "rtc.h"
#include "conf.h"
#include "string.h"
#include "stdio.h"
#include "keyboard.h"

/* Calendar configuration */
#define CAL_CELL_W      28
#define CAL_CELL_H      18
#define CAL_HEADER_H    30
#define CAL_DOW_H       18
#define CAL_PAD         6
#define CAL_EVENT_H     100
#define CAL_EVENT_MAX   120

/* Colors */
#define CAL_BG           XGUI_WHITE
#define CAL_HEADER_BG    XGUI_RGB(0, 100, 180)
#define CAL_HEADER_FG    XGUI_WHITE
#define CAL_DOW_FG       XGUI_RGB(100, 100, 100)
#define CAL_DAY_FG       XGUI_BLACK
#define CAL_TODAY_BG     XGUI_RGB(0, 100, 180)
#define CAL_TODAY_FG     XGUI_WHITE
#define CAL_SEL_BG       XGUI_RGB(180, 210, 255)
#define CAL_HAS_EVT      XGUI_RGB(220, 100, 100)
#define CAL_GRID_CLR     XGUI_RGB(220, 220, 220)
#define CAL_NAV_FG       XGUI_WHITE
#define CAL_EVT_BG       XGUI_RGB(245, 245, 245)
#define CAL_EVT_BORDER   XGUI_RGB(180, 180, 180)
#define CAL_EVT_TITLE_BG XGUI_RGB(0, 100, 180)
#define CAL_EVT_TITLE_FG XGUI_WHITE
#define CAL_INPUT_BG     XGUI_WHITE
#define CAL_INPUT_FG     XGUI_BLACK

/* State */
static xgui_window_t* cal_window = NULL;
static int cal_month = 1;   /* 1-12 */
static int cal_year = 2026;
static int cal_sel_day = 0; /* 0 = no selection */

/* Event input mode */
static int cal_input_mode = 0;  /* 0=view, 1=adding event */
static char cal_input_buf[CAL_EVENT_MAX];
static int cal_input_pos = 0;

/* Config database */
static conf_section_t cal_conf;
static int cal_conf_loaded = 0;

/*
 * Days in a given month (handles leap years)
 */
static int days_in_month(int month, int year) {
    static const int dim[] = { 0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (month == 2) {
        if ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0))
            return 29;
    }
    return dim[month];
}

/*
 * Day of week for a given date (0=Sunday, 6=Saturday)
 * Uses Tomohiko Sakamoto's algorithm
 */
static int day_of_week(int y, int m, int d) {
    static const int t[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    if (m < 3) y--;
    return (y + y/4 - y/100 + y/400 + t[m - 1] + d) % 7;
}

/*
 * Month names
 */
static const char* month_names[] = {
    "", "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December"
};

/*
 * Build a date key string like "20260208"
 */
static void make_date_key(char* buf, int year, int month, int day) {
    snprintf(buf, 16, "%04d%02d%02d", year, month, day);
}

/*
 * Load the calendar config
 */
static void cal_load_conf(void) {
    if (!cal_conf_loaded) {
        conf_load(&cal_conf, "calendar");
        cal_conf_loaded = 1;
    }
}

/*
 * Get event text for a date (returns "" if none)
 */
static const char* cal_get_event(int year, int month, int day) {
    char key[16];
    make_date_key(key, year, month, day);
    return conf_get(&cal_conf, key, "");
}

/*
 * Check if a date has an event
 */
static int cal_has_event(int year, int month, int day) {
    const char* ev = cal_get_event(year, month, day);
    return (ev && ev[0]);
}

/*
 * Save an event for a date
 */
static void cal_save_event(int year, int month, int day, const char* text) {
    char key[16];
    make_date_key(key, year, month, day);
    if (text && text[0]) {
        conf_set(&cal_conf, key, text);
    } else {
        conf_set(&cal_conf, key, "");
    }
    conf_save(&cal_conf);
}

/*
 * Compute grid_x for centering the 7-column grid
 */
static int cal_grid_x(int cw) {
    return (cw - 7 * CAL_CELL_W) / 2;
}

/*
 * Compute grid_y (top of day cells)
 */
static int cal_grid_y(void) {
    return CAL_HEADER_H + 2 + CAL_DOW_H;
}

/*
 * Paint callback
 */
static void cal_paint(xgui_window_t* win) {
    int cw = win->client_width;
    int ch = win->client_height;

    /* Get current date for highlighting today */
    rtc_time_t now;
    rtc_get_adjusted_time(&now);

    /* Header with month/year and nav arrows */
    xgui_win_rect_filled(win, 0, 0, cw, CAL_HEADER_H, CAL_HEADER_BG);

    /* Left arrow < */
    xgui_win_text_transparent(win, CAL_PAD + 2, 8, "<", CAL_NAV_FG);

    /* Right arrow > */
    xgui_win_text_transparent(win, cw - CAL_PAD - 8, 8, ">", CAL_NAV_FG);

    /* Month Year centered */
    char title[32];
    snprintf(title, sizeof(title), "%s %d", month_names[cal_month], cal_year);
    int title_w = (int)strlen(title) * 8;
    xgui_win_text_transparent(win, (cw - title_w) / 2, 8, title, CAL_HEADER_FG);

    /* Day-of-week headers */
    int grid_x = cal_grid_x(cw);
    int dow_y = CAL_HEADER_H + 2;
    static const char* dow[] = { "Su", "Mo", "Tu", "We", "Th", "Fr", "Sa" };
    for (int i = 0; i < 7; i++) {
        int x = grid_x + i * CAL_CELL_W + (CAL_CELL_W - 16) / 2;
        xgui_win_text_transparent(win, x, dow_y, dow[i], CAL_DOW_FG);
    }

    /* Separator */
    xgui_win_hline(win, grid_x, dow_y + CAL_DOW_H - 2, 7 * CAL_CELL_W, CAL_GRID_CLR);

    /* Day grid */
    int first_dow = day_of_week(cal_year, cal_month, 1);
    int num_days = days_in_month(cal_month, cal_year);
    int gy = cal_grid_y();

    int row = 0, col = first_dow;
    for (int d = 1; d <= num_days; d++) {
        int cx = grid_x + col * CAL_CELL_W;
        int cy = gy + row * CAL_CELL_H;

        bool is_today = (d == now.day && cal_month == now.month && cal_year == now.year);
        bool is_selected = (d == cal_sel_day);
        bool has_evt = cal_has_event(cal_year, cal_month, d);

        /* Background: selected > today > normal */
        if (is_selected) {
            xgui_win_rect_filled(win, cx + 1, cy, CAL_CELL_W - 2, CAL_CELL_H - 1, CAL_SEL_BG);
        } else if (is_today) {
            xgui_win_rect_filled(win, cx + 1, cy, CAL_CELL_W - 2, CAL_CELL_H - 1, CAL_TODAY_BG);
        }

        /* Day number */
        char dstr[4];
        snprintf(dstr, sizeof(dstr), "%2d", d);
        int tx = cx + (CAL_CELL_W - 16) / 2;
        uint32_t fg = is_today && !is_selected ? CAL_TODAY_FG : CAL_DAY_FG;
        xgui_win_text_transparent(win, tx, cy + 2, dstr, fg);

        /* Event dot indicator */
        if (has_evt) {
            int dot_x = cx + CAL_CELL_W - 6;
            int dot_y = cy + CAL_CELL_H - 6;
            xgui_win_rect_filled(win, dot_x, dot_y, 3, 3, CAL_HAS_EVT);
        }

        col++;
        if (col >= 7) {
            col = 0;
            row++;
        }
    }

    /* Grid lines */
    for (int r = 0; r <= 6; r++) {
        int y = gy + r * CAL_CELL_H;
        xgui_win_hline(win, grid_x, y, 7 * CAL_CELL_W, CAL_GRID_CLR);
    }
    for (int c = 0; c <= 7; c++) {
        int x = grid_x + c * CAL_CELL_W;
        xgui_win_vline(win, x, gy, 6 * CAL_CELL_H, CAL_GRID_CLR);
    }

    /* Event panel below the grid */
    int panel_top = gy + 6 * CAL_CELL_H + 4;
    int panel_h = ch - panel_top;
    if (panel_h < 20) return;

    xgui_win_rect_filled(win, 0, panel_top, cw, panel_h, CAL_EVT_BG);
    xgui_win_hline(win, 0, panel_top, cw, CAL_EVT_BORDER);

    if (cal_sel_day > 0) {
        /* Title bar for selected date */
        char sel_title[48];
        snprintf(sel_title, sizeof(sel_title), "%s %d, %d",
                 month_names[cal_month], cal_sel_day, cal_year);
        xgui_win_rect_filled(win, 0, panel_top + 1, cw, 16, CAL_EVT_TITLE_BG);
        xgui_win_text_transparent(win, 4, panel_top + 3, sel_title, CAL_EVT_TITLE_FG);

        /* "Add Event" prompt on the right */
        if (!cal_input_mode) {
            xgui_win_text_transparent(win, cw - 80, panel_top + 3, "[+ Add]", CAL_EVT_TITLE_FG);
        }

        int text_y = panel_top + 20;

        if (cal_input_mode) {
            /* Input field */
            xgui_win_text_transparent(win, 4, text_y, "New event:", XGUI_RGB(80, 80, 80));
            text_y += 14;
            xgui_win_rect_filled(win, 4, text_y, cw - 8, 16, CAL_INPUT_BG);
            xgui_win_rect(win, 4, text_y, cw - 8, 16, CAL_EVT_BORDER);
            xgui_win_text_transparent(win, 6, text_y + 2, cal_input_buf, CAL_INPUT_FG);
            /* Cursor */
            int cur_x = 6 + cal_input_pos * 8;
            if (cur_x < cw - 10) {
                xgui_win_rect_filled(win, cur_x, text_y + 2, 2, 12, CAL_INPUT_FG);
            }
            text_y += 20;
            xgui_win_text_transparent(win, 4, text_y, "Enter=Save  Esc=Cancel", XGUI_RGB(120, 120, 120));
        } else {
            /* Show existing event */
            const char* ev = cal_get_event(cal_year, cal_month, cal_sel_day);
            if (ev && ev[0]) {
                /* Wrap long event text */
                int max_chars = (cw - 8) / 8;
                if (max_chars < 1) max_chars = 1;
                int len = (int)strlen(ev);
                int off = 0;
                while (off < len && text_y + 14 < ch) {
                    char line[64];
                    int chunk = len - off;
                    if (chunk > max_chars) chunk = max_chars;
                    if (chunk > 63) chunk = 63;
                    memcpy(line, &ev[off], chunk);
                    line[chunk] = '\0';
                    xgui_win_text_transparent(win, 4, text_y, line, CAL_DAY_FG);
                    text_y += 14;
                    off += chunk;
                }
            } else {
                xgui_win_text_transparent(win, 4, text_y, "No events", XGUI_RGB(150, 150, 150));
            }
        }
    } else {
        xgui_win_text_transparent(win, 4, panel_top + 6, "Click a date to view events", XGUI_RGB(150, 150, 150));
    }
}

/*
 * Hit-test: which day was clicked? Returns 0 if none.
 */
static int cal_hit_day(int mx, int my, int cw) {
    int grid_x = cal_grid_x(cw);
    int gy = cal_grid_y();
    int first_dow = day_of_week(cal_year, cal_month, 1);
    int num_days = days_in_month(cal_month, cal_year);

    if (mx < grid_x || mx >= grid_x + 7 * CAL_CELL_W) return 0;
    if (my < gy || my >= gy + 6 * CAL_CELL_H) return 0;

    int col = (mx - grid_x) / CAL_CELL_W;
    int row = (my - gy) / CAL_CELL_H;
    int cell = row * 7 + col;
    int day = cell - first_dow + 1;

    if (day < 1 || day > num_days) return 0;
    return day;
}

/*
 * Event handler
 */
static void cal_handler(xgui_window_t* win, xgui_event_t* event) {
    if (event->type == XGUI_EVENT_WINDOW_CLOSE) {
        xgui_window_destroy(win);
        cal_window = NULL;
        cal_input_mode = 0;
        return;
    }

    /* Input mode: handle typing */
    if (cal_input_mode) {
        if (event->type == XGUI_EVENT_KEY_CHAR) {
            char c = event->key.character;
            if (c >= 32 && c < 127 && cal_input_pos < CAL_EVENT_MAX - 1) {
                cal_input_buf[cal_input_pos++] = c;
                cal_input_buf[cal_input_pos] = '\0';
                win->dirty = true;
            }
            return;
        }
        if (event->type == XGUI_EVENT_KEY_DOWN) {
            uint8_t key = event->key.keycode;
            if (key == '\n' || key == '\r') {
                /* Save event */
                if (cal_input_pos > 0 && cal_sel_day > 0) {
                    const char* existing = cal_get_event(cal_year, cal_month, cal_sel_day);
                    if (existing && existing[0]) {
                        /* Append to existing with "; " separator */
                        char combined[CAL_EVENT_MAX];
                        snprintf(combined, sizeof(combined), "%s; %s", existing, cal_input_buf);
                        cal_save_event(cal_year, cal_month, cal_sel_day, combined);
                    } else {
                        cal_save_event(cal_year, cal_month, cal_sel_day, cal_input_buf);
                    }
                }
                cal_input_mode = 0;
                win->dirty = true;
            } else if (key == KEY_ESCAPE) {
                cal_input_mode = 0;
                win->dirty = true;
            } else if (key == '\b') {
                if (cal_input_pos > 0) {
                    cal_input_pos--;
                    cal_input_buf[cal_input_pos] = '\0';
                    win->dirty = true;
                }
            }
        }
        return;
    }

    if (event->type == XGUI_EVENT_MOUSE_DOWN) {
        int mx = event->mouse.x;
        int my = event->mouse.y;
        int cw = win->client_width;

        /* Click in header area for navigation */
        if (my < CAL_HEADER_H) {
            if (mx < 30) {
                /* Previous month */
                cal_month--;
                if (cal_month < 1) {
                    cal_month = 12;
                    cal_year--;
                }
                cal_sel_day = 0;
                win->dirty = true;
            } else if (mx > cw - 30) {
                /* Next month */
                cal_month++;
                if (cal_month > 12) {
                    cal_month = 1;
                    cal_year++;
                }
                cal_sel_day = 0;
                win->dirty = true;
            }
            return;
        }

        /* Click on day cell */
        int day = cal_hit_day(mx, my, cw);
        if (day > 0) {
            cal_sel_day = day;
            cal_input_mode = 0;
            win->dirty = true;
            return;
        }

        /* Click on [+ Add] button in event panel title */
        if (cal_sel_day > 0) {
            int panel_top = cal_grid_y() + 6 * CAL_CELL_H + 4;
            if (my >= panel_top + 1 && my < panel_top + 17 && mx >= cw - 80) {
                cal_input_mode = 1;
                cal_input_buf[0] = '\0';
                cal_input_pos = 0;
                win->dirty = true;
                return;
            }
        }
    }
}

/*
 * Create the calendar window
 */
void xgui_calendar_toggle(void) {
    if (cal_window) {
        xgui_window_destroy(cal_window);
        cal_window = NULL;
        cal_input_mode = 0;
        return;
    }
    xgui_calendar_create();
}

void xgui_calendar_create(void) {
    if (cal_window) {
        xgui_window_focus(cal_window);
        return;
    }

    /* Load event database */
    cal_load_conf();

    /* Initialize to current month */
    rtc_time_t now;
    rtc_get_adjusted_time(&now);
    cal_month = now.month;
    cal_year = now.year;
    cal_sel_day = now.day;
    cal_input_mode = 0;

    int win_w = 7 * CAL_CELL_W + 20;
    int win_h = CAL_HEADER_H + CAL_DOW_H + 6 * CAL_CELL_H + CAL_EVENT_H + 14;

    int sw = xgui_display_width();
    int sh = xgui_display_height();
    int panel_h = 26;
    int wx = sw - win_w - 4;
    int wy = sh - panel_h - win_h - 4;

    cal_window = xgui_window_create("Calendar", wx, wy, win_w, win_h,
                                     XGUI_WINDOW_DEFAULT);
    if (!cal_window) return;

    cal_window->bg_color = CAL_BG;
    xgui_window_set_paint(cal_window, cal_paint);
    xgui_window_set_handler(cal_window, cal_handler);
}
