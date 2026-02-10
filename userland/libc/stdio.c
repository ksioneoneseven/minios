/*
 * MiniOS User-space Standard I/O Implementation
 */

#include "include/stdio.h"
#include "include/unistd.h"
#include "include/string.h"

/* Variadic argument macros (GCC built-ins) */
typedef __builtin_va_list va_list;
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_arg(ap, type)   __builtin_va_arg(ap, type)
#define va_end(ap)         __builtin_va_end(ap)

int puts(const char* str) {
    int len = strlen(str);
    write(STDOUT_FILENO, str, len);
    write(STDOUT_FILENO, "\n", 1);
    return len + 1;
}

int putchar(int c) {
    char ch = (char)c;
    write(STDOUT_FILENO, &ch, 1);
    return c;
}

int getchar(void) {
    char c;
    if (read(STDIN_FILENO, &c, 1) <= 0) {
        return EOF;
    }
    return (unsigned char)c;
}

/* Simple integer to string */
static int print_int(int n) {
    char buf[12];
    int i = 0;
    int neg = 0;
    unsigned int un;
    
    if (n < 0) {
        neg = 1;
        un = -n;
    } else {
        un = n;
    }
    
    do {
        buf[i++] = '0' + (un % 10);
        un /= 10;
    } while (un > 0);
    
    if (neg) buf[i++] = '-';
    
    int len = i;
    while (i > 0) {
        putchar(buf[--i]);
    }
    return len;
}

/* Simple unsigned integer to string */
static int print_uint(unsigned int n) {
    char buf[12];
    int i = 0;
    
    do {
        buf[i++] = '0' + (n % 10);
        n /= 10;
    } while (n > 0);
    
    int len = i;
    while (i > 0) {
        putchar(buf[--i]);
    }
    return len;
}

/* Simple hex print */
static int print_hex(unsigned int n) {
    char buf[9];
    int i = 0;
    const char* hex = "0123456789abcdef";
    
    do {
        buf[i++] = hex[n & 0xF];
        n >>= 4;
    } while (n > 0);
    
    int len = i;
    while (i > 0) {
        putchar(buf[--i]);
    }
    return len;
}

int printf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);

    int count = 0;

    while (*fmt) {
        if (*fmt != '%') {
            putchar(*fmt++);
            count++;
            continue;
        }

        fmt++; /* Skip '%' */

        /* Parse flags */
        int left_justify = 0;
        int zero_pad = 0;
        while (*fmt == '-' || *fmt == '0') {
            if (*fmt == '-') left_justify = 1;
            if (*fmt == '0') zero_pad = 1;
            fmt++;
        }

        /* Parse width */
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }

        /* Handle format specifier */
        switch (*fmt) {
            case 'd':
            case 'i': {
                int val = va_arg(args, int);
                char buf[12];
                int i = 0;
                int neg = 0;
                unsigned int un;

                if (val < 0) {
                    neg = 1;
                    un = -val;
                } else {
                    un = val;
                }

                do {
                    buf[i++] = '0' + (un % 10);
                    un /= 10;
                } while (un > 0);

                if (neg) buf[i++] = '-';

                int len = i;
                int pad = width - len;

                if (!left_justify) {
                    while (pad-- > 0) { putchar(zero_pad ? '0' : ' '); count++; }
                }
                while (i > 0) { putchar(buf[--i]); count++; }
                if (left_justify) {
                    while (pad-- > 0) { putchar(' '); count++; }
                }
                break;
            }
            case 'u':
                count += print_uint(va_arg(args, unsigned int));
                break;
            case 'x': {
                unsigned int val = va_arg(args, unsigned int);
                char buf[9];
                int i = 0;
                const char* hex = "0123456789abcdef";

                do {
                    buf[i++] = hex[val & 0xF];
                    val >>= 4;
                } while (val > 0);

                int len = i;
                int pad = width - len;

                if (!left_justify) {
                    while (pad-- > 0) { putchar(zero_pad ? '0' : ' '); count++; }
                }
                while (i > 0) { putchar(buf[--i]); count++; }
                if (left_justify) {
                    while (pad-- > 0) { putchar(' '); count++; }
                }
                break;
            }
            case 'c':
                putchar(va_arg(args, int));
                count++;
                break;
            case 's': {
                const char* s = va_arg(args, const char*);
                if (s == NULL) s = "(null)";
                int len = strlen(s);
                int pad = width - len;

                if (!left_justify) {
                    while (pad-- > 0) { putchar(' '); count++; }
                }
                while (*s) {
                    putchar(*s++);
                    count++;
                }
                if (left_justify) {
                    while (pad-- > 0) { putchar(' '); count++; }
                }
                break;
            }
            case 'p':
                putchar('0');
                putchar('x');
                count += 2 + print_hex(va_arg(args, unsigned int));
                break;
            case '%':
                putchar('%');
                count++;
                break;
            default:
                putchar('%');
                putchar(*fmt);
                count += 2;
                break;
        }
        fmt++;
    }

    va_end(args);
    return count;
}

