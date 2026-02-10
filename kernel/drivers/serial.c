#include "../include/serial.h"
#include "../include/io.h"

#define COM1_PORT 0x3F8

static int serial_transmit_empty(void) {
    return (inb(COM1_PORT + 5) & 0x20) != 0;
}

void serial_init(void) {
    /* Disable interrupts */
    outb(COM1_PORT + 1, 0x00);

    /* Enable DLAB */
    outb(COM1_PORT + 3, 0x80);

    /* Set divisor to 1 (115200 baud) */
    outb(COM1_PORT + 0, 0x01);
    outb(COM1_PORT + 1, 0x00);

    /* 8 bits, no parity, one stop bit */
    outb(COM1_PORT + 3, 0x03);

    /* Enable FIFO, clear them, 14-byte threshold */
    outb(COM1_PORT + 2, 0xC7);

    /* IRQs enabled, RTS/DSR set */
    outb(COM1_PORT + 4, 0x0B);
}

void serial_write_char(char c) {
    if (c == '\n') {
        serial_write_char('\r');
    }

    while (!serial_transmit_empty()) {
        /* spin */
    }

    outb(COM1_PORT, (uint8_t)c);
}

void serial_write_string(const char* s) {
    if (!s) {
        return;
    }

    while (*s) {
        serial_write_char(*s++);
    }
}

void serial_write_hex(uint32_t v) {
    static const char hex[] = "0123456789ABCDEF";
    char buf[11];

    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 0; i < 8; i++) {
        buf[2 + i] = hex[(v >> ((7 - i) * 4)) & 0xF];
    }
    buf[10] = '\0';

    serial_write_string(buf);
}
