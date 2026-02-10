#ifndef _SERIAL_H
#define _SERIAL_H

#include "types.h"

/* Initialize COM1 (0x3F8) for debugging output */
void serial_init(void);

/* Write a single character to COM1 */
void serial_write_char(char c);

/* Write a null-terminated string to COM1 */
void serial_write_string(const char* s);

/* Write a 32-bit hex value (0xXXXXXXXX) */
void serial_write_hex(uint32_t v);

#endif /* _SERIAL_H */
