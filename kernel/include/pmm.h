/*
 * MiniOS Physical Memory Manager Header
 * 
 * Manages physical memory using a bitmap allocator.
 * Each bit represents a 4KB page frame.
 */

#ifndef _PMM_H
#define _PMM_H

#include "types.h"
#include "multiboot.h"

/* Page size (4KB) */
#define PAGE_SIZE           4096
#define PAGE_SHIFT          12

/* Align address up to page boundary */
#define PAGE_ALIGN_UP(addr)   (((addr) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))

/* Align address down to page boundary */
#define PAGE_ALIGN_DOWN(addr) ((addr) & ~(PAGE_SIZE - 1))

/* Convert between addresses and frame numbers */
#define ADDR_TO_FRAME(addr)   ((addr) >> PAGE_SHIFT)
#define FRAME_TO_ADDR(frame)  ((frame) << PAGE_SHIFT)

/* Memory statistics */
typedef struct {
    uint32_t total_frames;      /* Total physical frames */
    uint32_t used_frames;       /* Currently allocated frames */
    uint32_t free_frames;       /* Available frames */
    uint32_t total_memory;      /* Total memory in bytes */
    uint32_t free_memory;       /* Free memory in bytes */
} pmm_stats_t;

/*
 * Initialize the physical memory manager
 * Parses multiboot memory map and sets up bitmap
 */
void pmm_init(multiboot_info_t* mboot, uint32_t kernel_end);

/*
 * Allocate a single physical page frame
 * Returns physical address or 0 on failure
 */
uint32_t pmm_alloc_frame(void);

/*
 * Allocate multiple contiguous physical page frames
 * Returns physical address of first frame or 0 on failure
 */
uint32_t pmm_alloc_frames(uint32_t count);

/*
 * Free a single physical page frame
 */
void pmm_free_frame(uint32_t addr);

/*
 * Free multiple contiguous physical page frames
 */
void pmm_free_frames(uint32_t addr, uint32_t count);

/*
 * Mark a frame as used (for reserved memory regions)
 */
void pmm_mark_used(uint32_t addr);

/*
 * Mark a range of frames as used
 */
void pmm_mark_region_used(uint32_t start, uint32_t size);

/*
 * Mark a range of frames as free
 */
void pmm_mark_region_free(uint32_t start, uint32_t size);

/*
 * Check if a frame is allocated
 */
bool pmm_is_frame_used(uint32_t addr);

/*
 * Get memory statistics
 */
void pmm_get_stats(pmm_stats_t* stats);

/*
 * Get total number of page frames
 */
uint32_t pmm_get_total_pages(void);

/*
 * Get number of used page frames
 */
uint32_t pmm_get_used_pages(void);

/*
 * Print memory map (for debugging)
 */
void pmm_print_stats(void);

#endif /* _PMM_H */

