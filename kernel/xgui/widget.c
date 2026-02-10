/*
 * MiniOS XGUI Widget Toolkit
 *
 * Basic UI widgets: buttons, labels, text fields, etc.
 */

#include "xgui/widget.h"
#include "xgui/wm.h"
#include "xgui/display.h"
#include "xgui/theme.h"
#include "heap.h"
#include "string.h"

/* Widget storage per window */
typedef struct {
    xgui_window_t* window;
    xgui_widget_t* widgets;
    xgui_widget_t* focused;
} widget_list_t;

static widget_list_t widget_lists[XGUI_MAX_WINDOWS];
static uint32_t next_widget_id = 1;

/*
 * Get or create widget list for a window
 */
static widget_list_t* get_widget_list(xgui_window_t* window) {
    for (int i = 0; i < XGUI_MAX_WINDOWS; i++) {
        if (widget_lists[i].window == window) {
            return &widget_lists[i];
        }
    }
    /* Allocate new list */
    for (int i = 0; i < XGUI_MAX_WINDOWS; i++) {
        if (widget_lists[i].window == NULL) {
            widget_lists[i].window = window;
            widget_lists[i].widgets = NULL;
            widget_lists[i].focused = NULL;
            return &widget_lists[i];
        }
    }
    return NULL;
}

/*
 * Add widget to window's list
 */
static void add_widget(xgui_window_t* window, xgui_widget_t* widget) {
    widget_list_t* list = get_widget_list(window);
    if (!list) return;
    
    widget->next = list->widgets;
    list->widgets = widget;
}

/*
 * Destroy all widgets for a window
 */
void xgui_widgets_destroy_all(xgui_window_t* window) {
    for (int i = 0; i < XGUI_MAX_WINDOWS; i++) {
        if (widget_lists[i].window == window) {
            /* Free all widgets */
            xgui_widget_t* w = widget_lists[i].widgets;
            while (w) {
                xgui_widget_t* next = w->next;
                kfree(w);
                w = next;
            }
            /* Clear the list entry */
            widget_lists[i].window = NULL;
            widget_lists[i].widgets = NULL;
            widget_lists[i].focused = NULL;
            return;
        }
    }
}

/*
 * Create base widget
 */
static xgui_widget_t* create_base_widget(xgui_window_t* parent, xgui_widget_type_t type,
                                          int x, int y, int width, int height) {
    xgui_widget_t* widget = (xgui_widget_t*)kmalloc(sizeof(xgui_widget_t));
    if (!widget) return NULL;
    
    memset(widget, 0, sizeof(xgui_widget_t));
    
    widget->type = type;
    widget->id = next_widget_id++;
    widget->x = x;
    widget->y = y;
    widget->width = width;
    widget->height = height;
    widget->flags = XGUI_WIDGET_VISIBLE | XGUI_WIDGET_ENABLED;
    widget->fg_color = XGUI_BLACK;
    widget->bg_color = xgui_theme_current()->button_bg;
    widget->parent = parent;
    
    add_widget(parent, widget);
    
    return widget;
}

/*
 * Create a label widget
 */
xgui_widget_t* xgui_label_create(xgui_window_t* parent, int x, int y, const char* text) {
    int width = xgui_display_text_width(text);
    int height = 16;
    
    xgui_widget_t* widget = create_base_widget(parent, XGUI_WIDGET_LABEL, x, y, width, height);
    if (!widget) return NULL;
    
    strncpy(widget->text, text, sizeof(widget->text) - 1);
    widget->bg_color = 0;  /* Transparent */
    
    return widget;
}

/*
 * Create a button widget
 */
xgui_widget_t* xgui_button_create(xgui_window_t* parent, int x, int y,
                                   int width, int height, const char* text) {
    xgui_widget_t* widget = create_base_widget(parent, XGUI_WIDGET_BUTTON, x, y, width, height);
    if (!widget) return NULL;
    
    strncpy(widget->text, text, sizeof(widget->text) - 1);
    widget->button.pressed = false;
    
    return widget;
}

/*
 * Create a text field widget
 */
xgui_widget_t* xgui_textfield_create(xgui_window_t* parent, int x, int y,
                                      int width, int max_length) {
    xgui_widget_t* widget = create_base_widget(parent, XGUI_WIDGET_TEXTFIELD, x, y, width, 20);
    if (!widget) return NULL;
    
    widget->bg_color = XGUI_WHITE;
    widget->textfield.cursor_pos = 0;
    widget->textfield.scroll_offset = 0;
    widget->textfield.max_length = (max_length < 127) ? max_length : 127;
    
    return widget;
}

/*
 * Create a checkbox widget
 */
