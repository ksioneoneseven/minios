/*
 * MiniOS VGA Text Mode Driver
 *
 * Provides text output to the VGA display in 80x25 text mode.
 * Supports colors, scrolling, cursor positioning, and scrollback buffer.
 */

#include "../include/vga.h"
#include "../include/string.h"

/* VGA buffer pointer */
static uint16_t* const vga_buffer = (uint16_t*)VGA_BUFFER;

/* Scrollback buffer - stores all output history */
static uint16_t scrollback[VGA_SCROLLBACK_LINES * VGA_WIDTH];

/* Current write position in scrollback (line number) */
static size_t scrollback_write_line = 0;

/* Total lines written to scrollback */
static size_t scrollback_total_lines = 0;

/* Current view offset (0 = viewing current output, >0 = viewing history) */
static size_t view_offset = 0;

/* Current cursor position (within current line) */
static size_t vga_row;
static size_t vga_column;

/* Output redirect hook */
static vga_output_redirect_t output_redirect = NULL;

void vga_set_output_redirect(vga_output_redirect_t redirect) {
    output_redirect = redirect;
}

bool vga_is_redirected(void) {
    return output_redirect != NULL;
}

/* Current text color attribute */
static uint8_t vga_color;

 static uint32_t cursor_blink_last_toggle_ticks = 0;
 static bool cursor_blink_on = false;
 static size_t cursor_blink_row = 0;
 static size_t cursor_blink_col = 0;
 static uint16_t cursor_blink_saved_entry = 0;
 static bool cursor_blink_enabled = true;

/*
 * Copy a line from scrollback to VGA buffer
 */
static void copy_line_to_vga(size_t scrollback_line, size_t vga_line) {
    size_t sb_index = (scrollback_line % VGA_SCROLLBACK_LINES) * VGA_WIDTH;
    size_t vga_index = vga_line * VGA_WIDTH;
    for (size_t x = 0; x < VGA_WIDTH; x++) {
        vga_buffer[vga_index + x] = scrollback[sb_index + x];
    }
}

/*
 * Refresh the VGA display from scrollback buffer
 */
static void vga_refresh_display(void) {
    size_t display_start;

    if (scrollback_total_lines <= VGA_HEIGHT) {
        /* Not enough lines to fill screen */
        display_start = 0;
    } else {
        /* Calculate which scrollback line to start from */
        display_start = scrollback_total_lines - VGA_HEIGHT - view_offset;
    }

    for (size_t y = 0; y < VGA_HEIGHT; y++) {
        size_t line = display_start + y;
        if (line < scrollback_total_lines) {
            copy_line_to_vga(line, y);
        } else {
            /* Clear lines beyond content */
            for (size_t x = 0; x < VGA_WIDTH; x++) {
                vga_buffer[y * VGA_WIDTH + x] = vga_entry(' ', vga_color);
            }
        }
    }
}

/*
 * Scroll the screen up by one line (internal - when new content added)
 */
static void vga_scroll(void) {
    /* Move to next line in scrollback buffer */
    scrollback_write_line = (scrollback_write_line + 1) % VGA_SCROLLBACK_LINES;
    scrollback_total_lines++;

    /* Clear the new line in scrollback */
    size_t sb_index = scrollback_write_line * VGA_WIDTH;
    for (size_t x = 0; x < VGA_WIDTH; x++) {
        scrollback[sb_index + x] = vga_entry(' ', vga_color);
    }

    /* If viewing current output, refresh display */
    if (view_offset == 0) {
        vga_refresh_display();
    }
}

 static void vga_enable_cursor(void) {
     __asm__ volatile("outb %0, %1" : : "a"((uint8_t)0x0A), "Nd"((uint16_t)0x3D4));
     __asm__ volatile("outb %0, %1" : : "a"((uint8_t)0x00), "Nd"((uint16_t)0x3D5));
     __asm__ volatile("outb %0, %1" : : "a"((uint8_t)0x0B), "Nd"((uint16_t)0x3D4));
     __asm__ volatile("outb %0, %1" : : "a"((uint8_t)0x0F), "Nd"((uint16_t)0x3D5));
 }

/*
 * Update hardware cursor position
 */
