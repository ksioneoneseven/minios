/*
 * MiniOS XGUI Window Manager
 *
 * Manages windows, z-order, focus, and window decorations.
 */

#ifndef _XGUI_WM_H
#define _XGUI_WM_H

#include "types.h"
#include "xgui/event.h"

/* Maximum number of windows */
#define XGUI_MAX_WINDOWS 32

/* Window flags */
#define XGUI_WINDOW_VISIBLE     0x0001
#define XGUI_WINDOW_FOCUSED     0x0002
#define XGUI_WINDOW_MOVABLE     0x0004
#define XGUI_WINDOW_RESIZABLE   0x0008
#define XGUI_WINDOW_CLOSABLE    0x0010
#define XGUI_WINDOW_MINIMIZABLE 0x0020
#define XGUI_WINDOW_MAXIMIZABLE 0x0040
#define XGUI_WINDOW_DECORATED   0x0080
#define XGUI_WINDOW_MODAL       0x0100
#define XGUI_WINDOW_TOPMOST     0x0200
#define XGUI_WINDOW_MINIMIZED  0x0400
#define XGUI_WINDOW_MAXIMIZED  0x0800

/* Default window flags */
#define XGUI_WINDOW_DEFAULT     (XGUI_WINDOW_VISIBLE | XGUI_WINDOW_MOVABLE | \
                                 XGUI_WINDOW_CLOSABLE | XGUI_WINDOW_DECORATED)

/* Window decoration dimensions */
#define XGUI_TITLE_HEIGHT       28
#define XGUI_BORDER_WIDTH       1
#define XGUI_BUTTON_SIZE        12

/* Window event handler callback */
struct xgui_window;
typedef void (*xgui_window_handler_t)(struct xgui_window* win, xgui_event_t* event);

/* Window paint callback */
typedef void (*xgui_window_paint_t)(struct xgui_window* win);

/* Window structure */
typedef struct xgui_window {
    uint32_t id;                    /* Unique window ID */
    char title[64];                 /* Window title */
    
    int x, y;                       /* Position (outer, including decorations) */
    int width, height;              /* Size (outer, including decorations) */
    
    int client_x, client_y;         /* Client area position (relative to window) */
    int client_width, client_height;/* Client area size */
    
    uint16_t flags;                 /* Window flags */
    uint32_t bg_color;              /* Background color */
    
    xgui_window_handler_t handler;  /* Event handler */
    xgui_window_paint_t paint;      /* Paint callback */
    void* user_data;                /* User data pointer */
    
    uint32_t* buffer;               /* Per-window pixel buffer (client area) */
    int buf_width, buf_height;      /* Buffer dimensions */
    bool dirty;                     /* Needs repaint */
    
    /* Saved geometry for maximize/restore */
    int saved_x, saved_y;
    int saved_width, saved_height;
    
    struct xgui_window* parent;     /* Parent window (for dialogs) */
    struct xgui_window* next;       /* Next window in z-order */
    struct xgui_window* prev;       /* Previous window in z-order */
} xgui_window_t;

/*
 * Initialize the window manager
 */
void xgui_wm_init(void);

/*
 * Create a new window
 * Returns window pointer or NULL on failure
 */
xgui_window_t* xgui_window_create(const char* title, int x, int y, 
                                   int width, int height, uint16_t flags);

/*
 * Destroy a window
 */
void xgui_window_destroy(xgui_window_t* win);

/*
 * Show a window
 */
void xgui_window_show(xgui_window_t* win);

/*
 * Hide a window
 */
void xgui_window_hide(xgui_window_t* win);

/*
 * Set window title
 */
void xgui_window_set_title(xgui_window_t* win, const char* title);

/*
 * Move a window
 */
void xgui_window_move(xgui_window_t* win, int x, int y);

/*
 * Resize a window
 */
void xgui_window_resize(xgui_window_t* win, int width, int height);

/*
 * Bring window to front (focus)
 */
void xgui_window_focus(xgui_window_t* win);

/*
 * Minimize a window (hide but keep in taskbar)
 */
void xgui_window_minimize(xgui_window_t* win);

/*
 * Restore a minimized window
 */
void xgui_window_restore(xgui_window_t* win);

/*
 * Maximize or restore a window (toggle)
 */
void xgui_window_maximize(xgui_window_t* win);

/*
 * Set window event handler
 */
void xgui_window_set_handler(xgui_window_t* win, xgui_window_handler_t handler);

/*
 * Set window paint callback
 */
void xgui_window_set_paint(xgui_window_t* win, xgui_window_paint_t paint);

/*
 * Set window user data
 */
void xgui_window_set_userdata(xgui_window_t* win, void* data);

/*
 * Get window user data
 */
void* xgui_window_get_userdata(xgui_window_t* win);

/*
 * Set window background color
 */
void xgui_window_set_bgcolor(xgui_window_t* win, uint32_t color);

/*
 * Get the currently focused window
 */
xgui_window_t* xgui_wm_get_focus(void);

/*
 * Get window at screen coordinates
 */
xgui_window_t* xgui_wm_window_at(int x, int y);

/*
 * Dispatch an event to the appropriate window
 */
void xgui_wm_dispatch_event(xgui_event_t* event);

/*
 * Redraw all windows
 */
void xgui_wm_redraw_all(void);

/*
 * Redraw a specific window
 */
void xgui_wm_redraw_window(xgui_window_t* win);

/*
 * Mark a window region as needing redraw
 */
void xgui_window_invalidate(xgui_window_t* win);

/*
 * Get client area coordinates from window coordinates
 */
void xgui_window_to_client(xgui_window_t* win, int* x, int* y);

/*
 * Get window coordinates from client area coordinates
 */
void xgui_window_from_client(xgui_window_t* win, int* x, int* y);

/*
 * Check if point is in window client area
 */
bool xgui_window_point_in_client(xgui_window_t* win, int x, int y);

/*
 * Get the top window in z-order
 */
xgui_window_t* xgui_wm_get_top(void);

/*
 * Get window count
 */
int xgui_wm_window_count(void);

/*
 * Check if a window drag is in progress
 */
bool xgui_wm_is_dragging(void);

/*
 * Composite all windows onto the screen backbuffer.
 * This blits each window's buffer; does NOT call paint callbacks.
 */
void xgui_wm_composite(void);

/* ---- Window-local drawing API ---- */
/* All coordinates are relative to the window's client area (0,0 = top-left) */

void xgui_win_pixel(xgui_window_t* win, int x, int y, uint32_t color);
void xgui_win_hline(xgui_window_t* win, int x, int y, int len, uint32_t color);
void xgui_win_vline(xgui_window_t* win, int x, int y, int len, uint32_t color);
void xgui_win_rect(xgui_window_t* win, int x, int y, int w, int h, uint32_t color);
void xgui_win_rect_filled(xgui_window_t* win, int x, int y, int w, int h, uint32_t color);
void xgui_win_rect_3d_raised(xgui_window_t* win, int x, int y, int w, int h);
void xgui_win_rect_3d_sunken(xgui_window_t* win, int x, int y, int w, int h);
void xgui_win_line(xgui_window_t* win, int x1, int y1, int x2, int y2, uint32_t color);
void xgui_win_text(xgui_window_t* win, int x, int y, const char* str, uint32_t fg, uint32_t bg);
void xgui_win_text_transparent(xgui_window_t* win, int x, int y, const char* str, uint32_t fg);
void xgui_win_clear(xgui_window_t* win, uint32_t color);

#endif /* _XGUI_WM_H */
