/*
 * MiniOS Paging Implementation
 * 
 * Sets up x86 paging with identity mapping for kernel space.
 * Uses 4KB pages with 2-level page tables.
 */

#include "../include/paging.h"
#include "../include/pmm.h"
#include "../include/string.h"
#include "../include/stdio.h"
#include "../include/panic.h"

/* Kernel page directory (global so processes can reference it) */
page_directory_t* kernel_directory = NULL;

/* Current active page directory */
static page_directory_t* current_directory = NULL;

/*
 * Load page directory into CR3
 */
static inline void load_page_directory(uint32_t phys_addr) {
    __asm__ volatile("mov %0, %%cr3" : : "r"(phys_addr));
}

/*
 * Enable paging by setting bit 31 of CR0
 */
static inline void enable_paging(void) {
    uint32_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000;  /* Set PG bit */
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0));
}

/*
 * Get current CR3 value
 */
static inline uint32_t get_cr3(void) {
    uint32_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}

void paging_flush_tlb_entry(uint32_t virtual_addr) {
    __asm__ volatile("invlpg (%0)" : : "r"(virtual_addr) : "memory");
}

void paging_flush_tlb(void) {
    uint32_t cr3 = get_cr3();
    __asm__ volatile("mov %0, %%cr3" : : "r"(cr3));
}

page_directory_t* paging_get_directory(void) {
    return current_directory;
}

void paging_switch_directory(page_directory_t* dir) {
    current_directory = dir;
    load_page_directory((uint32_t)dir);
}

/*
 * Get or create a page table for a given virtual address
 */
static page_table_t* get_page_table(page_directory_t* dir, uint32_t virtual_addr, bool create) {
    uint32_t dir_index = PAGE_DIR_INDEX(virtual_addr);
    
    if (dir->entries[dir_index] & PAGE_PRESENT) {
        /* Page table exists */
        return (page_table_t*)PAGE_FRAME(dir->entries[dir_index]);
    }
    
    if (!create) {
        return NULL;
    }
    
    /* Allocate a new page table */
    uint32_t table_phys = pmm_alloc_frame();
    if (table_phys == 0) {
        kernel_panic("paging: out of memory for page table");
        return NULL;
    }
    
    /* Clear the new page table */
    page_table_t* table = (page_table_t*)table_phys;
    memset(table, 0, sizeof(page_table_t));
    
    /* Add to page directory */
    dir->entries[dir_index] = table_phys | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
    
    return table;
}

void paging_map_page(uint32_t virtual_addr, uint32_t physical_addr, uint32_t flags) {
    virtual_addr = PAGE_ALIGN_DOWN(virtual_addr);
    physical_addr = PAGE_ALIGN_DOWN(physical_addr);
    
    page_table_t* table = get_page_table(current_directory, virtual_addr, true);
    if (table == NULL) {
        return;
    }
    
    uint32_t table_index = PAGE_TABLE_INDEX(virtual_addr);
    table->entries[table_index] = physical_addr | (flags & 0xFFF) | PAGE_PRESENT;
    
    paging_flush_tlb_entry(virtual_addr);
}

void paging_unmap_page(uint32_t virtual_addr) {
    virtual_addr = PAGE_ALIGN_DOWN(virtual_addr);
    
    page_table_t* table = get_page_table(current_directory, virtual_addr, false);
    if (table == NULL) {
        return;
    }
    
    uint32_t table_index = PAGE_TABLE_INDEX(virtual_addr);
    table->entries[table_index] = 0;
    
    paging_flush_tlb_entry(virtual_addr);
}

uint32_t paging_get_physical(uint32_t virtual_addr) {
    page_table_t* table = get_page_table(current_directory, virtual_addr, false);
    if (table == NULL) {
        return 0;
    }
    
    uint32_t table_index = PAGE_TABLE_INDEX(virtual_addr);
    if (!(table->entries[table_index] & PAGE_PRESENT)) {
        return 0;
    }
    
    return PAGE_FRAME(table->entries[table_index]) | (virtual_addr & 0xFFF);
}

