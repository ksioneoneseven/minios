/*
 * MiniOS XGUI Window Manager
 *
 * Manages windows, z-order, focus, and window decorations.
 */

#include "xgui/wm.h"
#include "xgui/display.h"
#include "xgui/font.h"
#include "xgui/event.h"
#include "xgui/widget.h"
#include "xgui/theme.h"
#include "heap.h"
#include "string.h"
#include "serial.h"
#include "stdio.h"

/* Window storage */
static xgui_window_t windows[XGUI_MAX_WINDOWS];
static int window_count = 0;
static uint32_t next_window_id = 1;

/* Z-order linked list (bottom to top) */
static xgui_window_t* window_bottom = NULL;
static xgui_window_t* window_top = NULL;

/* Currently focused window */
static xgui_window_t* focused_window = NULL;

/* Drag state */
static xgui_window_t* drag_window = NULL;
static int drag_offset_x = 0;
static int drag_offset_y = 0;
static bool dragging = false;

/* Traffic light hover state */
static xgui_window_t* hover_btn_win = NULL;  /* Window whose button is hovered */
static int hover_btn_idx = -1;               /* -1=none, 0=close, 1=min, 2=max */

/* Traffic light button layout constants */
#define TL_RADIUS     7
#define TL_START_X    10
#define TL_SPACING    20

/*
 * Find a free window slot
 */
static xgui_window_t* alloc_window(void) {
    for (int i = 0; i < XGUI_MAX_WINDOWS; i++) {
        if (windows[i].id == 0) {
            return &windows[i];
        }
    }
    return NULL;
}

/*
 * Add window to z-order list (at top)
 */
static void zorder_add(xgui_window_t* win) {
    win->prev = window_top;
    win->next = NULL;
    
    if (window_top) {
        window_top->next = win;
    }
    window_top = win;
    
    if (!window_bottom) {
        window_bottom = win;
    }
}

/*
 * Remove window from z-order list
 */
static void zorder_remove(xgui_window_t* win) {
    if (win->prev) {
        win->prev->next = win->next;
    } else {
        window_bottom = win->next;
    }
    
    if (win->next) {
        win->next->prev = win->prev;
    } else {
        window_top = win->prev;
    }
    
    win->prev = NULL;
    win->next = NULL;
}

/*
 * Bring window to top of z-order
 */
static void zorder_to_top(xgui_window_t* win) {
    if (win == window_top) {
        return;  /* Already at top */
    }
    
    zorder_remove(win);
    zorder_add(win);
}

/*
 * Calculate client area based on window flags
 */
static void calculate_client_area(xgui_window_t* win) {
    if (win->flags & XGUI_WINDOW_DECORATED) {
        win->client_x = XGUI_BORDER_WIDTH;
        win->client_y = XGUI_TITLE_HEIGHT;
        win->client_width = win->width - (2 * XGUI_BORDER_WIDTH);
        win->client_height = win->height - XGUI_TITLE_HEIGHT - XGUI_BORDER_WIDTH;
    } else {
        win->client_x = 0;
        win->client_y = 0;
        win->client_width = win->width;
        win->client_height = win->height;
    }
}

/*
 * Blend two colors: alpha 0=c1, 255=c2
 */
static uint32_t wm_blend(uint32_t c1, uint32_t c2, int alpha) {
    int r1 = (c1 >> 16) & 0xFF, g1 = (c1 >> 8) & 0xFF, b1 = c1 & 0xFF;
    int r2 = (c2 >> 16) & 0xFF, g2 = (c2 >> 8) & 0xFF, b2 = c2 & 0xFF;
    int r = r1 + ((r2 - r1) * alpha) / 255;
    int g = g1 + ((g2 - g1) * alpha) / 255;
    int b = b1 + ((b2 - b1) * alpha) / 255;
    return XGUI_RGB(r, g, b);
}

/*
 * Draw window decorations — Aqua-inspired glossy title bar
 */
