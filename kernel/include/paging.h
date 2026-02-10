/*
 * MiniOS Paging Header
 * 
 * Virtual memory management using x86 paging.
 * Uses 4KB pages with 2-level page tables.
 */

#ifndef _PAGING_H
#define _PAGING_H

#include "types.h"
#include "pmm.h"  /* For PAGE_SIZE, PAGE_ALIGN_UP/DOWN */

/* Page table constants */
#define PAGES_PER_TABLE     1024
#define TABLES_PER_DIR      1024

/* Page directory/table entry flags */
#define PAGE_PRESENT        0x001   /* Page is present in memory */
#define PAGE_WRITE          0x002   /* Page is writable */
#define PAGE_USER           0x004   /* Page is accessible from user mode */
#define PAGE_WRITETHROUGH   0x008   /* Write-through caching */
#define PAGE_NOCACHE        0x010   /* Disable caching */
#define PAGE_ACCESSED       0x020   /* Page has been accessed */
#define PAGE_DIRTY          0x040   /* Page has been written to */
#define PAGE_SIZE_4MB       0x080   /* 4MB page (only in page directory) */
#define PAGE_GLOBAL         0x100   /* Global page (not flushed on CR3 reload) */

/* Common flag combinations */
#define PAGE_KERNEL         (PAGE_PRESENT | PAGE_WRITE)
#define PAGE_KERNEL_RO      (PAGE_PRESENT)
#define PAGE_USER_RO        (PAGE_PRESENT | PAGE_USER)
#define PAGE_USER_RW        (PAGE_PRESENT | PAGE_WRITE | PAGE_USER)

/* Extract page directory and table indices from virtual address */
#define PAGE_DIR_INDEX(addr)    (((addr) >> 22) & 0x3FF)
#define PAGE_TABLE_INDEX(addr)  (((addr) >> 12) & 0x3FF)

/* Extract physical address from page entry */
#define PAGE_FRAME(entry)       ((entry) & 0xFFFFF000)

/* Page directory entry type */
typedef uint32_t page_dir_entry_t;

/* Page table entry type */
typedef uint32_t page_table_entry_t;

/* Page directory structure */
typedef struct {
    page_dir_entry_t entries[TABLES_PER_DIR];
} __attribute__((aligned(4096))) page_directory_t;

/* Page table structure */
typedef struct {
    page_table_entry_t entries[PAGES_PER_TABLE];
} __attribute__((aligned(4096))) page_table_t;

/* Kernel page directory (defined in paging.c) */
extern page_directory_t* kernel_directory;

/*
 * Initialize paging subsystem
 * Sets up identity mapping for kernel space
 */
void paging_init(void);

/*
 * Map a virtual address to a physical address
 * flags: PAGE_PRESENT, PAGE_WRITE, PAGE_USER, etc.
 */
void paging_map_page(uint32_t virtual_addr, uint32_t physical_addr, uint32_t flags);

/*
 * Unmap a virtual address
 */
void paging_unmap_page(uint32_t virtual_addr);

/*
 * Get the physical address for a virtual address
 * Returns 0 if not mapped
 */
uint32_t paging_get_physical(uint32_t virtual_addr);

/*
 * Check if a virtual address is mapped
 */
bool paging_is_mapped(uint32_t virtual_addr);

/*
 * Flush a single page from TLB
 */
void paging_flush_tlb_entry(uint32_t virtual_addr);

/*
 * Flush entire TLB (reload CR3)
 */
void paging_flush_tlb(void);

/*
 * Get current page directory
 */
page_directory_t* paging_get_directory(void);

/*
 * Switch to a different page directory
 */
void paging_switch_directory(page_directory_t* dir);

/*
 * Create a new page directory (for new process)
 */
page_directory_t* paging_create_directory(void);

/*
 * Clone a page directory (for fork)
 */
page_directory_t* paging_clone_directory(page_directory_t* src);

/*
 * Free a page directory and all its page tables
 */
void paging_free_directory(page_directory_t* dir);

#endif /* _PAGING_H */

