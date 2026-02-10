/*
 * MiniOS I/O Port Functions Header
 * 
 * Provides inline assembly functions for reading/writing
 * to x86 I/O ports.
 */

#ifndef _IO_H
#define _IO_H

#include "types.h"

/*
 * Read a byte from an I/O port
 */
static inline uint8_t inb(uint16_t port) {
    uint8_t result;
    __asm__ volatile("inb %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

/*
 * Write a byte to an I/O port
 */
static inline void outb(uint16_t port, uint8_t data) {
    __asm__ volatile("outb %0, %1" : : "a"(data), "Nd"(port));
}

/*
 * Read a word (16-bit) from an I/O port
 */
static inline uint16_t inw(uint16_t port) {
    uint16_t result;
    __asm__ volatile("inw %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

/*
 * Write a word (16-bit) to an I/O port
 */
static inline void outw(uint16_t port, uint16_t data) {
    __asm__ volatile("outw %0, %1" : : "a"(data), "Nd"(port));
}

/*
 * Read a dword (32-bit) from an I/O port
 */
static inline uint32_t inl(uint16_t port) {
    uint32_t result;
    __asm__ volatile("inl %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

/*
 * Write a dword (32-bit) to an I/O port
 */
static inline void outl(uint16_t port, uint32_t data) {
    __asm__ volatile("outl %0, %1" : : "a"(data), "Nd"(port));
}

/*
 * Wait for I/O operation to complete (short delay)
 * Writes to unused port 0x80 which takes ~1 microsecond
 */
static inline void io_wait(void) {
    __asm__ volatile("outb %%al, $0x80" : : "a"(0));
}

#endif /* _IO_H */

