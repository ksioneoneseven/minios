/*
 * MiniOS XGUI Display Abstraction
 *
 * Double-buffered pixel rendering for the GUI.
 */

#include "xgui/display.h"
#include "xgui/font.h"
#include "vesa.h"
#include "heap.h"
#include "string.h"
#include "serial.h"

/* Global display state */
static xgui_display_t display;

static inline uint32_t pack_rgb(uint8_t r, uint8_t g, uint8_t b) {
    uint32_t pixel = 0;
    pixel |= ((uint32_t)r) << display.red_position;
    pixel |= ((uint32_t)g) << display.green_position;
    pixel |= ((uint32_t)b) << display.blue_position;
    return pixel;
}

/*
 * Convert backbuffer pixel (0xAARRGGBB) to framebuffer pixel.
 * Uses VESA-provided channel positions.
 */
static inline uint32_t backbuf_to_fb32(uint32_t c) {
    uint8_t r = (uint8_t)((c >> 16) & 0xFF);
    uint8_t g = (uint8_t)((c >> 8) & 0xFF);
    uint8_t b = (uint8_t)(c & 0xFF);
    (void)(c >> 24);
    return pack_rgb(r, g, b);
}

static void flush_line_24bpp(int y) {
    uint8_t* dst = display.framebuffer + (uint32_t)y * (uint32_t)display.pitch;
    uint32_t* src = &display.backbuffer[y * display.width];

    for (int x = 0; x < display.width; x++) {
        uint32_t c = src[x];
        uint8_t r = (uint8_t)((c >> 16) & 0xFF);
        uint8_t g = (uint8_t)((c >> 8) & 0xFF);
        uint8_t b = (uint8_t)(c & 0xFF);
        uint32_t px = pack_rgb(r, g, b);
        dst[x * 3 + 0] = (uint8_t)(px & 0xFF);
        dst[x * 3 + 1] = (uint8_t)((px >> 8) & 0xFF);
        dst[x * 3 + 2] = (uint8_t)((px >> 16) & 0xFF);
    }
}

/*
 * Initialize the display system
 */
int xgui_display_init(void) {
    if (display.initialized) {
        return 0;  /* Already initialized */
    }

    serial_write_string("XGUI: display_init()\n");

    /* Check if VESA is available */
    if (!vesa_available()) {
        serial_write_string("XGUI: display_init - VESA not available\n");
        return -1;
    }

    vesa_info_t* vesa = vesa_get_info();

    display.framebuffer = vesa->framebuffer;
    display.width = vesa->width;
    display.height = vesa->height;
    display.pitch = vesa->pitch;
    display.bpp = (int)vesa->bpp;
    display.bytes_per_pixel = (int)vesa->bytes_per_pixel;
    display.red_position = (int)vesa->red_position;
    display.red_mask_size = (int)vesa->red_mask_size;
    display.green_position = (int)vesa->green_position;
    display.green_mask_size = (int)vesa->green_mask_size;
    display.blue_position = (int)vesa->blue_position;
    display.blue_mask_size = (int)vesa->blue_mask_size;

    serial_write_string("XGUI: fb bpp=");
    serial_write_hex((uint32_t)display.bpp);
    serial_write_string(" bytespp=");
    serial_write_hex((uint32_t)display.bytes_per_pixel);
    serial_write_string(" pitch=");
    serial_write_hex((uint32_t)display.pitch);
    serial_write_string("\n");
    serial_write_string("XGUI: rpos=");
    serial_write_hex((uint32_t)display.red_position);
    serial_write_string(" rmask=");
    serial_write_hex((uint32_t)display.red_mask_size);
    serial_write_string(" gpos=");
    serial_write_hex((uint32_t)display.green_position);
    serial_write_string(" gmask=");
    serial_write_hex((uint32_t)display.green_mask_size);
    serial_write_string(" bpos=");
    serial_write_hex((uint32_t)display.blue_position);
    serial_write_string(" bmask=");
    serial_write_hex((uint32_t)display.blue_mask_size);
    serial_write_string("\n");

    /* Allocate backbuffer */
    uint32_t buffer_size = display.width * display.height * sizeof(uint32_t);
    display.backbuffer = (uint32_t*)kmalloc(buffer_size);
    if (!display.backbuffer) {
        serial_write_string("XGUI: display_init - kmalloc backbuffer failed\n");
        return -1;
    }

    /* Allocate dirty line flags */
    display.dirty_lines = (bool*)kmalloc(display.height * sizeof(bool));
    if (!display.dirty_lines) {
        serial_write_string("XGUI: display_init - kmalloc dirty_lines failed\n");
        kfree(display.backbuffer);
        display.backbuffer = NULL;
        return -1;
    }

    /* Clear the backbuffer and mark all dirty */
    memset(display.backbuffer, 0, buffer_size);
    memset(display.dirty_lines, 1, display.height * sizeof(bool));

    display.initialized = true;
    serial_write_string("XGUI: display_init OK\n");
    return 0;
}

