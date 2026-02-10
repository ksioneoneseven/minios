/*
 * MiniOS XGUI - Graphical User Interface
 *
 * Main entry point and initialization for the GUI system.
 */

#include "xgui/xgui.h"
#include "xgui/display.h"
#include "xgui/event.h"
#include "xgui/wm.h"
#include "xgui/desktop.h"
#include "xgui/calculator.h"
#include "xgui/theme.h"
#include "mouse.h"
#include "keyboard.h"
#include "vesa.h"
#include "stdio.h"
#include "serial.h"
#include "vga.h"
#include "io.h"
#include "blockdev.h"
#include "timer.h"
#include "string.h"

/* Forward declarations for apps */
extern void xgui_skigame_update(void);
extern void xgui_flappycat_update(void);

/* XGUI state */
static bool xgui_running = false;
static bool xgui_initialized = false;

/* Last drawn cursor position for erasing */
#define CURSOR_SIZE 16
static int last_cursor_x = -1, last_cursor_y = -1;

/*
 * Erase cursor by re-flushing the clean backbuffer lines over the
 * framebuffer area where the cursor was drawn.
 */
static void cursor_erase(void) {
    if (last_cursor_x < 0) return;
    int y_start = last_cursor_y;
    int y_end = last_cursor_y + CURSOR_SIZE;
    xgui_display_flush_lines(y_start, y_end);
}

/* Mouse cursor bitmap (16x16, simple arrow) */
static const uint32_t cursor_bitmap[16 * 16] = {
    0xFF000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0xFF000000, 0xFF000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0xFF000000, 0xFFFFFFFF, 0xFF000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0xFF000000, 0xFFFFFFFF, 0xFFFFFFFF, 0xFF000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0xFF000000, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFF000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0xFF000000, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFF000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0xFF000000, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFF000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0xFF000000, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFF000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0xFF000000, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFF000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0xFF000000, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFF000000, 0xFF000000, 0xFF000000, 0xFF000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0xFF000000, 0xFFFFFFFF, 0xFFFFFFFF, 0xFF000000, 0xFFFFFFFF, 0xFFFFFFFF, 0xFF000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0xFF000000, 0xFFFFFFFF, 0xFF000000, 0x00000000, 0xFF000000, 0xFFFFFFFF, 0xFFFFFFFF, 0xFF000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0xFF000000, 0xFF000000, 0x00000000, 0x00000000, 0xFF000000, 0xFFFFFFFF, 0xFFFFFFFF, 0xFF000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0xFF000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0xFF000000, 0xFFFFFFFF, 0xFFFFFFFF, 0xFF000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0xFF000000, 0xFFFFFFFF, 0xFFFFFFFF, 0xFF000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0xFF000000, 0xFF000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
};

/*
 * Draw the mouse cursor directly to the framebuffer (not the backbuffer)
 */
void xgui_draw_cursor(int x, int y) {
    for (int cy = 0; cy < 16; cy++) {
        for (int cx = 0; cx < 16; cx++) {
            uint32_t pixel = cursor_bitmap[cy * 16 + cx];
            if (pixel != 0) {
                xgui_display_fb_pixel(x + cx, y + cy, pixel);
            }
        }
    }
    last_cursor_x = x;
    last_cursor_y = y;
}

/*
 * Initialize the XGUI system
 */
int xgui_init(void) {
    if (xgui_initialized) {
        return 0;
    }

    serial_write_string("XGUI: xgui_init()\n");
    
    printk("XGUI: Initializing...\n");

    /* Disable VGA text-mode cursor blink — timer ISR must not touch 0xB8000 in VESA mode */
    vga_cursor_blink_disable();
    
    /* Check for VESA support */
    if (!vesa_available()) {
        printk("XGUI: VESA not available\n");
        serial_write_string("XGUI: VESA not available\n");
        return -1;
    }
    
    /* Initialize display */
    if (xgui_display_init() < 0) {
        printk("XGUI: Display init failed\n");
        serial_write_string("XGUI: display init failed\n");
        return -1;
    }
    
    printk("XGUI: Display %dx%d\n", xgui_display_width(), xgui_display_height());
    
    /* Initialize mouse */
    if (mouse_init() < 0) {
        printk("XGUI: Mouse init failed (continuing without mouse)\n");
    } else {
        mouse_set_bounds(xgui_display_width(), xgui_display_height());
        mouse_set_position(xgui_display_width() / 2, xgui_display_height() / 2);
    }
    
    /* Initialize event system */
    xgui_event_init();
    
    /* Initialize window manager */
    xgui_wm_init();
    
    /* Initialize desktop */
    xgui_desktop_init();
    xgui_panel_init();
    
    /* Initialize theme system (applies Classic theme) */
    xgui_theme_init();
    
    /* Initialize clipboard */
    xgui_clipboard_init();
    
    xgui_initialized = true;
    printk("XGUI: Initialized successfully\n");
    serial_write_string("XGUI: initialized OK\n");
    
    return 0;
}

