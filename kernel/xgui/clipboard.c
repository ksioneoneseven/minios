/*
 * MiniOS XGUI Clipboard
 *
 * Global text clipboard for cut/copy/paste across GUI apps.
 */

#include "xgui/clipboard.h"
#include "string.h"

/* Clipboard buffer */
static char clipboard_buf[XGUI_CLIPBOARD_MAX];
static int clipboard_len = 0;

/*
 * Initialize the clipboard
 */
void xgui_clipboard_init(void) {
    clipboard_buf[0] = '\0';
    clipboard_len = 0;
}

/*
 * Copy text to clipboard (replaces current contents)
 */
void xgui_clipboard_set(const char* text, int length) {
    if (!text || length <= 0) {
        clipboard_buf[0] = '\0';
        clipboard_len = 0;
        return;
    }
    if (length >= XGUI_CLIPBOARD_MAX) {
        length = XGUI_CLIPBOARD_MAX - 1;
    }
    memcpy(clipboard_buf, text, length);
    clipboard_buf[length] = '\0';
    clipboard_len = length;
}

/*
 * Get clipboard text. Returns pointer to internal buffer.
 * Returns NULL if clipboard is empty.
 */
const char* xgui_clipboard_get(void) {
    if (clipboard_len == 0) return NULL;
    return clipboard_buf;
}

/*
 * Get clipboard text length
 */
int xgui_clipboard_length(void) {
    return clipboard_len;
}

/*
 * Clear the clipboard
 */
void xgui_clipboard_clear(void) {
    clipboard_buf[0] = '\0';
    clipboard_len = 0;
}

/*
 * Check if clipboard has content
 */
bool xgui_clipboard_has_content(void) {
    return clipboard_len > 0;
}
