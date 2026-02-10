/*
 * MiniOS Keyboard Driver Header
 * 
 * PS/2 keyboard driver using IRQ1.
 */

#ifndef _KEYBOARD_H
#define _KEYBOARD_H

#include "types.h"

/* Keyboard I/O ports */
#define KEYBOARD_DATA_PORT      0x60
#define KEYBOARD_STATUS_PORT    0x64
#define KEYBOARD_COMMAND_PORT   0x64

/* Keyboard status register bits */
#define KEYBOARD_STATUS_OUTPUT  0x01    /* Output buffer full */
#define KEYBOARD_STATUS_INPUT   0x02    /* Input buffer full */

/* Special key flags */
#define KEY_SHIFT       0x0100
#define KEY_CTRL        0x0200
#define KEY_ALT         0x0400
#define KEY_CAPSLOCK    0x0800
#define KEY_NUMLOCK     0x1000
#define KEY_SCROLLLOCK  0x2000
#define KEY_RELEASED    0x8000

/* Special key codes (returned when no ASCII equivalent) */
#define KEY_ESCAPE      0x1B
#define KEY_BACKSPACE   0x08
#define KEY_TAB         0x09
#define KEY_ENTER       0x0D
#define KEY_F1          0x80
#define KEY_F2          0x81
#define KEY_F3          0x82
#define KEY_F4          0x83
#define KEY_F5          0x84
#define KEY_F6          0x85
#define KEY_F7          0x86
#define KEY_F8          0x87
#define KEY_F9          0x88
#define KEY_F10         0x89
#define KEY_F11         0x8A
#define KEY_F12         0x8B
#define KEY_UP          0x90
#define KEY_DOWN        0x91
#define KEY_LEFT        0x92
#define KEY_RIGHT       0x93
#define KEY_HOME        0x94
#define KEY_END         0x95
#define KEY_PAGEUP      0x96
#define KEY_PAGEDOWN    0x97
#define KEY_INSERT      0x98
#define KEY_DELETE      0x99

/* Keyboard callback function type */
typedef void (*keyboard_callback_t)(char c, uint16_t keycode);

/* Initialize keyboard driver */
void keyboard_init(void);

/* Get the last pressed key (blocking) */
char keyboard_getchar(void);

/* Check if a key is available (non-blocking) */
bool keyboard_has_key(void);

/* Get key without blocking (returns 0 if no key) */
char keyboard_getchar_nonblock(void);

/* Register a callback for key events */
void keyboard_set_callback(keyboard_callback_t callback);

/* Get current modifier state */
uint16_t keyboard_get_modifiers(void);

#endif /* _KEYBOARD_H */

