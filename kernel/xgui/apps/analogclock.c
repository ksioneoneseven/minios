/*
 * MiniOS XGUI Analog Clock
 *
 * A round analog clock widget that displays the current time
 * with hour, minute, and second hands.
 */

#include "xgui/xgui.h"
#include "xgui/wm.h"
#include "rtc.h"
#include "string.h"
#include "stdio.h"
#include "timer.h"

/* Clock window */
static xgui_window_t* clock_window = NULL;

/* Last drawn second — used to detect when to redraw */
static uint8_t last_second = 255;

/*
 * Fixed-point sine table (0..90 degrees inclusive), scaled by 1024.
 * sin_table[i] = (int)(sin(i * pi/180) * 1024)
 */
static const int16_t sin_table[91] = {
       0,   18,   36,   54,   71,   89,  107,  125,  143,  160,
     178,  195,  213,  230,  248,  265,  282,  299,  316,  333,
     350,  367,  383,  400,  416,  432,  448,  464,  480,  496,
     511,  526,  542,  557,  572,  587,  601,  616,  630,  644,
     658,  672,  685,  698,  711,  724,  737,  749,  761,  773,
     784,  796,  807,  818,  828,  839,  849,  859,  868,  878,
     887,  896,  904,  912,  920,  928,  935,  943,  949,  956,
     962,  968,  974,  979,  984,  989,  994,  998, 1002, 1005,
    1008, 1011, 1014, 1016, 1018, 1020, 1022, 1023, 1023, 1024,
    1024,
};

/*
 * Get sine * 1024 for angle in degrees (0-359)
 */
static int isin(int deg) {
    deg = deg % 360;
    if (deg < 0) deg += 360;

    if (deg <= 90)  return  sin_table[deg];
    if (deg <= 180) return  sin_table[180 - deg];
    if (deg <= 270) return -sin_table[deg - 180];
    return -sin_table[360 - deg];
}

/*
 * Get cosine * 1024 for angle in degrees (0-359)
 */
static int icos(int deg) {
    return isin(deg + 90);
}

/*
 * Draw a filled circle using the midpoint algorithm
 */
static void draw_filled_circle(xgui_window_t* win, int cx, int cy, int r, uint32_t color) {
    int x = 0, y = r;
    int d = 1 - r;

    while (x <= y) {
        /* Draw horizontal spans for each octant pair */
        xgui_win_hline(win, cx - y, cy + x, y * 2 + 1, color);
        xgui_win_hline(win, cx - y, cy - x, y * 2 + 1, color);
        xgui_win_hline(win, cx - x, cy + y, x * 2 + 1, color);
        xgui_win_hline(win, cx - x, cy - y, x * 2 + 1, color);

        if (d < 0) {
            d += 2 * x + 3;
        } else {
            d += 2 * (x - y) + 5;
            y--;
        }
        x++;
    }
}

/*
 * Draw a circle outline using the midpoint algorithm
 */
static void draw_circle(xgui_window_t* win, int cx, int cy, int r, uint32_t color) {
    int x = 0, y = r;
    int d = 1 - r;

    while (x <= y) {
        xgui_win_pixel(win, cx + x, cy + y, color);
        xgui_win_pixel(win, cx - x, cy + y, color);
        xgui_win_pixel(win, cx + x, cy - y, color);
        xgui_win_pixel(win, cx - x, cy - y, color);
        xgui_win_pixel(win, cx + y, cy + x, color);
        xgui_win_pixel(win, cx - y, cy + x, color);
        xgui_win_pixel(win, cx + y, cy - x, color);
        xgui_win_pixel(win, cx - y, cy - x, color);

        if (d < 0) {
            d += 2 * x + 3;
        } else {
            d += 2 * (x - y) + 5;
            y--;
        }
        x++;
    }
}

/*
 * Draw a thick line (draws the line and 1px offsets for thickness)
 */
static void draw_thick_line(xgui_window_t* win, int x1, int y1, int x2, int y2, uint32_t color) {
    xgui_win_line(win, x1, y1, x2, y2, color);
    xgui_win_line(win, x1 + 1, y1, x2 + 1, y2, color);
    xgui_win_line(win, x1, y1 + 1, x2, y2 + 1, color);
    xgui_win_line(win, x1 - 1, y1, x2 - 1, y2, color);
    xgui_win_line(win, x1, y1 - 1, x2, y2 - 1, color);
}

/*
 * Draw a clock hand from center at given angle and length
 * angle: 0 = 12 o'clock, 90 = 3 o'clock, etc.
 */
static void draw_hand(xgui_window_t* win, int cx, int cy, int length,
                       int angle, uint32_t color, bool thick) {
    /* Convert clock angle (0=up) to math angle */
    int ex = cx + (length * isin(angle)) / 1024;
    int ey = cy - (length * icos(angle)) / 1024;

    if (thick) {
        draw_thick_line(win, cx, cy, ex, ey, color);
    } else {
        xgui_win_line(win, cx, cy, ex, ey, color);
    }
}

/*
 * Paint the analog clock
 */
