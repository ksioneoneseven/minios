/*
 * MiniOS IRQ (Hardware Interrupt) Handler Implementation
 * 
 * Handles hardware interrupts (IRQ 0-15, mapped to INT 32-47).
 * These are triggered by hardware devices like keyboard, timer, etc.
 */

#include "../include/idt.h"
#include "../include/isr.h"
#include "../include/pic.h"
#include "../include/vga.h"

/* IRQ handler table (separate from ISR handlers) */
static isr_handler_t irq_handlers[16];

/*
 * Register an IRQ handler
 * irq: IRQ number (0-15)
 * handler: Handler function
 */
void irq_register_handler(uint8_t irq, isr_handler_t handler) {
    if (irq < 16) {
        irq_handlers[irq] = handler;
    }
}

/*
 * Unregister an IRQ handler
 */
void irq_unregister_handler(uint8_t irq) {
    if (irq < 16) {
        irq_handlers[irq] = NULL;
    }
}

/*
 * Common IRQ handler - called from assembly stub
 */
void irq_handler(registers_t* regs) {
    /* Calculate IRQ number from interrupt number */
    uint8_t irq = regs->int_no - 32;

    /* Check for spurious IRQ 7 */
    if (irq == 7) {
        uint16_t isr = pic_get_isr();
        if ((isr & 0x80) == 0) {
            /* Spurious IRQ, don't send EOI */
            return;
        }
    }

    /* Check for spurious IRQ 15 */
    if (irq == 15) {
        uint16_t isr = pic_get_isr();
        if ((isr & 0x8000) == 0) {
            /* Spurious IRQ, send EOI to master only */
            pic_send_eoi(2);
            return;
        }
    }

    /* Send End of Interrupt signal to PIC BEFORE calling handler.
     * This is critical because the handler might call schedule() which
     * performs a context switch. If we send EOI after the handler,
     * and the handler switches to a different process, the EOI would
     * never be sent and the PIC would stop delivering interrupts! */
    pic_send_eoi(irq);

    /* Call registered handler if present */
    if (irq_handlers[irq] != NULL) {
        irq_handlers[irq](regs);
    }
}

/*
 * Initialize IRQ system
 */
void irq_init(void) {
    /* Clear all handlers */
    for (int i = 0; i < 16; i++) {
        irq_handlers[i] = NULL;
    }
    
    /* Initialize and remap the PIC */
    pic_init();
}