/*
 * Shutdown the display system
 */
void xgui_display_cleanup(void) {
    if (!display.initialized) {
        return;
    }

    if (display.backbuffer) {
        kfree(display.backbuffer);
        display.backbuffer = NULL;
    }

    if (display.dirty_lines) {
        kfree(display.dirty_lines);
        display.dirty_lines = NULL;
    }

    display.initialized = false;
}

/*
 * Reinitialize display after a VESA mode change.
 * Reallocates backbuffer and dirty lines for the new resolution.
 */
int xgui_display_reinit(void) {
    if (!display.initialized) return -1;

    vesa_info_t* vesa = vesa_get_info();

    /* Free old buffers */
    if (display.backbuffer) kfree(display.backbuffer);
    if (display.dirty_lines) kfree(display.dirty_lines);

    /* Update display state from VESA */
    display.framebuffer = vesa->framebuffer;
    display.width = vesa->width;
    display.height = vesa->height;
    display.pitch = vesa->pitch;
    display.bpp = (int)vesa->bpp;
    display.bytes_per_pixel = (int)vesa->bytes_per_pixel;

    /* Allocate new backbuffer */
    uint32_t buffer_size = display.width * display.height * sizeof(uint32_t);
    display.backbuffer = (uint32_t*)kmalloc(buffer_size);
    if (!display.backbuffer) {
        serial_write_string("XGUI: display_reinit - kmalloc backbuffer failed\n");
        display.initialized = false;
        return -1;
    }

    /* Allocate new dirty line flags */
    display.dirty_lines = (bool*)kmalloc(display.height * sizeof(bool));
    if (!display.dirty_lines) {
        serial_write_string("XGUI: display_reinit - kmalloc dirty_lines failed\n");
        kfree(display.backbuffer);
        display.backbuffer = NULL;
        display.initialized = false;
        return -1;
    }

    /* Clear and mark all dirty */
    memset(display.backbuffer, 0, buffer_size);
    memset(display.dirty_lines, 1, display.height * sizeof(bool));

    serial_write_string("XGUI: display_reinit OK\n");
    return 0;
}

/*
 * Check if display is available
 */
bool xgui_display_available(void) {
    return display.initialized;
}

/*
 * Get display dimensions
 */
int xgui_display_width(void) {
    return display.width;
}

int xgui_display_height(void) {
    return display.height;
}

/*
 * Get direct access to backbuffer
 */
uint32_t* xgui_display_get_backbuffer(void) {
    return display.backbuffer;
}

/*
 * Clear the entire display
 */
void xgui_display_clear(uint32_t color) {
    if (!display.initialized) return;

    uint32_t* p = display.backbuffer;
    int count = display.width * display.height;

    while (count--) {
        *p++ = color;
    }

    xgui_display_mark_all_dirty();
}

/*
 * Set a single pixel (with clipping)
 */
void xgui_display_pixel(int x, int y, uint32_t color) {
    if (!display.initialized) return;
    if (x < 0 || x >= display.width || y < 0 || y >= display.height) return;

    display.backbuffer[y * display.width + x] = color;
    display.dirty_lines[y] = true;
}

/*
 * Get a pixel color
 */
uint32_t xgui_display_get_pixel(int x, int y) {
    if (!display.initialized) return 0;
    if (x < 0 || x >= display.width || y < 0 || y >= display.height) return 0;

    return display.backbuffer[y * display.width + x];
}

/*
 * Draw a horizontal line (optimized)
 */
void xgui_display_hline(int x, int y, int length, uint32_t color) {
    if (!display.initialized) return;
    if (y < 0 || y >= display.height) return;
    if (length <= 0) return;

    /* Clip to screen bounds */
    if (x < 0) {
        length += x;
        x = 0;
    }
    if (x + length > display.width) {
        length = display.width - x;
    }
    if (length <= 0) return;

    uint32_t* p = &display.backbuffer[y * display.width + x];
    while (length--) {
        *p++ = color;
    }
    display.dirty_lines[y] = true;
}

/*
 * Draw a vertical line (optimized)
 */
