/*
 * MiniOS User-space Standard I/O
 */

#ifndef _STDIO_H
#define _STDIO_H

#include "syscall.h"

/* NULL pointer */
#ifndef NULL
#define NULL ((void*)0)
#endif

/* EOF */
#define EOF (-1)

/* Print a string to stdout */
int puts(const char* str);

/* Print a character to stdout */
int putchar(int c);

/* Get a character from stdin */
int getchar(void);

/* Formatted print to stdout */
int printf(const char* fmt, ...);

#endif /* _STDIO_H */