static void draw_decorations(xgui_window_t* win) {
    if (!(win->flags & XGUI_WINDOW_DECORATED)) {
        return;
    }
    
    int x = win->x;
    int y = win->y;
    int w = win->width;
    int h = win->height;
    int bw = XGUI_BORDER_WIDTH;
    int tb_h = XGUI_TITLE_HEIGHT;
    
    bool active = (win->flags & XGUI_WINDOW_FOCUSED);
    uint32_t title_base = active ? xgui_theme_current()->title_active
                                 : xgui_theme_current()->title_inactive;
    
    /* --- Outer border --- */
    uint32_t brd = active ? xgui_theme_current()->border
                          : wm_blend(xgui_theme_current()->border, XGUI_RGB(180, 180, 180), 128);
    xgui_display_rect(x, y, w, h, brd);
    
    /* --- Glossy title bar gradient --- */
    int tb_x = x + bw;
    int tb_y = y + bw;
    int tb_w = w - bw * 2;
    int tb_real_h = tb_h - bw;
    
    /* Extract base color components */
    int br = (title_base >> 16) & 0xFF;
    int bg = (title_base >> 8) & 0xFF;
    int bb = title_base & 0xFF;
    
    for (int row = 0; row < tb_real_h; row++) {
        int r, g, b;
        
        if (row < tb_real_h / 2) {
            /* Top half: lighter, glossy highlight */
            int t = (row * 255) / (tb_real_h / 2);
            /* Start very bright, blend toward base */
            int hi_r = br + (255 - br) * 6 / 10;
            int hi_g = bg + (255 - bg) * 6 / 10;
            int hi_b = bb + (255 - bb) * 6 / 10;
            r = hi_r + ((br + 30) - hi_r) * t / 255;
            g = hi_g + ((bg + 30) - hi_g) * t / 255;
            b = hi_b + ((bb + 30) - hi_b) * t / 255;
        } else {
            /* Bottom half: base to slightly darker */
            int t = ((row - tb_real_h / 2) * 255) / (tb_real_h / 2);
            r = br + 10 - (t * 30) / 255;
            g = bg + 10 - (t * 30) / 255;
            b = bb + 10 - (t * 30) / 255;
        }
        
        if (r > 255) r = 255;
        if (r < 0) r = 0;
        if (g > 255) g = 255;
        if (g < 0) g = 0;
        if (b > 255) b = 255;
        if (b < 0) b = 0;
        
        xgui_display_hline(tb_x, tb_y + row, tb_w, XGUI_RGB(r, g, b));
    }
    
    /* Glossy highlight line at very top */
    if (active) {
        uint32_t hi = XGUI_RGB(
            br + (255 - br) * 7 / 10,
            bg + (255 - bg) * 7 / 10,
            bb + (255 - bb) * 7 / 10
        );
        xgui_display_hline(tb_x, tb_y, tb_w, hi);
    }
    
    /* Subtle separator between title bar and client area */
    xgui_display_hline(tb_x, tb_y + tb_real_h - 1, tb_w,
                       wm_blend(title_base, XGUI_BLACK, 60));
    
    /* --- Title text (centered with shadow) --- */
    int title_len = strlen(win->title);
    int text_w = title_len * 8; /* approximate: 8px per char */
    int text_x = x + (w - text_w) / 2;
    int text_y = y + bw + (tb_real_h - 12) / 2;
    if (text_x < x + bw + 50) text_x = x + bw + 50; /* Don't overlap buttons */
    
    /* Text shadow */
    if (active) {
        uint32_t shadow = wm_blend(title_base, XGUI_BLACK, 180);
        xgui_display_text_transparent(text_x + 1, text_y + 1, win->title, shadow);
    }
    /* Title text */
    uint32_t title_text = active ? XGUI_WHITE : XGUI_RGB(80, 80, 80);
    xgui_display_text_transparent(text_x, text_y, win->title, title_text);
    
    /* --- Traffic light buttons (left side, Aqua style) --- */
    int btn_cy = y + bw + tb_real_h / 2;
    int btn_r = TL_RADIUS;
    bool any_hover = (hover_btn_win == win && hover_btn_idx >= 0);
    
    /* Colors for active/inactive */
    uint32_t cols[3], edges[3], darks[3];
    if (active) {
        cols[0]  = XGUI_RGB(255, 95, 87);   edges[0] = XGUI_RGB(190, 50, 40);   darks[0] = XGUI_RGB(130, 30, 20);
        cols[1]  = XGUI_RGB(255, 189, 46);  edges[1] = XGUI_RGB(190, 140, 20);  darks[1] = XGUI_RGB(140, 100, 10);
        cols[2]  = XGUI_RGB(39, 201, 63);   edges[2] = XGUI_RGB(20, 145, 40);   darks[2] = XGUI_RGB(10, 100, 25);
    } else {
        for (int i = 0; i < 3; i++) {
            cols[i]  = XGUI_RGB(190, 190, 190);
            edges[i] = XGUI_RGB(155, 155, 155);
            darks[i] = XGUI_RGB(120, 120, 120);
        }
    }

    for (int bi = 0; bi < 3; bi++) {
        /* Skip maximize if not maximizable */
        if (bi == 2 && !(win->flags & XGUI_WINDOW_MAXIMIZABLE)) continue;
        /* Skip close if not closable */
        if (bi == 0 && !(win->flags & XGUI_WINDOW_CLOSABLE)) continue;

        int cx = x + bw + TL_START_X + bi * TL_SPACING;
        uint32_t col = cols[bi];
        uint32_t edge = edges[bi];
        bool hovered = (hover_btn_win == win && hover_btn_idx == bi);

        /* Brighten on hover */
        if (hovered) {
            col = wm_blend(col, XGUI_WHITE, 50);
        }

        /* Draw filled circle with smooth edge (anti-aliased) */
        int r2_outer = btn_r * btn_r;
        int r2_inner = (btn_r - 1) * (btn_r - 1);
        for (int dy = -btn_r - 1; dy <= btn_r + 1; dy++) {
            for (int dx = -btn_r - 1; dx <= btn_r + 1; dx++) {
                int d2 = dx * dx + dy * dy;
                if (d2 <= r2_inner) {
                    /* Solid fill */
                    uint32_t pc = col;
                    /* Glossy highlight: top half gets brighter */
                    if (dy < 0) {
                        int alpha = 40 + (-dy * 50) / btn_r;
                        pc = wm_blend(col, XGUI_WHITE, alpha);
                    }
                    /* Subtle bottom shadow */
                    if (dy > btn_r / 2) {
                        int alpha = ((dy - btn_r / 2) * 30) / (btn_r / 2);
                        pc = wm_blend(pc, darks[bi], alpha);
                    }
                    xgui_display_pixel(cx + dx, btn_cy + dy, pc);
                } else if (d2 <= r2_outer) {
                    /* Edge ring */
                    xgui_display_pixel(cx + dx, btn_cy + dy, edge);
                }
            }
        }

        /* Draw hover symbols */
        if (any_hover && active) {
            uint32_t sym = darks[bi];
            if (bi == 0) {
                /* Close: × symbol */
                for (int s = -2; s <= 2; s++) {
                    xgui_display_pixel(cx + s, btn_cy + s, sym);
                    xgui_display_pixel(cx + s + 1, btn_cy + s, sym);
                    xgui_display_pixel(cx + s, btn_cy - s, sym);
                    xgui_display_pixel(cx + s + 1, btn_cy - s, sym);
                }
            } else if (bi == 1) {
                /* Minimize: − horizontal line */
                xgui_display_hline(cx - 3, btn_cy, 7, sym);
                xgui_display_hline(cx - 3, btn_cy + 1, 7, sym);
            } else if (bi == 2) {
                /* Maximize: + symbol */
                xgui_display_hline(cx - 3, btn_cy, 7, sym);
                xgui_display_hline(cx - 3, btn_cy + 1, 7, sym);
                xgui_display_vline(cx, btn_cy - 3, 7, sym);
                xgui_display_vline(cx + 1, btn_cy - 3, 7, sym);
            }
        }
    }
}

