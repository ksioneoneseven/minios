/*
 * MiniOS XGUI Task Manager
 *
 * Shows open windows, memory usage, and allows closing windows.
 */

#include "xgui/xgui.h"
#include "xgui/wm.h"
#include "xgui/widget.h"
#include "xgui/theme.h"
#include "heap.h"
#include "string.h"
#include "stdio.h"
#include "timer.h"

/* Task Manager configuration */
#define TM_WIDTH        340
#define TM_HEIGHT       320
#define TM_TOOLBAR_H    28
#define TM_ITEM_H       18
#define TM_MAX_TASKS    16
#define TM_LIST_X       6
#define TM_LIST_Y       (TM_TOOLBAR_H + 4)

/* Singleton window */
static xgui_window_t* tm_window = NULL;

/* Toolbar widgets */
static xgui_widget_t* btn_end_task = NULL;
static xgui_widget_t* btn_refresh = NULL;

/* Task list state */
typedef struct {
    xgui_window_t* win;
    char title[40];
    int mem_kb;     /* approximate buffer memory in KB */
} tm_task_t;

static tm_task_t tasks[TM_MAX_TASKS];
static int task_count = 0;
static int selected_task = -1;

/*
 * Refresh the task list from the window manager
 */
static void tm_refresh(void) {
    task_count = 0;
    selected_task = -1;

    xgui_window_t* win = xgui_wm_get_top();
    while (win && task_count < TM_MAX_TASKS) {
        if ((win->flags & XGUI_WINDOW_VISIBLE) && win != tm_window) {
            tasks[task_count].win = win;
            strncpy(tasks[task_count].title, win->title, 39);
            tasks[task_count].title[39] = '\0';
            /* Estimate memory: buffer size */
            tasks[task_count].mem_kb = (win->buf_width * win->buf_height * 4) / 1024;
            task_count++;
        }
        win = win->prev;
    }

    if (tm_window) tm_window->dirty = true;
}

/*
 * End Task button callback
 */
static void on_end_task(xgui_widget_t* widget) {
    (void)widget;
    if (selected_task >= 0 && selected_task < task_count) {
        xgui_window_t* target = tasks[selected_task].win;
        if (target && target != tm_window) {
            /* Send close event to the window */
            xgui_event_t ev;
            memset(&ev, 0, sizeof(ev));
            ev.type = XGUI_EVENT_WINDOW_CLOSE;
            if (target->handler) {
                target->handler(target, &ev);
            }
            tm_refresh();
        }
    }
}

/*
 * Refresh button callback
 */
static void on_refresh(xgui_widget_t* widget) {
    (void)widget;
    tm_refresh();
}

/*
 * Format bytes to human-readable
 */
static void format_kb(uint32_t kb, char* buf, int buf_size) {
    if (kb < 1024) {
        snprintf(buf, buf_size, "%u KB", kb);
    } else {
        snprintf(buf, buf_size, "%u MB", kb / 1024);
    }
}

/*
 * Paint callback
 */
static void tm_paint(xgui_window_t* win) {
    int cw = win->client_width;
    int ch = win->client_height;

    /* Toolbar background */
    xgui_win_rect_filled(win, 0, 0, cw, TM_TOOLBAR_H, XGUI_RGB(235, 235, 235));
    xgui_win_hline(win, 0, TM_TOOLBAR_H - 1, cw, XGUI_DARK_GRAY);

    /* Draw toolbar buttons */
    xgui_widgets_draw(win);

    /* List area background */
    int list_h = ch - TM_LIST_Y - 80;
    xgui_win_rect_filled(win, TM_LIST_X, TM_LIST_Y, cw - 12, list_h, XGUI_WHITE);
    xgui_win_rect_3d_sunken(win, TM_LIST_X, TM_LIST_Y, cw - 12, list_h);

    /* Column headers */
    int hdr_y = TM_LIST_Y + 2;
    xgui_win_rect_filled(win, TM_LIST_X + 2, hdr_y, cw - 16, TM_ITEM_H,
                         XGUI_LIGHT_GRAY);
    xgui_win_text(win, TM_LIST_X + 6, hdr_y + 2, "Window", XGUI_BLACK, XGUI_LIGHT_GRAY);
    xgui_win_text(win, cw - 80, hdr_y + 2, "Memory", XGUI_BLACK, XGUI_LIGHT_GRAY);
    xgui_win_hline(win, TM_LIST_X + 2, hdr_y + TM_ITEM_H, cw - 16, XGUI_DARK_GRAY);

    /* Task entries */
    int item_y = hdr_y + TM_ITEM_H + 2;
    int visible = (list_h - TM_ITEM_H - 6) / TM_ITEM_H;

    for (int i = 0; i < task_count && i < visible; i++) {
        int y = item_y + i * TM_ITEM_H;
        uint32_t bg = XGUI_WHITE;
        uint32_t fg = XGUI_BLACK;

        if (i == selected_task) {
            bg = XGUI_SELECTION;
            fg = XGUI_WHITE;
            xgui_win_rect_filled(win, TM_LIST_X + 2, y, cw - 16, TM_ITEM_H, bg);
        }

        /* Truncate title */
        char name[32];
        strncpy(name, tasks[i].title, 28);
        name[28] = '\0';
        if (strlen(tasks[i].title) > 28) strcat(name, "..");
        xgui_win_text(win, TM_LIST_X + 6, y + 2, name, fg, bg);

        /* Memory */
        char mem_str[16];
        format_kb(tasks[i].mem_kb, mem_str, sizeof(mem_str));
        xgui_win_text(win, cw - 80, y + 2, mem_str, fg, bg);
    }

    /* Memory info section */
    int info_y = ch - 72;
    xgui_win_hline(win, 6, info_y, cw - 12, XGUI_DARK_GRAY);
    xgui_win_text_transparent(win, 8, info_y + 6, "System Memory", XGUI_BLACK);

    heap_stats_t stats;
    heap_get_stats(&stats);

    char line[64];
    char total_str[16], used_str[16], free_str[16];
    format_kb(stats.total_size / 1024, total_str, sizeof(total_str));
    format_kb(stats.used_size / 1024, used_str, sizeof(used_str));
    format_kb(stats.free_size / 1024, free_str, sizeof(free_str));

    snprintf(line, sizeof(line), "Total: %s", total_str);
    xgui_win_text_transparent(win, 8, info_y + 22, line, XGUI_BLACK);

    snprintf(line, sizeof(line), "Used:  %s", used_str);
    xgui_win_text_transparent(win, 8, info_y + 36, line, XGUI_BLACK);

    snprintf(line, sizeof(line), "Free:  %s", free_str);
    xgui_win_text_transparent(win, 8, info_y + 50, line, XGUI_BLACK);

    /* Memory bar */
    int bar_x = 170;
    int bar_w = cw - bar_x - 10;
    int bar_y = info_y + 26;
    int bar_h = 16;
    xgui_win_rect_filled(win, bar_x, bar_y, bar_w, bar_h, XGUI_WHITE);
    xgui_win_rect_3d_sunken(win, bar_x, bar_y, bar_w, bar_h);

    /* Used portion */
    int used_w = 0;
    if (stats.total_size > 0) {
        /* Use KB to avoid 64-bit overflow */
        uint32_t used_kb = stats.used_size / 1024;
        uint32_t total_kb = stats.total_size / 1024;
        if (total_kb > 0) {
            used_w = (int)((bar_w - 4) * used_kb / total_kb);
        }
    }
    if (used_w > 0) {
        uint32_t bar_color = XGUI_RGB(60, 160, 60);
        if (stats.used_size > stats.total_size * 3 / 4) {
            bar_color = XGUI_RGB(200, 60, 60);
        } else if (stats.used_size > stats.total_size / 2) {
            bar_color = XGUI_RGB(200, 180, 40);
        }
        xgui_win_rect_filled(win, bar_x + 2, bar_y + 2, used_w, bar_h - 4, bar_color);
    }

    /* Window count */
    snprintf(line, sizeof(line), "Windows: %d", task_count);
    xgui_win_text_transparent(win, 170, info_y + 50, line, XGUI_BLACK);
}