void xgui_display_vline(int x, int y, int length, uint32_t color) {
    if (!display.initialized) return;
    if (x < 0 || x >= display.width) return;
    if (length <= 0) return;

    /* Clip to screen bounds */
    if (y < 0) {
        length += y;
        y = 0;
    }
    if (y + length > display.height) {
        length = display.height - y;
    }
    if (length <= 0) return;

    uint32_t* p = &display.backbuffer[y * display.width + x];
    while (length--) {
        *p = color;
        p += display.width;
        display.dirty_lines[y++] = true;
    }
}

/*
 * Draw a line using Bresenham's algorithm
 */
void xgui_display_line(int x1, int y1, int x2, int y2, uint32_t color) {
    if (!display.initialized) return;

    int dx = x2 - x1;
    int dy = y2 - y1;
    int sx = (dx > 0) ? 1 : -1;
    int sy = (dy > 0) ? 1 : -1;

    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;

    int err = dx - dy;

    while (1) {
        xgui_display_pixel(x1, y1, color);

        if (x1 == x2 && y1 == y2) break;

        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x1 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y1 += sy;
        }
    }
}

/*
 * Draw a rectangle outline
 */
void xgui_display_rect(int x, int y, int w, int h, uint32_t color) {
    if (!display.initialized) return;
    if (w <= 0 || h <= 0) return;

    xgui_display_hline(x, y, w, color);           /* Top */
    xgui_display_hline(x, y + h - 1, w, color);   /* Bottom */
    xgui_display_vline(x, y, h, color);           /* Left */
    xgui_display_vline(x + w - 1, y, h, color);   /* Right */
}

/*
 * Draw a filled rectangle
 */
void xgui_display_rect_filled(int x, int y, int w, int h, uint32_t color) {
    if (!display.initialized) return;
    if (w <= 0 || h <= 0) return;

    /* Clip to screen bounds */
    int x1 = x;
    int y1 = y;
    int x2 = x + w;
    int y2 = y + h;

    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 > display.width) x2 = display.width;
    if (y2 > display.height) y2 = display.height;

    if (x1 >= x2 || y1 >= y2) return;

    int clipped_w = x2 - x1;

    for (int row = y1; row < y2; row++) {
        uint32_t* p = &display.backbuffer[row * display.width + x1];
        for (int col = 0; col < clipped_w; col++) {
            *p++ = color;
        }
        display.dirty_lines[row] = true;
    }
}

/*
 * Draw a 3D raised border (button style)
 */
void xgui_display_rect_3d_raised(int x, int y, int w, int h) {
    if (w <= 0 || h <= 0) return;

    /* Top and left edges: light */
    xgui_display_hline(x, y, w - 1, XGUI_WHITE);
    xgui_display_vline(x, y, h - 1, XGUI_WHITE);

    /* Inner highlight */
    xgui_display_hline(x + 1, y + 1, w - 3, XGUI_LIGHT_GRAY);
    xgui_display_vline(x + 1, y + 1, h - 3, XGUI_LIGHT_GRAY);

    /* Bottom and right edges: dark */
    xgui_display_hline(x, y + h - 1, w, XGUI_BLACK);
    xgui_display_vline(x + w - 1, y, h, XGUI_BLACK);

    /* Inner shadow */
    xgui_display_hline(x + 1, y + h - 2, w - 2, XGUI_DARK_GRAY);
    xgui_display_vline(x + w - 2, y + 1, h - 2, XGUI_DARK_GRAY);
}

/*
 * Draw a 3D sunken border (pressed button style)
 */
void xgui_display_rect_3d_sunken(int x, int y, int w, int h) {
    if (w <= 0 || h <= 0) return;

    /* Top and left edges: dark */
    xgui_display_hline(x, y, w - 1, XGUI_BLACK);
    xgui_display_vline(x, y, h - 1, XGUI_BLACK);

    /* Inner shadow */
    xgui_display_hline(x + 1, y + 1, w - 3, XGUI_DARK_GRAY);
    xgui_display_vline(x + 1, y + 1, h - 3, XGUI_DARK_GRAY);

    /* Bottom and right edges: light */
    xgui_display_hline(x, y + h - 1, w, XGUI_WHITE);
    xgui_display_vline(x + w - 1, y, h, XGUI_WHITE);

    /* Inner highlight */
    xgui_display_hline(x + 1, y + h - 2, w - 2, XGUI_LIGHT_GRAY);
    xgui_display_vline(x + w - 2, y + 1, h - 2, XGUI_LIGHT_GRAY);
}

/*
 * Draw text at position using the built-in font
 */
