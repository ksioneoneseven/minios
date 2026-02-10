/*
 * MiniOS Kernel Standard I/O Header
 * 
 * Provides printf-like functions for kernel output.
 */

#ifndef _STDIO_H
#define _STDIO_H

#include "types.h"

/* Variadic argument macros - use GCC builtins */
typedef __builtin_va_list va_list;
#define va_start(ap, last)  __builtin_va_start(ap, last)
#define va_arg(ap, type)    __builtin_va_arg(ap, type)
#define va_end(ap)          __builtin_va_end(ap)
#define va_copy(dest, src)  __builtin_va_copy(dest, src)

/*
 * Kernel printf - prints formatted output to VGA console
 * Supports: %d, %i, %u, %x, %X, %c, %s, %p, %%
 * Returns: number of characters printed
 */
int printk(const char* format, ...);

/*
 * Kernel vprintf - printf with va_list
 */
int vprintk(const char* format, va_list args);

/*
 * Kernel sprintf - prints formatted output to buffer
 * Returns: number of characters written (not including null terminator)
 */
int sprintf(char* buffer, const char* format, ...);

/*
 * Kernel vsprintf - sprintf with va_list
 */
int vsprintf(char* buffer, const char* format, va_list args);

/*
 * Kernel snprintf - sprintf with buffer size limit
 */
int snprintf(char* buffer, size_t size, const char* format, ...);

#endif /* _STDIO_H */

