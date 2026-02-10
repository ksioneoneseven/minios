/*
 * MiniOS Program Loader Header
 * 
 * Loads and executes user-space programs.
 */

#ifndef _LOADER_H
#define _LOADER_H

#include "types.h"

/* Simple flat binary program header */
typedef struct {
    char name[32];          /* Program name */
    uint32_t entry_point;   /* Entry address (virtual) */
    uint32_t code_size;     /* Size of code/data */
    uint8_t* code;          /* Pointer to code */
} program_t;

/* Maximum number of embedded programs */
#define MAX_PROGRAMS 8

/*
 * Initialize the program loader
 */
void loader_init(void);

/*
 * Register an embedded program
 */
int loader_register(const char* name, uint32_t entry, uint32_t size, uint8_t* code);

/*
 * Find a program by name
 */
program_t* loader_find(const char* name);

/*
 * Load and execute a program
 * Returns PID on success, -1 on failure
 */
int loader_exec(const char* name);

/*
 * List all registered programs
 */
void loader_list(void);

#endif /* _LOADER_H */