void xgui_display_text(int x, int y, const char* str, uint32_t fg, uint32_t bg) {
    if (!display.initialized || !str) return;

    while (*str) {
        xgui_font_draw_char(x, y, *str, fg, bg);
        x += XGUI_FONT_WIDTH;
        str++;
    }
}

/*
 * Draw text with transparent background
 */
void xgui_display_text_transparent(int x, int y, const char* str, uint32_t fg) {
    if (!display.initialized || !str) return;

    while (*str) {
        const uint8_t* glyph = xgui_font_get_glyph(*str);

        for (int row = 0; row < XGUI_FONT_HEIGHT; row++) {
            uint8_t bits = glyph[row];
            for (int col = 0; col < XGUI_FONT_WIDTH; col++) {
                if (bits & (0x80 >> col)) {
                    xgui_display_pixel(x + col, y + row, fg);
                }
            }
        }

        x += XGUI_FONT_WIDTH;
        str++;
    }
}

/*
 * Get pixel width of a string
 */
int xgui_display_text_width(const char* str) {
    if (!str) return 0;
    return strlen(str) * XGUI_FONT_WIDTH;
}

/*
 * Copy a rectangular region within the backbuffer
 */
void xgui_display_copy_rect(int dst_x, int dst_y, int src_x, int src_y, int w, int h) {
    if (!display.initialized) return;
    if (w <= 0 || h <= 0) return;

    /* Handle overlapping regions */
    if (dst_y < src_y || (dst_y == src_y && dst_x < src_x)) {
        /* Copy top-to-bottom, left-to-right */
        for (int row = 0; row < h; row++) {
            int sy = src_y + row;
            int dy = dst_y + row;
            if (sy < 0 || sy >= display.height) continue;
            if (dy < 0 || dy >= display.height) continue;

            uint32_t* src = &display.backbuffer[sy * display.width + src_x];
            uint32_t* dst = &display.backbuffer[dy * display.width + dst_x];

            /* Handle horizontal clipping */
            int start = 0;
            int end = w;
            if (src_x < 0) start = -src_x;
            if (dst_x < 0) start = (start > -dst_x) ? start : -dst_x;
            if (src_x + w > display.width) end = display.width - src_x;
            if (dst_x + w > display.width) end = (end < display.width - dst_x) ? end : display.width - dst_x;

            if (start < end) {
                memmove(dst + start, src + start, (end - start) * sizeof(uint32_t));
                display.dirty_lines[dy] = true;
            }
        }
    } else {
        /* Copy bottom-to-top, right-to-left */
        for (int row = h - 1; row >= 0; row--) {
            int sy = src_y + row;
            int dy = dst_y + row;
            if (sy < 0 || sy >= display.height) continue;
            if (dy < 0 || dy >= display.height) continue;

            uint32_t* src = &display.backbuffer[sy * display.width + src_x];
            uint32_t* dst = &display.backbuffer[dy * display.width + dst_x];

            int start = 0;
            int end = w;
            if (src_x < 0) start = -src_x;
            if (dst_x < 0) start = (start > -dst_x) ? start : -dst_x;
            if (src_x + w > display.width) end = display.width - src_x;
            if (dst_x + w > display.width) end = (end < display.width - dst_x) ? end : display.width - dst_x;

            if (start < end) {
                memmove(dst + start, src + start, (end - start) * sizeof(uint32_t));
                display.dirty_lines[dy] = true;
            }
        }
    }
}

/*
 * Blit a bitmap to the display
 */
void xgui_display_blit(int x, int y, int w, int h, const uint32_t* bitmap) {
    if (!display.initialized || !bitmap) return;
    if (w <= 0 || h <= 0) return;

    for (int row = 0; row < h; row++) {
        int dy = y + row;
        if (dy < 0 || dy >= display.height) continue;

        for (int col = 0; col < w; col++) {
            int dx = x + col;
            if (dx < 0 || dx >= display.width) continue;

            uint32_t pixel = bitmap[row * w + col];

            /* Handle alpha blending if pixel has transparency */
            uint8_t alpha = XGUI_GET_A(pixel);
            if (alpha == 0xFF) {
                display.backbuffer[dy * display.width + dx] = pixel;
            } else if (alpha > 0) {
                /* Simple alpha blending */
                uint32_t bg = display.backbuffer[dy * display.width + dx];
                uint8_t inv_alpha = 255 - alpha;

                uint8_t r = (XGUI_GET_R(pixel) * alpha + XGUI_GET_R(bg) * inv_alpha) / 255;
                uint8_t g = (XGUI_GET_G(pixel) * alpha + XGUI_GET_G(bg) * inv_alpha) / 255;
                uint8_t b = (XGUI_GET_B(pixel) * alpha + XGUI_GET_B(bg) * inv_alpha) / 255;

                display.backbuffer[dy * display.width + dx] = XGUI_RGB(r, g, b);
            }
        }
        display.dirty_lines[dy] = true;
    }
}