/*
 * Initialize the window manager
 */
void xgui_wm_init(void) {
    memset(windows, 0, sizeof(windows));
    window_count = 0;
    next_window_id = 1;
    window_bottom = NULL;
    window_top = NULL;
    focused_window = NULL;
    drag_window = NULL;
    dragging = false;
}

/*
 * Create a new window
 */
xgui_window_t* xgui_window_create(const char* title, int x, int y,
                                   int width, int height, uint16_t flags) {
    xgui_window_t* win = alloc_window();
    if (!win) {
        return NULL;
    }
    
    win->id = next_window_id++;
    strncpy(win->title, title, sizeof(win->title) - 1);
    win->title[sizeof(win->title) - 1] = '\0';
    
    win->x = x;
    win->y = y;
    win->width = width;
    win->height = height;
    win->flags = flags;
    win->bg_color = xgui_theme_current()->window_bg;
    
    win->handler = NULL;
    win->paint = NULL;
    win->user_data = NULL;
    win->parent = NULL;
    
    calculate_client_area(win);
    
    /* Allocate per-window pixel buffer for client area */
    win->buf_width = win->client_width;
    win->buf_height = win->client_height;
    uint32_t buf_size = (uint32_t)win->buf_width * (uint32_t)win->buf_height * sizeof(uint32_t);
    win->buffer = (uint32_t*)kmalloc(buf_size);
    if (!win->buffer) {
        serial_write_string("XGUI: window buffer alloc failed\n");
        memset(win, 0, sizeof(xgui_window_t));
        return NULL;
    }
    /* Fill buffer with background color */
    for (int i = 0; i < win->buf_width * win->buf_height; i++) {
        win->buffer[i] = win->bg_color;
    }
    win->dirty = true;
    
    /* Add to z-order */
    zorder_add(win);
    window_count++;
    
    /* Focus the new window */
    if (flags & XGUI_WINDOW_VISIBLE) {
        xgui_window_focus(win);
    }
    
    return win;
}

/*
 * Destroy a window
 */
void xgui_window_destroy(xgui_window_t* win) {
    if (!win || win->id == 0) {
        return;
    }
    
    /* Destroy all widgets for this window */
    xgui_widgets_destroy_all(win);
    
    /* Free window buffer */
    if (win->buffer) {
        kfree(win->buffer);
        win->buffer = NULL;
    }
    
    /* Remove from z-order */
    zorder_remove(win);
    
    /* Update focus if this was focused */
    if (focused_window == win) {
        focused_window = window_top;
        if (focused_window) {
            focused_window->flags |= XGUI_WINDOW_FOCUSED;
        }
    }
    
    /* Clear the slot */
    memset(win, 0, sizeof(xgui_window_t));
    window_count--;
}

/*
 * Show a window
 */
void xgui_window_show(xgui_window_t* win) {
    if (win) {
        win->flags |= XGUI_WINDOW_VISIBLE;
    }
}

/*
 * Hide a window
 */
void xgui_window_hide(xgui_window_t* win) {
    if (win) {
        win->flags &= ~XGUI_WINDOW_VISIBLE;
    }
}

/*
 * Set window title
 */
void xgui_window_set_title(xgui_window_t* win, const char* title) {
    if (win) {
        strncpy(win->title, title, sizeof(win->title) - 1);
        win->title[sizeof(win->title) - 1] = '\0';
    }
}

