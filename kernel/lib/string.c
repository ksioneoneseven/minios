/*
 * MiniOS String Functions
 * 
 * Implementation of basic string and memory manipulation functions.
 * No external dependencies - everything implemented from scratch.
 */

#include "../include/string.h"

/*
 * Set memory to a specific value
 */
void* memset(void* dest, int val, size_t count) {
    unsigned char* ptr = (unsigned char*)dest;
    while (count--) {
        *ptr++ = (unsigned char)val;
    }
    return dest;
}

/*
 * Copy memory from source to destination
 */
void* memcpy(void* dest, const void* src, size_t count) {
    unsigned char* d = (unsigned char*)dest;
    const unsigned char* s = (const unsigned char*)src;
    while (count--) {
        *d++ = *s++;
    }
    return dest;
}

/*
 * Copy memory from source to destination (handles overlapping regions)
 */
void* memmove(void* dest, const void* src, size_t count) {
    unsigned char* d = (unsigned char*)dest;
    const unsigned char* s = (const unsigned char*)src;

    if (d < s) {
        /* Copy forward */
        while (count--) {
            *d++ = *s++;
        }
    } else if (d > s) {
        /* Copy backward to handle overlap */
        d += count;
        s += count;
        while (count--) {
            *--d = *--s;
        }
    }
    return dest;
}

/*
 * Compare memory regions
 */
int memcmp(const void* ptr1, const void* ptr2, size_t count) {
    const unsigned char* p1 = (const unsigned char*)ptr1;
    const unsigned char* p2 = (const unsigned char*)ptr2;
    while (count--) {
        if (*p1 != *p2) {
            return *p1 - *p2;
        }
        p1++;
        p2++;
    }
    return 0;
}

/*
 * Get the length of a null-terminated string
 */
size_t strlen(const char* str) {
    size_t len = 0;
    while (str[len]) {
        len++;
    }
    return len;
}

/*
 * Compare two strings
 */
int strcmp(const char* str1, const char* str2) {
    while (*str1 && (*str1 == *str2)) {
        str1++;
        str2++;
    }
    return *(unsigned char*)str1 - *(unsigned char*)str2;
}

/*
 * Compare two strings up to n characters
 */
int strncmp(const char* str1, const char* str2, size_t n) {
    while (n && *str1 && (*str1 == *str2)) {
        str1++;
        str2++;
        n--;
    }
    if (n == 0) {
        return 0;
    }
    return *(unsigned char*)str1 - *(unsigned char*)str2;
}

/*
 * Copy a string
 */
char* strcpy(char* dest, const char* src) {
    char* ret = dest;
    while ((*dest++ = *src++));
    return ret;
}

/*
 * Copy a string up to n characters
 */
char* strncpy(char* dest, const char* src, size_t n) {
    char* ret = dest;
    while (n && (*dest++ = *src++)) {
        n--;
    }
    /* Null-pad remaining space */
    while (n--) {
        *dest++ = '\0';
    }
    return ret;
}

/*
 * Concatenate strings
 */
char* strcat(char* dest, const char* src) {
    char* ret = dest;
    /* Find end of dest */
    while (*dest) {
        dest++;
    }
    /* Copy src */
    while ((*dest++ = *src++));
    return ret;
}

/*
 * Find first occurrence of character in string
 */
char* strchr(const char* str, int c) {
    while (*str) {
        if (*str == (char)c) {
            return (char*)str;
        }
        str++;
    }
    /* Check for null terminator */
    if (c == '\0') {
        return (char*)str;
    }
    return NULL;
}

/*
 * Find last occurrence of character in string
 */
char* strrchr(const char* str, int c) {
    const char* last = NULL;
    while (*str) {
        if (*str == (char)c) {
            last = str;
        }
        str++;
    }
    /* Check for null terminator */
    if (c == '\0') {
        return (char*)str;
    }
    return (char*)last;
}

/*
 * Find substring in string
 */
char* strstr(const char* haystack, const char* needle) {
    if (!needle[0]) return (char*)haystack;
    
    for (; *haystack; haystack++) {
        const char* h = haystack;
        const char* n = needle;
        
        while (*h && *n && *h == *n) {
            h++;
            n++;
        }
        
        if (!*n) return (char*)haystack;
    }
    
    return NULL;
}

/*
 * Convert string to integer
 */
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
