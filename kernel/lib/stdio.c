/*
 * MiniOS Kernel Standard I/O Implementation
 * 
 * Provides printf-like functions for kernel output.
 */

#include "../include/stdio.h"
#include "../include/vga.h"
#include "../include/string.h"

/* Output mode for vsprintf_internal */
#define OUTPUT_VGA      0
#define OUTPUT_BUFFER   1

/* Internal state for formatted output */
typedef struct {
    int mode;
    char* buffer;
    size_t pos;
    size_t max_size;
} output_state_t;

/*
 * Output a character
 */
static void output_char(output_state_t* state, char c) {
    if (state->mode == OUTPUT_VGA) {
        vga_putchar(c);
        state->pos++;
    } else {
        if (state->max_size == 0 || state->pos < state->max_size - 1) {
            state->buffer[state->pos++] = c;
        }
    }
}

/*
 * Output a string
 */
static void output_string(output_state_t* state, const char* str) {
    while (*str) {
        output_char(state, *str++);
    }
}

/*
 * Convert unsigned integer to string
 */
static void output_uint(output_state_t* state, uint32_t num, int base, int uppercase, int width, char pad) {
    char buffer[32];
    char* digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    int i = 0;
    
    if (num == 0) {
        buffer[i++] = '0';
    } else {
        while (num > 0) {
            buffer[i++] = digits[num % base];
            num /= base;
        }
    }
    
    /* Pad if needed */
    while (i < width) {
        buffer[i++] = pad;
    }
    
    /* Output in reverse */
    while (i > 0) {
        output_char(state, buffer[--i]);
    }
}

/*
 * Convert signed integer to string
 */
static void output_int(output_state_t* state, int32_t num, int width, char pad) {
    if (num < 0) {
        output_char(state, '-');
        num = -num;
        if (width > 0) width--;
    }
    output_uint(state, (uint32_t)num, 10, 0, width, pad);
}

/*
 * Internal vsprintf implementation
 */
static int vsprintf_internal(output_state_t* state, const char* format, va_list args) {
    state->pos = 0;
    
    while (*format) {
        if (*format != '%') {
            output_char(state, *format++);
            continue;
        }
        
        format++;  /* Skip '%' */

        /* Parse flags */
        int left_align = 0;
        if (*format == '-') {
            left_align = 1;
            format++;
        }

        /* Parse width and padding */
        char pad = ' ';
        int width = 0;

        if (*format == '0' && !left_align) {
            pad = '0';
            format++;
        }

        while (*format >= '0' && *format <= '9') {
            width = width * 10 + (*format - '0');
            format++;
        }

        /* Parse format specifier */
        switch (*format) {
            case 'd':
            case 'i':
                output_int(state, va_arg(args, int32_t), width, pad);
                break;
            case 'u':
                output_uint(state, va_arg(args, uint32_t), 10, 0, width, pad);
                break;
            case 'x':
                output_uint(state, va_arg(args, uint32_t), 16, 0, width, pad);
                break;
            case 'X':
                output_uint(state, va_arg(args, uint32_t), 16, 1, width, pad);
                break;
            case 'p':
                output_string(state, "0x");
                output_uint(state, va_arg(args, uint32_t), 16, 0, 8, '0');
                break;
            case 'c':
                output_char(state, (char)va_arg(args, int));
                break;
            case 's': {
                const char* str = va_arg(args, const char*);
                if (str == NULL) str = "(null)";
                int len = 0;
                const char* s = str;
                while (*s++) len++;

                if (!left_align) {
                    /* Right align: pad first */
                    while (len < width) { output_char(state, ' '); len++; }
                    output_string(state, str);
                } else {
                    /* Left align: string first, then pad */
                    output_string(state, str);
                    while (len < width) { output_char(state, ' '); len++; }
                }
                break;
            }
            case '%':
                output_char(state, '%');
                break;
            default:
                output_char(state, '%');
                output_char(state, *format);
                break;
        }
        format++;
    }
    
    /* Null-terminate buffer output */
    if (state->mode == OUTPUT_BUFFER) {
        state->buffer[state->pos] = '\0';
    }
    
    return state->pos;
}

int vprintk(const char* format, va_list args) {
    output_state_t state = { .mode = OUTPUT_VGA, .buffer = NULL, .pos = 0, .max_size = 0 };
    return vsprintf_internal(&state, format, args);
}

int printk(const char* format, ...) {
    va_list args;
    va_start(args, format);
    int ret = vprintk(format, args);
    va_end(args);
    return ret;
}

int vsprintf(char* buffer, const char* format, va_list args) {
    output_state_t state = { .mode = OUTPUT_BUFFER, .buffer = buffer, .pos = 0, .max_size = 0 };
    return vsprintf_internal(&state, format, args);
}

int sprintf(char* buffer, const char* format, ...) {
    va_list args;
    va_start(args, format);
    int ret = vsprintf(buffer, format, args);
    va_end(args);
    return ret;
}

int snprintf(char* buffer, size_t size, const char* format, ...) {
    output_state_t state = { .mode = OUTPUT_BUFFER, .buffer = buffer, .pos = 0, .max_size = size };
    va_list args;
    va_start(args, format);
    int ret = vsprintf_internal(&state, format, args);
    va_end(args);
    return ret;
}