/*
 * Move a window
 */
void xgui_window_move(xgui_window_t* win, int x, int y) {
    if (win) {
        win->x = x;
        win->y = y;
    }
}

/*
 * Resize a window
 */
void xgui_window_resize(xgui_window_t* win, int width, int height) {
    if (win) {
        win->width = width;
        win->height = height;
        calculate_client_area(win);
    }
}

/*
 * Bring window to front (focus)
 */
void xgui_window_focus(xgui_window_t* win) {
    if (!win) {
        return;
    }
    
    /* Remove focus from previous window */
    if (focused_window && focused_window != win) {
        focused_window->flags &= ~XGUI_WINDOW_FOCUSED;
        
        /* Send blur event */
        xgui_event_t event;
        memset(&event, 0, sizeof(event));
        event.type = XGUI_EVENT_WINDOW_BLUR;
        event.window_id = focused_window->id;
        if (focused_window->handler) {
            focused_window->handler(focused_window, &event);
        }
    }
    
    /* Focus new window */
    win->flags |= XGUI_WINDOW_FOCUSED;
    focused_window = win;
    
    /* Bring to top of z-order */
    zorder_to_top(win);
    
    /* Send focus event */
    xgui_event_t event;
    memset(&event, 0, sizeof(event));
    event.type = XGUI_EVENT_WINDOW_FOCUS;
    event.window_id = win->id;
    if (win->handler) {
        win->handler(win, &event);
    }
}

/*
 * Set window event handler
 */
void xgui_window_set_handler(xgui_window_t* win, xgui_window_handler_t handler) {
    if (win) {
        win->handler = handler;
    }
}

/*
 * Set window paint callback
 */
void xgui_window_set_paint(xgui_window_t* win, xgui_window_paint_t paint) {
    if (win) {
        win->paint = paint;
    }
}

/*
 * Set window user data
 */
void xgui_window_set_userdata(xgui_window_t* win, void* data) {
    if (win) {
        win->user_data = data;
    }
}

/*
 * Get window user data
 */
void* xgui_window_get_userdata(xgui_window_t* win) {
    return win ? win->user_data : NULL;
}

/*
 * Set window background color
 */
void xgui_window_set_bgcolor(xgui_window_t* win, uint32_t color) {
    if (win) {
        win->bg_color = color;
    }
}

/*
 * Get the currently focused window
 */
xgui_window_t* xgui_wm_get_focus(void) {
    return focused_window;
}

/*
 * Get window at screen coordinates
 */
xgui_window_t* xgui_wm_window_at(int x, int y) {
    /* Search from top to bottom, skip minimized */
    for (xgui_window_t* win = window_top; win; win = win->prev) {
        if (!(win->flags & XGUI_WINDOW_VISIBLE) || (win->flags & XGUI_WINDOW_MINIMIZED)) {
            continue;
        }
        
        if (x >= win->x && x < win->x + win->width &&
            y >= win->y && y < win->y + win->height) {
            return win;
        }
    }
    return NULL;
}

/*
 * Check if click is on close button (left-side traffic light, first circle)
 */
static bool is_close_button(xgui_window_t* win, int x, int y) {
    if (!(win->flags & XGUI_WINDOW_CLOSABLE) || !(win->flags & XGUI_WINDOW_DECORATED)) {
        return false;
    }
    
    int bw = XGUI_BORDER_WIDTH;
    int tb_real_h = XGUI_TITLE_HEIGHT - bw;
    int cx = win->x + bw + TL_START_X;
    int cy = win->y + bw + tb_real_h / 2;
    int r = TL_RADIUS + 1;
    
    int dx = x - cx;
    int dy = y - cy;
    return (dx * dx + dy * dy <= r * r);
}

/*
 * Check if click is on minimize button (second traffic light circle, yellow)
 */
static bool is_minimize_button(xgui_window_t* win, int x, int y) {
    if (!(win->flags & XGUI_WINDOW_DECORATED)) {
        return false;
    }

    int bw = XGUI_BORDER_WIDTH;
    int tb_real_h = XGUI_TITLE_HEIGHT - bw;
    int cx = win->x + bw + TL_START_X + TL_SPACING;
    int cy = win->y + bw + tb_real_h / 2;
    int r = TL_RADIUS + 1;

    int dx = x - cx;
    int dy = y - cy;
    return (dx * dx + dy * dy <= r * r);
}

/*
 * Check if click is on maximize button (third traffic light circle, green)
 */
static bool is_maximize_button(xgui_window_t* win, int x, int y) {
    if (!(win->flags & XGUI_WINDOW_MAXIMIZABLE)) {
        return false;
    }

    int bw = XGUI_BORDER_WIDTH;
    int tb_real_h = XGUI_TITLE_HEIGHT - bw;
    int cx = win->x + bw + TL_START_X + TL_SPACING * 2;
    int cy = win->y + bw + tb_real_h / 2;
    int r = TL_RADIUS + 1;

    int dx = x - cx;
    int dy = y - cy;
    return (dx * dx + dy * dy <= r * r);
}

