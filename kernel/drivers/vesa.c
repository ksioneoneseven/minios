/*
 * MiniOS VESA VBE Framebuffer Driver
 *
 * Provides graphics mode support via VESA BIOS Extensions.
 * Parses multiboot framebuffer info and maps the framebuffer into memory.
 */

#include "../include/vesa.h"
#include "../include/paging.h"
#include "../include/stdio.h"
#include "../include/string.h"
#include "../include/serial.h"

/* Global framebuffer state */
static vesa_info_t vesa_info = {
    .framebuffer = NULL,
    .width = 0,
    .height = 0,
    .pitch = 0,
    .bpp = 0,
    .bytes_per_pixel = 0,
    .red_position = 0,
    .red_mask_size = 0,
    .green_position = 0,
    .green_mask_size = 0,
    .blue_position = 0,
    .blue_mask_size = 0,
    .size = 0,
    .available = false
};

static inline uint32_t pack_rgb(uint8_t r, uint8_t g, uint8_t b) {
    uint32_t pixel = 0;
    pixel |= ((uint32_t)r) << vesa_info.red_position;
    pixel |= ((uint32_t)g) << vesa_info.green_position;
    pixel |= ((uint32_t)b) << vesa_info.blue_position;
    return pixel;
}

static inline void write_pixel(int x, int y, uint32_t pixel) {
    uint8_t* p = vesa_info.framebuffer + (uint32_t)y * vesa_info.pitch + (uint32_t)x * vesa_info.bytes_per_pixel;
    if (vesa_info.bytes_per_pixel == 4) {
        *(uint32_t*)p = pixel;
    } else if (vesa_info.bytes_per_pixel == 3) {
        p[0] = (uint8_t)(pixel & 0xFF);
        p[1] = (uint8_t)((pixel >> 8) & 0xFF);
        p[2] = (uint8_t)((pixel >> 16) & 0xFF);
    }
}

static inline uint32_t read_pixel(int x, int y) {
    uint8_t* p = vesa_info.framebuffer + (uint32_t)y * vesa_info.pitch + (uint32_t)x * vesa_info.bytes_per_pixel;
    if (vesa_info.bytes_per_pixel == 4) {
        return *(uint32_t*)p;
    }
    if (vesa_info.bytes_per_pixel == 3) {
        return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16);
    }
    return 0;
}

/*
 * Initialize VESA driver from multiboot info
 */
