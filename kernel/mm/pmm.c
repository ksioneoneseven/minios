/*
 * MiniOS Physical Memory Manager
 * 
 * Uses a bitmap to track physical page frames.
 * Each bit represents a 4KB page (1 = used, 0 = free).
 */

#include "../include/pmm.h"
#include "../include/string.h"
#include "../include/stdio.h"

/* Bitmap for tracking physical frames */
static uint32_t* pmm_bitmap = NULL;
static uint32_t pmm_bitmap_size = 0;    /* Size in uint32_t entries */
static uint32_t pmm_total_frames = 0;
static uint32_t pmm_used_frames = 0;

/* Memory bounds */
static uint32_t pmm_memory_start = 0;
static uint32_t pmm_memory_end = 0;

/*
 * Set a bit in the bitmap (mark frame as used)
 */
static inline void bitmap_set(uint32_t frame) {
    pmm_bitmap[frame / 32] |= (1 << (frame % 32));
}

/*
 * Clear a bit in the bitmap (mark frame as free)
 */
static inline void bitmap_clear(uint32_t frame) {
    pmm_bitmap[frame / 32] &= ~(1 << (frame % 32));
}

/*
 * Test a bit in the bitmap
 */
static inline bool bitmap_test(uint32_t frame) {
    return (pmm_bitmap[frame / 32] & (1 << (frame % 32))) != 0;
}

/*
 * Find first free frame
 */
static uint32_t bitmap_find_free(void) {
    for (uint32_t i = 0; i < pmm_bitmap_size; i++) {
        if (pmm_bitmap[i] != 0xFFFFFFFF) {
            /* Found a uint32_t with at least one free bit */
            for (uint32_t j = 0; j < 32; j++) {
                uint32_t frame = i * 32 + j;
                if (frame < pmm_total_frames && !bitmap_test(frame)) {
                    return frame;
                }
            }
        }
    }
    return (uint32_t)-1;  /* No free frames */
}

/*
 * Find contiguous free frames
 */
static uint32_t bitmap_find_free_region(uint32_t count) {
    uint32_t start = 0;
    uint32_t found = 0;
    
    for (uint32_t frame = 0; frame < pmm_total_frames; frame++) {
        if (!bitmap_test(frame)) {
            if (found == 0) start = frame;
            found++;
            if (found == count) return start;
        } else {
            found = 0;
        }
    }
    return (uint32_t)-1;  /* Not enough contiguous frames */
}

void pmm_mark_used(uint32_t addr) {
    uint32_t frame = ADDR_TO_FRAME(addr);
    if (frame < pmm_total_frames && !bitmap_test(frame)) {
        bitmap_set(frame);
        pmm_used_frames++;
    }
}

void pmm_mark_region_used(uint32_t start, uint32_t size) {
    uint32_t addr = PAGE_ALIGN_DOWN(start);
    uint32_t end = PAGE_ALIGN_UP(start + size);
    
    while (addr < end) {
        pmm_mark_used(addr);
        addr += PAGE_SIZE;
    }
}

void pmm_mark_region_free(uint32_t start, uint32_t size) {
    uint32_t addr = PAGE_ALIGN_UP(start);
    uint32_t end = PAGE_ALIGN_DOWN(start + size);
    
    while (addr < end) {
        uint32_t frame = ADDR_TO_FRAME(addr);
        if (frame < pmm_total_frames && bitmap_test(frame)) {
            bitmap_clear(frame);
            pmm_used_frames--;
        }
        addr += PAGE_SIZE;
    }
}

uint32_t pmm_alloc_frame(void) {
    uint32_t frame = bitmap_find_free();
    if (frame == (uint32_t)-1) {
        return 0;  /* Out of memory */
    }
    
    bitmap_set(frame);
    pmm_used_frames++;
    return FRAME_TO_ADDR(frame);
}

uint32_t pmm_alloc_frames(uint32_t count) {
    uint32_t start = bitmap_find_free_region(count);
    if (start == (uint32_t)-1) {
        return 0;  /* Not enough contiguous memory */
    }
    
    for (uint32_t i = 0; i < count; i++) {
        bitmap_set(start + i);
        pmm_used_frames++;
    }
    return FRAME_TO_ADDR(start);
}

void pmm_free_frame(uint32_t addr) {
    uint32_t frame = ADDR_TO_FRAME(addr);
    if (frame < pmm_total_frames && bitmap_test(frame)) {
        bitmap_clear(frame);
        pmm_used_frames--;
    }
}