/*
 * Run the XGUI main loop
 */
void xgui_run(void) {
    if (!xgui_initialized) {
        if (xgui_init() < 0) {
            return;
        }
    }

    serial_write_string("XGUI: xgui_run() enter\n");
    
    /* Ensure interrupts are enabled for mouse/keyboard input */
    __asm__ volatile("sti");
    
    xgui_running = true;

    /* ============================================================
     * XP-Style Boot Welcome Screen
     * ============================================================ */
    {
        int w = xgui_display_width();
        int h = xgui_display_height();

        /* --- Draw gradient background (dark navy to black) --- */
        for (int y = 0; y < h; y++) {
            int t = (y * 255) / h;
            int r = 0;
            int g = 0;
            int b = 90 - (t * 90) / 255;  /* Navy blue fading to black */
            if (b < 0) b = 0;
            uint32_t c = XGUI_RGB(r, g, b);
            xgui_display_hline(0, y, w, c);
        }

        /* --- "MiniOS" logo text (large, centered, white) --- */
        /* Draw it multiple times offset for a bold/shadow effect */
        const char* logo = "M i n i O S";
        int logo_tw = xgui_display_text_width(logo);
        int logo_x = (w - logo_tw) / 2;
        int logo_y = h / 2 - 50;

        /* Drop shadow */
        xgui_display_text_transparent(logo_x + 2, logo_y + 2, logo, XGUI_RGB(0, 0, 40));
        xgui_display_text_transparent(logo_x + 1, logo_y + 1, logo, XGUI_RGB(0, 0, 60));
        /* Main text */
        xgui_display_text_transparent(logo_x, logo_y, logo, XGUI_WHITE);

        /* --- Thin accent line under logo --- */
        int line_w = 200;
        int line_x = (w - line_w) / 2;
        int line_y = logo_y + 20;
        for (int x = 0; x < line_w; x++) {
            /* Gradient: transparent -> blue -> transparent */
            int dist = x < line_w / 2 ? x : line_w - x;
            int alpha = (dist * 255) / (line_w / 2);
            int br = (80 * alpha) / 255;
            int bg = (140 * alpha) / 255;
            int bb = (220 * alpha) / 255;
            xgui_display_pixel(line_x + x, line_y, XGUI_RGB(br, bg, bb));
            xgui_display_pixel(line_x + x, line_y + 1, XGUI_RGB(br / 2, bg / 2, bb / 2));
        }

        /* --- "Welcome" text --- */
        const char* welcome = "Welcome";
        int wel_tw = xgui_display_text_width(welcome);
        xgui_display_text_transparent((w - wel_tw) / 2 + 1, line_y + 14 + 1, welcome, XGUI_RGB(0, 0, 40));
        xgui_display_text_transparent((w - wel_tw) / 2, line_y + 14, welcome, XGUI_RGB(200, 220, 255));

        /* --- Progress bar track (centered, below welcome) --- */
        int bar_w = 180;
        int bar_h = 8;
        int bar_x = (w - bar_w) / 2;
        int bar_y = line_y + 42;

        /* Track background (dark inset) */
        xgui_display_rect_filled(bar_x - 1, bar_y - 1, bar_w + 2, bar_h + 2, XGUI_RGB(10, 10, 40));
        xgui_display_hline(bar_x - 1, bar_y - 1, bar_w + 2, XGUI_RGB(0, 0, 20));
        xgui_display_hline(bar_x - 1, bar_y + bar_h, bar_w + 2, XGUI_RGB(30, 30, 80));

        xgui_display_mark_all_dirty();
        xgui_display_flush_all();

        /* --- Animated progress bar (XP-style glowing segments) --- */
        int seg_w = 12;
        int seg_gap = 2;
        int total_segs = bar_w / (seg_w + seg_gap);
        int glow_width = 3;  /* Number of bright segments in the glow */

        /* Run animation for several passes */
        for (int pass = 0; pass < 4; pass++) {
            for (int pos = -glow_width; pos <= total_segs + glow_width; pos++) {
                /* Clear bar interior */
                xgui_display_rect_filled(bar_x, bar_y, bar_w, bar_h, XGUI_RGB(10, 10, 40));

                /* Draw segments with glow effect */
                for (int s = 0; s < total_segs; s++) {
                    int sx = bar_x + s * (seg_w + seg_gap);
                    int dist = pos - s;
                    if (dist < 0) dist = -dist;

                    uint32_t seg_color;
                    if (dist == 0) {
                        seg_color = XGUI_RGB(120, 180, 255);  /* Brightest */
                    } else if (dist == 1) {
                        seg_color = XGUI_RGB(60, 120, 220);   /* Bright */
                    } else if (dist == 2) {
                        seg_color = XGUI_RGB(30, 70, 160);    /* Medium */
                    } else if (dist == 3) {
                        seg_color = XGUI_RGB(15, 40, 100);    /* Dim */
                    } else {
                        seg_color = XGUI_RGB(8, 20, 60);      /* Base dim */
                    }

                    /* Draw segment with slight gradient */
                    for (int row = 0; row < bar_h; row++) {
                        int bright = (row < bar_h / 2) ? 15 : -10;
                        int sr = (int)((seg_color >> 16) & 0xFF) + bright;
                        int sg = (int)((seg_color >> 8) & 0xFF) + bright;
                        int sb = (int)(seg_color & 0xFF) + bright;
                        if (sr < 0) sr = 0;
                        if (sr > 255) sr = 255;
                        if (sg < 0) sg = 0;
                        if (sg > 255) sg = 255;
                        if (sb < 0) sb = 0;
                        if (sb > 255) sb = 255;
                        xgui_display_hline(sx, bar_y + row, seg_w, XGUI_RGB(sr, sg, sb));
                    }
                }

                /* Flush just the bar region */
                xgui_display_mark_dirty(bar_y - 2, bar_y + bar_h + 2);
                xgui_display_flush();

                /* Small delay between frames */
                for (volatile int d = 0; d < 12000000; d++);
            }
        }

        /* Brief hold on the completed welcome screen */
        for (volatile int d = 0; d < 600000000; d++);

        /* Fade to black */
        for (int step = 0; step < 16; step++) {
            int fade = step * 16;
            if (fade > 255) fade = 255;
            for (int y = 0; y < h; y++) {
                /* Read current row and darken */
                for (int x = 0; x < w; x += 4) {
                    uint32_t px = xgui_display_get_pixel(x, y);
                    int pr = (int)XGUI_GET_R(px) - 16; if (pr < 0) pr = 0;
                    int pg = (int)XGUI_GET_G(px) - 16; if (pg < 0) pg = 0;
                    int pb = (int)XGUI_GET_B(px) - 16; if (pb < 0) pb = 0;
                    uint32_t dark = XGUI_RGB(pr, pg, pb);
                    xgui_display_pixel(x, y, dark);
                    xgui_display_pixel(x + 1, y, dark);
                    xgui_display_pixel(x + 2, y, dark);
                    xgui_display_pixel(x + 3, y, dark);
                }
            }
            xgui_display_mark_all_dirty();
            xgui_display_flush();
            for (volatile int d = 0; d < 24000000; d++);
        }
    }

    /* Create analog clock widget */
    xgui_analogclock_create();
    
    /* Restore sticky notes from previous session */
    xgui_stickynotes_restore();
    
    /* Initial draw */
    xgui_desktop_draw();
    xgui_panel_draw();
    xgui_wm_redraw_all();
    xgui_wm_composite();
    xgui_draw_start_menu();

    int mouse_x, mouse_y;
    mouse_get_position(&mouse_x, &mouse_y);

    /* Flush backbuffer to framebuffer, then overlay cursor */
    xgui_display_flush_all();
    xgui_draw_cursor(mouse_x, mouse_y);

    /* Main event loop */
    bool needs_redraw = false;
    int last_mouse_x = mouse_x, last_mouse_y = mouse_y;

    while (xgui_running) {
        xgui_event_t event;
        bool mouse_moved = false;

        /* Process events */
        while (xgui_event_poll(&event)) {
            if (event.type == XGUI_EVENT_QUIT) {
                xgui_running = false;
                break;
            }

            /* Check for ESC key to exit */
            if (event.type == XGUI_EVENT_KEY_DOWN && event.key.keycode == 0x1B) {
                xgui_running = false;
                break;
            }

            /* Context menu handling — intercept ALL events when visible */
            if (xgui_contextmenu_visible()) {
                /* Swallow ALL mouse events except left-button down/click */
                if (event.type == XGUI_EVENT_MOUSE_UP ||
                    event.type == XGUI_EVENT_MOUSE_DBLCLICK) {
                    needs_redraw = true;
                    continue;
                }
                if (event.type == XGUI_EVENT_MOUSE_MOVE) {
                    xgui_contextmenu_handle_event(&event);
                    needs_redraw = true;
                    continue;
                }
                if (event.type == XGUI_EVENT_MOUSE_DOWN ||
                    event.type == XGUI_EVENT_MOUSE_CLICK) {
                    /* Swallow right-button events entirely */
                    if (event.mouse.button & XGUI_MOUSE_RIGHT) {
                        needs_redraw = true;
                        continue;
                    }
                    /* Left-click: check if inside menu or dismiss */
                    xgui_ctx_action_t action = xgui_contextmenu_handle_event(&event);
                    if (action != XGUI_CTX_NONE) {
                        /* Deliver action as synthetic Ctrl+Shift+key (control char codes).
                         * Shift is set so terminal distinguishes copy/paste from SIGINT. */
                        xgui_event_t synth;
                        memset(&synth, 0, sizeof(synth));
                        synth.type = XGUI_EVENT_KEY_DOWN;
                        synth.key.modifiers = XGUI_MOD_CTRL | XGUI_MOD_SHIFT;
                        if (action == XGUI_CTX_CUT)        synth.key.keycode = 24;
                        if (action == XGUI_CTX_COPY)       synth.key.keycode = 3;
                        if (action == XGUI_CTX_PASTE)      synth.key.keycode = 22;
                        if (action == XGUI_CTX_SELECT_ALL)  synth.key.keycode = 1;
                        xgui_wm_dispatch_event(&synth);
                    }
                    needs_redraw = true;
                    continue;
                }
                /* Any key press dismisses context menu */
                if (event.type == XGUI_EVENT_KEY_DOWN || event.type == XGUI_EVENT_KEY_CHAR) {
                    xgui_contextmenu_hide();
                    needs_redraw = true;
                    continue;
                }
                /* Swallow all other events while menu is open */
                needs_redraw = true;
                continue;
            }

            /* Handle panel clicks */
            if (event.type == XGUI_EVENT_MOUSE_DOWN) {
                if (xgui_panel_click(event.mouse.x, event.mouse.y)) {
                    needs_redraw = true;
                    continue;
                }
            }

            /* Mouse move: dispatch for drag and drawing */
            if (event.type == XGUI_EVENT_MOUSE_MOVE) {
                xgui_wm_dispatch_event(&event);
                needs_redraw = true;
                continue;
            }

            /* Dispatch to window manager (sets dirty flags on windows) */
            xgui_wm_dispatch_event(&event);
            needs_redraw = true;
        }

        /* Check if cursor position changed */
        mouse_get_position(&mouse_x, &mouse_y);
        if (mouse_x != last_mouse_x || mouse_y != last_mouse_y) {
            mouse_moved = true;
            last_mouse_x = mouse_x;
            last_mouse_y = mouse_y;
        }

        /* Update analog clock (invalidates if second changed) */
        xgui_analogclock_update();

        /* Update ski game (advances game state each frame) */
        xgui_skigame_update();

        /* Update flappy cat game */
        xgui_flappycat_update();

        /* Always redraw — the analog clock ticks every second */
        needs_redraw = true;

        /* Repaint any dirty windows into their own buffers */
        xgui_wm_redraw_all();

        if (needs_redraw) {
            /* Rebuild the screen backbuffer */
            xgui_desktop_draw();
            xgui_wm_composite();
            xgui_panel_draw();
            xgui_draw_start_menu();

            /* Draw context menu overlay (on top of everything) */
            xgui_contextmenu_draw();

            /* Flush backbuffer to framebuffer */
            xgui_display_flush();

            /* Overlay cursor on framebuffer */
            xgui_draw_cursor(mouse_x, mouse_y);

            needs_redraw = false;
        } else if (mouse_moved) {
            /* Erase old cursor by re-flushing backbuffer lines */
            cursor_erase();

            /* Draw cursor at new position on framebuffer */
            xgui_draw_cursor(mouse_x, mouse_y);
        }

        /* Small delay to avoid 100% CPU */
        __asm__ volatile("sti");
        __asm__ volatile("hlt");
    }
    
    /* Cleanup */
    xgui_display_cleanup();
}