/*
 * Mark a region as dirty
 */
void xgui_display_mark_dirty(int y_start, int y_end) {
    if (!display.initialized) return;

    if (y_start < 0) y_start = 0;
    if (y_end > display.height) y_end = display.height;

    for (int y = y_start; y < y_end; y++) {
        display.dirty_lines[y] = true;
    }
}

/*
 * Mark the entire display as dirty
 */
void xgui_display_mark_all_dirty(void) {
    if (!display.initialized) return;
    memset(display.dirty_lines, 1, display.height * sizeof(bool));
}

/*
 * Flush dirty lines to framebuffer
 */
void xgui_display_flush(void) {
    if (!display.initialized) return;

    if (display.bytes_per_pixel == 3) {
        for (int y = 0; y < display.height; y++) {
            if (display.dirty_lines[y]) {
                flush_line_24bpp(y);
                display.dirty_lines[y] = false;
            }
        }
        return;
    }

    for (int y = 0; y < display.height; y++) {
        if (display.dirty_lines[y]) {
            uint32_t* src = &display.backbuffer[y * display.width];
            uint32_t* dst = (uint32_t*)(display.framebuffer + (uint32_t)y * (uint32_t)display.pitch);
            for (int x = 0; x < display.width; x++) {
                dst[x] = backbuf_to_fb32(src[x]);
            }
            display.dirty_lines[y] = false;
        }
    }
}

/*
 * Draw a pixel directly to the framebuffer (for cursor overlay)
 * Does NOT touch the backbuffer or dirty flags.
 */
void xgui_display_fb_pixel(int x, int y, uint32_t color) {
    if (!display.initialized) return;
    if (x < 0 || x >= display.width || y < 0 || y >= display.height) return;

    uint8_t r = (uint8_t)((color >> 16) & 0xFF);
    uint8_t g_val = (uint8_t)((color >> 8) & 0xFF);
    uint8_t b = (uint8_t)(color & 0xFF);

    if (display.bytes_per_pixel == 3) {
        uint32_t px = pack_rgb(r, g_val, b);
        uint8_t* dst = display.framebuffer + (uint32_t)y * (uint32_t)display.pitch + x * 3;
        dst[0] = (uint8_t)(px & 0xFF);
        dst[1] = (uint8_t)((px >> 8) & 0xFF);
        dst[2] = (uint8_t)((px >> 16) & 0xFF);
    } else {
        uint32_t* dst = (uint32_t*)(display.framebuffer + (uint32_t)y * (uint32_t)display.pitch);
        dst[x] = pack_rgb(r, g_val, b);
    }
}

/*
 * Flush specific lines from backbuffer to framebuffer.
 * Used to erase cursor by re-flushing the clean backbuffer lines.
 */
void xgui_display_flush_lines(int y_start, int y_end) {
    if (!display.initialized) return;
    if (y_start < 0) y_start = 0;
    if (y_end > display.height) y_end = display.height;

    if (display.bytes_per_pixel == 3) {
        for (int y = y_start; y < y_end; y++) {
            flush_line_24bpp(y);
        }
    } else {
        for (int y = y_start; y < y_end; y++) {
            uint32_t* src = &display.backbuffer[y * display.width];
            uint32_t* dst = (uint32_t*)(display.framebuffer + (uint32_t)y * (uint32_t)display.pitch);
            for (int x = 0; x < display.width; x++) {
                dst[x] = backbuf_to_fb32(src[x]);
            }
        }
    }
}

/*
 * Force flush the entire backbuffer
 */
void xgui_display_flush_all(void) {
    if (!display.initialized) return;

    if (display.bytes_per_pixel == 3) {
        for (int y = 0; y < display.height; y++) {
            flush_line_24bpp(y);
            display.dirty_lines[y] = false;
        }
        return;
    }

    for (int y = 0; y < display.height; y++) {
        uint32_t* src = &display.backbuffer[y * display.width];
        uint32_t* dst = (uint32_t*)(display.framebuffer + (uint32_t)y * (uint32_t)display.pitch);
        for (int x = 0; x < display.width; x++) {
            dst[x] = backbuf_to_fb32(src[x]);
        }
        display.dirty_lines[y] = false;
    }
}
