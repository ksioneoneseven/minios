/*
 * MiniOS XGUI Font Rendering
 *
 * 8x16 bitmap font for text rendering on framebuffer.
 */

#ifndef _XGUI_FONT_H
#define _XGUI_FONT_H

#include "../types.h"

/* Font dimensions */
#define XGUI_FONT_WIDTH     8
#define XGUI_FONT_HEIGHT    16

/* Get font bitmap for a character */
const uint8_t* xgui_font_get_glyph(char c);

/* Draw a single character at (x, y) with foreground and background colors */
void xgui_font_draw_char(int x, int y, char c, uint32_t fg, uint32_t bg);

/* Draw a string at (x, y) */
void xgui_font_draw_string(int x, int y, const char* str, uint32_t fg, uint32_t bg);

/* Draw a string with transparent background */
void xgui_font_draw_string_transparent(int x, int y, const char* str, uint32_t fg);

/* Calculate pixel width of a string */
int xgui_font_string_width(const char* str);

/* Calculate pixel height of a string (accounts for newlines) */
int xgui_font_string_height(const char* str);

#endif /* _XGUI_FONT_H */
