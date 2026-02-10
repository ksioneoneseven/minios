/*
 * MiniOS Interrupt Service Routines Header
 * 
 * Declares the CPU exception handlers (ISR 0-31) and
 * hardware interrupt handlers (IRQ 0-15).
 */

#ifndef _ISR_H
#define _ISR_H

#include "idt.h"

/* CPU Exception numbers (0-31) */
#define ISR_DIVIDE_ERROR        0   /* Division by zero */
#define ISR_DEBUG               1   /* Debug exception */
#define ISR_NMI                 2   /* Non-maskable interrupt */
#define ISR_BREAKPOINT          3   /* Breakpoint (INT 3) */
#define ISR_OVERFLOW            4   /* Overflow (INTO) */
#define ISR_BOUND_RANGE         5   /* Bound range exceeded */
#define ISR_INVALID_OPCODE      6   /* Invalid opcode */
#define ISR_DEVICE_NOT_AVAIL    7   /* Device not available (FPU) */
#define ISR_DOUBLE_FAULT        8   /* Double fault */
#define ISR_COPROCESSOR_SEG     9   /* Coprocessor segment overrun */
#define ISR_INVALID_TSS         10  /* Invalid TSS */
#define ISR_SEGMENT_NOT_PRESENT 11  /* Segment not present */
#define ISR_STACK_FAULT         12  /* Stack-segment fault */
#define ISR_GENERAL_PROTECTION  13  /* General protection fault */
#define ISR_PAGE_FAULT          14  /* Page fault */
#define ISR_RESERVED_15         15  /* Reserved */
#define ISR_FPU_ERROR           16  /* x87 FPU error */
#define ISR_ALIGNMENT_CHECK     17  /* Alignment check */
#define ISR_MACHINE_CHECK       18  /* Machine check */
#define ISR_SIMD_ERROR          19  /* SIMD floating-point exception */
#define ISR_VIRTUALIZATION      20  /* Virtualization exception */
/* 21-31 are reserved */

/* Hardware IRQ numbers (remapped to 32-47) */
#define IRQ0  32    /* Programmable Interval Timer */
#define IRQ1  33    /* Keyboard */
#define IRQ2  34    /* Cascade (used internally) */
#define IRQ3  35    /* COM2 */
#define IRQ4  36    /* COM1 */
#define IRQ5  37    /* LPT2 */
#define IRQ6  38    /* Floppy Disk */
#define IRQ7  39    /* LPT1 / Spurious */
#define IRQ8  40    /* CMOS RTC */
#define IRQ9  41    /* Free / ACPI */
#define IRQ10 42    /* Free */
#define IRQ11 43    /* Free */
#define IRQ12 44    /* PS/2 Mouse */
#define IRQ13 45    /* FPU / Coprocessor */
#define IRQ14 46    /* Primary ATA */
#define IRQ15 47    /* Secondary ATA */

/* Initialize ISR handlers */
void isr_init(void);

/* Initialize IRQ handlers */
void irq_init(void);

/* Register an ISR handler (for any interrupt 0-255) */
void isr_register_handler(uint8_t num, isr_handler_t handler);

/* Register an IRQ handler (irq: 0-15) */
void irq_register_handler(uint8_t irq, isr_handler_t handler);

/* Unregister an IRQ handler */
void irq_unregister_handler(uint8_t irq);

/* External ISR stubs (defined in assembly) */
extern void isr0(void);
extern void isr1(void);
extern void isr2(void);
extern void isr3(void);
extern void isr4(void);
extern void isr5(void);
extern void isr6(void);
extern void isr7(void);
extern void isr8(void);
extern void isr9(void);
extern void isr10(void);
extern void isr11(void);
extern void isr12(void);
extern void isr13(void);
extern void isr14(void);
extern void isr15(void);
extern void isr16(void);
extern void isr17(void);
extern void isr18(void);
extern void isr19(void);
extern void isr20(void);
extern void isr21(void);
extern void isr22(void);
extern void isr23(void);
extern void isr24(void);
extern void isr25(void);
extern void isr26(void);
extern void isr27(void);
extern void isr28(void);
extern void isr29(void);
extern void isr30(void);
extern void isr31(void);
extern void isr128(void);   /* System call interrupt (0x80) */

/* External IRQ stubs (defined in assembly) */
extern void irq0(void);
extern void irq1(void);
extern void irq2(void);
extern void irq3(void);
extern void irq4(void);
extern void irq5(void);
extern void irq6(void);
extern void irq7(void);
extern void irq8(void);
extern void irq9(void);
extern void irq10(void);
extern void irq11(void);
extern void irq12(void);
extern void irq13(void);
extern void irq14(void);
extern void irq15(void);

#endif /* _ISR_H */

