/*
 * MiniOS Process Management Header
 * 
 * Defines process control block (PCB) and process management functions.
 */

#ifndef _PROCESS_H
#define _PROCESS_H

#include "types.h"

/* Forward declaration */
struct file_descriptor;
typedef void (*signal_handler_t)(int);

/* Maximum number of processes */
#define MAX_PROCESSES       64
#define MAX_OPEN_FILES      16  /* Max open files per process */
#define KERNEL_STACK_SIZE   16384   /* 16KB kernel stack per process */
#define USER_STACK_SIZE     8192    /* 8KB user stack per process */
#define USER_STACK_BASE     0xBFFFF000  /* User stack starts near 3GB */
#define USER_HEAP_START     0x40100000  /* User heap starts after code (1GB + 1MB) */
#define USER_HEAP_MAX       0x80000000  /* User heap max (2GB) */

/* Process states */
typedef enum {
    PROCESS_STATE_UNUSED = 0,   /* PCB slot is free */
    PROCESS_STATE_CREATED,      /* Process created but not ready */
    PROCESS_STATE_READY,        /* Ready to run */
    PROCESS_STATE_RUNNING,      /* Currently executing */
    PROCESS_STATE_BLOCKED,      /* Waiting for something */
    PROCESS_STATE_ZOMBIE        /* Terminated, waiting for parent */
} process_state_t;

/* CPU registers saved during context switch */
typedef struct {
    /* Pushed by context switch code */
    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t esp;       /* Note: not used directly, kept for alignment */
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;
    
    /* Pushed by interrupt/exception */
    uint32_t eip;
    uint32_t cs;
    uint32_t eflags;
    
    /* Only present on privilege change */
    uint32_t user_esp;
    uint32_t user_ss;
} __attribute__((packed)) cpu_state_t;

/* Process Control Block (PCB) */
typedef struct process {
    /* Process identification */
    uint32_t pid;               /* Process ID */
    uint32_t ppid;              /* Parent process ID */
    char name[32];              /* Process name */

    /* Process state */
    process_state_t state;      /* Current state */
    int exit_code;              /* Exit code (if zombie) */
    uint8_t is_user_mode;       /* 1 if user mode process, 0 if kernel */

    /* User/Group ownership */
    uint32_t uid;               /* Real user ID */
    uint32_t gid;               /* Real group ID */
    uint32_t euid;              /* Effective user ID */
    uint32_t egid;              /* Effective group ID */

    /* CPU context */
    cpu_state_t* context;       /* Saved CPU state */
    uint32_t kernel_stack;      /* Top of kernel stack */
    uint32_t kernel_stack_base; /* Base of kernel stack (for deallocation) */

    /* User mode stack */
    uint32_t user_stack;        /* Top of user stack (if user mode) */
    uint32_t user_stack_base;   /* Base of user stack (for deallocation) */
    uint32_t user_entry;        /* User mode entry point */

    /* Memory management */
    uint32_t page_directory;    /* Physical address of page directory */
    uint32_t heap_start;        /* Start of user heap */
    uint32_t heap_break;        /* Current heap break (end of heap) */

    /* File descriptors */
    struct file_descriptor* fd_table[MAX_OPEN_FILES];

    /* Scheduling */
    uint32_t priority;          /* Process priority (0 = highest) */
    uint32_t time_slice;        /* Remaining time slice */
    uint64_t total_ticks;       /* Total CPU ticks used */

    /* Signals */
    uint32_t pending_signals;   /* Bitmap of pending signals */
    signal_handler_t signal_handlers[32];  /* Signal handlers (NSIG) */
    uint32_t blocked_signals;   /* Bitmap of blocked signals */

    /* Linked list pointers */
    struct process* next;       /* Next process in list */
    struct process* prev;       /* Previous process in list */
} process_t;

/* Process table */
extern process_t process_table[MAX_PROCESSES];
extern process_t* current_process;
extern process_t* ready_queue;

/*
 * Initialize the process subsystem
 */
void process_init(void);

/*
 * Create a new kernel process
 * entry_point: function to execute
 * name: process name
 * Returns: PID or -1 on error
 */
int process_create(void (*entry_point)(void), const char* name);

/*
 * Create a new user mode process
 * entry_point: user space entry point address
 * name: process name
 * Returns: PID or -1 on error
 */
int process_create_user(void (*entry_point)(void), const char* name);

/*
 * Switch to user mode and start executing at entry_point
 * This function does not return
 */
void enter_user_mode(uint32_t entry_point, uint32_t user_stack);

/*
 * Terminate current process
 */
void process_exit(int exit_code);

/*
 * Terminate a specific process
 */
void process_terminate(process_t* proc, int exit_code);

/*
 * Get process by PID
 */
process_t* process_get(uint32_t pid);

/*
 * Get current running process
 */
process_t* process_current(void);

/*
 * Add process to ready queue
 */
void process_ready(process_t* proc);

/*
 * Remove process from ready queue
 */
void process_unready(process_t* proc);

/*
 * Block current process
 */
void process_block(process_state_t reason);

/*
 * Unblock a process
 */
void process_unblock(process_t* proc);

/*
 * Print process list (for debugging)
 */
void process_print_all(void);

/*
 * Fork current process
 * Returns: child PID in parent, 0 in child, -1 on error
 */
int process_fork(void);

/*
 * Execute a new program in current process
 * path: path to ELF executable
 * argv: argument vector (currently ignored)
 * Returns: does not return on success, -1 on error
 */
int process_exec(const char* path, char* const argv[]);

/*
 * Wait for child process to exit
 * status: pointer to store exit status (can be NULL)
 * Returns: PID of exited child, -1 if no children
 */
int process_wait(int* status);

/*
 * Create and start a user process from an ELF file
 * Used to launch initial userland processes from kernel
 * Returns: PID on success, -1 on error
 */
int process_exec_elf(const char* path);

#endif /* _PROCESS_H */

