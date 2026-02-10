/*
 * MiniOS PS/2 Keyboard Driver
 * 
 * Handles keyboard input via IRQ1.
 * Translates scancodes to ASCII characters.
 */

#include "../include/keyboard.h"
#include "../include/timer.h"
#include "../include/io.h"
#include "../include/isr.h"
#include "../include/pic.h"
#include "../include/serial.h"
#include "../include/vga.h"
#include "../include/signal.h"
#include "../include/process.h"
#include "../include/stdio.h"

/* Keyboard buffer */
#define KEYBOARD_BUFFER_SIZE 256
static char keyboard_buffer[KEYBOARD_BUFFER_SIZE];
static uint16_t keyboard_mods_buffer[KEYBOARD_BUFFER_SIZE];  /* Modifier state at time of keypress */
static volatile size_t buffer_head = 0;
static volatile size_t buffer_tail = 0;

/* Modifier key state */
static uint16_t modifier_state = 0;

/* Last retrieved modifier state */
static uint16_t last_key_modifiers = 0;

/* Callback function */
static keyboard_callback_t key_callback = NULL;

/* US keyboard scancode to ASCII (lowercase) */
static const char scancode_to_ascii[128] = {
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    '7', '8', '9', '-', '4', '5', '6', '+', '1', '2', '3', '0', '.',
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

/* US keyboard scancode to ASCII (uppercase/shifted) */
static const char scancode_to_ascii_shift[128] = {
    0, 27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0, '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    '*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    '7', '8', '9', '-', '4', '5', '6', '+', '1', '2', '3', '0', '.',
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

/* Scancode constants */
#define SC_LSHIFT       0x2A
#define SC_RSHIFT       0x36
#define SC_LCTRL        0x1D
#define SC_LALT         0x38
#define SC_CAPSLOCK     0x3A
#define SC_EXTENDED     0xE0

/* Extended scancode state */
static bool extended_scancode = false;

/* Extended scancode mappings (after 0xE0 prefix) */
#define SC_EXT_UP       0x48
#define SC_EXT_DOWN     0x50
#define SC_EXT_LEFT     0x4B
#define SC_EXT_RIGHT    0x4D
#define SC_EXT_HOME     0x47
#define SC_EXT_END      0x4F
#define SC_EXT_PAGEUP   0x49
#define SC_EXT_PAGEDOWN 0x51
#define SC_EXT_INSERT   0x52
#define SC_EXT_DELETE   0x53
#define SC_EXT_RCTRL    0x1D
#define SC_EXT_RALT     0x38

/* Function key scancodes */
#define SC_F1           0x3B
#define SC_F2           0x3C
#define SC_F3           0x3D
#define SC_F4           0x3E
#define SC_F5           0x3F
#define SC_F6           0x40
#define SC_F7           0x41
#define SC_F8           0x42
#define SC_F9           0x43
#define SC_F10          0x44
#define SC_F11          0x57
#define SC_F12          0x58

/*
 * Add character to keyboard buffer with current modifier state
 */
static void buffer_put(char c) {
    size_t next = (buffer_head + 1) % KEYBOARD_BUFFER_SIZE;
    if (next != buffer_tail) {
        keyboard_buffer[buffer_head] = c;
        keyboard_mods_buffer[buffer_head] = modifier_state;  /* Capture modifier state */
        buffer_head = next;
    }
}

/*
 * Keyboard interrupt handler
 */
static void keyboard_handler(registers_t* regs) {
    (void)regs;  /* Unused */

    uint8_t scancode = inb(KEYBOARD_DATA_PORT);

    /* Handle extended scancode prefix */
    if (scancode == SC_EXTENDED) {
        extended_scancode = true;
        return;
    }

    bool released = (scancode & 0x80) != 0;
    scancode &= 0x7F;

    /* Handle extended scancodes (arrow keys, etc.) */
    if (extended_scancode) {
        extended_scancode = false;

        /* Handle extended modifier keys (Right Ctrl, Right Alt) */
        switch (scancode) {
            case SC_EXT_RCTRL:
                if (released) modifier_state &= ~KEY_CTRL;
                else modifier_state |= KEY_CTRL;
                return;
            case SC_EXT_RALT:
                if (released) modifier_state &= ~KEY_ALT;
                else modifier_state |= KEY_ALT;
                return;
        }

        if (!released) {
            char special = 0;
            switch (scancode) {
                case SC_EXT_UP:       special = KEY_UP;       break;
                case SC_EXT_DOWN:     special = KEY_DOWN;     break;
                case SC_EXT_LEFT:     special = KEY_LEFT;     break;
                case SC_EXT_RIGHT:    special = KEY_RIGHT;    break;
                case SC_EXT_HOME:     special = KEY_HOME;     break;
                case SC_EXT_END:      special = KEY_END;      break;
                case SC_EXT_PAGEUP:   special = KEY_PAGEUP;   break;
                case SC_EXT_PAGEDOWN: special = KEY_PAGEDOWN; break;
                case SC_EXT_INSERT:   special = KEY_INSERT;   break;
                case SC_EXT_DELETE:   special = KEY_DELETE;   break;
            }
            if (special != 0) {
                buffer_put(special);
                if (key_callback) {
                    key_callback(special, scancode | modifier_state | 0xE000);
                }
            }
        }
        return;
    }

    /* Handle modifier keys */
    switch (scancode) {
        case SC_LSHIFT:
        case SC_RSHIFT:
            if (released) modifier_state &= ~KEY_SHIFT;
            else modifier_state |= KEY_SHIFT;
            return;
        case SC_LCTRL:
            if (released) modifier_state &= ~KEY_CTRL;
            else modifier_state |= KEY_CTRL;
            return;
        case SC_LALT:
            if (released) modifier_state &= ~KEY_ALT;
            else modifier_state |= KEY_ALT;
            return;
        case SC_CAPSLOCK:
            if (!released) modifier_state ^= KEY_CAPSLOCK;
            return;
    }

    /* Only process key presses, not releases */
    if (released) return;

    /* Handle function keys */
    char special = 0;
    switch (scancode) {
        case SC_F1:  special = KEY_F1;  break;
        case SC_F2:  special = KEY_F2;  break;
        case SC_F3:  special = KEY_F3;  break;
        case SC_F4:  special = KEY_F4;  break;
        case SC_F5:  special = KEY_F5;  break;
        case SC_F6:  special = KEY_F6;  break;
        case SC_F7:  special = KEY_F7;  break;
        case SC_F8:  special = KEY_F8;  break;
        case SC_F9:  special = KEY_F9;  break;
        case SC_F10: special = KEY_F10; break;
        case SC_F11: special = KEY_F11; break;
        case SC_F12: special = KEY_F12; break;
    }

    if (special != 0) {
        buffer_put(special);
        if (key_callback) {
            key_callback(special, scancode | modifier_state);
        }
        return;
    }

    /* Translate scancode to ASCII */
    char c;
    bool shift = (modifier_state & KEY_SHIFT) != 0;
    bool caps = (modifier_state & KEY_CAPSLOCK) != 0;
    bool ctrl = (modifier_state & KEY_CTRL) != 0;

    if (shift ^ caps) {
        c = scancode_to_ascii_shift[scancode];
    } else {
        c = scancode_to_ascii[scancode];
    }

    /* Handle Ctrl+letter combinations (produce control characters 1-26) */
    if (ctrl && c != 0) {
        if (c >= 'a' && c <= 'z') {
            c = c - 'a' + 1;  /* Ctrl+a = 1, Ctrl+z = 26 */
        } else if (c >= 'A' && c <= 'Z') {
            c = c - 'A' + 1;  /* Ctrl+A = 1, Ctrl+Z = 26 */
        } else if (c == '[') {
            c = 27;  /* Ctrl+[ = ESC */
        } else if (c == '\\') {
            c = 28;  /* Ctrl+\ */
        } else if (c == ']') {
            c = 29;  /* Ctrl+] */
        } else if (c == '^' || c == '6') {
            c = 30;  /* Ctrl+^ or Ctrl+6 */
        } else if (c == '_' || c == '-') {
            c = 31;  /* Ctrl+_ or Ctrl+- */
        }
    }

    /* Handle Ctrl+C - send SIGINT to current process */
    if (c == 3) {  /* Ctrl+C produces control character 3 */
        process_t* proc = process_current();
        if (proc && proc->pid != 0) {  /* Don't kill kernel idle process */
            signal_send(proc->pid, SIGINT);
            /* Still add Ctrl+C to buffer so shell can handle it if needed */
        }
    }

    /* Add to buffer if valid character */
    if (c != 0) {
        buffer_put(c);

        /* Call callback if registered */
        if (key_callback) {
            key_callback(c, scancode | modifier_state);
        }
    }
}

void keyboard_init(void) {
    buffer_head = 0;
    buffer_tail = 0;
    modifier_state = 0;
    extended_scancode = false;
    key_callback = NULL;

    /* Register IRQ1 handler */
    irq_register_handler(1, keyboard_handler);

    /* Enable keyboard IRQ */
    pic_enable_irq(1);
}

bool keyboard_has_key(void) {
    return buffer_head != buffer_tail;
}

char keyboard_getchar_nonblock(void) {
    if (buffer_head == buffer_tail) return 0;
    char c = keyboard_buffer[buffer_tail];
    last_key_modifiers = keyboard_mods_buffer[buffer_tail];  /* Capture modifiers from when key was pressed */
    buffer_tail = (buffer_tail + 1) % KEYBOARD_BUFFER_SIZE;
    return c;
}

char keyboard_getchar(void) {
    /* Enable interrupts so keyboard IRQ can fire */
    __asm__ volatile("sti");

    while (!keyboard_has_key()) {
        /* Busy wait - less efficient but more reliable for debugging */
        __asm__ volatile("pause");  /* CPU hint for spin-wait loops */
    }
    return keyboard_getchar_nonblock();
}

void keyboard_set_callback(keyboard_callback_t callback) {
    key_callback = callback;
}

uint16_t keyboard_get_modifiers(void) {
    return last_key_modifiers;  /* Return modifiers from when last key was pressed */
}

