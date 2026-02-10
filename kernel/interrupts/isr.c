/*
 * MiniOS Interrupt Service Routines (ISR) Implementation
 * 
 * Handles CPU exceptions (interrupts 0-31).
 * These are triggered by the CPU when errors occur.
 */

#include "../include/idt.h"
#include "../include/isr.h"
#include "../include/vga.h"
#include "../include/string.h"
#include "../include/serial.h"

/* Interrupt handler table */
static isr_handler_t interrupt_handlers[IDT_ENTRIES];

/* Exception names for debugging */
static const char* exception_names[] = {
    "Division By Zero",
    "Debug",
    "Non Maskable Interrupt",
    "Breakpoint",
    "Overflow",
    "Bound Range Exceeded",
    "Invalid Opcode",
    "Device Not Available",
    "Double Fault",
    "Coprocessor Segment Overrun",
    "Invalid TSS",
    "Segment Not Present",
    "Stack-Segment Fault",
    "General Protection Fault",
    "Page Fault",
    "Reserved",
    "x87 FPU Error",
    "Alignment Check",
    "Machine Check",
    "SIMD Floating-Point Exception",
    "Virtualization Exception",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved"
};

/*
 * Print a hexadecimal number
 */
static void isr_print_hex(uint32_t num) {
    char hex_chars[] = "0123456789ABCDEF";
    char buffer[11] = "0x00000000";
    
    for (int i = 9; i >= 2; i--) {
        buffer[i] = hex_chars[num & 0xF];
        num >>= 4;
    }
    
    vga_puts(buffer);
}

/*
 * Register an interrupt handler
 */
void register_interrupt_handler(uint8_t n, isr_handler_t handler) {
    interrupt_handlers[n] = handler;
}

/*
 * Register an ISR handler (alias for register_interrupt_handler)
 */
void isr_register_handler(uint8_t num, isr_handler_t handler) {
    interrupt_handlers[num] = handler;
}

/*
 * Initialize ISR system
 */
void isr_init(void) {
    /* Clear all handlers */
    memset(interrupt_handlers, 0, sizeof(interrupt_handlers));
}

/*
 * Common ISR handler - called from assembly stub
 */
void isr_handler(registers_t* regs) {
    /* Check if we have a registered handler */
    if (interrupt_handlers[regs->int_no] != NULL) {
        interrupt_handlers[regs->int_no](regs);
        return;
    }
    
    /* Default handler: print exception info to BOTH serial and VGA */
    serial_write_string("\n=== KERNEL PANIC ===");
    serial_write_string("\nException: ");
    if (regs->int_no < 32) {
        serial_write_string(exception_names[regs->int_no]);
    } else {
        serial_write_string("Unknown");
    }
    serial_write_string(" int=");
    serial_write_hex(regs->int_no);
    serial_write_string(" err=");
    serial_write_hex(regs->err_code);
    serial_write_string("\n  EAX="); serial_write_hex(regs->eax);
    serial_write_string(" EBX="); serial_write_hex(regs->ebx);
    serial_write_string(" ECX="); serial_write_hex(regs->ecx);
    serial_write_string(" EDX="); serial_write_hex(regs->edx);
    serial_write_string("\n  ESI="); serial_write_hex(regs->esi);
    serial_write_string(" EDI="); serial_write_hex(regs->edi);
    serial_write_string(" EBP="); serial_write_hex(regs->ebp);
    serial_write_string(" ESP="); serial_write_hex(regs->esp);
    serial_write_string("\n  EIP="); serial_write_hex(regs->eip);
    serial_write_string(" CS="); serial_write_hex(regs->cs);
    serial_write_string(" EFLAGS="); serial_write_hex(regs->eflags);
    serial_write_string("\nSystem halted.\n");

    vga_write("\n========== KERNEL PANIC ==========\n",
              vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_RED));
    
    vga_write("Exception: ", vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
    if (regs->int_no < 32) {
        vga_puts(exception_names[regs->int_no]);
    } else {
        vga_puts("Unknown");
    }
    vga_puts("\n");
    
    vga_puts("Interrupt: ");
    isr_print_hex(regs->int_no);
    vga_puts("  Error Code: ");
    isr_print_hex(regs->err_code);
    vga_puts("\n\n");
    
    /* Print registers */
    vga_write("Registers:\n", vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    
    vga_puts("  EAX="); isr_print_hex(regs->eax);
    vga_puts("  EBX="); isr_print_hex(regs->ebx);
    vga_puts("  ECX="); isr_print_hex(regs->ecx);
    vga_puts("\n");
    
    vga_puts("  EDX="); isr_print_hex(regs->edx);
    vga_puts("  ESI="); isr_print_hex(regs->esi);
    vga_puts("  EDI="); isr_print_hex(regs->edi);
    vga_puts("\n");
    
    vga_puts("  EBP="); isr_print_hex(regs->ebp);
    vga_puts("  ESP="); isr_print_hex(regs->esp);
    vga_puts("\n");
    
    vga_puts("  EIP="); isr_print_hex(regs->eip);
    vga_puts("  CS=");  isr_print_hex(regs->cs);
    vga_puts("  EFLAGS="); isr_print_hex(regs->eflags);
    vga_puts("\n");
    
    vga_write("\nSystem halted.\n", vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
    
    /* Halt the CPU */
    __asm__ volatile("cli");
    while (1) {
        __asm__ volatile("hlt");
    }
}

