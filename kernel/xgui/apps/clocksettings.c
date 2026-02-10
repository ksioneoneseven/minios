/*
 * MiniOS XGUI Clock Settings
 *
 * Allows the user to set the timezone offset and manually adjust the time.
 * Opened by clicking the clock in the panel.
 */

#include "xgui/xgui.h"
#include "xgui/wm.h"
#include "xgui/widget.h"
#include "rtc.h"
#include "string.h"
#include "stdio.h"

/* Window */
static xgui_window_t* clock_win = NULL;

/* Widgets */
static xgui_widget_t* tz_field = NULL;
static xgui_widget_t* hr_field = NULL;
static xgui_widget_t* min_field = NULL;
static xgui_widget_t* sec_field = NULL;

/* Common timezone table: name, offset in minutes from UTC */
typedef struct {
    const char* label;
    int offset;
} tz_entry_t;

static const tz_entry_t timezones[] = {
    { "UTC-12  Baker Island",    -720 },
    { "UTC-11  Samoa",           -660 },
    { "UTC-10  Hawaii",          -600 },
    { "UTC-9   Alaska",          -540 },
    { "UTC-8   Pacific (PST)",   -480 },
    { "UTC-7   Mountain (MST)",  -420 },
    { "UTC-6   Central (CST)",   -360 },
    { "UTC-5   Eastern (EST)",   -300 },
    { "UTC-4   Atlantic",        -240 },
    { "UTC-3   Buenos Aires",    -180 },
    { "UTC-2   Mid-Atlantic",    -120 },
    { "UTC-1   Azores",           -60 },
    { "UTC+0   London (GMT)",       0 },
    { "UTC+1   Paris (CET)",       60 },
    { "UTC+2   Cairo (EET)",      120 },
    { "UTC+3   Moscow",           180 },
    { "UTC+3:30 Tehran",          210 },
    { "UTC+4   Dubai",            240 },
    { "UTC+4:30 Kabul",           270 },
    { "UTC+5   Karachi",          300 },
    { "UTC+5:30 Mumbai (IST)",    330 },
    { "UTC+5:45 Kathmandu",       345 },
    { "UTC+6   Dhaka",            360 },
    { "UTC+6:30 Yangon",          390 },
    { "UTC+7   Bangkok",          420 },
    { "UTC+8   Beijing/Perth",    480 },
    { "UTC+9   Tokyo",            540 },
    { "UTC+9:30 Adelaide",        570 },
    { "UTC+10  Sydney (AEST)",    600 },
    { "UTC+11  Solomon Is.",      660 },
    { "UTC+12  Auckland (NZST)",  720 },
};

#define TZ_COUNT (sizeof(timezones) / sizeof(timezones[0]))

/* Currently selected timezone index */
static int selected_tz = 12;  /* Default: UTC+0 */

/* Scroll offset for timezone list */
static int tz_scroll = 0;

/* Number of visible timezone rows */
#define TZ_VISIBLE_ROWS 8
#define TZ_LIST_X       10
#define TZ_LIST_Y       30
#define TZ_LIST_W       260
#define TZ_SCROLL_W     18
#define TZ_ROW_H        16

/* Manual time fields area */
#define MANUAL_Y         (TZ_LIST_Y + TZ_VISIBLE_ROWS * TZ_ROW_H + 50)

/*
 * Find the timezone index matching the current offset
 */
static int find_tz_for_offset(int offset_minutes) {
    for (int i = 0; i < (int)TZ_COUNT; i++) {
        if (timezones[i].offset == offset_minutes)
            return i;
    }
    return 12; /* fallback to UTC */
}

/*
 * Simple string-to-int
 */
static int str_to_int(const char* s) {
    int val = 0;
    int sign = 1;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') { s++; }
    while (*s >= '0' && *s <= '9') {
        val = val * 10 + (*s - '0');
        s++;
    }
    return val * sign;
}

/*
 * Paint callback
 */
