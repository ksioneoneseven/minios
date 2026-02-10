/*
 * MiniOS User-space Standard Library
 */

#ifndef _STDLIB_H
#define _STDLIB_H

#include "syscall.h"

/* NULL pointer */
#ifndef NULL
#define NULL ((void*)0)
#endif

/* Exit program */
void exit(int status) __attribute__((noreturn));

/* Memory allocation */
void* malloc(size_t size);
void free(void* ptr);
void* calloc(size_t nmemb, size_t size);
void* realloc(void* ptr, size_t size);

/* String conversion */
int atoi(const char* str);

#endif /* _STDLIB_H */

