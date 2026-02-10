/*
 * MiniOS User-space String Functions
 */

#ifndef _STRING_H
#define _STRING_H

#include "syscall.h"

/* NULL pointer */
#ifndef NULL
#define NULL ((void*)0)
#endif

/* String length */
size_t strlen(const char* str);

/* String copy */
char* strcpy(char* dest, const char* src);
char* strncpy(char* dest, const char* src, size_t n);

/* String compare */
int strcmp(const char* s1, const char* s2);
int strncmp(const char* s1, const char* s2, size_t n);

/* Memory operations */
void* memset(void* s, int c, size_t n);
void* memcpy(void* dest, const void* src, size_t n);
int memcmp(const void* s1, const void* s2, size_t n);

/* String concatenation */
char* strcat(char* dest, const char* src);

#endif /* _STRING_H */