/*
 * Check if click is on title bar
 */
static bool is_title_bar(xgui_window_t* win, int x, int y) {
    if (!(win->flags & XGUI_WINDOW_DECORATED)) {
        return false;
    }
    
    int title_x = win->x + XGUI_BORDER_WIDTH;
    int title_y = win->y + XGUI_BORDER_WIDTH;
    int title_w = win->width - (2 * XGUI_BORDER_WIDTH);
    int title_h = XGUI_TITLE_HEIGHT - XGUI_BORDER_WIDTH;
    
    return (x >= title_x && x < title_x + title_w &&
            y >= title_y && y < title_y + title_h);
}

/*
 * Dispatch an event to the appropriate window
 */
void xgui_wm_dispatch_event(xgui_event_t* event) {
    switch (event->type) {
        case XGUI_EVENT_MOUSE_DOWN: {
            xgui_window_t* win = xgui_wm_window_at(event->mouse.x, event->mouse.y);
            
            if (win) {
                /* Focus the window */
                if (win != focused_window) {
                    xgui_window_focus(win);
                }
                
                /* Check for close button click */
                if (is_close_button(win, event->mouse.x, event->mouse.y)) {
                    xgui_event_t close_event;
                    memset(&close_event, 0, sizeof(close_event));
                    close_event.type = XGUI_EVENT_WINDOW_CLOSE;
                    close_event.window_id = win->id;
                    if (win->handler) {
                        win->handler(win, &close_event);
                    }
                    return;
                }

                /* Check for maximize button click */
                if (is_maximize_button(win, event->mouse.x, event->mouse.y)) {
                    xgui_window_maximize(win);
                    return;
                }

                /* Check for minimize button click */
                if (is_minimize_button(win, event->mouse.x, event->mouse.y)) {
                    xgui_window_minimize(win);
                    /* Notify handler so apps can save state */
                    if (win->handler) {
                        xgui_event_t move_event;
                        move_event.type = XGUI_EVENT_WINDOW_MOVE;
                        move_event.window_id = win->id;
                        win->handler(win, &move_event);
                    }
                    return;
                }
                
                /* Check for title bar drag */
                if (is_title_bar(win, event->mouse.x, event->mouse.y) &&
                    (win->flags & XGUI_WINDOW_MOVABLE)) {
                    drag_window = win;
                    drag_offset_x = event->mouse.x - win->x;
                    drag_offset_y = event->mouse.y - win->y;
                    dragging = true;
                    return;
                }
                
                /* Pass to window handler with client-relative coordinates */
                if (win->handler) {
                    xgui_event_t client_event = *event;
                    client_event.window_id = win->id;
                    client_event.mouse.x -= (win->x + win->client_x);
                    client_event.mouse.y -= (win->y + win->client_y);
                    win->handler(win, &client_event);
                    win->dirty = true;
                }
            }
            break;
        }
        
        case XGUI_EVENT_MOUSE_UP: {
            if (dragging) {
                /* Notify the window it was moved */
                if (drag_window && drag_window->handler) {
                    xgui_event_t move_event;
                    move_event.type = XGUI_EVENT_WINDOW_MOVE;
                    move_event.window_id = drag_window->id;
                    drag_window->handler(drag_window, &move_event);
                }
                dragging = false;
                drag_window = NULL;
            }
            
            /* Pass to focused window */
            if (focused_window && focused_window->handler) {
                xgui_event_t client_event = *event;
                client_event.window_id = focused_window->id;
                client_event.mouse.x -= (focused_window->x + focused_window->client_x);
                client_event.mouse.y -= (focused_window->y + focused_window->client_y);
                focused_window->handler(focused_window, &client_event);
                focused_window->dirty = true;
            }
            break;
        }
        
        case XGUI_EVENT_MOUSE_MOVE: {
            if (dragging && drag_window) {
                /* Move the window */
                int new_x = event->mouse.x - drag_offset_x;
                int new_y = event->mouse.y - drag_offset_y;
                
                /* Keep title bar on screen */
                if (new_y < 0) new_y = 0;
                
                xgui_window_move(drag_window, new_x, new_y);
            } else {
                /* Update traffic light hover state */
                xgui_window_t* old_hw = hover_btn_win;
                int old_hi = hover_btn_idx;
                hover_btn_win = NULL;
                hover_btn_idx = -1;

                xgui_window_t* top = xgui_wm_window_at(event->mouse.x, event->mouse.y);
                if (top && (top->flags & XGUI_WINDOW_DECORATED)) {
                    if (is_close_button(top, event->mouse.x, event->mouse.y)) {
                        hover_btn_win = top;
                        hover_btn_idx = 0;
                    } else if (is_minimize_button(top, event->mouse.x, event->mouse.y)) {
                        hover_btn_win = top;
                        hover_btn_idx = 1;
                    } else if (is_maximize_button(top, event->mouse.x, event->mouse.y)) {
                        hover_btn_win = top;
                        hover_btn_idx = 2;
                    }
                }

                /* Force redraw if hover state changed */
                if (hover_btn_win != old_hw || hover_btn_idx != old_hi) {
                    /* Decorations are drawn during composite, so just mark dirty */
                }

                /* Pass to focused window */
                if (focused_window && focused_window->handler) {
                    xgui_event_t client_event = *event;
                    client_event.window_id = focused_window->id;
                    client_event.mouse.x -= (focused_window->x + focused_window->client_x);
                    client_event.mouse.y -= (focused_window->y + focused_window->client_y);
                    focused_window->handler(focused_window, &client_event);
                }
            }
            break;
        }
        
        case XGUI_EVENT_KEY_DOWN:
        case XGUI_EVENT_KEY_UP:
        case XGUI_EVENT_KEY_CHAR: {
            /* Send keyboard events to focused window */
            if (focused_window && focused_window->handler) {
                xgui_event_t key_event = *event;
                key_event.window_id = focused_window->id;
                focused_window->handler(focused_window, &key_event);
                focused_window->dirty = true;
            }
            break;
        }
        
        default:
            break;
    }
}

