/*
 * MiniOS User Access Validation Header
 * 
 * Provides safe copy operations between user and kernel space.
 * These functions validate pointers before accessing them.
 */

#ifndef _UACCESS_H
#define _UACCESS_H

#include "types.h"

/* User space memory boundaries */
#define USER_SPACE_START    0x40000000  /* 1GB - start of user space */
#define USER_SPACE_END      0xC0000000  /* 3GB - end of user space */

/* Error codes */
#define EFAULT  14  /* Bad address */

/*
 * Check if a user-space pointer is valid
 * addr: user-space address to check
 * size: size of the access
 * write: true if write access, false if read
 * Returns: true if valid, false otherwise
 */
bool access_ok(const void* addr, size_t size, bool write);

/*
 * Copy data from user space to kernel space
 * dst: kernel destination buffer
 * src: user source buffer
 * size: number of bytes to copy
 * Returns: 0 on success, -EFAULT on failure
 */
int copyin(void* dst, const void* src, size_t size);

/*
 * Copy data from kernel space to user space
 * dst: user destination buffer
 * src: kernel source buffer
 * size: number of bytes to copy
 * Returns: 0 on success, -EFAULT on failure
 */
int copyout(void* dst, const void* src, size_t size);

/*
 * Copy a null-terminated string from user space
 * dst: kernel destination buffer
 * src: user source string
 * max_len: maximum bytes to copy (including null terminator)
 * Returns: length of string on success, -EFAULT on failure
 */
int copyinstr(char* dst, const char* src, size_t max_len);

/*
 * Read a single byte from user space
 * addr: user address
 * value: pointer to store the value
 * Returns: 0 on success, -EFAULT on failure
 */
int get_user_byte(const void* addr, uint8_t* value);

/*
 * Read a 32-bit word from user space
 * addr: user address
 * value: pointer to store the value
 * Returns: 0 on success, -EFAULT on failure
 */
int get_user_word(const void* addr, uint32_t* value);

/*
 * Write a single byte to user space
 * addr: user address
 * value: value to write
 * Returns: 0 on success, -EFAULT on failure
 */
int put_user_byte(void* addr, uint8_t value);

/*
 * Write a 32-bit word to user space
 * addr: user address
 * value: value to write
 * Returns: 0 on success, -EFAULT on failure
 */
int put_user_word(void* addr, uint32_t value);

#endif /* _UACCESS_H */