void vesa_init(multiboot_info_t* mboot) {
    if (!mboot) {
        printk("VESA: No multiboot info\n");
        serial_write_string("VESA: No multiboot info\n");
        return;
    }

    printk("VESA: Multiboot flags = 0x%08X\n", mboot->flags);
    serial_write_string("VESA: multiboot flags=");
    serial_write_hex(mboot->flags);
    serial_write_string("\n");

    /* Check for framebuffer info (preferred method) */
    if (mboot->flags & MULTIBOOT_INFO_FRAMEBUFFER) {
        printk("VESA: Framebuffer flag is set\n");
        serial_write_string("VESA: framebuffer flag set\n");
        uint64_t fb_addr = mboot->framebuffer_addr;
        uint32_t fb_pitch = mboot->framebuffer_pitch;
        uint32_t fb_width = mboot->framebuffer_width;
        uint32_t fb_height = mboot->framebuffer_height;
        uint8_t fb_bpp = mboot->framebuffer_bpp;
        uint8_t fb_type = mboot->framebuffer_type;

        serial_write_string("VESA: fb_addr=");
        serial_write_hex((uint32_t)fb_addr);
        serial_write_string(" pitch=");
        serial_write_hex(fb_pitch);
        serial_write_string(" w=");
        serial_write_hex(fb_width);
        serial_write_string(" h=");
        serial_write_hex(fb_height);
        serial_write_string(" bpp=");
        serial_write_hex((uint32_t)fb_bpp);
        serial_write_string(" type=");
        serial_write_hex((uint32_t)fb_type);
        serial_write_string("\n");

        /* Type 1 = RGB color, Type 2 = EGA text mode */
        if (fb_type != 1) {
            printk("VESA: Framebuffer type %d not supported (need RGB)\n", fb_type);
            return;
        }

        /* Support 24bpp (RGB) and 32bpp */
        if (fb_bpp != 24 && fb_bpp != 32) {
            printk("VESA: %d bpp not supported (need 24 or 32)\n", fb_bpp);
            return;
        }

        printk("VESA: Framebuffer at 0x%08X, %dx%d, %d bpp\n",
               (uint32_t)fb_addr, fb_width, fb_height, fb_bpp);

        /* Calculate framebuffer size */
        uint32_t fb_size = fb_pitch * fb_height;

        serial_write_string("VESA: mapping framebuffer pages... size=");
        serial_write_hex(fb_size);
        serial_write_string("\n");

        /* Map framebuffer into kernel address space */
        uint32_t fb_phys = (uint32_t)fb_addr;
        for (uint32_t offset = 0; offset < fb_size; offset += 4096) {
            paging_map_page(fb_phys + offset, fb_phys + offset,
                           PAGE_KERNEL | PAGE_NOCACHE);
            if ((offset & 0xFFFFF) == 0) {
                serial_write_string("VESA: mapped offset=");
                serial_write_hex(offset);
                serial_write_string("\n");
            }
        }

        serial_write_string("VESA: framebuffer mapping done\n");

        /* Store framebuffer info */
        vesa_info.framebuffer = (uint8_t*)fb_phys;
        vesa_info.width = fb_width;
        vesa_info.height = fb_height;
        vesa_info.pitch = fb_pitch;
        vesa_info.bpp = fb_bpp;
        vesa_info.bytes_per_pixel = (uint8_t)(fb_bpp / 8);
        /*
         * Multiboot spec: color_info is (position, mask_size) pairs
         * for red, green, blue channels.
         */
        vesa_info.red_position = mboot->color_info[0];
        vesa_info.red_mask_size = mboot->color_info[1];
        vesa_info.green_position = mboot->color_info[2];
        vesa_info.green_mask_size = mboot->color_info[3];
        vesa_info.blue_position = mboot->color_info[4];
        vesa_info.blue_mask_size = mboot->color_info[5];

        /* Sanity check: if mask sizes are 0, multiboot didn't provide
         * valid color info. Fall back to standard BGR byte order. */
        if (vesa_info.red_mask_size == 0 || vesa_info.green_mask_size == 0 ||
            vesa_info.blue_mask_size == 0) {
            serial_write_string("VESA: color_info invalid, using BGR default\n");
            vesa_info.blue_position = 0;
            vesa_info.blue_mask_size = 8;
            vesa_info.green_position = 8;
            vesa_info.green_mask_size = 8;
            vesa_info.red_position = 16;
            vesa_info.red_mask_size = 8;
        }
        vesa_info.size = fb_size;
        vesa_info.available = true;

        serial_write_string("VESA: init OK (framebuffer) bpp=");
        serial_write_hex((uint32_t)vesa_info.bpp);
        serial_write_string(" bytespp=");
        serial_write_hex((uint32_t)vesa_info.bytes_per_pixel);
        serial_write_string(" rpos=");
        serial_write_hex((uint32_t)vesa_info.red_position);
        serial_write_string(" gpos=");
        serial_write_hex((uint32_t)vesa_info.green_position);
        serial_write_string(" bpos=");
        serial_write_hex((uint32_t)vesa_info.blue_position);
        serial_write_string("\n");

        printk("VESA: Initialized %dx%d @ %d bpp (pitch=%d)\n",
               fb_width, fb_height, fb_bpp, fb_pitch);

        return;
    }

    /* Fallback: Check for VBE info */
    if (mboot->flags & MULTIBOOT_INFO_VBE_INFO) {
        serial_write_string("VESA: VBE info flag set\n");
        if (mboot->vbe_mode_info == 0) {
            printk("VESA: VBE mode info pointer is NULL\n");
            serial_write_string("VESA: vbe_mode_info ptr NULL\n");
            return;
        }

        vbe_mode_info_t* vbe = (vbe_mode_info_t*)mboot->vbe_mode_info;

        /* Support 24bpp and 32bpp */
        if (vbe->bpp != 24 && vbe->bpp != 32) {
            printk("VESA: VBE %d bpp not supported (need 24 or 32)\n", vbe->bpp);
            return;
        }

        uint32_t fb_phys = vbe->framebuffer;
        uint32_t fb_size = vbe->pitch * vbe->height;

        printk("VESA: VBE framebuffer at 0x%08X, %dx%d, %d bpp\n",
               fb_phys, vbe->width, vbe->height, vbe->bpp);
        serial_write_string("VESA: VBE fb_phys=");
        serial_write_hex(fb_phys);
        serial_write_string(" pitch=");
        serial_write_hex((uint32_t)vbe->pitch);
        serial_write_string(" w=");
        serial_write_hex((uint32_t)vbe->width);
        serial_write_string(" h=");
        serial_write_hex((uint32_t)vbe->height);
        serial_write_string(" bpp=");
        serial_write_hex((uint32_t)vbe->bpp);
        serial_write_string("\n");

        /* Map framebuffer */
        for (uint32_t offset = 0; offset < fb_size; offset += 4096) {
            paging_map_page(fb_phys + offset, fb_phys + offset,
                           PAGE_KERNEL | PAGE_NOCACHE);
        }

        /* Store info */
        vesa_info.framebuffer = (uint8_t*)fb_phys;
        vesa_info.width = vbe->width;
        vesa_info.height = vbe->height;
        vesa_info.pitch = vbe->pitch;
        vesa_info.bpp = vbe->bpp;
        vesa_info.bytes_per_pixel = (uint8_t)(vbe->bpp / 8);
        vesa_info.red_position = vbe->red_position;
        vesa_info.red_mask_size = vbe->red_mask;
        vesa_info.green_position = vbe->green_position;
        vesa_info.green_mask_size = vbe->green_mask;
        vesa_info.blue_position = vbe->blue_position;
        vesa_info.blue_mask_size = vbe->blue_mask;
        vesa_info.size = fb_size;
        vesa_info.available = true;

        serial_write_string("VESA: init OK (VBE) bpp=");
        serial_write_hex((uint32_t)vesa_info.bpp);
        serial_write_string(" bytespp=");
        serial_write_hex((uint32_t)vesa_info.bytes_per_pixel);
        serial_write_string(" rpos=");
        serial_write_hex((uint32_t)vesa_info.red_position);
        serial_write_string(" gpos=");
        serial_write_hex((uint32_t)vesa_info.green_position);
        serial_write_string(" bpos=");
        serial_write_hex((uint32_t)vesa_info.blue_position);
        serial_write_string("\n");

        printk("VESA: Initialized via VBE %dx%d @ %d bpp\n",
               vbe->width, vbe->height, vbe->bpp);

        return;
    }

    printk("VESA: No framebuffer info available\n");
    serial_write_string("VESA: No framebuffer/VBE info available\n");
}

