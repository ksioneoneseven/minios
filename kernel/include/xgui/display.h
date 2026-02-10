/*
 * MiniOS XGUI Display Abstraction
 *
 * Double-buffered pixel rendering for the GUI.
 */

#ifndef _XGUI_DISPLAY_H
#define _XGUI_DISPLAY_H

#include "types.h"

/*
 * Color format: 32-bit ARGB
 */
#define XGUI_RGB(r, g, b)       (0xFF000000 | ((r) << 16) | ((g) << 8) | (b))
#define XGUI_RGBA(r, g, b, a)   (((a) << 24) | ((r) << 16) | ((g) << 8) | (b))

/* Extract color components */
#define XGUI_GET_R(c)   (((c) >> 16) & 0xFF)
#define XGUI_GET_G(c)   (((c) >> 8) & 0xFF)
#define XGUI_GET_B(c)   ((c) & 0xFF)
#define XGUI_GET_A(c)   (((c) >> 24) & 0xFF)

/*
 * Standard color palette
 */
#define XGUI_BLACK          XGUI_RGB(0, 0, 0)
#define XGUI_WHITE          XGUI_RGB(255, 255, 255)
#define XGUI_RED            XGUI_RGB(255, 0, 0)
#define XGUI_GREEN          XGUI_RGB(0, 255, 0)
#define XGUI_BLUE           XGUI_RGB(0, 0, 255)
#define XGUI_YELLOW         XGUI_RGB(255, 255, 0)
#define XGUI_CYAN           XGUI_RGB(0, 255, 255)
#define XGUI_MAGENTA        XGUI_RGB(255, 0, 255)

/* Grayscale */
#define XGUI_GRAY           XGUI_RGB(128, 128, 128)
#define XGUI_LIGHT_GRAY     XGUI_RGB(192, 192, 192)
#define XGUI_DARK_GRAY      XGUI_RGB(64, 64, 64)

/* Desktop theme colors */
#define XGUI_DESKTOP_BG     XGUI_RGB(70, 130, 180)   /* Steel Blue */
#define XGUI_PANEL_BG       XGUI_RGB(192, 192, 192)  /* Light Gray */
#define XGUI_TITLE_ACTIVE   XGUI_RGB(0, 0, 128)      /* Dark Blue */
#define XGUI_TITLE_INACTIVE XGUI_RGB(128, 128, 128)  /* Gray */
#define XGUI_WINDOW_BG      XGUI_RGB(255, 255, 255)  /* White */
#define XGUI_BUTTON_BG      XGUI_RGB(212, 208, 200)  /* Button gray */
#define XGUI_BUTTON_PRESSED XGUI_RGB(128, 128, 128)  /* Dark gray */
#define XGUI_SELECTION      XGUI_RGB(51, 153, 255)   /* Light blue */
#define XGUI_MENU_HIGHLIGHT XGUI_RGB(49, 106, 197)   /* Blue */
#define XGUI_BORDER         XGUI_RGB(127, 157, 185)  /* Border color */

/*
 * Display state structure
 */
typedef struct {
    uint32_t* backbuffer;       /* Off-screen render buffer */
    uint8_t*  framebuffer;      /* VESA framebuffer (direct) */
    int       width;            /* Screen width in pixels */
    int       height;           /* Screen height in pixels */
    int       pitch;            /* Bytes per scanline */
    int       bpp;              /* Bits per pixel */
    int       bytes_per_pixel;  /* Bytes per pixel */
    int       red_position;
    int       red_mask_size;
    int       green_position;
    int       green_mask_size;
    int       blue_position;
    int       blue_mask_size;
    bool*     dirty_lines;      /* Dirty line flags for partial update */
    bool      initialized;      /* True if display is ready */
} xgui_display_t;

/*
 * Initialize the display system
 * Returns 0 on success, -1 if VESA not available
 */
int xgui_display_init(void);

/*
 * Shutdown the display system and free resources
 */
void xgui_display_cleanup(void);

/*
 * Check if display is available
 */
bool xgui_display_available(void);

/*
 * Get display dimensions
 */
int xgui_display_width(void);
int xgui_display_height(void);

/*
 * Get direct access to backbuffer (for advanced rendering)
 */
uint32_t* xgui_display_get_backbuffer(void);

/*
 * Clear the entire display to a solid color
 */
void xgui_display_clear(uint32_t color);

/*
 * Set a single pixel (clipped to screen bounds)
 */
void xgui_display_pixel(int x, int y, uint32_t color);

/*
 * Get a pixel color
 */
uint32_t xgui_display_get_pixel(int x, int y);

/*
 * Draw a horizontal line (optimized)
 */
void xgui_display_hline(int x, int y, int length, uint32_t color);

/*
 * Draw a vertical line (optimized)
 */
void xgui_display_vline(int x, int y, int length, uint32_t color);

/*
 * Draw a line (Bresenham's algorithm)
 */
void xgui_display_line(int x1, int y1, int x2, int y2, uint32_t color);

/*
 * Draw a rectangle outline
 */
void xgui_display_rect(int x, int y, int w, int h, uint32_t color);

/*
 * Draw a filled rectangle
 */
void xgui_display_rect_filled(int x, int y, int w, int h, uint32_t color);

/*
 * Draw a 3D raised border (button style)
 */
void xgui_display_rect_3d_raised(int x, int y, int w, int h);

/*
 * Draw a 3D sunken border (pressed button style)
 */
void xgui_display_rect_3d_sunken(int x, int y, int w, int h);

/*
 * Draw text at position
 * bg can be 0 for transparent background
 */
void xgui_display_text(int x, int y, const char* str, uint32_t fg, uint32_t bg);

/*
 * Draw text with transparent background
 */
void xgui_display_text_transparent(int x, int y, const char* str, uint32_t fg);

/*
 * Get pixel width of a string
 */
int xgui_display_text_width(const char* str);

/*
 * Copy a rectangular region within the backbuffer
 * Handles overlapping regions correctly
 */
void xgui_display_copy_rect(int dst_x, int dst_y, int src_x, int src_y, int w, int h);

/*
 * Blit a bitmap to the display
 * bitmap is in 32-bit ARGB format
 */
void xgui_display_blit(int x, int y, int w, int h, const uint32_t* bitmap);

/*
 * Mark a region as dirty (will be flushed on next flush call)
 */
void xgui_display_mark_dirty(int y_start, int y_end);

/*
 * Mark the entire display as dirty
 */
void xgui_display_mark_all_dirty(void);

/*
 * Flush the backbuffer to the framebuffer
 * Only copies dirty lines for efficiency
 */
void xgui_display_flush(void);

/*
 * Draw a pixel directly to the framebuffer (cursor overlay)
 * Does NOT touch the backbuffer.
 */
void xgui_display_fb_pixel(int x, int y, uint32_t color);

/*
 * Flush specific lines from backbuffer to framebuffer (erase cursor overlay)
 */
void xgui_display_flush_lines(int y_start, int y_end);

/*
 * Force flush the entire backbuffer (ignores dirty flags)
 */
void xgui_display_flush_all(void);

#endif /* _XGUI_DISPLAY_H */