static void clock_settings_paint(xgui_window_t* win) {
    int cw = win->client_width;

    /* Title */
    xgui_win_rect_filled(win, 0, 0, cw, 22, XGUI_TITLE_ACTIVE);
    xgui_win_text_transparent(win, 8, 4, "Date & Time Settings", XGUI_WHITE);

    /* Timezone section */
    xgui_win_text_transparent(win, TZ_LIST_X, TZ_LIST_Y - 14, "Time Zone:", XGUI_BLACK);

    /* Timezone list background */
    int list_h = TZ_VISIBLE_ROWS * TZ_ROW_H;
    int text_w = TZ_LIST_W - TZ_SCROLL_W;  /* Text area width */
    xgui_win_rect_filled(win, TZ_LIST_X, TZ_LIST_Y, text_w, list_h, XGUI_WHITE);
    xgui_win_rect(win, TZ_LIST_X, TZ_LIST_Y, text_w, list_h, XGUI_DARK_GRAY);

    /* Draw timezone entries */
    for (int i = 0; i < TZ_VISIBLE_ROWS; i++) {
        int idx = tz_scroll + i;
        if (idx >= (int)TZ_COUNT) break;

        int row_y = TZ_LIST_Y + i * TZ_ROW_H;

        if (idx == selected_tz) {
            xgui_win_rect_filled(win, TZ_LIST_X + 1, row_y + 1,
                                 text_w - 2, TZ_ROW_H - 1, XGUI_TITLE_ACTIVE);
            xgui_win_text_transparent(win, TZ_LIST_X + 4, row_y + 2,
                                      timezones[idx].label, XGUI_WHITE);
        } else {
            xgui_win_text_transparent(win, TZ_LIST_X + 4, row_y + 2,
                                      timezones[idx].label, XGUI_BLACK);
        }
    }

    /* Scrollbar column */
    int sb_x = TZ_LIST_X + text_w;
    int btn_h = TZ_SCROLL_W;

    /* Up button */
    xgui_win_rect_filled(win, sb_x, TZ_LIST_Y, TZ_SCROLL_W, btn_h,
                          XGUI_RGB(212, 208, 200));
    xgui_win_rect_3d_raised(win, sb_x, TZ_LIST_Y, TZ_SCROLL_W, btn_h);
    /* Up arrow */
    {
        int ax = sb_x + TZ_SCROLL_W / 2;
        int ay = TZ_LIST_Y + btn_h / 2 - 2;
        uint32_t ac = (tz_scroll > 0) ? XGUI_BLACK : XGUI_RGB(160, 160, 160);
        for (int r = 0; r < 4; r++)
            xgui_win_hline(win, ax - r, ay + r, r * 2 + 1, ac);
    }

    /* Down button */
    int dn_y = TZ_LIST_Y + list_h - btn_h;
    xgui_win_rect_filled(win, sb_x, dn_y, TZ_SCROLL_W, btn_h,
                          XGUI_RGB(212, 208, 200));
    xgui_win_rect_3d_raised(win, sb_x, dn_y, TZ_SCROLL_W, btn_h);
    /* Down arrow */
    {
        int ax = sb_x + TZ_SCROLL_W / 2;
        int ay = dn_y + btn_h / 2 + 2;
        uint32_t ac = (tz_scroll + TZ_VISIBLE_ROWS < (int)TZ_COUNT) ? XGUI_BLACK : XGUI_RGB(160, 160, 160);
        for (int r = 0; r < 4; r++)
            xgui_win_hline(win, ax - r, ay - r, r * 2 + 1, ac);
    }

    /* Scrollbar track between buttons */
    int track_y = TZ_LIST_Y + btn_h;
    int track_h = list_h - btn_h * 2;
    xgui_win_rect_filled(win, sb_x, track_y, TZ_SCROLL_W, track_h,
                          XGUI_RGB(230, 230, 230));

    /* Scrollbar thumb */
    if ((int)TZ_COUNT > TZ_VISIBLE_ROWS && track_h > 10) {
        int thumb_h = track_h * TZ_VISIBLE_ROWS / (int)TZ_COUNT;
        if (thumb_h < 10) thumb_h = 10;
        int thumb_y = track_y + (track_h - thumb_h) * tz_scroll / ((int)TZ_COUNT - TZ_VISIBLE_ROWS);
        xgui_win_rect_filled(win, sb_x + 1, thumb_y, TZ_SCROLL_W - 2, thumb_h,
                              XGUI_RGB(192, 192, 192));
        xgui_win_rect_3d_raised(win, sb_x + 1, thumb_y, TZ_SCROLL_W - 2, thumb_h);
    }

    /* Current time display */
    rtc_time_t now;
    rtc_get_adjusted_time(&now);

    uint8_t h = now.hours;
    const char* ampm = (h < 12) ? "AM" : "PM";
    if (h == 0) h = 12;
    else if (h > 12) h -= 12;

    char current[48];
    snprintf(current, sizeof(current), "Current: %d:%02d:%02d %s  %d/%d/%d",
             h, now.minutes, now.seconds, ampm,
             now.month, now.day, now.year);
    xgui_win_text_transparent(win, TZ_LIST_X, MANUAL_Y + 10, current, XGUI_RGB(0, 0, 160));

    /* Manual time adjustment section */
    xgui_win_text_transparent(win, TZ_LIST_X, MANUAL_Y + 30, "Set Time Manually:", XGUI_BLACK);

    xgui_win_text_transparent(win, TZ_LIST_X, MANUAL_Y + 52, "Hour:", XGUI_BLACK);
    xgui_win_text_transparent(win, TZ_LIST_X + 90, MANUAL_Y + 52, "Min:", XGUI_BLACK);
    xgui_win_text_transparent(win, TZ_LIST_X + 170, MANUAL_Y + 52, "Sec:", XGUI_BLACK);

    /* Draw widgets */
    xgui_widgets_draw(win);
}

