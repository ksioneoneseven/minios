/*
 * MiniOS XGUI Context Menu
 *
 * Right-click popup menu with Cut/Copy/Paste options.
 */

#ifndef _XGUI_CONTEXTMENU_H
#define _XGUI_CONTEXTMENU_H

#include "types.h"
#include "xgui/event.h"

/* Context menu actions */
typedef enum {
    XGUI_CTX_NONE = 0,
    XGUI_CTX_CUT,
    XGUI_CTX_COPY,
    XGUI_CTX_PASTE,
    XGUI_CTX_SELECT_ALL
} xgui_ctx_action_t;

/*
 * Show the context menu at screen coordinates (x, y).
 * has_selection: if true, Cut and Copy are enabled.
 * has_clipboard: if true, Paste is enabled.
 */
void xgui_contextmenu_show(int x, int y, bool has_selection, bool has_clipboard);

/*
 * Hide the context menu
 */
void xgui_contextmenu_hide(void);

/*
 * Check if the context menu is currently visible
 */
bool xgui_contextmenu_visible(void);

/*
 * Handle a mouse event for the context menu.
 * Returns the selected action, or XGUI_CTX_NONE if no action.
 * Automatically hides the menu on click.
 */
xgui_ctx_action_t xgui_contextmenu_handle_event(xgui_event_t* event);

/*
 * Draw the context menu (call after compositing windows)
 */
void xgui_contextmenu_draw(void);

#endif /* _XGUI_CONTEXTMENU_H */
