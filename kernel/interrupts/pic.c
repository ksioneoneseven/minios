/*
 * MiniOS 8259 PIC (Programmable Interrupt Controller) Implementation
 * 
 * The 8259 PIC handles hardware interrupts from devices.
 * By default, IRQs 0-7 are mapped to INT 8-15, which conflicts
 * with CPU exceptions. We remap them to INT 32-47.
 */

#include "../include/pic.h"
#include "../include/io.h"

/*
 * Initialize and remap the PICs
 * 
 * By default:
 *   Master PIC: IRQ 0-7 -> INT 8-15
 *   Slave PIC:  IRQ 8-15 -> INT 70-77
 * 
 * After remapping:
 *   Master PIC: IRQ 0-7 -> INT 32-39
 *   Slave PIC:  IRQ 8-15 -> INT 40-47
 */
void pic_init(void) {
    /* Note: We could save and restore masks, but we initialize fresh */
    
    /* Start initialization sequence (ICW1) */
    outb(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();
    outb(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();
    
    /* Set vector offsets (ICW2) */
    outb(PIC1_DATA, PIC1_OFFSET);   /* Master: IRQ 0-7 -> INT 32-39 */
    io_wait();
    outb(PIC2_DATA, PIC2_OFFSET);   /* Slave: IRQ 8-15 -> INT 40-47 */
    io_wait();
    
    /* Tell Master PIC there's a slave at IRQ2 (ICW3) */
    outb(PIC1_DATA, 0x04);          /* Slave on IRQ2 (bit 2) */
    io_wait();
    outb(PIC2_DATA, 0x02);          /* Slave cascade identity */
    io_wait();
    
    /* Set 8086 mode (ICW4) */
    outb(PIC1_DATA, ICW4_8086);
    io_wait();
    outb(PIC2_DATA, ICW4_8086);
    io_wait();
    
    /* Restore saved masks (or set to all masked initially) */
    outb(PIC1_DATA, 0xFF);          /* Mask all IRQs initially */
    outb(PIC2_DATA, 0xFF);
    
    /* Enable cascade IRQ (IRQ2) so slave PIC works */
    pic_enable_irq(2);
}

/*
 * Send End of Interrupt signal
 * Must be called after handling an IRQ
 */
void pic_send_eoi(uint8_t irq) {
    /* If IRQ came from slave PIC, send EOI to both */
    if (irq >= 8) {
        outb(PIC2_COMMAND, PIC_EOI);
    }
    outb(PIC1_COMMAND, PIC_EOI);
}

/*
 * Enable a specific IRQ (unmask it)
 */
void pic_enable_irq(uint8_t irq) {
    uint16_t port;
    uint8_t value;
    
    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }
    
    value = inb(port) & ~(1 << irq);
    outb(port, value);
}

/*
 * Disable a specific IRQ (mask it)
 */
void pic_disable_irq(uint8_t irq) {
    uint16_t port;
    uint8_t value;
    
    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }
    
    value = inb(port) | (1 << irq);
    outb(port, value);
}

/*
 * Get the combined ISR (In-Service Register)
 * Returns which IRQs are currently being serviced
 */
uint16_t pic_get_isr(void) {
    outb(PIC1_COMMAND, 0x0B);
    outb(PIC2_COMMAND, 0x0B);
    return (inb(PIC2_COMMAND) << 8) | inb(PIC1_COMMAND);
}

/*
 * Get the combined IRR (Interrupt Request Register)
 * Returns which IRQs are pending
 */
uint16_t pic_get_irr(void) {
    outb(PIC1_COMMAND, 0x0A);
    outb(PIC2_COMMAND, 0x0A);
    return (inb(PIC2_COMMAND) << 8) | inb(PIC1_COMMAND);
}