static void vga_update_cursor(void) {
    uint16_t pos = vga_row * VGA_WIDTH + vga_column;
    
    /* VGA cursor position is set via I/O ports 0x3D4/0x3D5 */
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)0x0F), "Nd"((uint16_t)0x3D4));
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)(pos & 0xFF)), "Nd"((uint16_t)0x3D5));
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)0x0E), "Nd"((uint16_t)0x3D4));
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)((pos >> 8) & 0xFF)), "Nd"((uint16_t)0x3D5));
}

 void vga_cursor_blink_disable(void) {
     cursor_blink_enabled = false;
 }

 void vga_cursor_blink_tick(uint32_t ticks) {
     if (!cursor_blink_enabled) return;
     if (view_offset != 0) {
         return;
     }

     if ((ticks - cursor_blink_last_toggle_ticks) < 25) {
         return;
     }
     cursor_blink_last_toggle_ticks = ticks;

     size_t index = vga_row * VGA_WIDTH + vga_column;

     if (cursor_blink_saved_entry != 0 && (cursor_blink_row != vga_row || cursor_blink_col != vga_column)) {
         size_t old_index = cursor_blink_row * VGA_WIDTH + cursor_blink_col;
         vga_buffer[old_index] = cursor_blink_saved_entry;
         cursor_blink_saved_entry = 0;
         cursor_blink_on = false;
     }

     if (cursor_blink_saved_entry == 0) {
         cursor_blink_saved_entry = vga_buffer[index];
         cursor_blink_row = vga_row;
         cursor_blink_col = vga_column;
     }

     uint16_t base = cursor_blink_saved_entry;
     uint8_t ch = (uint8_t)(base & 0xFF);
     uint8_t attr = (uint8_t)((base >> 8) & 0xFF);
     uint8_t fg = attr & 0x0F;
     uint8_t bg = (attr >> 4) & 0x0F;
     uint8_t inv_attr = (uint8_t)((fg << 4) | bg);

     cursor_blink_on = !cursor_blink_on;
     vga_buffer[index] = cursor_blink_on ? vga_entry(ch, inv_attr) : base;
 }

/*
 * Initialize VGA driver
 */
void vga_init(void) {
    vga_row = 0;
    vga_column = 0;
    vga_color = vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    scrollback_write_line = 0;
    scrollback_total_lines = 0;
    view_offset = 0;

    /* Clear scrollback buffer */
    for (size_t i = 0; i < VGA_SCROLLBACK_LINES * VGA_WIDTH; i++) {
        scrollback[i] = vga_entry(' ', vga_color);
    }

    /* Initialize first line */
    scrollback_total_lines = 1;

     vga_enable_cursor();

    vga_clear();
}

/*
 * Clear the entire screen
 */
void vga_clear(void) {
    /* Clear VGA buffer */
    for (size_t y = 0; y < VGA_HEIGHT; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            const size_t index = y * VGA_WIDTH + x;
            vga_buffer[index] = vga_entry(' ', vga_color);
        }
    }

    /* Reset scrollback to fresh state */
    scrollback_write_line = 0;
    scrollback_total_lines = 1;
    view_offset = 0;

    /* Clear first line in scrollback */
    for (size_t x = 0; x < VGA_WIDTH; x++) {
        scrollback[x] = vga_entry(' ', vga_color);
    }

    vga_row = 0;
    vga_column = 0;
    vga_update_cursor();
}

/*
 * Set the current text color
 */
void vga_set_color(uint8_t color) {
    vga_color = color;
}

/*
 * Move cursor left by one position
 */
void vga_move_cursor_left(void) {
    if (vga_column > 0) {
        vga_column--;
    } else if (vga_row > 0) {
        vga_row--;
        vga_column = VGA_WIDTH - 1;
    }
    vga_update_cursor();
}

/*
 * Move cursor right by one position
 */
void vga_move_cursor_right(void) {
    if (vga_column < VGA_WIDTH - 1) {
        vga_column++;
    } else if (vga_row < VGA_HEIGHT - 1) {
        vga_row++;
        vga_column = 0;
    }
    vga_update_cursor();
}

/*
 * Restore cursor cell before modifying display (called before writing chars)
 */
static void vga_cursor_blink_restore(void) {
    if (cursor_blink_saved_entry != 0) {
        size_t old_index = cursor_blink_row * VGA_WIDTH + cursor_blink_col;
        vga_buffer[old_index] = cursor_blink_saved_entry;
        cursor_blink_saved_entry = 0;
        cursor_blink_on = false;
    }
}

/*
 * Write a single character at current cursor position
 */
void vga_putchar(char c) {
    /* If output is being redirected, send there instead */
    if (output_redirect) {
        output_redirect(c);
        return;
    }

    /* Restore cursor cell before writing */
    vga_cursor_blink_restore();

    /* Auto-scroll to bottom when typing */
    if (view_offset != 0) {
        view_offset = 0;
        vga_refresh_display();
    }

    /* Handle special characters */
    if (c == '\n') {
        vga_column = 0;
        vga_row++;
    } else if (c == '\r') {
        vga_column = 0;
    } else if (c == '\t') {
        /* Tab: advance to next multiple of 8 */
        vga_column = (vga_column + 8) & ~7;
    } else if (c == '\b') {
        /* Backspace */
        if (vga_column > 0) {
            vga_column--;
            /* Update both VGA buffer and scrollback */
            size_t vga_index = vga_row * VGA_WIDTH + vga_column;
            size_t sb_index = (scrollback_write_line % VGA_SCROLLBACK_LINES) * VGA_WIDTH + vga_column;
            vga_buffer[vga_index] = vga_entry(' ', vga_color);
            scrollback[sb_index] = vga_entry(' ', vga_color);
        }
    } else {
        /* Regular character - write to both VGA and scrollback */
        size_t vga_index = vga_row * VGA_WIDTH + vga_column;
        size_t sb_index = (scrollback_write_line % VGA_SCROLLBACK_LINES) * VGA_WIDTH + vga_column;
        uint16_t entry = vga_entry(c, vga_color);
        vga_buffer[vga_index] = entry;
        scrollback[sb_index] = entry;
        vga_column++;
    }

    /* Handle line wrap */
    if (vga_column >= VGA_WIDTH) {
        vga_column = 0;
        vga_row++;
    }

    /* Handle scrolling */
    if (vga_row >= VGA_HEIGHT) {
        vga_scroll();
        vga_row = VGA_HEIGHT - 1;
    }
    
    /* Update hardware cursor */
    vga_update_cursor();
}