/*
 * Check if graphics mode is available
 */
bool vesa_available(void) {
    return vesa_info.available;
}

/*
 * Get framebuffer information
 */
vesa_info_t* vesa_get_info(void) {
    return &vesa_info;
}

/*
 * Get direct framebuffer pointer
 */
uint8_t* vesa_get_framebuffer(void) {
    return vesa_info.framebuffer;
}

/*
 * Get screen width
 */
int vesa_get_width(void) {
    return (int)vesa_info.width;
}

/*
 * Get screen height
 */
int vesa_get_height(void) {
    return (int)vesa_info.height;
}

/*
 * Get pitch (bytes per scanline)
 */
int vesa_get_pitch(void) {
    return (int)vesa_info.pitch;
}

/*
 * Set a pixel at (x, y) to the given color
 */
void vesa_set_pixel(int x, int y, uint32_t color) {
    if (!vesa_info.available) return;
    if (x < 0 || x >= (int)vesa_info.width) return;
    if (y < 0 || y >= (int)vesa_info.height) return;

    uint8_t r = (uint8_t)((color >> 16) & 0xFF);
    uint8_t g = (uint8_t)((color >> 8) & 0xFF);
    uint8_t b = (uint8_t)(color & 0xFF);
    write_pixel(x, y, pack_rgb(r, g, b));
}

/*
 * Get a pixel at (x, y)
 */
