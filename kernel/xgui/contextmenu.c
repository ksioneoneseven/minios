/*
 * MiniOS XGUI Context Menu
 *
 * Right-click popup menu with Cut/Copy/Paste/Select All options.
 */

#include "xgui/contextmenu.h"
#include "xgui/display.h"
#include "xgui/clipboard.h"
#include "xgui/theme.h"
#include "string.h"

/* Menu dimensions */
#define CTX_ITEM_H      20
#define CTX_ITEM_COUNT  4
#define CTX_WIDTH       160
#define CTX_HEIGHT      (CTX_ITEM_H * CTX_ITEM_COUNT + 6)  /* 4 items + border padding */
#define CTX_PAD         3

/* Menu state */
static bool ctx_visible = false;
static int ctx_x = 0;
static int ctx_y = 0;
static int ctx_hover = -1;  /* Hovered item index, -1 = none */

/* Item enabled state */
static bool ctx_cut_enabled = false;
static bool ctx_copy_enabled = false;
static bool ctx_paste_enabled = false;

/* Menu item labels */
static const char* ctx_labels[CTX_ITEM_COUNT] = {
    "Cut",
    "Copy",
    "Paste",
    "Select All"
};

/* Map index to action */
static const xgui_ctx_action_t ctx_actions[CTX_ITEM_COUNT] = {
    XGUI_CTX_CUT,
    XGUI_CTX_COPY,
    XGUI_CTX_PASTE,
    XGUI_CTX_SELECT_ALL
};

/*
 * Show the context menu at screen coordinates (x, y).
 */
void xgui_contextmenu_show(int x, int y, bool has_selection, bool has_clipboard) {
    /* Clamp to screen bounds */
    int sw = xgui_display_width();
    int sh = xgui_display_height();
    if (x + CTX_WIDTH > sw) x = sw - CTX_WIDTH;
    if (y + CTX_HEIGHT > sh) y = sh - CTX_HEIGHT;
    if (x < 0) x = 0;
    if (y < 0) y = 0;

    ctx_x = x;
    ctx_y = y;
    ctx_visible = true;
    ctx_hover = -1;
    ctx_cut_enabled = has_selection;
    ctx_copy_enabled = has_selection;
    ctx_paste_enabled = has_clipboard;
}

/*
 * Hide the context menu
 */
void xgui_contextmenu_hide(void) {
    ctx_visible = false;
    ctx_hover = -1;
}

/*
 * Check if the context menu is currently visible
 */
bool xgui_contextmenu_visible(void) {
    return ctx_visible;
}

/*
 * Handle a mouse event for the context menu.
 * Returns the selected action, or XGUI_CTX_NONE if no action.
 */
xgui_ctx_action_t xgui_contextmenu_handle_event(xgui_event_t* event) {
    if (!ctx_visible) return XGUI_CTX_NONE;

    int mx = 0, my = 0;

    if (event->type == XGUI_EVENT_MOUSE_MOVE) {
        mx = event->mouse.x;
        my = event->mouse.y;

        /* Update hover */
        if (mx >= ctx_x && mx < ctx_x + CTX_WIDTH &&
            my >= ctx_y + CTX_PAD && my < ctx_y + CTX_PAD + CTX_ITEM_COUNT * CTX_ITEM_H) {
            ctx_hover = (my - ctx_y - CTX_PAD) / CTX_ITEM_H;
        } else {
            ctx_hover = -1;
        }
        return XGUI_CTX_NONE;
    }

    if (event->type == XGUI_EVENT_MOUSE_DOWN || event->type == XGUI_EVENT_MOUSE_CLICK) {
        mx = event->mouse.x;
        my = event->mouse.y;

        /* Click inside menu? */
        if (mx >= ctx_x && mx < ctx_x + CTX_WIDTH &&
            my >= ctx_y + CTX_PAD && my < ctx_y + CTX_PAD + CTX_ITEM_COUNT * CTX_ITEM_H) {
            int idx = (my - ctx_y - CTX_PAD) / CTX_ITEM_H;
            if (idx >= 0 && idx < CTX_ITEM_COUNT) {
                /* Check if item is enabled */
                bool enabled = true;
                if (idx == 0) enabled = ctx_cut_enabled;
                if (idx == 1) enabled = ctx_copy_enabled;
                if (idx == 2) enabled = ctx_paste_enabled;
                /* Select All is always enabled */

                xgui_contextmenu_hide();
                if (enabled) {
                    return ctx_actions[idx];
                }
                return XGUI_CTX_NONE;
            }
        }

        /* Click outside menu — dismiss */
        xgui_contextmenu_hide();
        return XGUI_CTX_NONE;
    }

    return XGUI_CTX_NONE;
}

/*
 * Draw the context menu (call after compositing windows)
 */
void xgui_contextmenu_draw(void) {
    if (!ctx_visible) return;

    /* Shadow */
    xgui_display_rect_filled(ctx_x + 2, ctx_y + 2, CTX_WIDTH, CTX_HEIGHT,
                             XGUI_RGB(40, 40, 40));

    /* Background */
    xgui_display_rect_filled(ctx_x, ctx_y, CTX_WIDTH, CTX_HEIGHT, XGUI_RGB(250, 250, 250));

    /* Border */
    xgui_display_rect(ctx_x, ctx_y, CTX_WIDTH, CTX_HEIGHT, XGUI_RGB(160, 160, 160));

    /* Items */
    for (int i = 0; i < CTX_ITEM_COUNT; i++) {
        int iy = ctx_y + CTX_PAD + i * CTX_ITEM_H;

        /* Determine if enabled */
        bool enabled = true;
        if (i == 0) enabled = ctx_cut_enabled;
        if (i == 1) enabled = ctx_copy_enabled;
        if (i == 2) enabled = ctx_paste_enabled;

        /* Hover highlight */
        if (i == ctx_hover && enabled) {
            xgui_display_rect_filled(ctx_x + 2, iy, CTX_WIDTH - 4, CTX_ITEM_H,
                                     XGUI_RGB(51, 153, 255));
            xgui_display_text_transparent(ctx_x + 10, iy + 3, ctx_labels[i], XGUI_WHITE);
        } else {
            uint32_t color = enabled ? XGUI_BLACK : XGUI_RGB(160, 160, 160);
            xgui_display_text_transparent(ctx_x + 10, iy + 3, ctx_labels[i], color);
        }

        /* Keyboard shortcut hint (right-aligned) */
        const char* shortcut = NULL;
        if (i == 0) shortcut = "Ctrl+X";
        if (i == 1) shortcut = "Ctrl+C";
        if (i == 2) shortcut = "Ctrl+V";
        if (i == 3) shortcut = "Ctrl+A";

        if (shortcut) {
            int sw = xgui_display_text_width(shortcut);
            uint32_t sc_color = (i == ctx_hover && enabled) ? XGUI_RGB(220, 220, 255) :
                                (enabled ? XGUI_RGB(128, 128, 128) : XGUI_RGB(192, 192, 192));
            xgui_display_text_transparent(ctx_x + CTX_WIDTH - sw - 10, iy + 3, shortcut, sc_color);
        }
    }

    /* Separator line between Paste and Select All */
    int sep_y = ctx_y + CTX_PAD + 3 * CTX_ITEM_H - 1;
    xgui_display_hline(ctx_x + 4, sep_y, CTX_WIDTH - 8, XGUI_RGB(210, 210, 210));

    /* Mark dirty */
    xgui_display_mark_dirty(ctx_y, ctx_y + CTX_HEIGHT + 4);
}
