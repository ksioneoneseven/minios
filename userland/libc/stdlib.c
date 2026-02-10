/*
 * MiniOS User-space Standard Library Implementation
 */

#include "include/stdlib.h"
#include "include/unistd.h"
#include "include/string.h"

/* Simple heap allocator using sbrk */

/* Allocation block header */
typedef struct block_header {
    size_t size;
    int free;
    struct block_header* next;
} block_header_t;

#define BLOCK_SIZE sizeof(block_header_t)
#define ALIGN4(x) (((x) + 3) & ~3)

static block_header_t* heap_start = NULL;

/* Find a free block or create a new one */
static block_header_t* find_block(size_t size) {
    block_header_t* current = heap_start;
    
    /* First fit algorithm */
    while (current) {
        if (current->free && current->size >= size) {
            return current;
        }
        current = current->next;
    }
    
    return NULL;
}

/* Request more memory from kernel */
static block_header_t* request_space(size_t size) {
    block_header_t* block = sbrk(BLOCK_SIZE + size);
    if (block == (void*)-1) {
        return NULL;  /* sbrk failed */
    }
    
    block->size = size;
    block->free = 0;
    block->next = NULL;
    
    if (heap_start == NULL) {
        heap_start = block;
    } else {
        /* Find last block and link */
        block_header_t* last = heap_start;
        while (last->next) {
            last = last->next;
        }
        last->next = block;
    }
    
    return block;
}

void* malloc(size_t size) {
    if (size == 0) return NULL;
    
    size = ALIGN4(size);
    
    block_header_t* block = find_block(size);
    
    if (block) {
        block->free = 0;
    } else {
        block = request_space(size);
        if (block == NULL) {
            return NULL;
        }
    }
    
    return (void*)(block + 1);
}

void free(void* ptr) {
    if (ptr == NULL) return;
    
    block_header_t* block = (block_header_t*)ptr - 1;
    block->free = 1;
    
    /* TODO: Coalesce adjacent free blocks */
}

void* calloc(size_t nmemb, size_t size) {
    size_t total = nmemb * size;
    void* ptr = malloc(total);
    if (ptr) {
        memset(ptr, 0, total);
    }
    return ptr;
}

void* realloc(void* ptr, size_t size) {
    if (ptr == NULL) {
        return malloc(size);
    }
    if (size == 0) {
        free(ptr);
        return NULL;
    }
    
    block_header_t* block = (block_header_t*)ptr - 1;
    
    if (block->size >= size) {
        return ptr;  /* Already big enough */
    }
    
    void* new_ptr = malloc(size);
    if (new_ptr) {
        memcpy(new_ptr, ptr, block->size);
        free(ptr);
    }
    return new_ptr;
}

int atoi(const char* str) {
    int result = 0;
    int sign = 1;
    
    /* Skip whitespace */
    while (*str == ' ' || *str == '\t') str++;
    
    /* Handle sign */
    if (*str == '-') {
        sign = -1;
        str++;
    } else if (*str == '+') {
        str++;
    }
    
    /* Convert digits */
    while (*str >= '0' && *str <= '9') {
        result = result * 10 + (*str - '0');
        str++;
    }
    
    return sign * result;
}

void exit(int status) {
    syscall1(SYS_EXIT, status);
    /* Should never return, but just in case */
    while (1) {
        __asm__ volatile("hlt");
    }
}

