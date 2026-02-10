/*
 * MiniOS Program Loader Implementation
 * 
 * Handles loading and execution of user-space programs.
 * Currently supports flat binary format embedded in kernel.
 */

#include "../include/loader.h"
#include "../include/process.h"
#include "../include/scheduler.h"
#include "../include/paging.h"
#include "../include/heap.h"
#include "../include/string.h"
#include "../include/stdio.h"

/* Program registry */
static program_t programs[MAX_PROGRAMS];
static int num_programs = 0;

/*
 * Initialize the program loader
 */
void loader_init(void) {
    num_programs = 0;
    for (int i = 0; i < MAX_PROGRAMS; i++) {
        programs[i].name[0] = '\0';
        programs[i].entry_point = 0;
        programs[i].code_size = 0;
        programs[i].code = NULL;
    }
    printk("Loader: Initialized\n");
}

/*
 * Register an embedded program
 */
int loader_register(const char* name, uint32_t entry, uint32_t size, uint8_t* code) {
    if (num_programs >= MAX_PROGRAMS) {
        return -1;
    }
    
    program_t* prog = &programs[num_programs++];
    strncpy(prog->name, name, 31);
    prog->name[31] = '\0';
    prog->entry_point = entry;
    prog->code_size = size;
    prog->code = code;
    
    printk("Loader: Registered '%s' (entry=0x%x, size=%u)\n", 
           name, entry, size);
    return 0;
}

/*
 * Find a program by name
 */
program_t* loader_find(const char* name) {
    for (int i = 0; i < num_programs; i++) {
        if (strcmp(programs[i].name, name) == 0) {
            return &programs[i];
        }
    }
    return NULL;
}

/*
 * Load and execute a program
 */
int loader_exec(const char* name) {
    program_t* prog = loader_find(name);
    if (prog == NULL) {
        printk("Loader: Program '%s' not found\n", name);
        return -1;
    }
    
    /* Copy program code to user memory
     * For simplicity, we'll place it at the entry point address
     * In a real OS, we'd allocate pages and map them properly
     */
    
    /* For now, since we share the kernel page directory,
     * we can just use the code directly at its current location.
     * This is a simplification - real loading would:
     * 1. Create a new page directory
     * 2. Allocate pages for code, data, stack
     * 3. Copy code to the allocated pages
     * 4. Set up proper permissions
     */
    
    /* Create user process */
    int pid = process_create_user((void (*)(void))prog->entry_point, prog->name);
    if (pid < 0) {
        printk("Loader: Failed to create process for '%s'\n", name);
        return -1;
    }
    
    printk("Loader: Started '%s' (PID %d)\n", name, pid);
    return pid;
}

/*
 * List all registered programs
 */
void loader_list(void) {
    printk("Registered programs:\n");
    for (int i = 0; i < num_programs; i++) {
        printk("  %s (entry=0x%x, size=%u bytes)\n",
               programs[i].name,
               programs[i].entry_point,
               programs[i].code_size);
    }
    if (num_programs == 0) {
        printk("  (none)\n");
    }
}

