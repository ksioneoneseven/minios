/*
 * MiniOS Kernel Heap Header
 * 
 * Provides dynamic memory allocation for the kernel.
 * Uses a simple first-fit free list allocator.
 */

#ifndef _HEAP_H
#define _HEAP_H

#include "types.h"

/* Heap configuration */
#define HEAP_INITIAL_SIZE   (128 * 1024 * 1024) /* 128MB initial heap */
#define HEAP_MAX_SIZE       (512 * 1024 * 1024) /* 512MB max heap */
#define HEAP_MIN_BLOCK      32              /* Minimum block size */

/* Heap block header */
typedef struct heap_block {
    uint32_t size;              /* Size of block (including header) */
    uint32_t magic;             /* Magic number for validation */
    struct heap_block* next;    /* Next free block (only valid if free) */
    bool is_free;               /* Is this block free? */
} __attribute__((packed)) heap_block_t;

/* Magic number for heap validation */
#define HEAP_MAGIC  0xDEADBEEF

/*
 * Initialize the kernel heap
 * start: starting address of heap
 * size: initial size in bytes
 */
void heap_init(uint32_t start, uint32_t size);

/*
 * Allocate memory from the kernel heap
 * Returns pointer to allocated memory or NULL on failure
 */
void* kmalloc(size_t size);

/*
 * Allocate aligned memory from the kernel heap
 * alignment must be a power of 2
 */
void* kmalloc_aligned(size_t size, size_t alignment);

/*
 * Allocate zeroed memory from the kernel heap
 */
void* kzalloc(size_t size);

/*
 * Allocate memory for an array
 */
void* kcalloc(size_t count, size_t size);

/*
 * Reallocate memory block
 */
void* krealloc(void* ptr, size_t new_size);

/*
 * Free memory allocated by kmalloc
 */
void kfree(void* ptr);

/*
 * Get heap statistics
 */
typedef struct {
    uint32_t total_size;        /* Total heap size */
    uint32_t used_size;         /* Used memory */
    uint32_t free_size;         /* Free memory */
    uint32_t block_count;       /* Total number of blocks */
    uint32_t free_block_count;  /* Number of free blocks */
} heap_stats_t;

void heap_get_stats(heap_stats_t* stats);

/*
 * Print heap statistics (for debugging)
 */
void heap_print_stats(void);

/*
 * Validate heap integrity (for debugging)
 */
bool heap_validate(void);

#endif /* _HEAP_H */