/*
 * Repaint dirty windows (calls paint callbacks into window buffers)
 */
void xgui_wm_redraw_all(void) {
    for (xgui_window_t* win = window_bottom; win; win = win->next) {
        if ((win->flags & XGUI_WINDOW_VISIBLE) && !(win->flags & XGUI_WINDOW_MINIMIZED) && win->dirty && win->buffer) {
            /* Clear buffer with background color */
            for (int i = 0; i < win->buf_width * win->buf_height; i++) {
                win->buffer[i] = win->bg_color;
            }
            /* Call paint callback - it draws into win->buffer */
            if (win->paint) {
                win->paint(win);
            }
            win->dirty = false;
        }
    }
}

/*
 * Composite all windows onto the screen backbuffer.
 * Draws decorations and blits window buffers. Does NOT call paint callbacks.
 */
void xgui_wm_composite(void) {
    int sw = xgui_display_width();
    int sh = xgui_display_height();
    uint32_t* screen = xgui_display_get_backbuffer();

    for (xgui_window_t* win = window_bottom; win; win = win->next) {
        if (!(win->flags & XGUI_WINDOW_VISIBLE) || (win->flags & XGUI_WINDOW_MINIMIZED)) continue;

        /* Drop shadow behind window (right and bottom edges) */
        {
            int wx = win->x;
            int wy = win->y;
            int ww = win->width;
            int wh = win->height;
            int shadow_depth = 8;

            for (int d = 1; d <= shadow_depth; d++) {
                int alpha = 100 - (d * 90) / shadow_depth;
                if (alpha < 8) alpha = 8;
                int inv = 255 - alpha;

                /* Right edge shadow strip */
                int rx = wx + ww + d - 1;
                if (rx >= 0 && rx < sw) {
                    for (int row = wy + d; row < wy + wh + d && row < sh; row++) {
                        if (row < 0) continue;
                        uint32_t bg = screen[row * sw + rx];
                        int r = ((bg >> 16) & 0xFF) * inv >> 8;
                        int g = ((bg >> 8) & 0xFF) * inv >> 8;
                        int b = (bg & 0xFF) * inv >> 8;
                        screen[row * sw + rx] = 0xFF000000 | (r << 16) | (g << 8) | b;
                    }
                }

                /* Bottom edge shadow strip */
                int by = wy + wh + d - 1;
                if (by >= 0 && by < sh) {
                    for (int col = wx + d; col < wx + ww + d && col < sw; col++) {
                        if (col < 0) continue;
                        uint32_t bg = screen[by * sw + col];
                        int r = ((bg >> 16) & 0xFF) * inv >> 8;
                        int g = ((bg >> 8) & 0xFF) * inv >> 8;
                        int b = (bg & 0xFF) * inv >> 8;
                        screen[by * sw + col] = 0xFF000000 | (r << 16) | (g << 8) | b;
                    }
                }
            }
        }

        /* Draw decorations onto screen backbuffer */
        draw_decorations(win);

        /* Blit window buffer onto screen backbuffer */
        if (win->buffer) {
            int sx = win->x + win->client_x;
            int sy = win->y + win->client_y;

            int blit_w = win->client_width;
            int blit_h = win->client_height;

            for (int row = 0; row < blit_h; row++) {
                int dy = sy + row;
                if (dy < 0 || dy >= sh) continue;
                for (int col = 0; col < blit_w; col++) {
                    int dx = sx + col;
                    if (dx < 0 || dx >= sw) continue;
                    screen[dy * sw + dx] = win->buffer[row * win->buf_width + col];
                }
            }
        }

        /* Mark window area dirty on screen (include shadow) */
        xgui_display_mark_dirty(win->y, win->y + win->height + 8);
    }
}

/*
 * Redraw a specific window (force repaint + composite)
 */
void xgui_wm_redraw_window(xgui_window_t* win) {
    if (!win || !(win->flags & XGUI_WINDOW_VISIBLE)) {
        return;
    }
    win->dirty = true;
}

/*
 * Mark a window as needing redraw
 */
void xgui_window_invalidate(xgui_window_t* win) {
    if (win) {
        win->dirty = true;
    }
}

/*
 * Get client area coordinates from window coordinates
 */
