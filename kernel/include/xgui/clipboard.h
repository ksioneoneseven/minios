/*
 * MiniOS XGUI Clipboard
 *
 * Global text clipboard for cut/copy/paste across GUI apps.
 */

#ifndef _XGUI_CLIPBOARD_H
#define _XGUI_CLIPBOARD_H

#include "types.h"

/* Maximum clipboard size */
#define XGUI_CLIPBOARD_MAX  4096

/*
 * Initialize the clipboard
 */
void xgui_clipboard_init(void);

/*
 * Copy text to clipboard (replaces current contents)
 */
void xgui_clipboard_set(const char* text, int length);

/*
 * Get clipboard text. Returns pointer to internal buffer.
 * Returns NULL if clipboard is empty.
 */
const char* xgui_clipboard_get(void);

/*
 * Get clipboard text length
 */
int xgui_clipboard_length(void);

/*
 * Clear the clipboard
 */
void xgui_clipboard_clear(void);

/*
 * Check if clipboard has content
 */
bool xgui_clipboard_has_content(void);

#endif /* _XGUI_CLIPBOARD_H */
