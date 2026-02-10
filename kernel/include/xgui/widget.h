/*
 * MiniOS XGUI Widget Toolkit
 *
 * Basic UI widgets: buttons, labels, text fields, etc.
 */

#ifndef _XGUI_WIDGET_H
#define _XGUI_WIDGET_H

#include "types.h"
#include "xgui/wm.h"
#include "xgui/event.h"

/* Widget types */
typedef enum {
    XGUI_WIDGET_LABEL,
    XGUI_WIDGET_BUTTON,
    XGUI_WIDGET_TEXTFIELD,
    XGUI_WIDGET_CHECKBOX,
    XGUI_WIDGET_LISTBOX
} xgui_widget_type_t;

/* Widget flags */
#define XGUI_WIDGET_VISIBLE     0x01
#define XGUI_WIDGET_ENABLED     0x02
#define XGUI_WIDGET_FOCUSED     0x04
#define XGUI_WIDGET_HOVERED     0x08

/* Forward declaration */
struct xgui_widget;

/* Widget event callback */
typedef void (*xgui_widget_callback_t)(struct xgui_widget* widget, xgui_event_t* event);

/* Widget click callback (simpler) */
typedef void (*xgui_click_callback_t)(struct xgui_widget* widget);

/* Base widget structure */
typedef struct xgui_widget {
    xgui_widget_type_t type;
    uint32_t id;
    
    int x, y;               /* Position relative to parent window */
    int width, height;
    
    uint8_t flags;
    uint32_t fg_color;
    uint32_t bg_color;
    
    char text[128];         /* Widget text/label */
    
    xgui_window_t* parent;  /* Parent window */
    xgui_widget_callback_t handler;
    xgui_click_callback_t on_click;
    void* user_data;
    
    /* Type-specific data */
    union {
        struct {
            bool pressed;
        } button;
        struct {
            int cursor_pos;
            int scroll_offset;
            int max_length;
        } textfield;
        struct {
            bool checked;
        } checkbox;
        struct {
            int selected_index;
            int scroll_offset;
            int item_count;
            char** items;
        } listbox;
    };
    
    struct xgui_widget* next;   /* Next widget in window's list */
} xgui_widget_t;

/* Maximum widgets per window */
#define XGUI_MAX_WIDGETS_PER_WINDOW 32

/*
 * Create a label widget
 */
xgui_widget_t* xgui_label_create(xgui_window_t* parent, int x, int y, 
                                  const char* text);

/*
 * Create a button widget
 */
xgui_widget_t* xgui_button_create(xgui_window_t* parent, int x, int y,
                                   int width, int height, const char* text);

/*
 * Create a text field widget
 */
xgui_widget_t* xgui_textfield_create(xgui_window_t* parent, int x, int y,
                                      int width, int max_length);

/*
 * Create a checkbox widget
 */
xgui_widget_t* xgui_checkbox_create(xgui_window_t* parent, int x, int y,
                                     const char* text, bool checked);

/*
 * Destroy a widget
 */
void xgui_widget_destroy(xgui_widget_t* widget);

/*
 * Destroy all widgets for a window
 */
void xgui_widgets_destroy_all(xgui_window_t* window);

/*
 * Set widget text
 */
void xgui_widget_set_text(xgui_widget_t* widget, const char* text);

/*
 * Get widget text
 */
const char* xgui_widget_get_text(xgui_widget_t* widget);

/*
 * Set widget position
 */
void xgui_widget_set_position(xgui_widget_t* widget, int x, int y);

/*
 * Set widget size
 */
void xgui_widget_set_size(xgui_widget_t* widget, int width, int height);

/*
 * Set widget enabled state
 */
void xgui_widget_set_enabled(xgui_widget_t* widget, bool enabled);

/*
 * Set widget visible state
 */
void xgui_widget_set_visible(xgui_widget_t* widget, bool visible);

/*
 * Set click callback
 */
void xgui_widget_set_onclick(xgui_widget_t* widget, xgui_click_callback_t callback);

/*
 * Set event handler
 */
void xgui_widget_set_handler(xgui_widget_t* widget, xgui_widget_callback_t handler);

/*
 * Set user data
 */
void xgui_widget_set_userdata(xgui_widget_t* widget, void* data);

/*
 * Get user data
 */
void* xgui_widget_get_userdata(xgui_widget_t* widget);

/*
 * Draw a widget
 */
void xgui_widget_draw(xgui_widget_t* widget);

/*
 * Draw all widgets in a window
 */
void xgui_widgets_draw(xgui_window_t* window);

/*
 * Handle event for widgets in a window
 * Returns true if event was handled
 */
bool xgui_widgets_handle_event(xgui_window_t* window, xgui_event_t* event);

/*
 * Get widget at position within window
 */
xgui_widget_t* xgui_widget_at(xgui_window_t* window, int x, int y);

/*
 * Focus a widget
 */
void xgui_widget_focus(xgui_widget_t* widget);

/*
 * Get checkbox state
 */
bool xgui_checkbox_get_checked(xgui_widget_t* widget);

/*
 * Set checkbox state
 */
void xgui_checkbox_set_checked(xgui_widget_t* widget, bool checked);

#endif /* _XGUI_WIDGET_H */
