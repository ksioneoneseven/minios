/*
 * MiniOS VESA VBE Framebuffer Driver
 *
 * Provides graphics mode support via VESA BIOS Extensions.
 */

#ifndef _VESA_H
#define _VESA_H

#include "types.h"
#include "multiboot.h"

/* Framebuffer state */
typedef struct {
    uint8_t*  framebuffer;      /* Pointer to framebuffer memory */
    uint32_t  width;            /* Width in pixels */
    uint32_t  height;           /* Height in pixels */
    uint32_t  pitch;            /* Bytes per scanline */
    uint32_t  bpp;              /* Bits per pixel */
    uint8_t   bytes_per_pixel;  /* Bytes per pixel */
    uint8_t   red_position;
    uint8_t   red_mask_size;
    uint8_t   green_position;
    uint8_t   green_mask_size;
    uint8_t   blue_position;
    uint8_t   blue_mask_size;
    uint32_t  size;             /* Total framebuffer size in bytes */
    bool      available;        /* True if graphics mode is available */
} vesa_info_t;

/*
 * Initialize VESA driver from multiboot info
 * Must be called early in kernel_main, before paging is fully set up
 */
void vesa_init(multiboot_info_t* mboot);

/*
 * Check if graphics mode is available
 */
bool vesa_available(void);

/*
 * Get framebuffer information
 */
vesa_info_t* vesa_get_info(void);

/*
 * Get direct framebuffer pointer
 */
uint8_t* vesa_get_framebuffer(void);

/*
 * Get screen dimensions
 */
int vesa_get_width(void);
int vesa_get_height(void);
int vesa_get_pitch(void);

/*
 * Check if Bochs VBE is available (QEMU -vga std)
 */
bool vesa_bochs_available(void);

/*
 * Set display resolution via Bochs VBE I/O ports (runtime mode switch).
 * Only works with QEMU -vga std. Returns 0 on success, -1 on failure.
 */
int vesa_set_mode(int width, int height, int bpp);

/*
 * Basic drawing operations (direct to framebuffer)
 */
void vesa_set_pixel(int x, int y, uint32_t color);
uint32_t vesa_get_pixel(int x, int y);
void vesa_fill_rect(int x, int y, int w, int h, uint32_t color);
void vesa_draw_rect(int x, int y, int w, int h, uint32_t color);
void vesa_clear(uint32_t color);

/*
 * Memory copy operations
 */
void vesa_copy_rect(int dst_x, int dst_y, int src_x, int src_y, int w, int h);

/*
 * Color helper macros (32-bit ARGB format)
 */
#define VESA_RGB(r, g, b)       (0xFF000000 | ((r) << 16) | ((g) << 8) | (b))
#define VESA_RGBA(r, g, b, a)   (((a) << 24) | ((r) << 16) | ((g) << 8) | (b))

/* Standard colors */
#define VESA_BLACK              VESA_RGB(0, 0, 0)
#define VESA_WHITE              VESA_RGB(255, 255, 255)
#define VESA_RED                VESA_RGB(255, 0, 0)
#define VESA_GREEN              VESA_RGB(0, 255, 0)
#define VESA_BLUE               VESA_RGB(0, 0, 255)
#define VESA_YELLOW             VESA_RGB(255, 255, 0)
#define VESA_CYAN               VESA_RGB(0, 255, 255)
#define VESA_MAGENTA            VESA_RGB(255, 0, 255)
#define VESA_GRAY               VESA_RGB(128, 128, 128)
#define VESA_LIGHT_GRAY         VESA_RGB(192, 192, 192)
#define VESA_DARK_GRAY          VESA_RGB(64, 64, 64)

#endif /* _VESA_H */
