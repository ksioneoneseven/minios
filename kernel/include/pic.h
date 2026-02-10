/*
 * MiniOS 8259 PIC (Programmable Interrupt Controller) Header
 * 
 * The 8259 PIC handles hardware interrupts from devices.
 * There are two PICs: master (IRQ 0-7) and slave (IRQ 8-15).
 */

#ifndef _PIC_H
#define _PIC_H

#include "types.h"

/* PIC I/O ports */
#define PIC1_COMMAND    0x20    /* Master PIC command port */
#define PIC1_DATA       0x21    /* Master PIC data port */
#define PIC2_COMMAND    0xA0    /* Slave PIC command port */
#define PIC2_DATA       0xA1    /* Slave PIC data port */

/* PIC commands */
#define PIC_EOI         0x20    /* End of interrupt */

/* ICW1 (Initialization Command Word 1) */
#define ICW1_ICW4       0x01    /* ICW4 needed */
#define ICW1_SINGLE     0x02    /* Single mode (vs cascade) */
#define ICW1_INTERVAL4  0x04    /* Call address interval 4 */
#define ICW1_LEVEL      0x08    /* Level triggered mode */
#define ICW1_INIT       0x10    /* Initialization */

/* ICW4 (Initialization Command Word 4) */
#define ICW4_8086       0x01    /* 8086/88 mode */
#define ICW4_AUTO       0x02    /* Auto EOI */
#define ICW4_BUF_SLAVE  0x08    /* Buffered mode (slave) */
#define ICW4_BUF_MASTER 0x0C    /* Buffered mode (master) */
#define ICW4_SFNM       0x10    /* Special fully nested mode */

/* IRQ offset after remapping */
#define PIC1_OFFSET     0x20    /* IRQ 0-7 -> INT 32-39 */
#define PIC2_OFFSET     0x28    /* IRQ 8-15 -> INT 40-47 */

/* Initialize and remap the PICs */
void pic_init(void);

/* Send End of Interrupt signal */
void pic_send_eoi(uint8_t irq);

/* Enable a specific IRQ */
void pic_enable_irq(uint8_t irq);

/* Disable a specific IRQ */
void pic_disable_irq(uint8_t irq);

/* Get the combined ISR (In-Service Register) */
uint16_t pic_get_isr(void);

/* Get the combined IRR (Interrupt Request Register) */
uint16_t pic_get_irr(void);

#endif /* _PIC_H */

