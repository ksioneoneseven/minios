/*
 * MiniOS Kernel Heap Implementation
 * 
 * Simple first-fit free list allocator.
 * Blocks are split when possible and merged on free.
 */

#include "../include/heap.h"
#include "../include/string.h"
#include "../include/stdio.h"
#include "../include/panic.h"

/* Heap state */
static uint32_t heap_start = 0;
static uint32_t heap_end = 0;
static uint32_t heap_size = 0;
static heap_block_t* free_list = NULL;

/* Statistics */
static uint32_t total_allocated = 0;
static uint32_t block_count = 0;

/*
 * Get block header from user pointer
 */
static inline heap_block_t* get_block(void* ptr) {
    return (heap_block_t*)((uint8_t*)ptr - sizeof(heap_block_t));
}

/*
 * Get user pointer from block header
 */
static inline void* get_ptr(heap_block_t* block) {
    return (void*)((uint8_t*)block + sizeof(heap_block_t));
}

/*
 * Validate a block's magic number
 */
static inline bool validate_block(heap_block_t* block) {
    return block->magic == HEAP_MAGIC;
}

/*
 * Split a block if it's large enough
 */
static void split_block(heap_block_t* block, size_t size) {
    size_t total_needed = size + sizeof(heap_block_t);
    size_t remaining = block->size - total_needed;
    
    /* Only split if remaining space is useful */
    if (remaining >= HEAP_MIN_BLOCK + sizeof(heap_block_t)) {
        heap_block_t* new_block = (heap_block_t*)((uint8_t*)block + total_needed);
        new_block->size = remaining;
        new_block->magic = HEAP_MAGIC;
        new_block->is_free = true;
        new_block->next = block->next;
        
        block->size = total_needed;
        block->next = new_block;
        
        block_count++;
    }
}

/*
 * Merge adjacent free blocks
 */
static void merge_blocks(void) {
    heap_block_t* block = (heap_block_t*)heap_start;
    
    while ((uint32_t)block < heap_end) {
        if (block->is_free) {
            heap_block_t* next = (heap_block_t*)((uint8_t*)block + block->size);
            
            while ((uint32_t)next < heap_end && next->is_free) {
                block->size += next->size;
                block->next = next->next;
                block_count--;
                next = (heap_block_t*)((uint8_t*)block + block->size);
            }
        }
        block = (heap_block_t*)((uint8_t*)block + block->size);
    }
}

void heap_init(uint32_t start, uint32_t size) {
    heap_start = start;
    heap_size = size;
    heap_end = start + size;
    
    /* Create initial free block spanning entire heap */
    free_list = (heap_block_t*)start;
    free_list->size = size;
    free_list->magic = HEAP_MAGIC;
    free_list->is_free = true;
    free_list->next = NULL;
    
    block_count = 1;
    total_allocated = 0;
    
    printk("Heap: Initialized at 0x%08X, size %u KB\n", start, size / 1024);
}

void* kmalloc(size_t size) {
    if (size == 0) return NULL;
    
    /* Align size to 4 bytes */
    size = (size + 3) & ~3;
    
    /* Find first fit */
    heap_block_t* block = (heap_block_t*)heap_start;
    
    while ((uint32_t)block < heap_end) {
        if (!validate_block(block)) {
            kernel_panic("heap: corrupted block detected");
            return NULL;
        }
        
        if (block->is_free && block->size >= size + sizeof(heap_block_t)) {
            /* Found a suitable block */
            split_block(block, size);
            block->is_free = false;
            total_allocated += block->size;
            return get_ptr(block);
        }
        
        block = (heap_block_t*)((uint8_t*)block + block->size);
    }
    
    /* No suitable block found */
    printk("heap: out of memory (requested %u bytes)\n", size);
    return NULL;
}

void* kmalloc_aligned(size_t size, size_t alignment) {
    /* Simple implementation: allocate extra and align */
    void* ptr = kmalloc(size + alignment);
    if (ptr == NULL) return NULL;
    
    uint32_t addr = (uint32_t)ptr;
    uint32_t aligned = (addr + alignment - 1) & ~(alignment - 1);
    
    /* For simplicity, we waste the unaligned portion */
    /* A more sophisticated implementation would track this */
    return (void*)aligned;
}

void* kzalloc(size_t size) {
    void* ptr = kmalloc(size);
    if (ptr) memset(ptr, 0, size);
    return ptr;
}

void* kcalloc(size_t count, size_t size) {
    return kzalloc(count * size);
}

void* krealloc(void* ptr, size_t new_size) {
    if (ptr == NULL) {
        return kmalloc(new_size);
    }

    if (new_size == 0) {
        kfree(ptr);
        return NULL;
    }

    heap_block_t* block = get_block(ptr);
    if (!validate_block(block)) {
        kernel_panic("heap: krealloc on corrupted block");
        return NULL;
    }

    size_t old_size = block->size - sizeof(heap_block_t);

    /* If new size fits in current block, just return */
    if (new_size <= old_size) {
        return ptr;
    }

    /* Allocate new block and copy */
    void* new_ptr = kmalloc(new_size);
    if (new_ptr == NULL) {
        return NULL;
    }

    memcpy(new_ptr, ptr, old_size);
    kfree(ptr);

    return new_ptr;
}

void kfree(void* ptr) {
    if (ptr == NULL) return;

    heap_block_t* block = get_block(ptr);

    if (!validate_block(block)) {
        kernel_panic("heap: kfree on corrupted block");
        return;
    }

    if (block->is_free) {
        kernel_panic("heap: double free detected");
        return;
    }

    block->is_free = true;
    total_allocated -= block->size;

    /* Merge adjacent free blocks */
    merge_blocks();
}

void heap_get_stats(heap_stats_t* stats) {
    stats->total_size = heap_size;
    stats->used_size = total_allocated;
    stats->free_size = heap_size - total_allocated;
    stats->block_count = block_count;

    /* Count free blocks */
    uint32_t free_count = 0;
    heap_block_t* block = (heap_block_t*)heap_start;
    while ((uint32_t)block < heap_end) {
        if (block->is_free) free_count++;
        block = (heap_block_t*)((uint8_t*)block + block->size);
    }
    stats->free_block_count = free_count;
}

void heap_print_stats(void) {
    heap_stats_t stats;
    heap_get_stats(&stats);

    printk("Heap: Total: %u KB, Used: %u KB, Free: %u KB\n",
           stats.total_size / 1024,
           stats.used_size / 1024,
           stats.free_size / 1024);
    printk("Heap: Blocks: %u total, %u free\n",
           stats.block_count, stats.free_block_count);
}

bool heap_validate(void) {
    heap_block_t* block = (heap_block_t*)heap_start;

    while ((uint32_t)block < heap_end) {
        if (!validate_block(block)) {
            printk("Heap: Invalid block at 0x%08X\n", (uint32_t)block);
            return false;
        }

        if (block->size == 0) {
            printk("Heap: Zero-size block at 0x%08X\n", (uint32_t)block);
            return false;
        }

        block = (heap_block_t*)((uint8_t*)block + block->size);
    }

    return true;
}

/* Getters for heap info */
uint32_t heap_get_start(void) { return heap_start; }
uint32_t heap_get_end(void) { return heap_end; }

uint32_t heap_get_used(void) {
    uint32_t used = 0;
    heap_block_t* block = (heap_block_t*)heap_start;
    while ((uint32_t)block < heap_end) {
        if (!block->is_free) {
            used += block->size;
        }
        block = (heap_block_t*)((uint8_t*)block + block->size);
    }
    return used;
}