static void analogclock_paint(xgui_window_t* win) {
    int cw = win->client_width;
    int ch = win->client_height;

    /* Clock center and radius — leave 20px at bottom for digital time */
    int usable_h = ch - 20;
    int radius = (cw < usable_h ? cw : usable_h) / 2 - 8;
    int cx = cw / 2;
    int cy = usable_h / 2;

    /* Clock face — white filled circle */
    draw_filled_circle(win, cx, cy, radius, XGUI_WHITE);

    /* Outer ring */
    draw_circle(win, cx, cy, radius, XGUI_RGB(60, 60, 60));
    draw_circle(win, cx, cy, radius - 1, XGUI_RGB(100, 100, 100));

    /* Hour markers */
    for (int i = 0; i < 12; i++) {
        int angle = i * 30;
        int inner = radius - 12;
        int outer = radius - 4;

        int x1 = cx + (inner * isin(angle)) / 1024;
        int y1 = cy - (inner * icos(angle)) / 1024;
        int x2 = cx + (outer * isin(angle)) / 1024;
        int y2 = cy - (outer * icos(angle)) / 1024;

        /* Major markers are thicker */
        draw_thick_line(win, x1, y1, x2, y2, XGUI_RGB(40, 40, 40));
    }

    /* Minute tick marks */
    for (int i = 0; i < 60; i++) {
        if (i % 5 == 0) continue;  /* Skip hour positions */
        int angle = i * 6;
        int inner = radius - 6;
        int outer = radius - 3;

        int x1 = cx + (inner * isin(angle)) / 1024;
        int y1 = cy - (inner * icos(angle)) / 1024;
        int x2 = cx + (outer * isin(angle)) / 1024;
        int y2 = cy - (outer * icos(angle)) / 1024;

        xgui_win_line(win, x1, y1, x2, y2, XGUI_RGB(160, 160, 160));
    }

    /* Hour numbers — placed well inside the tick marks */
    static const char* nums[] = {
        "12", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11"
    };
    int num_radius = radius * 3 / 4;

    for (int i = 0; i < 12; i++) {
        int angle = i * 30;
        int nx = cx + (num_radius * isin(angle)) / 1024;
        int ny = cy - (num_radius * icos(angle)) / 1024;

        /* Center the text: each char is 8px wide, ~12px tall */
        int tlen = nums[i][1] ? 2 : 1;
        int tx = nx - tlen * 4;
        int ty = ny - 6;

        xgui_win_text_transparent(win, tx, ty, nums[i], XGUI_RGB(40, 40, 40));
    }

    /* Get current time */
    rtc_time_t now;
    rtc_get_adjusted_time(&now);

    /* Hour hand — short and thick */
    int hour_angle = ((now.hours % 12) * 30) + (now.minutes / 2);
    draw_hand(win, cx, cy, radius * 50 / 100, hour_angle,
              XGUI_RGB(20, 20, 20), true);

    /* Minute hand — medium length, thick */
    int min_angle = now.minutes * 6 + now.seconds / 10;
    draw_hand(win, cx, cy, radius * 72 / 100, min_angle,
              XGUI_RGB(30, 30, 30), true);

    /* Second hand — long and thin, red */
    int sec_angle = now.seconds * 6;
    draw_hand(win, cx, cy, radius * 80 / 100, sec_angle,
              XGUI_RGB(200, 0, 0), false);

    /* Center dot */
    draw_filled_circle(win, cx, cy, 4, XGUI_RGB(60, 60, 60));
    draw_filled_circle(win, cx, cy, 2, XGUI_RGB(200, 0, 0));

    /* Digital time at bottom */
    uint8_t h = now.hours;
    const char* ampm = (h < 12) ? "AM" : "PM";
    if (h == 0) h = 12;
    else if (h > 12) h -= 12;

    char time_str[16];
    snprintf(time_str, sizeof(time_str), "%d:%02d:%02d %s",
             h, now.minutes, now.seconds, ampm);

    int tw = 0;
    for (const char* p = time_str; *p; p++) tw += 8;
    xgui_win_text_transparent(win, cx - tw / 2, cy + radius + 4,
                               time_str, XGUI_RGB(80, 80, 80));
}

/*
 * Event handler
 */
static void analogclock_handler(xgui_window_t* win, xgui_event_t* event) {
    if (event->type == XGUI_EVENT_WINDOW_CLOSE) {
        xgui_window_destroy(win);
        clock_window = NULL;
    }
}

/*
 * Update callback — called from the main loop to refresh every second
 */
void xgui_analogclock_update(void) {
    if (!clock_window) return;

    rtc_time_t now;
    rtc_get_adjusted_time(&now);

    if (now.seconds != last_second) {
        last_second = now.seconds;
        xgui_window_invalidate(clock_window);
    }
}

/*
 * Create the analog clock window
 */
void xgui_analogclock_create(void) {
    if (clock_window) {
        xgui_window_focus(clock_window);
        return;
    }

    int sw = xgui_display_width();

    int size = 200;
    int wx = sw - size - 10;
    int wy = 10;

    clock_window = xgui_window_create("Clock", wx, wy, size, size + 20,
                                       XGUI_WINDOW_DEFAULT | XGUI_WINDOW_RESIZABLE);
    if (!clock_window) return;

    xgui_window_set_paint(clock_window, analogclock_paint);
    xgui_window_set_handler(clock_window, analogclock_handler);
    xgui_window_set_bgcolor(clock_window, XGUI_RGB(240, 240, 240));

    last_second = 255;
}