void xgui_window_to_client(xgui_window_t* win, int* x, int* y) {
    if (win && x && y) {
        *x -= win->client_x;
        *y -= win->client_y;
    }
}

/*
 * Get window coordinates from client area coordinates
 */
void xgui_window_from_client(xgui_window_t* win, int* x, int* y) {
    if (win && x && y) {
        *x += win->client_x;
        *y += win->client_y;
    }
}

/*
 * Check if point is in window client area
 */
bool xgui_window_point_in_client(xgui_window_t* win, int x, int y) {
    if (!win) {
        return false;
    }
    
    int cx = win->x + win->client_x;
    int cy = win->y + win->client_y;
    
    return (x >= cx && x < cx + win->client_width &&
            y >= cy && y < cy + win->client_height);
}

/*
 * Get the top window in z-order
 */
xgui_window_t* xgui_wm_get_top(void) {
    return window_top;
}

/*
 * Get window count
 */
int xgui_wm_window_count(void) {
    return window_count;
}

/*
 * Minimize a window
 */
void xgui_window_minimize(xgui_window_t* win) {
    if (!win) return;
    win->flags |= XGUI_WINDOW_MINIMIZED;

    /* If this was the focused window, focus the next visible one */
    if (focused_window == win) {
        win->flags &= ~XGUI_WINDOW_FOCUSED;
        focused_window = NULL;
        /* Find topmost non-minimized visible window */
        for (xgui_window_t* w = window_top; w; w = w->prev) {
            if ((w->flags & XGUI_WINDOW_VISIBLE) && !(w->flags & XGUI_WINDOW_MINIMIZED) && w != win) {
                xgui_window_focus(w);
                break;
            }
        }
    }
}

/*
 * Restore a minimized window
 */
void xgui_window_restore(xgui_window_t* win) {
    if (!win) return;
    win->flags &= ~XGUI_WINDOW_MINIMIZED;
    win->dirty = true;
    xgui_window_focus(win);
}

/*
 * Maximize or restore a window (toggle)
 */
void xgui_window_maximize(xgui_window_t* win) {
    if (!win) return;

    if (win->flags & XGUI_WINDOW_MAXIMIZED) {
        /* Restore to saved geometry */
        win->x = win->saved_x;
        win->y = win->saved_y;
        win->width = win->saved_width;
        win->height = win->saved_height;
        win->flags &= ~XGUI_WINDOW_MAXIMIZED;
    } else {
        /* Save current geometry */
        win->saved_x = win->x;
        win->saved_y = win->y;
        win->saved_width = win->width;
        win->saved_height = win->height;

        /* Maximize to fill screen (leave room for panel at bottom) */
        int sw = xgui_display_width();
        int sh = xgui_display_height();
        int panel_h = 26;
        win->x = 0;
        win->y = 0;
        win->width = sw;
        win->height = sh - panel_h;
        win->flags |= XGUI_WINDOW_MAXIMIZED;
    }

    /* Recalculate client area */
    calculate_client_area(win);

    /* Reallocate pixel buffer for new size */
    if (win->buffer) {
        kfree(win->buffer);
        win->buffer = NULL;
    }
    win->buf_width = win->client_width;
    win->buf_height = win->client_height;
    uint32_t buf_size = (uint32_t)win->buf_width * (uint32_t)win->buf_height * sizeof(uint32_t);
    win->buffer = (uint32_t*)kmalloc(buf_size);
    if (win->buffer) {
        for (int i = 0; i < win->buf_width * win->buf_height; i++) {
            win->buffer[i] = win->bg_color;
        }
    }

    win->dirty = true;
}

/*
 * Check if a window drag is in progress
 */
bool xgui_wm_is_dragging(void) {
    return dragging;
}

/* ================================================================
 * Window-local drawing API
 * All coordinates are relative to the window's client area (0,0 = top-left).
 * These draw into the window's own pixel buffer, NOT the screen backbuffer.
 * ================================================================ */

void xgui_win_pixel(xgui_window_t* win, int x, int y, uint32_t color) {
    if (!win || !win->buffer) return;
    if (x < 0 || x >= win->buf_width || y < 0 || y >= win->buf_height) return;
    win->buffer[y * win->buf_width + x] = color;
}

void xgui_win_hline(xgui_window_t* win, int x, int y, int len, uint32_t color) {
    if (!win || !win->buffer) return;
    if (y < 0 || y >= win->buf_height || len <= 0) return;
    if (x < 0) { len += x; x = 0; }
    if (x + len > win->buf_width) len = win->buf_width - x;
    if (len <= 0) return;
    uint32_t* p = &win->buffer[y * win->buf_width + x];
    while (len--) *p++ = color;
}

void xgui_win_vline(xgui_window_t* win, int x, int y, int len, uint32_t color) {
    if (!win || !win->buffer) return;
    if (x < 0 || x >= win->buf_width || len <= 0) return;
    if (y < 0) { len += y; y = 0; }
    if (y + len > win->buf_height) len = win->buf_height - y;
    if (len <= 0) return;
    uint32_t* p = &win->buffer[y * win->buf_width + x];
    while (len--) { *p = color; p += win->buf_width; }
}