/*
 * Event handler
 */
static void tm_handler(xgui_window_t* win, xgui_event_t* event) {
    /* Let widgets handle first */
    if (xgui_widgets_handle_event(win, event)) {
        win->dirty = true;
        return;
    }

    if (event->type == XGUI_EVENT_WINDOW_CLOSE) {
        xgui_window_destroy(win);
        tm_window = NULL;
        btn_end_task = NULL;
        btn_refresh = NULL;
        return;
    }

    /* Click in task list */
    if (event->type == XGUI_EVENT_MOUSE_DOWN && event->mouse.button == XGUI_MOUSE_LEFT) {
        int mx = event->mouse.x;
        int my = event->mouse.y;
        int cw = win->client_width;

        int hdr_y = TM_LIST_Y + 2;
        int item_y = hdr_y + TM_ITEM_H + 2;
        int list_h = win->client_height - TM_LIST_Y - 80;
        int visible = (list_h - TM_ITEM_H - 6) / TM_ITEM_H;

        if (mx >= TM_LIST_X && mx < cw - 6 &&
            my >= item_y && my < item_y + visible * TM_ITEM_H) {
            int clicked = (my - item_y) / TM_ITEM_H;
            if (clicked >= 0 && clicked < task_count) {
                selected_task = clicked;
                win->dirty = true;
            }
        }
    }

    /* Double-click to focus window */
    if (event->type == XGUI_EVENT_MOUSE_DOWN && event->mouse.button == XGUI_MOUSE_LEFT) {
        static int last_click_item = -1;
        static uint32_t last_click_time = 0;
        uint32_t now = timer_get_ticks();

        if (selected_task >= 0 && selected_task == last_click_item &&
            (now - last_click_time) < 50) {
            /* Double-click: focus the window */
            if (selected_task < task_count && tasks[selected_task].win) {
                xgui_window_focus(tasks[selected_task].win);
            }
            last_click_item = -1;
        } else {
            last_click_item = selected_task;
            last_click_time = now;
        }
    }
}

/*
 * Create the Task Manager window
 */
void xgui_taskmgr_create(void) {
    if (tm_window) {
        xgui_window_focus(tm_window);
        tm_refresh();
        return;
    }

    tm_window = xgui_window_create("Task Manager", 150, 60,
                                    TM_WIDTH, TM_HEIGHT,
                                    XGUI_WINDOW_DEFAULT);
    if (!tm_window) return;

    xgui_window_set_paint(tm_window, tm_paint);
    xgui_window_set_handler(tm_window, tm_handler);
    xgui_window_set_bgcolor(tm_window, XGUI_LIGHT_GRAY);

    /* Toolbar buttons */
    btn_end_task = xgui_button_create(tm_window, 4, 3, 70, 22, "End Task");
    btn_refresh = xgui_button_create(tm_window, 78, 3, 60, 22, "Refresh");

    if (btn_end_task) xgui_widget_set_onclick(btn_end_task, on_end_task);
    if (btn_refresh) xgui_widget_set_onclick(btn_refresh, on_refresh);

    tm_refresh();
}
