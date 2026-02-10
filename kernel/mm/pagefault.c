/*
 * MiniOS Page Fault Handler
 * 
 * Handles page faults with intelligent recovery:
 * - User mode faults: kill the offending process
 * - Kernel mode faults: kernel panic (unavoidable)
 */

#include "../include/idt.h"
#include "../include/isr.h"
#include "../include/paging.h"
#include "../include/process.h"
#include "../include/stdio.h"
#include "../include/vga.h"
#include "../include/serial.h"

/* Page fault error code bits */
#define PF_PRESENT      0x01    /* Page was present (protection violation) */
#define PF_WRITE        0x02    /* Write access caused the fault */
#define PF_USER         0x04    /* Fault occurred in user mode */
#define PF_RESERVED     0x08    /* Reserved bit set in page entry */
#define PF_IFETCH       0x10    /* Instruction fetch caused the fault */

/*
 * Get the faulting address from CR2
 */
static inline uint32_t get_cr2(void) {
    uint32_t cr2;
    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
    return cr2;
}

/*
 * Page fault handler
 * Called by ISR 14
 */
static void page_fault_handler(registers_t* regs) {
    uint32_t fault_addr = get_cr2();
    uint32_t err = regs->err_code;
    
    /* Decode fault type */
    bool present = (err & PF_PRESENT) != 0;
    bool write = (err & PF_WRITE) != 0;
    bool user = (err & PF_USER) != 0;
    bool reserved = (err & PF_RESERVED) != 0;
    bool ifetch = (err & PF_IFETCH) != 0;
    
    /* Check if fault occurred in user mode */
    if (user) {
        /* User mode page fault - kill the process, preserve the system */
        process_t* proc = process_current();
        
        printk("\n");
        printk("SEGMENTATION FAULT in process '%s' (PID %u)\n",
               proc ? proc->name : "unknown",
               proc ? proc->pid : 0);
        printk("  Fault address: 0x%08X\n", fault_addr);
        printk("  Fault type: %s%s%s%s\n",
               present ? "protection-violation " : "not-present ",
               write ? "write " : "read ",
               reserved ? "reserved-bit " : "",
               ifetch ? "instruction-fetch" : "");
        printk("  EIP: 0x%08X\n", regs->eip);
        
        if (proc) {
            /* Terminate the process with signal 11 (SIGSEGV) */
            process_terminate(proc, 128 + 11);  /* Exit code = 128 + signal */
        }
        
        /* Schedule next process */
        extern void schedule(void);
        schedule();
        
        /* Should not return here */
        return;
    }
    
    /* Kernel mode page fault - this is fatal */
    serial_write_string("KERNEL PAGE FAULT: cr2=");
    serial_write_hex(fault_addr);
    serial_write_string(" err=");
    serial_write_hex(err);
    serial_write_string(" eip=");
    serial_write_hex(regs->eip);
    serial_write_string("\n");

    vga_set_color(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_RED));
    vga_puts("\n========== KERNEL PAGE FAULT ==========\n");
    
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
    printk("Page fault at address 0x%08X\n", fault_addr);
    printk("Error code: 0x%08X\n", err);
    printk("Fault type: %s %s%s%s\n",
           present ? "protection-violation" : "not-present",
           write ? "write" : "read",
           reserved ? " reserved-bit" : "",
           ifetch ? " instruction-fetch" : "");
    
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    vga_puts("\nRegisters:\n");
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    
    printk("  EAX=%08X  EBX=%08X  ECX=%08X  EDX=%08X\n",
           regs->eax, regs->ebx, regs->ecx, regs->edx);
    printk("  ESI=%08X  EDI=%08X  EBP=%08X  ESP=%08X\n",
           regs->esi, regs->edi, regs->ebp, regs->esp);
    printk("  EIP=%08X  CS=%04X  EFLAGS=%08X\n",
           regs->eip, regs->cs, regs->eflags);
    
    process_t* proc = process_current();
    if (proc) {
        printk("  Current process: '%s' (PID %d)\n", proc->name, proc->pid);
    }
    
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
    vga_puts("\nKernel halted. Please reboot.\n");
    
    /* Halt the CPU */
    __asm__ volatile("cli");
    while (1) {
        __asm__ volatile("hlt");
    }
}

/*
 * Initialize page fault handler
 * Must be called after ISR initialization
 */
void pagefault_init(void) {
    /* Register handler for interrupt 14 (page fault) */
    isr_register_handler(14, page_fault_handler);
    printk("PageFault: Handler registered\n");
}