xgui_widget_t* xgui_checkbox_create(xgui_window_t* parent, int x, int y,
                                     const char* text, bool checked) {
    int width = 16 + 4 + xgui_display_text_width(text);
    
    xgui_widget_t* widget = create_base_widget(parent, XGUI_WIDGET_CHECKBOX, x, y, width, 16);
    if (!widget) return NULL;
    
    strncpy(widget->text, text, sizeof(widget->text) - 1);
    widget->checkbox.checked = checked;
    widget->bg_color = 0;  /* Transparent */
    
    return widget;
}

/*
 * Destroy a widget
 */
void xgui_widget_destroy(xgui_widget_t* widget) {
    if (!widget) return;
    
    /* Remove from window's list */
    widget_list_t* list = get_widget_list(widget->parent);
    if (list) {
        if (list->widgets == widget) {
            list->widgets = widget->next;
        } else {
            for (xgui_widget_t* w = list->widgets; w; w = w->next) {
                if (w->next == widget) {
                    w->next = widget->next;
                    break;
                }
            }
        }
        if (list->focused == widget) {
            list->focused = NULL;
        }
    }
    
    kfree(widget);
}

/*
 * Set widget text
 */
void xgui_widget_set_text(xgui_widget_t* widget, const char* text) {
    if (widget) {
        strncpy(widget->text, text, sizeof(widget->text) - 1);
        widget->text[sizeof(widget->text) - 1] = '\0';
    }
}

/*
 * Get widget text
 */
const char* xgui_widget_get_text(xgui_widget_t* widget) {
    return widget ? widget->text : "";
}

/*
 * Set widget position
 */
void xgui_widget_set_position(xgui_widget_t* widget, int x, int y) {
    if (widget) {
        widget->x = x;
        widget->y = y;
    }
}

/*
 * Set widget size
 */
void xgui_widget_set_size(xgui_widget_t* widget, int width, int height) {
    if (widget) {
        widget->width = width;
        widget->height = height;
    }
}

/*
 * Set widget enabled state
 */
void xgui_widget_set_enabled(xgui_widget_t* widget, bool enabled) {
    if (widget) {
        if (enabled) {
            widget->flags |= XGUI_WIDGET_ENABLED;
        } else {
            widget->flags &= ~XGUI_WIDGET_ENABLED;
        }
    }
}

/*
 * Set widget visible state
 */
void xgui_widget_set_visible(xgui_widget_t* widget, bool visible) {
    if (widget) {
        if (visible) {
            widget->flags |= XGUI_WIDGET_VISIBLE;
        } else {
            widget->flags &= ~XGUI_WIDGET_VISIBLE;
        }
    }
}

/*
 * Set click callback
 */
void xgui_widget_set_onclick(xgui_widget_t* widget, xgui_click_callback_t callback) {
    if (widget) {
        widget->on_click = callback;
    }
}

/*
 * Set event handler
 */
void xgui_widget_set_handler(xgui_widget_t* widget, xgui_widget_callback_t handler) {
    if (widget) {
        widget->handler = handler;
    }
}

/*
 * Set user data
 */
void xgui_widget_set_userdata(xgui_widget_t* widget, void* data) {
    if (widget) {
        widget->user_data = data;
    }
}

/*
 * Get user data
 */
void* xgui_widget_get_userdata(xgui_widget_t* widget) {
    return widget ? widget->user_data : NULL;
}

/*
 * Draw a label
 */
static void draw_label(xgui_widget_t* widget, int x, int y) {
    uint32_t color = (widget->flags & XGUI_WIDGET_ENABLED) ? widget->fg_color : XGUI_GRAY;
    xgui_win_text_transparent(widget->parent, x, y, widget->text, color);
}

/*
 * Draw a button
 */
static void draw_button(xgui_widget_t* widget, int x, int y) {
    bool pressed = widget->button.pressed;
    bool enabled = (widget->flags & XGUI_WIDGET_ENABLED);
    xgui_window_t* win = widget->parent;
    
    /* Background */
    uint32_t bg = enabled ? (pressed ? xgui_theme_current()->button_pressed : xgui_theme_current()->button_bg) : XGUI_LIGHT_GRAY;
    xgui_win_rect_filled(win, x, y, widget->width, widget->height, bg);
    
    /* Border */
    if (pressed) {
        xgui_win_rect_3d_sunken(win, x, y, widget->width, widget->height);
    } else {
        xgui_win_rect_3d_raised(win, x, y, widget->width, widget->height);
    }
    
    /* Text (centered) */
    int text_w = xgui_display_text_width(widget->text);
    int text_x = x + (widget->width - text_w) / 2;
    int text_y = y + (widget->height - 16) / 2;
    
    if (pressed) {
        text_x++;
        text_y++;
    }
    
    uint32_t fg = enabled ? xgui_theme_current()->button_text : XGUI_GRAY;
    xgui_win_text_transparent(win, text_x, text_y, widget->text, fg);
}

