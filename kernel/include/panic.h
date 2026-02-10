/*
 * MiniOS Kernel Panic Header
 * 
 * Provides functions for handling unrecoverable kernel errors.
 */

#ifndef _PANIC_H
#define _PANIC_H

#include "types.h"
#include "idt.h"

/*
 * Kernel panic - halt the system with an error message
 * This function never returns.
 */
void kernel_panic(const char* message);

/*
 * Kernel panic with format string
 */
void kernel_panicf(const char* format, ...);

/*
 * Kernel panic with register dump
 * Called from exception handlers
 */
void kernel_panic_regs(const char* message, registers_t* regs);

/*
 * Assert macro - panic if condition is false
 */
#define ASSERT(condition) \
    do { \
        if (!(condition)) { \
            kernel_panicf("Assertion failed: %s\nFile: %s\nLine: %d", \
                         #condition, __FILE__, __LINE__); \
        } \
    } while (0)

/*
 * Kernel bug macro - indicates a bug in kernel code
 */
#define BUG() kernel_panicf("BUG at %s:%d", __FILE__, __LINE__)

/*
 * Kernel bug if condition is true
 */
#define BUG_ON(condition) \
    do { \
        if (condition) { \
            kernel_panicf("BUG_ON(%s) at %s:%d", #condition, __FILE__, __LINE__); \
        } \
    } while (0)

#endif /* _PANIC_H */

