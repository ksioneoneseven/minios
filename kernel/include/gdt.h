/*
 * MiniOS Global Descriptor Table (GDT) Header
 * 
 * Defines structures and functions for setting up the GDT.
 * The GDT defines memory segments for protected mode operation.
 */

#ifndef _GDT_H
#define _GDT_H

#include "types.h"

/* GDT segment selectors (offset into GDT) */
#define GDT_NULL_SEGMENT     0x00
#define GDT_KERNEL_CODE      0x08    /* Kernel code segment */
#define GDT_KERNEL_DATA      0x10    /* Kernel data segment */
#define GDT_USER_CODE        0x18    /* User code segment */
#define GDT_USER_DATA        0x20    /* User data segment */
#define GDT_TSS              0x28    /* Task State Segment */

/* GDT access byte flags */
#define GDT_ACCESS_PRESENT   0x80    /* Segment is present */
#define GDT_ACCESS_RING0     0x00    /* Ring 0 (kernel) */
#define GDT_ACCESS_RING3     0x60    /* Ring 3 (user) */
#define GDT_ACCESS_SYSTEM    0x00    /* System segment */
#define GDT_ACCESS_CODEDATA  0x10    /* Code/data segment */
#define GDT_ACCESS_EXEC      0x08    /* Executable (code) */
#define GDT_ACCESS_RW        0x02    /* Read/write */
#define GDT_ACCESS_ACCESSED  0x01    /* Accessed */

/* GDT flags (granularity byte upper nibble) */
#define GDT_FLAG_GRANULARITY 0x80    /* 4KB granularity */
#define GDT_FLAG_32BIT       0x40    /* 32-bit segment */

/* GDT entry structure (8 bytes) */
typedef struct {
    uint16_t limit_low;      /* Lower 16 bits of limit */
    uint16_t base_low;       /* Lower 16 bits of base */
    uint8_t  base_middle;    /* Next 8 bits of base */
    uint8_t  access;         /* Access flags */
    uint8_t  granularity;    /* Granularity + upper 4 bits of limit */
    uint8_t  base_high;      /* Upper 8 bits of base */
} __attribute__((packed)) gdt_entry_t;

/* GDT pointer structure (for LGDT instruction) */
typedef struct {
    uint16_t limit;          /* Size of GDT - 1 */
    uint32_t base;           /* Address of GDT */
} __attribute__((packed)) gdt_ptr_t;

/* Task State Segment structure */
typedef struct {
    uint32_t prev_tss;       /* Previous TSS (for hardware task switching) */
    uint32_t esp0;           /* Stack pointer for ring 0 */
    uint32_t ss0;            /* Stack segment for ring 0 */
    uint32_t esp1;           /* Stack pointer for ring 1 */
    uint32_t ss1;            /* Stack segment for ring 1 */
    uint32_t esp2;           /* Stack pointer for ring 2 */
    uint32_t ss2;            /* Stack segment for ring 2 */
    uint32_t cr3;            /* Page directory base */
    uint32_t eip;            /* Instruction pointer */
    uint32_t eflags;         /* Flags register */
    uint32_t eax, ecx, edx, ebx;
    uint32_t esp, ebp, esi, edi;
    uint32_t es, cs, ss, ds, fs, gs;
    uint32_t ldt;            /* LDT selector */
    uint16_t trap;           /* Trap on task switch */
    uint16_t iomap_base;     /* I/O map base address */
} __attribute__((packed)) tss_entry_t;

/* Initialize the GDT */
void gdt_init(void);

/* Set the kernel stack in TSS (for syscalls/interrupts) */
void tss_set_kernel_stack(uint32_t stack);

/* External assembly function to load GDT */
extern void gdt_flush(uint32_t gdt_ptr);

/* External assembly function to load TSS */
extern void tss_flush(void);

#endif /* _GDT_H */