/*
 * Draw a text field
 */
static void draw_textfield(xgui_widget_t* widget, int x, int y) {
    bool focused = (widget->flags & XGUI_WIDGET_FOCUSED);
    bool enabled = (widget->flags & XGUI_WIDGET_ENABLED);
    xgui_window_t* win = widget->parent;
    
    /* Background */
    xgui_win_rect_filled(win, x, y, widget->width, widget->height, 
                         enabled ? XGUI_WHITE : XGUI_LIGHT_GRAY);
    
    /* Border */
    xgui_win_rect_3d_sunken(win, x, y, widget->width, widget->height);
    
    /* Text */
    int text_x = x + 4;
    int text_y = y + 2;
    
    /* Calculate visible portion */
    int max_chars = (widget->width - 8) / 8;
    char visible_text[128];
    int text_len = strlen(widget->text);
    int start = widget->textfield.scroll_offset;
    
    if (start > text_len) start = text_len;
    int len = text_len - start;
    if (len > max_chars) len = max_chars;
    
    strncpy(visible_text, widget->text + start, len);
    visible_text[len] = '\0';
    
    xgui_win_text_transparent(win, text_x, text_y, visible_text, XGUI_BLACK);
    
    /* Cursor */
    if (focused && enabled) {
        int cursor_x = text_x + (widget->textfield.cursor_pos - start) * 8;
        if (cursor_x >= x + 4 && cursor_x < x + widget->width - 4) {
            xgui_win_vline(win, cursor_x, y + 2, widget->height - 4, XGUI_BLACK);
        }
    }
}

/*
 * Draw a checkbox
 */
static void draw_checkbox(xgui_widget_t* widget, int x, int y) {
    bool checked = widget->checkbox.checked;
    bool enabled = (widget->flags & XGUI_WIDGET_ENABLED);
    xgui_window_t* win = widget->parent;
    
    /* Box */
    xgui_win_rect_filled(win, x, y, 14, 14, enabled ? XGUI_WHITE : XGUI_LIGHT_GRAY);
    xgui_win_rect_3d_sunken(win, x, y, 14, 14);
    
    /* Checkmark */
    if (checked) {
        uint32_t color = enabled ? XGUI_BLACK : XGUI_GRAY;
        xgui_win_line(win, x + 3, y + 7, x + 5, y + 10, color);
        xgui_win_line(win, x + 5, y + 10, x + 11, y + 3, color);
    }
    
    /* Label */
    uint32_t fg = enabled ? XGUI_BLACK : XGUI_GRAY;
    xgui_win_text_transparent(win, x + 18, y, widget->text, fg);
}

/*
 * Draw a widget
 */
void xgui_widget_draw(xgui_widget_t* widget) {
    if (!widget || !(widget->flags & XGUI_WIDGET_VISIBLE)) {
        return;
    }
    
    /* Widget positions are already client-relative */
    int x = widget->x;
    int y = widget->y;
    
    switch (widget->type) {
        case XGUI_WIDGET_LABEL:
            draw_label(widget, x, y);
            break;
        case XGUI_WIDGET_BUTTON:
            draw_button(widget, x, y);
            break;
        case XGUI_WIDGET_TEXTFIELD:
            draw_textfield(widget, x, y);
            break;
        case XGUI_WIDGET_CHECKBOX:
            draw_checkbox(widget, x, y);
            break;
        default:
            break;
    }
}

/*
 * Draw all widgets in a window
 */
void xgui_widgets_draw(xgui_window_t* window) {
    widget_list_t* list = get_widget_list(window);
    if (!list) return;
    
    for (xgui_widget_t* w = list->widgets; w; w = w->next) {
        xgui_widget_draw(w);
    }
}

/*
 * Get widget at position
 */
xgui_widget_t* xgui_widget_at(xgui_window_t* window, int x, int y) {
    widget_list_t* list = get_widget_list(window);
    if (!list) return NULL;
    
    for (xgui_widget_t* w = list->widgets; w; w = w->next) {
        if (!(w->flags & XGUI_WIDGET_VISIBLE)) continue;
        
        if (x >= w->x && x < w->x + w->width &&
            y >= w->y && y < w->y + w->height) {
            return w;
        }
    }
    return NULL;
}

/*
 * Focus a widget
 */
void xgui_widget_focus(xgui_widget_t* widget) {
    if (!widget) return;
    
    widget_list_t* list = get_widget_list(widget->parent);
    if (!list) return;
    
    /* Remove focus from previous */
    if (list->focused && list->focused != widget) {
        list->focused->flags &= ~XGUI_WIDGET_FOCUSED;
    }
    
    /* Focus new widget */
    widget->flags |= XGUI_WIDGET_FOCUSED;
    list->focused = widget;
}

