/*
 * MiniOS VGA Text Mode Driver Header
 * 
 * Provides functions for writing text to the VGA text mode display.
 * Standard VGA text mode: 80 columns x 25 rows, 16 colors.
 */

#ifndef _VGA_H
#define _VGA_H

#include "types.h"

/* VGA text mode dimensions */
#define VGA_WIDTH   80
#define VGA_HEIGHT  25

/* VGA text buffer address */
#define VGA_BUFFER  0xB8000

/* VGA color codes */
typedef enum {
    VGA_COLOR_BLACK         = 0,
    VGA_COLOR_BLUE          = 1,
    VGA_COLOR_GREEN         = 2,
    VGA_COLOR_CYAN          = 3,
    VGA_COLOR_RED           = 4,
    VGA_COLOR_MAGENTA       = 5,
    VGA_COLOR_BROWN         = 6,
    VGA_COLOR_LIGHT_GREY    = 7,
    VGA_COLOR_DARK_GREY     = 8,
    VGA_COLOR_LIGHT_BLUE    = 9,
    VGA_COLOR_LIGHT_GREEN   = 10,
    VGA_COLOR_LIGHT_CYAN    = 11,
    VGA_COLOR_LIGHT_RED     = 12,
    VGA_COLOR_LIGHT_MAGENTA = 13,
    VGA_COLOR_LIGHT_BROWN   = 14,  /* Yellow */
    VGA_COLOR_WHITE         = 15
} vga_color_t;

/*
 * Create a VGA color attribute byte
 * fg: foreground color (0-15)
 * bg: background color (0-15)
 */
static inline uint8_t vga_entry_color(vga_color_t fg, vga_color_t bg) {
    return fg | (bg << 4);
}

/*
 * Create a VGA character entry (character + color attribute)
 * c: ASCII character
 * color: color attribute from vga_entry_color()
 */
static inline uint16_t vga_entry(unsigned char c, uint8_t color) {
    return (uint16_t)c | ((uint16_t)color << 8);
}

/* Initialize VGA driver (clear screen, reset cursor) */
void vga_init(void);

/* Clear the entire screen */
void vga_clear(void);

/* Set the current text color */
void vga_set_color(uint8_t color);

/* Write a single character at current cursor position */
void vga_putchar(char c);

/* Write a null-terminated string */
void vga_puts(const char* str);

/* Write a string with specific color */
void vga_write(const char* str, uint8_t color);

/* Write a string at specific position */
void vga_write_at(const char* str, uint8_t color, size_t x, size_t y);

/* Move cursor to specific position */
void vga_set_cursor(size_t x, size_t y);

/* Scrollback buffer support */
#define VGA_SCROLLBACK_LINES 200  /* Total lines in scrollback buffer */

/* Scroll the view up (show older content) */
void vga_scroll_up(size_t lines);

/* Scroll the view down (show newer content) */
void vga_scroll_down(size_t lines);

/* Scroll to bottom (current output) */
void vga_scroll_to_bottom(void);

/* Check if currently viewing scrollback */
bool vga_is_scrolled(void);

/* Cursor movement for command line editing */
void vga_move_cursor_left(void);
void vga_move_cursor_right(void);

 void vga_cursor_blink_tick(uint32_t ticks);
 void vga_cursor_blink_disable(void);

/* Output redirect hook - when set, vga_putchar sends chars here instead of VGA */
typedef void (*vga_output_redirect_t)(char c);
void vga_set_output_redirect(vga_output_redirect_t redirect);

/* Check if VGA output is currently redirected (e.g. running inside GUI terminal) */
bool vga_is_redirected(void);

#endif /* _VGA_H */