/*
 * Write a null-terminated string
 */
void vga_puts(const char* str) {
    while (*str) {
        vga_putchar(*str);
        str++;
    }
}

/*
 * Write a string with specific color
 */
void vga_write(const char* str, uint8_t color) {
    if (output_redirect) {
        /* Emit ANSI escape code for the foreground color */
        uint8_t fg = color & 0x0F;
        /* Map VGA color to ANSI SGR code */
        static const char* ansi_fg[] = {
            "\x1b[30m",  /* 0  black */
            "\x1b[34m",  /* 1  blue */
            "\x1b[32m",  /* 2  green */
            "\x1b[36m",  /* 3  cyan */
            "\x1b[31m",  /* 4  red */
            "\x1b[35m",  /* 5  magenta */
            "\x1b[33m",  /* 6  brown/yellow */
            "\x1b[37m",  /* 7  light grey */
            "\x1b[90m",  /* 8  dark grey */
            "\x1b[94m",  /* 9  light blue */
            "\x1b[92m",  /* 10 light green */
            "\x1b[96m",  /* 11 light cyan */
            "\x1b[91m",  /* 12 light red */
            "\x1b[95m",  /* 13 light magenta */
            "\x1b[93m",  /* 14 yellow */
            "\x1b[97m",  /* 15 white */
        };
        /* Send escape code */
        for (const char* p = ansi_fg[fg]; *p; p++)
            output_redirect(*p);
        /* Send the text */
        for (const char* p = str; *p; p++)
            output_redirect(*p);
        /* Reset color */
        const char* reset = "\x1b[0m";
        for (const char* p = reset; *p; p++)
            output_redirect(*p);
        return;
    }
    uint8_t old_color = vga_color;
    vga_color = color;
    vga_puts(str);
    vga_color = old_color;
}

/*
 * Write a string at specific position (direct to VGA, bypasses scrollback)
 * This is used for full-screen applications like spreadsheet
 */
void vga_write_at(const char* str, uint8_t color, size_t x, size_t y) {
    size_t col = x;
    size_t row = y;

    while (*str && row < VGA_HEIGHT) {
        if (*str == '\n') {
            col = x;
            row++;
        } else if (*str == '\r') {
            col = x;
        } else if (*str == '\t') {
            col = (col + 8) & ~7;
        } else {
            if (col < VGA_WIDTH) {
                size_t index = row * VGA_WIDTH + col;
                vga_buffer[index] = vga_entry(*str, color);
                col++;
            }
        }
        str++;

        /* Wrap to next line if needed */
        if (col >= VGA_WIDTH) {
            col = 0;
            row++;
        }
    }
}

/*
 * Move cursor to specific position
 */
void vga_set_cursor(size_t x, size_t y) {
    if (x < VGA_WIDTH && y < VGA_HEIGHT) {
        vga_column = x;
        vga_row = y;
    }
}

/*
 * Scroll the view up (show older content)
 */
void vga_scroll_up(size_t lines) {
    /* Calculate maximum scroll offset */
    size_t max_offset = 0;
    if (scrollback_total_lines > VGA_HEIGHT) {
        max_offset = scrollback_total_lines - VGA_HEIGHT;
        /* Limit to actual buffer size */
        if (max_offset > VGA_SCROLLBACK_LINES - VGA_HEIGHT) {
            max_offset = VGA_SCROLLBACK_LINES - VGA_HEIGHT;
        }
    }

    view_offset += lines;
    if (view_offset > max_offset) {
        view_offset = max_offset;
    }

    vga_refresh_display();
}

/*
 * Scroll the view down (show newer content)
 */
void vga_scroll_down(size_t lines) {
    if (view_offset >= lines) {
        view_offset -= lines;
    } else {
        view_offset = 0;
    }

    vga_refresh_display();
}

/*
 * Scroll to bottom (current output)
 */
void vga_scroll_to_bottom(void) {
    if (view_offset != 0) {
        view_offset = 0;
        vga_refresh_display();
    }
}

/*
 * Check if currently viewing scrollback
 */
bool vga_is_scrolled(void) {
    return view_offset != 0;
}