/*
 * Handle text field key input
 */
static bool handle_textfield_key(xgui_widget_t* widget, xgui_event_t* event) {
    if (event->type != XGUI_EVENT_KEY_CHAR && event->type != XGUI_EVENT_KEY_DOWN) {
        return false;
    }
    
    int len = strlen(widget->text);
    
    if (event->type == XGUI_EVENT_KEY_DOWN) {
        uint8_t key = event->key.keycode;
        
        /* Backspace */
        if (key == '\b' && widget->textfield.cursor_pos > 0) {
            /* Remove character before cursor */
            for (int i = widget->textfield.cursor_pos - 1; i < len; i++) {
                widget->text[i] = widget->text[i + 1];
            }
            widget->textfield.cursor_pos--;
            return true;
        }
        
        /* Left arrow */
        if (key == 0x92 && widget->textfield.cursor_pos > 0) {
            widget->textfield.cursor_pos--;
            return true;
        }
        
        /* Right arrow */
        if (key == 0x93 && widget->textfield.cursor_pos < len) {
            widget->textfield.cursor_pos++;
            return true;
        }
    }
    
    if (event->type == XGUI_EVENT_KEY_CHAR) {
        char c = event->key.character;
        
        /* Printable character */
        if (c >= 32 && c < 127 && len < widget->textfield.max_length) {
            /* Insert at cursor */
            for (int i = len; i >= widget->textfield.cursor_pos; i--) {
                widget->text[i + 1] = widget->text[i];
            }
            widget->text[widget->textfield.cursor_pos] = c;
            widget->textfield.cursor_pos++;
            return true;
        }
    }
    
    return false;
}

/*
 * Handle event for widgets
 */
bool xgui_widgets_handle_event(xgui_window_t* window, xgui_event_t* event) {
    widget_list_t* list = get_widget_list(window);
    if (!list) return false;
    
    switch (event->type) {
        case XGUI_EVENT_MOUSE_DOWN: {
            xgui_widget_t* widget = xgui_widget_at(window, event->mouse.x, event->mouse.y);
            
            if (widget && (widget->flags & XGUI_WIDGET_ENABLED)) {
                xgui_widget_focus(widget);
                
                if (widget->type == XGUI_WIDGET_BUTTON) {
                    widget->button.pressed = true;
                    return true;
                }
                if (widget->type == XGUI_WIDGET_CHECKBOX) {
                    widget->checkbox.checked = !widget->checkbox.checked;
                    if (widget->on_click) {
                        widget->on_click(widget);
                    }
                    return true;
                }
                if (widget->type == XGUI_WIDGET_TEXTFIELD) {
                    /* Set cursor position based on click */
                    int rel_x = event->mouse.x - widget->x - 4;
                    int pos = rel_x / 8 + widget->textfield.scroll_offset;
                    int len = strlen(widget->text);
                    if (pos < 0) pos = 0;
                    if (pos > len) pos = len;
                    widget->textfield.cursor_pos = pos;
                    return true;
                }
            }
            break;
        }
        
        case XGUI_EVENT_MOUSE_UP: {
            /* Check for button release */
            for (xgui_widget_t* w = list->widgets; w; w = w->next) {
                if (w->type == XGUI_WIDGET_BUTTON && w->button.pressed) {
                    w->button.pressed = false;
                    
                    /* Check if still over button */
                    if (event->mouse.x >= w->x && event->mouse.x < w->x + w->width &&
                        event->mouse.y >= w->y && event->mouse.y < w->y + w->height) {
                        if (w->on_click) {
                            w->on_click(w);
                        }
                    }
                    return true;
                }
            }
            break;
        }
        
        case XGUI_EVENT_KEY_DOWN:
        case XGUI_EVENT_KEY_CHAR: {
            if (list->focused && list->focused->type == XGUI_WIDGET_TEXTFIELD) {
                return handle_textfield_key(list->focused, event);
            }
            break;
        }
        
        default:
            break;
    }
    
    return false;
}

/*
 * Get checkbox state
 */
bool xgui_checkbox_get_checked(xgui_widget_t* widget) {
    if (widget && widget->type == XGUI_WIDGET_CHECKBOX) {
        return widget->checkbox.checked;
    }
    return false;
}

/*
 * Set checkbox state
 */
void xgui_checkbox_set_checked(xgui_widget_t* widget, bool checked) {
    if (widget && widget->type == XGUI_WIDGET_CHECKBOX) {
        widget->checkbox.checked = checked;
    }
}