bool paging_is_mapped(uint32_t virtual_addr) {
    page_table_t* table = get_page_table(current_directory, virtual_addr, false);
    if (table == NULL) {
        return false;
    }

    uint32_t table_index = PAGE_TABLE_INDEX(virtual_addr);
    return (table->entries[table_index] & PAGE_PRESENT) != 0;
}

page_directory_t* paging_create_directory(void) {
    uint32_t phys = pmm_alloc_frame();
    if (phys == 0) {
        return NULL;
    }

    page_directory_t* dir = (page_directory_t*)phys;
    memset(dir, 0, sizeof(page_directory_t));

    /* Copy kernel page tables (upper memory) to new directory */
    /* For now, we identity map, so just copy all entries */
    for (int i = 0; i < TABLES_PER_DIR; i++) {
        dir->entries[i] = kernel_directory->entries[i];
    }

    return dir;
}

page_directory_t* paging_clone_directory(page_directory_t* src) {
    /* For now, just create a copy - COW can be added later */
    page_directory_t* dir = paging_create_directory();
    if (dir == NULL) {
        return NULL;
    }

    /* Copy user-space page tables */
    for (int i = 0; i < 768; i++) {  /* First 3GB is user space */
        if (src->entries[i] & PAGE_PRESENT) {
            /* Allocate new page table */
            uint32_t table_phys = pmm_alloc_frame();
            if (table_phys == 0) {
                /* TODO: cleanup on failure */
                return NULL;
            }

            page_table_t* src_table = (page_table_t*)PAGE_FRAME(src->entries[i]);
            page_table_t* dst_table = (page_table_t*)table_phys;

            /* Copy page table entries */
            memcpy(dst_table, src_table, sizeof(page_table_t));

            dir->entries[i] = table_phys | (src->entries[i] & 0xFFF);
        }
    }

    return dir;
}

void paging_free_directory(page_directory_t* dir) {
    if (dir == NULL || dir == kernel_directory) {
        return;
    }

    /* Free user-space page tables */
    for (int i = 0; i < 768; i++) {
        if (dir->entries[i] & PAGE_PRESENT) {
            pmm_free_frame(PAGE_FRAME(dir->entries[i]));
        }
    }

    /* Free the directory itself */
    pmm_free_frame((uint32_t)dir);
}

/*
 * Initialize paging with identity mapping
 */
void paging_init(void) {
    printk("Paging: Initializing...\n");

    /* Allocate kernel page directory */
    uint32_t dir_phys = pmm_alloc_frame();
    if (dir_phys == 0) {
        kernel_panic("paging: cannot allocate page directory");
        return;
    }

    kernel_directory = (page_directory_t*)dir_phys;
    memset(kernel_directory, 0, sizeof(page_directory_t));
    current_directory = kernel_directory;

    /* Identity map physical memory so page tables allocated later remain accessible */
    uint32_t total_pages = pmm_get_total_pages();
    uint64_t map_limit64 = (total_pages == 0) ? (uint64_t)0x1000000 : ((uint64_t)total_pages * (uint64_t)PAGE_SIZE);

    /* Cap mapping to 512MB to avoid long boot times on large RAM configs */
    if (map_limit64 > 0x20000000ULL) {
        map_limit64 = 0x20000000ULL;
    }

    uint32_t map_limit = (uint32_t)map_limit64;
    if (map_limit < 0x1000000) {
        map_limit = 0x1000000;
    }

    printk("Paging: Identity mapping first %u MB...\n", map_limit / (1024 * 1024));

    for (uint32_t addr = 0; addr < map_limit; addr += PAGE_SIZE) {
        paging_map_page(addr, addr, PAGE_KERNEL);
    }

    /* Load page directory and enable paging */
    printk("Paging: Enabling paging...\n");
    load_page_directory((uint32_t)kernel_directory);
    enable_paging();

    printk("Paging: Enabled! Page directory at 0x%08X\n", (uint32_t)kernel_directory);
}