/*
 * Request XGUI to exit
 */
void xgui_quit(void) {
    xgui_running = false;
}

/*
 * Perform a real shutdown sequence:
 * 1. Display shutdown screen
 * 2. Flush all block devices
 * 3. Power off via ACPI (QEMU)
 */
void xgui_shutdown(void) {
    int w = xgui_display_width();
    int h = xgui_display_height();

    /* Stop the GUI event loop */
    xgui_running = false;

    /* Disable further input */
    __asm__ volatile("cli");

    /* --- Phase 1: "Saving your settings..." (2 seconds) --- */
    xgui_display_clear(XGUI_RGB(0, 0, 0));
    {
        const char* msg = "Saving your settings...";
        int tw = xgui_display_text_width(msg);
        xgui_display_text_transparent((w - tw) / 2, h / 2 - 6, msg, XGUI_WHITE);
    }
    xgui_display_mark_all_dirty();
    xgui_display_flush();
    for (volatile int i = 0; i < 600000000; i++);

    /* --- Phase 2: "Flushing disks..." (flush + 2 seconds) --- */
    xgui_display_clear(XGUI_RGB(0, 0, 0));
    {
        const char* msg = "MiniOS is shutting down...";
        int tw = xgui_display_text_width(msg);
        xgui_display_text_transparent((w - tw) / 2, h / 2 - 20, msg, XGUI_WHITE);
    }
    {
        const char* msg = "Flushing disks...";
        int tw = xgui_display_text_width(msg);
        xgui_display_text_transparent((w - tw) / 2, h / 2 + 4, msg, XGUI_LIGHT_GRAY);
    }
    xgui_display_mark_all_dirty();
    xgui_display_flush();

    /* Re-enable interrupts briefly for disk I/O */
    __asm__ volatile("sti");

    /* Flush all registered block devices */
    serial_write_string("SHUTDOWN: Flushing block devices...\n");
    for (int i = 0; i < 16; i++) {
        blockdev_t* dev = blockdev_get(i);
        if (dev) {
            blockdev_flush(dev);
            serial_write_string("SHUTDOWN: Flushed ");
            serial_write_string(dev->name);
            serial_write_string("\n");
        }
    }

    __asm__ volatile("cli");
    for (volatile int i = 0; i < 600000000; i++);

    /* --- Phase 3: "Shutting down devices..." (2 seconds) --- */
    xgui_display_clear(XGUI_RGB(0, 0, 0));
    {
        const char* msg = "Shutting down devices...";
        int tw = xgui_display_text_width(msg);
        xgui_display_text_transparent((w - tw) / 2, h / 2 - 6, msg, XGUI_WHITE);
    }
    xgui_display_mark_all_dirty();
    xgui_display_flush();
    for (volatile int i = 0; i < 600000000; i++);

    /* --- Phase 4: Safe to turn off message (10 seconds) --- */
    xgui_display_clear(XGUI_RGB(0, 0, 0));
    {
        const char* msg = "IT IS NOW SAFE TO TURN OFF YOUR COMPUTER.";
        int tw = xgui_display_text_width(msg);
        xgui_display_text_transparent((w - tw) / 2, h / 2 - 6, msg, XGUI_RGB(255, 170, 0));
    }
    xgui_display_mark_all_dirty();
    xgui_display_flush();

    serial_write_string("SHUTDOWN: Safe to turn off screen displayed.\n");

    /* Hold for ~30 seconds */
    for (volatile long i = 0; i < 2000000000L; i++);

    serial_write_string("SHUTDOWN: Powering off...\n");

    /* ACPI power off for QEMU (PIIX4 PM, port 0x604, value 0x2000) */
    outw(0x604, 0x2000);

    /* If ACPI didn't work, try Bochs/old QEMU shutdown port */
    outw(0xB004, 0x2000);

    /* If nothing worked, halt the CPU silently */
    while (1) {
        __asm__ volatile("hlt");
    }
}

/*
 * Check if XGUI is running
 */
bool xgui_is_running(void) {
    return xgui_running;
}
