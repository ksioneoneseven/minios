/*
 * MiniOS Kernel Panic Implementation
 * 
 * Handles unrecoverable kernel errors by displaying
 * diagnostic information and halting the system.
 */

#include "../include/panic.h"
#include "../include/stdio.h"
#include "../include/vga.h"

/*
 * Halt the CPU permanently
 */
static void halt_forever(void) {
    __asm__ volatile("cli");  /* Disable interrupts */
    while (1) {
        __asm__ volatile("hlt");
    }
}

/*
 * Print panic header
 */
static void print_panic_header(void) {
    vga_set_color(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_RED));
    vga_puts("\n\n");
    vga_puts("================================================================================");
    vga_puts("                            KERNEL PANIC                                        ");
    vga_puts("================================================================================");
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
}

/*
 * Kernel panic - halt the system with an error message
 */
void kernel_panic(const char* message) {
    print_panic_header();
    vga_puts("\n\n");
    vga_puts(message);
    vga_puts("\n\n");
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    vga_puts("System halted. Please reboot.\n");
    halt_forever();
}

/*
 * Kernel panic with format string
 */
void kernel_panicf(const char* format, ...) {
    print_panic_header();
    vga_puts("\n\n");
    
    va_list args;
    va_start(args, format);
    vprintk(format, args);
    va_end(args);
    
    vga_puts("\n\n");
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    vga_puts("System halted. Please reboot.\n");
    halt_forever();
}

/*
 * Kernel panic with register dump
 */
void kernel_panic_regs(const char* message, registers_t* regs) {
    print_panic_header();
    vga_puts("\n\n");
    vga_puts(message);
    vga_puts("\n\n");
    
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    vga_puts("Register Dump:\n");
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    
    printk("  EAX=%08X  EBX=%08X  ECX=%08X  EDX=%08X\n",
           regs->eax, regs->ebx, regs->ecx, regs->edx);
    printk("  ESI=%08X  EDI=%08X  EBP=%08X  ESP=%08X\n",
           regs->esi, regs->edi, regs->ebp, regs->esp);
    printk("  EIP=%08X  CS=%04X  EFLAGS=%08X\n",
           regs->eip, regs->cs, regs->eflags);
    printk("  DS=%04X  INT=%02X  ERR=%08X\n",
           regs->ds, regs->int_no, regs->err_code);
    
    vga_puts("\n");
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    vga_puts("System halted. Please reboot.\n");
    halt_forever();
}

