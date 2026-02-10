/*
 * MiniOS Interrupt Descriptor Table (IDT) Header
 * 
 * Defines structures and functions for setting up the IDT.
 * The IDT maps interrupt numbers to handler functions.
 */

#ifndef _IDT_H
#define _IDT_H

#include "types.h"

/* Number of IDT entries (256 possible interrupts) */
#define IDT_ENTRIES 256

/* IDT gate types */
#define IDT_GATE_TASK       0x05    /* Task gate */
#define IDT_GATE_INT16      0x06    /* 16-bit interrupt gate */
#define IDT_GATE_TRAP16     0x07    /* 16-bit trap gate */
#define IDT_GATE_INT32      0x0E    /* 32-bit interrupt gate */
#define IDT_GATE_TRAP32     0x0F    /* 32-bit trap gate */

/* IDT flags */
#define IDT_FLAG_PRESENT    0x80    /* Gate is present */
#define IDT_FLAG_RING0      0x00    /* Ring 0 (kernel) */
#define IDT_FLAG_RING3      0x60    /* Ring 3 (user) */

/* IDT entry structure (8 bytes) */
typedef struct {
    uint16_t base_low;       /* Lower 16 bits of handler address */
    uint16_t selector;       /* Kernel code segment selector */
    uint8_t  zero;           /* Always zero */
    uint8_t  flags;          /* Type and attributes */
    uint16_t base_high;      /* Upper 16 bits of handler address */
} __attribute__((packed)) idt_entry_t;

/* IDT pointer structure (for LIDT instruction) */
typedef struct {
    uint16_t limit;          /* Size of IDT - 1 */
    uint32_t base;           /* Address of IDT */
} __attribute__((packed)) idt_ptr_t;

/* CPU register state pushed by interrupt handler */
typedef struct {
    /* Pushed by our interrupt stub */
    uint32_t ds;             /* Data segment */
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;  /* pusha */
    uint32_t int_no;         /* Interrupt number */
    uint32_t err_code;       /* Error code (or dummy) */
    
    /* Pushed by CPU automatically */
    uint32_t eip;            /* Instruction pointer */
    uint32_t cs;             /* Code segment */
    uint32_t eflags;         /* Flags register */
    uint32_t useresp;        /* User stack pointer (if privilege change) */
    uint32_t ss;             /* User stack segment (if privilege change) */
} __attribute__((packed)) registers_t;

/* Interrupt handler function type */
typedef void (*isr_handler_t)(registers_t*);

/* Initialize the IDT */
void idt_init(void);

/* Set an IDT gate */
void idt_set_gate(uint8_t num, uint32_t base, uint16_t selector, uint8_t flags);

/* Register an interrupt handler */
void register_interrupt_handler(uint8_t n, isr_handler_t handler);

/* External assembly function to load IDT */
extern void idt_flush(uint32_t idt_ptr);

#endif /* _IDT_H */

