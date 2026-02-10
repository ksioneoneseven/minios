/*
 * MiniOS Global Descriptor Table (GDT) Implementation
 * 
 * Sets up the GDT with 5 entries:
 * 0: Null descriptor
 * 1: Kernel code segment (0x08)
 * 2: Kernel data segment (0x10)
 * 3: User code segment (0x18)
 * 4: User data segment (0x20)
 * 5: TSS (0x28)
 */

#include "../include/gdt.h"
#include "../include/string.h"

/* GDT entries */
static gdt_entry_t gdt_entries[6];
static gdt_ptr_t gdt_ptr;

/* Task State Segment */
static tss_entry_t tss_entry;

/*
 * Set a GDT entry
 */
static void gdt_set_gate(int32_t num, uint32_t base, uint32_t limit, 
                         uint8_t access, uint8_t gran) {
    gdt_entries[num].base_low    = (base & 0xFFFF);
    gdt_entries[num].base_middle = (base >> 16) & 0xFF;
    gdt_entries[num].base_high   = (base >> 24) & 0xFF;
    
    gdt_entries[num].limit_low   = (limit & 0xFFFF);
    gdt_entries[num].granularity = ((limit >> 16) & 0x0F) | (gran & 0xF0);
    
    gdt_entries[num].access      = access;
}

/*
 * Write the TSS entry to the GDT
 */
static void write_tss(int32_t num, uint16_t ss0, uint32_t esp0) {
    uint32_t base = (uint32_t)&tss_entry;
    uint32_t limit = sizeof(tss_entry);
    
    /* Add TSS descriptor to GDT */
    gdt_set_gate(num, base, limit, 0xE9, 0x00);
    
    /* Clear TSS */
    memset(&tss_entry, 0, sizeof(tss_entry));
    
    /* Set kernel stack segment and pointer */
    tss_entry.ss0 = ss0;
    tss_entry.esp0 = esp0;
    
    /* Set segment selectors for kernel */
    tss_entry.cs = 0x08 | 0x03;  /* Kernel code with RPL 3 */
    tss_entry.ss = tss_entry.ds = tss_entry.es = tss_entry.fs = tss_entry.gs = 0x10 | 0x03;
}

/*
 * Initialize the GDT
 */
void gdt_init(void) {
    gdt_ptr.limit = (sizeof(gdt_entry_t) * 6) - 1;
    gdt_ptr.base = (uint32_t)&gdt_entries;
    
    /* Null descriptor */
    gdt_set_gate(0, 0, 0, 0, 0);
    
    /* Kernel code segment: base=0, limit=4GB, ring 0, executable */
    gdt_set_gate(1, 0, 0xFFFFFFFF,
                 GDT_ACCESS_PRESENT | GDT_ACCESS_RING0 | GDT_ACCESS_CODEDATA | 
                 GDT_ACCESS_EXEC | GDT_ACCESS_RW,
                 GDT_FLAG_GRANULARITY | GDT_FLAG_32BIT);
    
    /* Kernel data segment: base=0, limit=4GB, ring 0, read/write */
    gdt_set_gate(2, 0, 0xFFFFFFFF,
                 GDT_ACCESS_PRESENT | GDT_ACCESS_RING0 | GDT_ACCESS_CODEDATA | 
                 GDT_ACCESS_RW,
                 GDT_FLAG_GRANULARITY | GDT_FLAG_32BIT);
    
    /* User code segment: base=0, limit=4GB, ring 3, executable */
    gdt_set_gate(3, 0, 0xFFFFFFFF,
                 GDT_ACCESS_PRESENT | GDT_ACCESS_RING3 | GDT_ACCESS_CODEDATA | 
                 GDT_ACCESS_EXEC | GDT_ACCESS_RW,
                 GDT_FLAG_GRANULARITY | GDT_FLAG_32BIT);
    
    /* User data segment: base=0, limit=4GB, ring 3, read/write */
    gdt_set_gate(4, 0, 0xFFFFFFFF,
                 GDT_ACCESS_PRESENT | GDT_ACCESS_RING3 | GDT_ACCESS_CODEDATA | 
                 GDT_ACCESS_RW,
                 GDT_FLAG_GRANULARITY | GDT_FLAG_32BIT);
    
    /* TSS - will be updated with proper kernel stack later */
    write_tss(5, 0x10, 0x0);
    
    /* Load GDT and TSS */
    gdt_flush((uint32_t)&gdt_ptr);
    tss_flush();
}

/*
 * Set the kernel stack in TSS (called when switching to user mode)
 */
void tss_set_kernel_stack(uint32_t stack) {
    tss_entry.esp0 = stack;
}

