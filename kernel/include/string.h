/*
 * MiniOS String Functions Header
 * 
 * Basic string and memory manipulation functions
 * for use in the kernel (no libc dependency).
 */

#ifndef _STRING_H
#define _STRING_H

#include "types.h"

/* Memory operations */

/*
 * Set memory to a specific value
 * dest: destination pointer
 * val: value to set (cast to unsigned char)
 * count: number of bytes to set
 * Returns: dest
 */
void* memset(void* dest, int val, size_t count);

/*
 * Copy memory from source to destination
 * dest: destination pointer
 * src: source pointer
 * count: number of bytes to copy
 * Returns: dest
 * Note: Does not handle overlapping regions (use memmove for that)
 */
void* memcpy(void* dest, const void* src, size_t count);

/*
 * Copy memory from source to destination (handles overlapping regions)
 * dest: destination pointer
 * src: source pointer
 * count: number of bytes to copy
 * Returns: dest
 */
void* memmove(void* dest, const void* src, size_t count);

/*
 * Compare memory regions
 * ptr1: first memory region
 * ptr2: second memory region
 * count: number of bytes to compare
 * Returns: 0 if equal, <0 if ptr1 < ptr2, >0 if ptr1 > ptr2
 */
int memcmp(const void* ptr1, const void* ptr2, size_t count);

/* String operations */

/*
 * Get the length of a null-terminated string
 * str: input string
 * Returns: number of characters (not including null terminator)
 */
size_t strlen(const char* str);

/*
 * Compare two strings
 * str1: first string
 * str2: second string
 * Returns: 0 if equal, <0 if str1 < str2, >0 if str1 > str2
 */
int strcmp(const char* str1, const char* str2);

/*
 * Compare two strings up to n characters
 * str1: first string
 * str2: second string
 * n: maximum characters to compare
 * Returns: 0 if equal, <0 if str1 < str2, >0 if str1 > str2
 */
int strncmp(const char* str1, const char* str2, size_t n);

/*
 * Copy a string
 * dest: destination buffer
 * src: source string
 * Returns: dest
 * Warning: dest must be large enough to hold src
 */
char* strcpy(char* dest, const char* src);

/*
 * Copy a string up to n characters
 * dest: destination buffer
 * src: source string
 * n: maximum characters to copy
 * Returns: dest
 * Note: dest is null-padded if src is shorter than n
 */
char* strncpy(char* dest, const char* src, size_t n);

/*
 * Concatenate strings
 * dest: destination buffer (must have space)
 * src: source string to append
 * Returns: dest
 */
char* strcat(char* dest, const char* src);

/*
 * Find first occurrence of character in string
 * str: string to search
 * c: character to find
 * Returns: pointer to character, or NULL if not found
 */
char* strchr(const char* str, int c);

/*
 * Find last occurrence of character in string
 * str: string to search
 * c: character to find
 * Returns: pointer to character, or NULL if not found
 */
char* strrchr(const char* str, int c);

/*
 * Find substring in string
 * haystack: string to search in
 * needle: substring to find
 * Returns: pointer to first occurrence, or NULL if not found
 */
char* strstr(const char* haystack, const char* needle);

/*
 * Convert string to integer
 * str: string to convert
 * Returns: integer value
 */
int atoi(const char* str);

#endif /* _STRING_H */

