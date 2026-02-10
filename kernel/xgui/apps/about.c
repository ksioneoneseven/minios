/*
 * MiniOS XGUI About Dialog
 *
 * Shows information about MiniOS.
 */

#include "xgui/xgui.h"
#include "xgui/wm.h"
#include "xgui/widget.h"
#include "string.h"

/* About window */
static xgui_window_t* about_window = NULL;

/*
 * Window paint callback
 */
static void about_paint(xgui_window_t* win) {
    /* Draw logo/title area */
    xgui_win_rect_filled(win, 10, 10, win->client_width - 20, 40, XGUI_TITLE_ACTIVE);
    xgui_win_text_transparent(win, 20, 22, "MiniOS", XGUI_WHITE);
    
    /* Version and info */
    xgui_win_text_transparent(win, 15, 60, "Version 1.0", XGUI_BLACK);
    xgui_win_text_transparent(win, 15, 80, "A minimal operating system", XGUI_BLACK);
    xgui_win_text_transparent(win, 15, 100, "with graphical interface.", XGUI_BLACK);
    
    xgui_win_text_transparent(win, 15, 130, "Features:", XGUI_BLACK);
    xgui_win_text_transparent(win, 20, 150, "- VESA graphics", XGUI_DARK_GRAY);
    xgui_win_text_transparent(win, 20, 165, "- Window manager", XGUI_DARK_GRAY);
    xgui_win_text_transparent(win, 20, 180, "- Mouse & keyboard", XGUI_DARK_GRAY);
    
    xgui_widgets_draw(win);
}

/*
 * OK button click handler
 */
static void ok_click(xgui_widget_t* widget) {
    (void)widget;
    if (about_window) {
        xgui_window_destroy(about_window);
        about_window = NULL;
    }
}

/*
 * Window event handler
 */
static void about_handler(xgui_window_t* win, xgui_event_t* event) {
    if (xgui_widgets_handle_event(win, event)) {
        return;
    }
    
    if (event->type == XGUI_EVENT_WINDOW_CLOSE) {
        xgui_window_destroy(win);
        about_window = NULL;
    }
}

/*
 * Create the About dialog
 */
void xgui_about_create(void) {
    if (about_window) {
        xgui_window_focus(about_window);
        return;
    }
    
    /* Create window */
    about_window = xgui_window_create("About MiniOS", 200, 150, 250, 250, XGUI_WINDOW_DEFAULT);
    if (!about_window) return;
    
    xgui_window_set_paint(about_window, about_paint);
    xgui_window_set_handler(about_window, about_handler);
    
    /* Create OK button */
    xgui_widget_t* ok_btn = xgui_button_create(about_window, 85, 200, 80, 25, "OK");
    xgui_widget_set_onclick(ok_btn, ok_click);
}