void xgui_win_rect(xgui_window_t* win, int x, int y, int w, int h, uint32_t color) {
    if (w <= 0 || h <= 0) return;
    xgui_win_hline(win, x, y, w, color);
    xgui_win_hline(win, x, y + h - 1, w, color);
    xgui_win_vline(win, x, y, h, color);
    xgui_win_vline(win, x + w - 1, y, h, color);
}

void xgui_win_rect_filled(xgui_window_t* win, int x, int y, int w, int h, uint32_t color) {
    if (!win || !win->buffer || w <= 0 || h <= 0) return;
    int x1 = x, y1 = y, x2 = x + w, y2 = y + h;
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 > win->buf_width) x2 = win->buf_width;
    if (y2 > win->buf_height) y2 = win->buf_height;
    if (x1 >= x2 || y1 >= y2) return;
    int cw = x2 - x1;
    for (int row = y1; row < y2; row++) {
        uint32_t* p = &win->buffer[row * win->buf_width + x1];
        for (int col = 0; col < cw; col++) *p++ = color;
    }
}

void xgui_win_rect_3d_raised(xgui_window_t* win, int x, int y, int w, int h) {
    if (w <= 0 || h <= 0) return;
    xgui_win_hline(win, x, y, w - 1, XGUI_WHITE);
    xgui_win_vline(win, x, y, h - 1, XGUI_WHITE);
    xgui_win_hline(win, x + 1, y + 1, w - 3, XGUI_LIGHT_GRAY);
    xgui_win_vline(win, x + 1, y + 1, h - 3, XGUI_LIGHT_GRAY);
    xgui_win_hline(win, x, y + h - 1, w, XGUI_BLACK);
    xgui_win_vline(win, x + w - 1, y, h, XGUI_BLACK);
    xgui_win_hline(win, x + 1, y + h - 2, w - 2, XGUI_DARK_GRAY);
    xgui_win_vline(win, x + w - 2, y + 1, h - 2, XGUI_DARK_GRAY);
}

void xgui_win_rect_3d_sunken(xgui_window_t* win, int x, int y, int w, int h) {
    if (w <= 0 || h <= 0) return;
    xgui_win_hline(win, x, y, w - 1, XGUI_BLACK);
    xgui_win_vline(win, x, y, h - 1, XGUI_BLACK);
    xgui_win_hline(win, x + 1, y + 1, w - 3, XGUI_DARK_GRAY);
    xgui_win_vline(win, x + 1, y + 1, h - 3, XGUI_DARK_GRAY);
    xgui_win_hline(win, x, y + h - 1, w, XGUI_WHITE);
    xgui_win_vline(win, x + w - 1, y, h, XGUI_WHITE);
    xgui_win_hline(win, x + 1, y + h - 2, w - 2, XGUI_LIGHT_GRAY);
    xgui_win_vline(win, x + w - 2, y + 1, h - 2, XGUI_LIGHT_GRAY);
}

void xgui_win_line(xgui_window_t* win, int x1, int y1, int x2, int y2, uint32_t color) {
    int dx = x2 - x1;
    int dy = y2 - y1;
    int sx = (dx > 0) ? 1 : -1;
    int sy = (dy > 0) ? 1 : -1;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    int err = dx - dy;
    while (1) {
        xgui_win_pixel(win, x1, y1, color);
        if (x1 == x2 && y1 == y2) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x1 += sx; }
        if (e2 < dx) { err += dx; y1 += sy; }
    }
}

void xgui_win_text(xgui_window_t* win, int x, int y, const char* str, uint32_t fg, uint32_t bg) {
    if (!win || !win->buffer || !str) return;
    while (*str) {
        const uint8_t* glyph = xgui_font_get_glyph(*str);
        for (int row = 0; row < XGUI_FONT_HEIGHT; row++) {
            uint8_t bits = glyph[row];
            for (int col = 0; col < XGUI_FONT_WIDTH; col++) {
                if (bits & (0x80 >> col)) {
                    xgui_win_pixel(win, x + col, y + row, fg);
                } else {
                    xgui_win_pixel(win, x + col, y + row, bg);
                }
            }
        }
        x += XGUI_FONT_WIDTH;
        str++;
    }
}

void xgui_win_text_transparent(xgui_window_t* win, int x, int y, const char* str, uint32_t fg) {
    if (!win || !win->buffer || !str) return;
    while (*str) {
        const uint8_t* glyph = xgui_font_get_glyph(*str);
        for (int row = 0; row < XGUI_FONT_HEIGHT; row++) {
            uint8_t bits = glyph[row];
            for (int col = 0; col < XGUI_FONT_WIDTH; col++) {
                if (bits & (0x80 >> col)) {
                    xgui_win_pixel(win, x + col, y + row, fg);
                }
            }
        }
        x += XGUI_FONT_WIDTH;
        str++;
    }
}

void xgui_win_clear(xgui_window_t* win, uint32_t color) {
    if (!win || !win->buffer) return;
    for (int i = 0; i < win->buf_width * win->buf_height; i++) {
        win->buffer[i] = color;
    }
}