/*
 * Apply timezone button click
 */
static void apply_tz_click(xgui_widget_t* widget) {
    (void)widget;
    rtc_set_tz_offset(timezones[selected_tz].offset);
    xgui_theme_save_timezone(timezones[selected_tz].offset);
    if (clock_win) xgui_window_invalidate(clock_win);
}

/*
 * Apply manual time button click
 */
static void apply_time_click(xgui_widget_t* widget) {
    (void)widget;

    /* Read the text fields */
    int target_h = str_to_int(xgui_widget_get_text(hr_field));
    int target_m = str_to_int(xgui_widget_get_text(min_field));
    int target_s = str_to_int(xgui_widget_get_text(sec_field));

    /* Clamp values */
    if (target_h < 0) target_h = 0;
    if (target_h > 23) target_h = 23;
    if (target_m < 0) target_m = 0;
    if (target_m > 59) target_m = 59;
    if (target_s < 0) target_s = 0;
    if (target_s > 59) target_s = 59;

    /* Read current RTC time + timezone (without manual offset) */
    rtc_time_t raw;
    rtc_read_time(&raw);
    int tz_off = rtc_get_tz_offset();
    int raw_secs = (int)raw.hours * 3600 + (int)raw.minutes * 60 + (int)raw.seconds;
    raw_secs += tz_off * 60;
    /* Normalize */
    while (raw_secs >= 86400) raw_secs -= 86400;
    while (raw_secs < 0)      raw_secs += 86400;

    int target_secs = target_h * 3600 + target_m * 60 + target_s;
    int diff = target_secs - raw_secs;

    rtc_set_manual_offset(diff);
    if (clock_win) xgui_window_invalidate(clock_win);
}

/*
 * Close button click
 */
static void close_click(xgui_widget_t* widget) {
    (void)widget;
    if (clock_win) {
        xgui_window_destroy(clock_win);
        clock_win = NULL;
        tz_field = NULL;
        hr_field = NULL;
        min_field = NULL;
        sec_field = NULL;
    }
}

/*
 * Event handler
 */