void pmm_free_frames(uint32_t addr, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        pmm_free_frame(addr + i * PAGE_SIZE);
    }
}

bool pmm_is_frame_used(uint32_t addr) {
    uint32_t frame = ADDR_TO_FRAME(addr);
    if (frame >= pmm_total_frames) return true;
    return bitmap_test(frame);
}

void pmm_get_stats(pmm_stats_t* stats) {
    stats->total_frames = pmm_total_frames;
    stats->used_frames = pmm_used_frames;
    stats->free_frames = pmm_total_frames - pmm_used_frames;
    stats->total_memory = pmm_total_frames * PAGE_SIZE;
    stats->free_memory = stats->free_frames * PAGE_SIZE;
}

uint32_t pmm_get_total_pages(void) {
    return pmm_total_frames;
}

uint32_t pmm_get_used_pages(void) {
    return pmm_used_frames;
}

void pmm_print_stats(void) {
    pmm_stats_t stats;
    pmm_get_stats(&stats);

    printk("PMM: Total memory: %u KB (%u frames)\n",
           stats.total_memory / 1024, stats.total_frames);
    printk("PMM: Used memory:  %u KB (%u frames)\n",
           (stats.used_frames * PAGE_SIZE) / 1024, stats.used_frames);
    printk("PMM: Free memory:  %u KB (%u frames)\n",
           stats.free_memory / 1024, stats.free_frames);
}

/*
 * Initialize the physical memory manager
 */
void pmm_init(multiboot_info_t* mboot, uint32_t kernel_end) {
    /* Check if memory map is available */
    if (!(mboot->flags & MULTIBOOT_INFO_MEM_MAP)) {
        printk("PMM: ERROR - No memory map from bootloader!\n");
        return;
    }

    /* Find total memory from multiboot info */
    uint32_t total_memory = (mboot->mem_upper + 1024) * 1024;  /* Convert to bytes */
    pmm_total_frames = total_memory / PAGE_SIZE;
    pmm_memory_end = total_memory;

    /* Calculate bitmap size (1 bit per frame, rounded up to uint32_t) */
    pmm_bitmap_size = (pmm_total_frames + 31) / 32;
    uint32_t bitmap_bytes = pmm_bitmap_size * sizeof(uint32_t);

    /* Place bitmap right after kernel */
    pmm_bitmap = (uint32_t*)PAGE_ALIGN_UP(kernel_end);
    pmm_memory_start = (uint32_t)pmm_bitmap + bitmap_bytes;
    pmm_memory_start = PAGE_ALIGN_UP(pmm_memory_start);

    /* Initially mark ALL frames as used */
    memset(pmm_bitmap, 0xFF, bitmap_bytes);
    pmm_used_frames = pmm_total_frames;

    /* Parse memory map and mark available regions as free */
    multiboot_mmap_entry_t* mmap = (multiboot_mmap_entry_t*)mboot->mmap_addr;
    multiboot_mmap_entry_t* mmap_end = (multiboot_mmap_entry_t*)(mboot->mmap_addr + mboot->mmap_length);

    printk("PMM: Parsing memory map...\n");

    while (mmap < mmap_end) {
        if (mmap->type == MULTIBOOT_MEMORY_AVAILABLE) {
            uint32_t start = (uint32_t)mmap->addr;
            uint32_t size = (uint32_t)mmap->len;

            /* Skip memory below 1MB (reserved for BIOS, VGA, etc.) */
            if (start < 0x100000) {
                if (start + size <= 0x100000) {
                    mmap = (multiboot_mmap_entry_t*)((uint32_t)mmap + mmap->size + sizeof(mmap->size));
                    continue;
                }
                size -= (0x100000 - start);
                start = 0x100000;
            }

            printk("  Available: 0x%08X - 0x%08X (%u KB)\n",
                   start, start + size, size / 1024);

            pmm_mark_region_free(start, size);
        }

        /* Move to next entry */
        mmap = (multiboot_mmap_entry_t*)((uint32_t)mmap + mmap->size + sizeof(mmap->size));
    }

    /* Mark kernel and bitmap as used */
    pmm_mark_region_used(0x100000, kernel_end - 0x100000);  /* Kernel */
    pmm_mark_region_used((uint32_t)pmm_bitmap, bitmap_bytes);  /* Bitmap */

    printk("PMM: Bitmap at 0x%08X (%u bytes)\n", (uint32_t)pmm_bitmap, bitmap_bytes);
    pmm_print_stats();
}

