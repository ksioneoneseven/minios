/*
 * MiniOS Standard Type Definitions
 * 
 * Provides fixed-width integer types and common type aliases
 * for use throughout the kernel.
 */

#ifndef _TYPES_H
#define _TYPES_H

/* Fixed-width unsigned integer types */
typedef unsigned char       uint8_t;
typedef unsigned short      uint16_t;
typedef unsigned int        uint32_t;
typedef unsigned long long  uint64_t;

/* Fixed-width signed integer types */
typedef signed char         int8_t;
typedef signed short        int16_t;
typedef signed int          int32_t;
typedef signed long long    int64_t;

/* Size type for memory operations */
typedef uint32_t            size_t;
typedef int32_t             ssize_t;

/* Boolean type */
typedef enum { false = 0, true = 1 } bool;

/* NULL pointer */
#define NULL ((void*)0)

#endif /* _TYPES_H */