uint32_t vesa_get_pixel(int x, int y) {
    if (!vesa_info.available) return 0;
    if (x < 0 || x >= (int)vesa_info.width) return 0;
    if (y < 0 || y >= (int)vesa_info.height) return 0;

    uint32_t v = read_pixel(x, y);
    uint8_t r = (uint8_t)((v >> vesa_info.red_position) & 0xFF);
    uint8_t g = (uint8_t)((v >> vesa_info.green_position) & 0xFF);
    uint8_t b = (uint8_t)((v >> vesa_info.blue_position) & 0xFF);
    return VESA_RGB(r, g, b);
}

/*
 * Fill a rectangle with a solid color
 */
void vesa_fill_rect(int x, int y, int w, int h, uint32_t color) {
    if (!vesa_info.available) return;

    /* Clip to screen bounds */
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > (int)vesa_info.width) w = (int)vesa_info.width - x;
    if (y + h > (int)vesa_info.height) h = (int)vesa_info.height - y;

    if (w <= 0 || h <= 0) return;

    uint8_t r = (uint8_t)((color >> 16) & 0xFF);
    uint8_t g = (uint8_t)((color >> 8) & 0xFF);
    uint8_t b = (uint8_t)(color & 0xFF);
    uint32_t px = pack_rgb(r, g, b);

    for (int row = 0; row < h; row++) {
        for (int col = 0; col < w; col++) {
            write_pixel(x + col, y + row, px);
        }
    }
}

/*
 * Draw a rectangle outline
 */
void vesa_draw_rect(int x, int y, int w, int h, uint32_t color) {
    if (!vesa_info.available) return;

    /* Draw four lines */
    for (int i = 0; i < w; i++) {
        vesa_set_pixel(x + i, y, color);           /* Top */
        vesa_set_pixel(x + i, y + h - 1, color);   /* Bottom */
    }
    for (int i = 0; i < h; i++) {
        vesa_set_pixel(x, y + i, color);           /* Left */
        vesa_set_pixel(x + w - 1, y + i, color);   /* Right */
    }
}

/*
 * Clear the entire screen with a color
 */
void vesa_clear(uint32_t color) {
    if (!vesa_info.available) return;

    uint8_t r = (uint8_t)((color >> 16) & 0xFF);
    uint8_t g = (uint8_t)((color >> 8) & 0xFF);
    uint8_t b = (uint8_t)(color & 0xFF);
    uint32_t px = pack_rgb(r, g, b);

    for (uint32_t y = 0; y < vesa_info.height; y++) {
        for (uint32_t x = 0; x < vesa_info.width; x++) {
            write_pixel((int)x, (int)y, px);
        }
    }
}

/*
 * Copy a rectangle from one location to another
 * Handles overlapping regions correctly
 */
void vesa_copy_rect(int dst_x, int dst_y, int src_x, int src_y, int w, int h) {
    if (!vesa_info.available) return;

    uint32_t row_bytes = (uint32_t)w * vesa_info.bytes_per_pixel;

    /* Determine copy direction to handle overlaps */
    if (dst_y < src_y || (dst_y == src_y && dst_x < src_x)) {
        /* Copy forward */
        for (int row = 0; row < h; row++) {
            uint8_t* src = vesa_info.framebuffer + (uint32_t)(src_y + row) * vesa_info.pitch + (uint32_t)src_x * vesa_info.bytes_per_pixel;
            uint8_t* dst = vesa_info.framebuffer + (uint32_t)(dst_y + row) * vesa_info.pitch + (uint32_t)dst_x * vesa_info.bytes_per_pixel;
            memmove(dst, src, row_bytes);
        }
    } else {
        /* Copy backward */
        for (int row = h - 1; row >= 0; row--) {
            uint8_t* src = vesa_info.framebuffer + (uint32_t)(src_y + row) * vesa_info.pitch + (uint32_t)src_x * vesa_info.bytes_per_pixel;
            uint8_t* dst = vesa_info.framebuffer + (uint32_t)(dst_y + row) * vesa_info.pitch + (uint32_t)dst_x * vesa_info.bytes_per_pixel;
            memmove(dst, src, row_bytes);
        }
    }
}
