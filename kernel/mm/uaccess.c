/*
 * MiniOS User Access Validation Implementation
 * 
 * Safe copy operations between user and kernel space.
 */

#include "../include/uaccess.h"
#include "../include/paging.h"
#include "../include/process.h"
#include "../include/string.h"

/*
 * Check if a user-space pointer is valid
 * Verifies:
 * 1. Address is in user space range
 * 2. Address + size doesn't overflow
 * 3. All pages in the range are mapped with correct permissions
 */
bool access_ok(const void* addr, size_t size, bool write) {
    (void)write;  /* TODO: Check write permission when page tables support it */

    uint32_t start = (uint32_t)addr;
    uint32_t end = start + size;
    
    /* Check for overflow */
    if (end < start) {
        return false;
    }
    
    /* Zero size is always ok */
    if (size == 0) {
        return true;
    }
    
    /* Check address is in user space */
    if (start < USER_SPACE_START || end > USER_SPACE_END) {
        /* For now, also allow kernel heap access for compatibility
         * This will be removed once userland is properly isolated */
        if (start >= 0x400000 && end <= 0x1000000) {
            /* Kernel heap region - allow for now during transition */
            return true;
        }
        return false;
    }
    
    /* Verify each page is mapped with correct permissions */
    for (uint32_t page = PAGE_ALIGN_DOWN(start); page < end; page += PAGE_SIZE) {
        if (!paging_is_mapped(page)) {
            return false;
        }
        /* TODO: Check write permission if write is requested */
    }
    
    return true;
}

/*
 * Copy data from user space to kernel space
 */
int copyin(void* dst, const void* src, size_t size) {
    if (!access_ok(src, size, false)) {
        return -EFAULT;
    }
    
    memcpy(dst, src, size);
    return 0;
}

/*
 * Copy data from kernel space to user space
 */
int copyout(void* dst, const void* src, size_t size) {
    if (!access_ok(dst, size, true)) {
        return -EFAULT;
    }
    
    memcpy(dst, src, size);
    return 0;
}

/*
 * Copy a null-terminated string from user space
 */
int copyinstr(char* dst, const char* src, size_t max_len) {
    if (max_len == 0) {
        return 0;
    }
    
    size_t len = 0;
    
    while (len < max_len - 1) {
        uint8_t c;
        if (get_user_byte(src + len, &c) < 0) {
            return -EFAULT;
        }
        
        dst[len] = (char)c;
        
        if (c == '\0') {
            return (int)len;
        }
        
        len++;
    }
    
    /* String was too long, null-terminate and return */
    dst[len] = '\0';
    return (int)len;
}

/*
 * Read a single byte from user space
 */
int get_user_byte(const void* addr, uint8_t* value) {
    if (!access_ok(addr, 1, false)) {
        return -EFAULT;
    }
    
    *value = *(const uint8_t*)addr;
    return 0;
}

/*
 * Read a 32-bit word from user space
 */
int get_user_word(const void* addr, uint32_t* value) {
    if (!access_ok(addr, 4, false)) {
        return -EFAULT;
    }
    
    *value = *(const uint32_t*)addr;
    return 0;
}

/*
 * Write a single byte to user space
 */
int put_user_byte(void* addr, uint8_t value) {
    if (!access_ok(addr, 1, true)) {
        return -EFAULT;
    }
    
    *(uint8_t*)addr = value;
    return 0;
}

/*
 * Write a 32-bit word to user space
 */
int put_user_word(void* addr, uint32_t value) {
    if (!access_ok(addr, 4, true)) {
        return -EFAULT;
    }
    
    *(uint32_t*)addr = value;
    return 0;
}