static void clock_settings_handler(xgui_window_t* win, xgui_event_t* event) {
    if (xgui_widgets_handle_event(win, event)) {
        return;
    }

    if (event->type == XGUI_EVENT_WINDOW_CLOSE) {
        xgui_window_destroy(win);
        clock_win = NULL;
        tz_field = NULL;
        hr_field = NULL;
        min_field = NULL;
        sec_field = NULL;
        return;
    }

    /* Handle clicks on the timezone list and scrollbar */
    if (event->type == XGUI_EVENT_MOUSE_DOWN) {
        int mx = event->mouse.x;
        int my = event->mouse.y;

        int list_h = TZ_VISIBLE_ROWS * TZ_ROW_H;
        int text_w = TZ_LIST_W - TZ_SCROLL_W;
        int sb_x = TZ_LIST_X + text_w;
        int btn_h = TZ_SCROLL_W;

        /* Scrollbar up button */
        if (mx >= sb_x && mx < sb_x + TZ_SCROLL_W &&
            my >= TZ_LIST_Y && my < TZ_LIST_Y + btn_h) {
            if (tz_scroll > 0) {
                tz_scroll--;
                xgui_window_invalidate(win);
            }
            return;
        }

        /* Scrollbar down button */
        int dn_y = TZ_LIST_Y + list_h - btn_h;
        if (mx >= sb_x && mx < sb_x + TZ_SCROLL_W &&
            my >= dn_y && my < dn_y + btn_h) {
            if (tz_scroll + TZ_VISIBLE_ROWS < (int)TZ_COUNT) {
                tz_scroll++;
                xgui_window_invalidate(win);
            }
            return;
        }

        /* Scrollbar track click — jump scroll position */
        if (mx >= sb_x && mx < sb_x + TZ_SCROLL_W &&
            my >= TZ_LIST_Y + btn_h && my < dn_y) {
            int track_h = list_h - btn_h * 2;
            int rel = my - (TZ_LIST_Y + btn_h);
            int max_scroll = (int)TZ_COUNT - TZ_VISIBLE_ROWS;
            if (max_scroll > 0 && track_h > 0) {
                tz_scroll = rel * max_scroll / track_h;
                if (tz_scroll < 0) tz_scroll = 0;
                if (tz_scroll > max_scroll) tz_scroll = max_scroll;
                xgui_window_invalidate(win);
            }
            return;
        }

        /* Click in timezone list text area */
        if (mx >= TZ_LIST_X && mx < TZ_LIST_X + text_w &&
            my >= TZ_LIST_Y && my < TZ_LIST_Y + list_h) {
            int row = (my - TZ_LIST_Y) / TZ_ROW_H;
            int idx = tz_scroll + row;
            if (idx >= 0 && idx < (int)TZ_COUNT) {
                selected_tz = idx;
                xgui_window_invalidate(win);
            }
        }
    }

}

/*
 * Create the clock settings window
 */
void xgui_clock_settings_create(void) {
    if (clock_win) {
        xgui_window_focus(clock_win);
        return;
    }

    /* Initialize selected timezone from current offset */
    selected_tz = find_tz_for_offset(rtc_get_tz_offset());

    /* Scroll so selected timezone is visible */
    if (selected_tz < tz_scroll) {
        tz_scroll = selected_tz;
    } else if (selected_tz >= tz_scroll + TZ_VISIBLE_ROWS) {
        tz_scroll = selected_tz - TZ_VISIBLE_ROWS + 1;
    }

    int win_w = 280;
    int win_h = MANUAL_Y + 114;

    /* Position near the clock (bottom-right of screen) */
    int sw = xgui_display_width();
    int sh = xgui_display_height();
    int wx = sw - win_w - 10;
    int wy = sh - 26 - win_h - 10;  /* 26 = panel height approx */

    clock_win = xgui_window_create("Date & Time", wx, wy, win_w, win_h, XGUI_WINDOW_DEFAULT);
    if (!clock_win) return;

    xgui_window_set_paint(clock_win, clock_settings_paint);
    xgui_window_set_handler(clock_win, clock_settings_handler);

    /* Get current adjusted time for field defaults */
    rtc_time_t now;
    rtc_get_adjusted_time(&now);

    char buf[8];

    /* Hour field */
    hr_field = xgui_textfield_create(clock_win, TZ_LIST_X + 40, MANUAL_Y + 48, 40, 3);
    snprintf(buf, sizeof(buf), "%d", now.hours);
    xgui_widget_set_text(hr_field, buf);

    /* Minute field */
    min_field = xgui_textfield_create(clock_win, TZ_LIST_X + 120, MANUAL_Y + 48, 40, 3);
    snprintf(buf, sizeof(buf), "%d", now.minutes);
    xgui_widget_set_text(min_field, buf);

    /* Second field */
    sec_field = xgui_textfield_create(clock_win, TZ_LIST_X + 200, MANUAL_Y + 48, 40, 3);
    snprintf(buf, sizeof(buf), "%d", now.seconds);
    xgui_widget_set_text(sec_field, buf);

    /* Apply Timezone button */
    xgui_widget_t* apply_tz_btn = xgui_button_create(clock_win, TZ_LIST_X, MANUAL_Y - 20, 120, 22, "Apply Timezone");
    xgui_widget_set_onclick(apply_tz_btn, apply_tz_click);

    /* Set Time button */
    xgui_widget_t* set_time_btn = xgui_button_create(clock_win, TZ_LIST_X, MANUAL_Y + 74, 100, 22, "Set Time");
    xgui_widget_set_onclick(set_time_btn, apply_time_click);

    /* Close button */
    xgui_widget_t* close_btn = xgui_button_create(clock_win, TZ_LIST_X + 110, MANUAL_Y + 74, 80, 22, "Close");
    xgui_widget_set_onclick(close_btn, close_click);
}
