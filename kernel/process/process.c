/*
 * MiniOS Process Management Implementation
 */

#include "../include/process.h"
#include "../include/heap.h"
#include "../include/string.h"
#include "../include/stdio.h"
#include "../include/paging.h"
#include "../include/gdt.h"
#include "../include/user.h"
#include "../include/pmm.h"
#include "../include/elf.h"
#include "../include/signal.h"

/* Process table */
process_t process_table[MAX_PROCESSES];
process_t* current_process = NULL;
process_t* ready_queue = NULL;

/* Next available PID */
static uint32_t next_pid = 0;

/*
 * Allocate a free process slot
 */
static process_t* alloc_process_slot(void) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].state == PROCESS_STATE_UNUSED) {
            return &process_table[i];
        }
    }
    return NULL;
}

/*
 * Initialize the process subsystem
 */
void process_init(void) {
    /* Clear process table */
    memset(process_table, 0, sizeof(process_table));
    
    current_process = NULL;
    ready_queue = NULL;
    next_pid = 0;
    
    printk("Process: Initialized (max %d processes)\n", MAX_PROCESSES);
}

/*
 * Create a new kernel process
 */
int process_create(void (*entry_point)(void), const char* name) {
    process_t* proc = alloc_process_slot();
    if (proc == NULL) {
        printk("Process: No free process slots\n");
        return -1;
    }
    
    /* Allocate kernel stack */
    uint32_t stack_base = (uint32_t)kmalloc(KERNEL_STACK_SIZE);
    if (stack_base == 0) {
        printk("Process: Cannot allocate kernel stack\n");
        return -1;
    }
    
    /* Stack grows downward, so top is at base + size */
    uint32_t stack_top = stack_base + KERNEL_STACK_SIZE;
    
    /* Initialize PCB */
    proc->pid = next_pid++;
    proc->ppid = current_process ? current_process->pid : 0;
    strncpy(proc->name, name, sizeof(proc->name) - 1);
    proc->name[sizeof(proc->name) - 1] = '\0';
    
    proc->state = PROCESS_STATE_CREATED;
    proc->exit_code = 0;
    proc->is_user_mode = 0;  /* Kernel mode process */

    /* Inherit uid/gid from current process or use current logged-in user */
    if (current_process) {
        proc->uid = current_process->uid;
        proc->gid = current_process->gid;
        proc->euid = current_process->euid;
        proc->egid = current_process->egid;
    } else {
        proc->uid = current_uid;
        proc->gid = current_gid;
        proc->euid = current_uid;
        proc->egid = current_gid;
    }

    proc->kernel_stack = stack_top;
    proc->kernel_stack_base = stack_base;
    proc->user_stack = 0;
    proc->user_stack_base = 0;
    proc->user_entry = 0;

    /* For kernel processes, use kernel page directory */
    extern page_directory_t* kernel_directory;
    proc->page_directory = (uint32_t)kernel_directory;

    /* Initialize heap (kernel processes don't use user heap) */
    proc->heap_start = 0;
    proc->heap_break = 0;

    /* Initialize file descriptor table */
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        proc->fd_table[i] = NULL;
    }

    /* Initialize signal handling */
    signal_init_process(proc);

    proc->priority = 1;         /* Default priority */
    proc->time_slice = 10;      /* 10 timer ticks */
    proc->total_ticks = 0;
    
    proc->next = NULL;
    proc->prev = NULL;
    
    /* Set up initial stack frame for context switch
     * switch_to expects the stack to have:
     *   [esp+0]  = edi (will be popped)
     *   [esp+4]  = esi (will be popped)
     *   [esp+8]  = ebx (will be popped)
     *   [esp+12] = ebp (will be popped)
     *   [esp+16] = return address (entry point)
     */
    uint32_t* stack = (uint32_t*)stack_top;

    /* Return address - where switch_to will "return" to */
    *(--stack) = (uint32_t)entry_point;

    /* Callee-saved registers (will be popped by switch_to) */
    *(--stack) = 0;             /* ebp */
    *(--stack) = 0;             /* ebx */
    *(--stack) = 0;             /* esi */
    *(--stack) = 0;             /* edi */

    /* context points to where ESP will be after switch_to loads it */
    proc->context = (cpu_state_t*)stack;
    
    /* Add to ready queue */
    process_ready(proc);
    
    printk("Process: Created '%s' (PID %d)\n", name, proc->pid);

    return proc->pid;
}

/*
 * User mode entry trampoline
 * This function is called when a user process is first scheduled.
 * It sets up the TSS and transitions to user mode.
 */
static void user_mode_trampoline(void) {
    /* Get current process */
    process_t* proc = current_process;

    printk("trampoline: PID %d '%s'\n", proc->pid, proc->name);
    printk("trampoline: entry=0x%x stack=0x%x\n", proc->user_entry, proc->user_stack);
    printk("trampoline: kstack=0x%x\n", proc->kernel_stack);

    /* Set kernel stack in TSS for syscalls/interrupts */
    tss_set_kernel_stack(proc->kernel_stack);

    printk("trampoline: TSS set, entering user mode...\n");

    /* Jump to user mode - this never returns */
    enter_user_mode(proc->user_entry, proc->user_stack);
}

/*
 * Create a new user mode process
 */
int process_create_user(void (*entry_point)(void), const char* name) {
    process_t* proc = alloc_process_slot();
    if (proc == NULL) {
        printk("Process: No free process slots\n");
        return -1;
    }

    /* Allocate kernel stack (for syscalls/interrupts) */
    uint32_t kstack_base = (uint32_t)kmalloc(KERNEL_STACK_SIZE);
    if (kstack_base == 0) {
        printk("Process: Cannot allocate kernel stack\n");
        return -1;
    }
    uint32_t kstack_top = kstack_base + KERNEL_STACK_SIZE;

    /* Allocate user stack */
    uint32_t ustack_base = (uint32_t)kmalloc(USER_STACK_SIZE);
    if (ustack_base == 0) {
        printk("Process: Cannot allocate user stack\n");
        kfree((void*)kstack_base);
        return -1;
    }
    uint32_t ustack_top = ustack_base + USER_STACK_SIZE;

    /* Initialize PCB */
    proc->pid = next_pid++;
    proc->ppid = current_process ? current_process->pid : 0;
    strncpy(proc->name, name, sizeof(proc->name) - 1);
    proc->name[sizeof(proc->name) - 1] = '\0';

    proc->state = PROCESS_STATE_CREATED;
    proc->exit_code = 0;
    proc->is_user_mode = 1;  /* User mode process */

    /* Inherit uid/gid from current process or use current logged-in user */
    if (current_process) {
        proc->uid = current_process->uid;
        proc->gid = current_process->gid;
        proc->euid = current_process->euid;
        proc->egid = current_process->egid;
    } else {
        proc->uid = current_uid;
        proc->gid = current_gid;
        proc->euid = current_uid;
        proc->egid = current_gid;
    }

    proc->kernel_stack = kstack_top;
    proc->kernel_stack_base = kstack_base;
    proc->user_stack = ustack_top;
    proc->user_stack_base = ustack_base;
    proc->user_entry = (uint32_t)entry_point;

    /* For now, use kernel page directory (shared address space) */
    extern page_directory_t* kernel_directory;
    proc->page_directory = (uint32_t)kernel_directory;

    /* Initialize heap for user processes */
    proc->heap_start = 0;
    proc->heap_break = 0;

    /* Initialize file descriptor table */
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        proc->fd_table[i] = NULL;
    }

    /* Initialize signal handling */
    signal_init_process(proc);

    proc->priority = 1;
    proc->time_slice = 10;
    proc->total_ticks = 0;

    proc->next = NULL;
    proc->prev = NULL;

    /* Set up kernel stack for context switch
     * The trampoline will handle the transition to user mode
     */
    uint32_t* stack = (uint32_t*)kstack_top;

    /* Return address - trampoline function */
    *(--stack) = (uint32_t)user_mode_trampoline;

    /* Callee-saved registers */
    *(--stack) = 0;             /* ebp */
    *(--stack) = 0;             /* ebx */
    *(--stack) = 0;             /* esi */
    *(--stack) = 0;             /* edi */

    proc->context = (cpu_state_t*)stack;

    /* Add to ready queue */
    process_ready(proc);

    printk("Process: Created user '%s' (PID %d)\n", name, proc->pid);

    return proc->pid;
}

/*
 * Terminate current process
 */
void process_exit(int exit_code) {
    if (current_process == NULL) {
        return;
    }

    printk("Process: '%s' (PID %d) exiting with code %d\n",
           current_process->name, current_process->pid, exit_code);

    current_process->exit_code = exit_code;
    current_process->state = PROCESS_STATE_ZOMBIE;

    /* Remove from ready queue if present */
    process_unready(current_process);

    /* Wake parent if waiting */
    if (current_process->ppid != 0) {
        process_t* parent = process_get(current_process->ppid);
        if (parent && parent->state == PROCESS_STATE_BLOCKED) {
            /* Parent is blocked (likely waiting for children) - unblock it */
            process_unblock(parent);
        }
    }

    /* Reparent children to init (PID 1) */
    for (int i = 0; i < MAX_PROCESSES; i++) {
        process_t* child = &process_table[i];
        if (child->state != PROCESS_STATE_UNUSED &&
            child->ppid == current_process->pid) {
            /* This is our child - reparent to init */
            child->ppid = 1;

            /* If child is a zombie, wake init to clean it up */
            if (child->state == PROCESS_STATE_ZOMBIE) {
                process_t* init = process_get(1);
                if (init && init->state == PROCESS_STATE_BLOCKED) {
                    process_unblock(init);
                }
            }
        }
    }

    /* Trigger reschedule */
    extern void schedule(void);
    schedule();
}

/*
 * Get process by PID
 */
process_t* process_get(uint32_t pid) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].state != PROCESS_STATE_UNUSED &&
            process_table[i].pid == pid) {
            return &process_table[i];
        }
    }
    return NULL;
}

/*
 * Get current running process
 */
process_t* process_current(void) {
    return current_process;
}

/*
 * Add process to ready queue
 */
void process_ready(process_t* proc) {
    if (proc == NULL || proc->state == PROCESS_STATE_UNUSED) {
        return;
    }

    proc->state = PROCESS_STATE_READY;

    /* Add to end of ready queue */
    if (ready_queue == NULL) {
        ready_queue = proc;
        proc->next = NULL;
        proc->prev = NULL;
    } else {
        process_t* last = ready_queue;
        while (last->next != NULL) {
            last = last->next;
        }
        last->next = proc;
        proc->prev = last;
        proc->next = NULL;
    }
}

/*
 * Remove process from ready queue
 */
void process_unready(process_t* proc) {
    if (proc == NULL) {
        return;
    }

    /* Remove from linked list */
    if (proc->prev != NULL) {
        proc->prev->next = proc->next;
    } else if (ready_queue == proc) {
        ready_queue = proc->next;
    }

    if (proc->next != NULL) {
        proc->next->prev = proc->prev;
    }

    proc->next = NULL;
    proc->prev = NULL;
}

/*
 * Block current process
 */
void process_block(process_state_t reason) {
    if (current_process == NULL) {
        return;
    }

    current_process->state = reason;
    process_unready(current_process);

    /* Trigger reschedule */
    extern void schedule(void);
    schedule();
}

/*
 * Unblock a process
 */
void process_unblock(process_t* proc) {
    if (proc == NULL || proc->state == PROCESS_STATE_UNUSED) {
        return;
    }

    process_ready(proc);
}

/*
 * Print process list (for debugging)
 */
void process_print_all(void) {
    printk("PID  PPID  STATE     NAME\n");
    printk("---  ----  --------  ----------------\n");

    for (int i = 0; i < MAX_PROCESSES; i++) {
        process_t* proc = &process_table[i];
        if (proc->state == PROCESS_STATE_UNUSED) {
            continue;
        }

        const char* state_str;
        switch (proc->state) {
            case PROCESS_STATE_CREATED: state_str = "CREATED"; break;
            case PROCESS_STATE_READY:   state_str = "READY"; break;
            case PROCESS_STATE_RUNNING: state_str = "RUNNING"; break;
            case PROCESS_STATE_BLOCKED: state_str = "BLOCKED"; break;
            case PROCESS_STATE_ZOMBIE:  state_str = "ZOMBIE"; break;
            default: state_str = "UNKNOWN"; break;
        }

        printk("%3d  %4d  %-8s  %s\n",
               proc->pid, proc->ppid, state_str, proc->name);
    }
}

/*
 * Terminate a specific process
 */
void process_terminate(process_t* proc, int exit_code) {
    if (proc == NULL) {
        return;
    }

    /* Don't allow killing idle process */
    if (proc->pid == 0) {
        return;
    }

    printk("Process: '%s' (PID %d) terminated with code %d\n",
           proc->name, proc->pid, exit_code);

    proc->exit_code = exit_code;
    proc->state = PROCESS_STATE_ZOMBIE;

    /* Remove from ready queue if present */
    process_unready(proc);

    /* If this is the current process, reschedule */
    if (proc == current_process) {
        extern void schedule(void);
        schedule();
    }
}

/*
 * Fork current process
 * Creates a copy of current process with new address space
 */
int process_fork(void) {
    process_t* parent = current_process;
    if (parent == NULL) {
        return -1;
    }

    /* Allocate new process slot */
    process_t* child = alloc_process_slot();
    if (child == NULL) {
        printk("fork: no free process slots\n");
        return -1;
    }

    /* Allocate kernel stack for child */
    uint32_t kstack_base = (uint32_t)kmalloc(KERNEL_STACK_SIZE);
    if (kstack_base == 0) {
        printk("fork: cannot allocate kernel stack\n");
        return -1;
    }
    uint32_t kstack_top = kstack_base + KERNEL_STACK_SIZE;

    /* Clone parent's page directory */
    page_directory_t* child_dir = paging_clone_directory(
        (page_directory_t*)parent->page_directory);
    if (child_dir == NULL) {
        kfree((void*)kstack_base);
        printk("fork: cannot clone page directory\n");
        return -1;
    }

    /* Initialize child PCB */
    child->pid = next_pid++;
    child->ppid = parent->pid;
    strncpy(child->name, parent->name, sizeof(child->name) - 1);
    child->name[sizeof(child->name) - 1] = '\0';

    child->state = PROCESS_STATE_CREATED;
    child->exit_code = 0;
    child->is_user_mode = parent->is_user_mode;

    child->uid = parent->uid;
    child->gid = parent->gid;
    child->euid = parent->euid;
    child->egid = parent->egid;

    child->kernel_stack = kstack_top;
    child->kernel_stack_base = kstack_base;
    child->user_stack = parent->user_stack;
    child->user_stack_base = parent->user_stack_base;
    child->user_entry = parent->user_entry;

    child->page_directory = (uint32_t)child_dir;
    child->heap_start = parent->heap_start;
    child->heap_break = parent->heap_break;

    /* Copy file descriptor table (increment refcounts) */
    memset(child->fd_table, 0, sizeof(child->fd_table));
    /* TODO: Implement proper fd sharing with refcount */

    child->priority = parent->priority;
    child->time_slice = 10;
    child->total_ticks = 0;
    child->next = NULL;
    child->prev = NULL;

    /* Set up child's kernel stack for context switch */
    /* Child will return 0 from fork */
    uint32_t* stack = (uint32_t*)kstack_top;

    /* We need to set up the stack so that when the child is scheduled,
     * it returns from fork() with 0 */
    *(--stack) = 0;             /* Return value (EAX = 0 for child) */
    *(--stack) = 0;             /* ebp */
    *(--stack) = 0;             /* ebx */
    *(--stack) = 0;             /* esi */
    *(--stack) = 0;             /* edi */

    child->context = (cpu_state_t*)stack;

    /* Add child to ready queue */
    process_ready(child);

    printk("fork: created child '%s' (PID %d) from parent PID %d\n",
           child->name, child->pid, parent->pid);

    /* Return child PID to parent */
    return child->pid;
}

/*
 * Execute a new program in current process
 * Replaces current address space with new ELF executable
 */
int process_exec(const char* path, char* const argv[]) {
    process_t* proc = current_process;
    if (proc == NULL) {
        return -1;
    }

    /* Load the ELF executable */
    uint32_t entry;
    int result = elf_load_file(path, &entry);
    if (result < 0) {
        printk("exec: failed to load '%s'\n", path);
        return -1;
    }

    /* Allocate new user stack */
    uint32_t new_stack_base = USER_STACK_BASE;
    uint32_t stack_frame = pmm_alloc_frame();
    if (stack_frame == 0) {
        printk("exec: cannot allocate user stack\n");
        return -1;
    }

    paging_map_page(new_stack_base, stack_frame, PAGE_USER | PAGE_WRITE);
    memset((void*)new_stack_base, 0, PAGE_SIZE);

    uint32_t stack_top = new_stack_base + PAGE_SIZE;

    /* Count arguments and calculate space needed */
    int argc = 0;
    size_t strings_size = 0;

    if (argv != NULL) {
        for (int i = 0; argv[i] != NULL; i++) {
            argc++;
            strings_size += strlen(argv[i]) + 1;  /* +1 for null terminator */
        }
    }

    /* If no args provided, use program name as argv[0] */
    if (argc == 0) {
        /* Extract program name from path */
        const char* name = path;
        for (const char* p = path; *p; p++) {
            if (*p == '/') {
                name = p + 1;
            }
        }
        argc = 1;
        strings_size = strlen(name) + 1;
    }

    /* Stack layout (growing downward):
     * [strings area] - argument strings
     * [argv[argc] = NULL]
     * [argv[argc-1]]
     * ...
     * [argv[0]]
     * [argc]         <- ESP points here
     */

    /* Reserve space for strings */
    stack_top -= strings_size;
    stack_top &= ~3;  /* Align to 4 bytes */
    uint32_t strings_start = stack_top;

    /* Copy argument strings to stack */
    uint32_t* argv_ptrs = (uint32_t*)kmalloc((argc + 1) * sizeof(uint32_t));
    if (argv_ptrs == NULL) {
        return -1;
    }

    char* str_ptr = (char*)strings_start;
    if (argv != NULL && argv[0] != NULL) {
        for (int i = 0; i < argc; i++) {
            size_t len = strlen(argv[i]) + 1;
            memcpy(str_ptr, argv[i], len);
            argv_ptrs[i] = (uint32_t)str_ptr;
            str_ptr += len;
        }
    } else {
        /* Use program name as argv[0] */
        const char* name = path;
        for (const char* p = path; *p; p++) {
            if (*p == '/') {
                name = p + 1;
            }
        }
        size_t len = strlen(name) + 1;
        memcpy(str_ptr, name, len);
        argv_ptrs[0] = (uint32_t)str_ptr;
    }
    argv_ptrs[argc] = 0;  /* NULL terminator */

    /* Reserve space for argv array (argc + 1 pointers) */
    stack_top -= (argc + 1) * sizeof(uint32_t);
    stack_top &= ~3;
    uint32_t argv_start = stack_top;

    /* Copy argv array to stack */
    memcpy((void*)argv_start, argv_ptrs, (argc + 1) * sizeof(uint32_t));
    kfree(argv_ptrs);

    /* Push argv pointer */
    stack_top -= sizeof(uint32_t);
    *(uint32_t*)stack_top = argv_start;

    /* Push argc */
    stack_top -= sizeof(uint32_t);
    *(uint32_t*)stack_top = (uint32_t)argc;

    /* Align stack to 16 bytes (ABI requirement) */
    stack_top &= ~0xF;

    /* Update process entry point and stack */
    proc->user_entry = entry;
    proc->user_stack = stack_top;
    proc->user_stack_base = new_stack_base;

    /* Reset heap */
    proc->heap_start = USER_HEAP_START;
    proc->heap_break = USER_HEAP_START;

    /* Extract program name from path */
    const char* name = path;
    for (const char* p = path; *p; p++) {
        if (*p == '/') {
            name = p + 1;
        }
    }
    strncpy(proc->name, name, sizeof(proc->name) - 1);
    proc->name[sizeof(proc->name) - 1] = '\0';

    printk("exec: loaded '%s' at entry 0x%08X (argc=%d)\n", path, entry, argc);

    /* Jump to user mode at new entry point */
    enter_user_mode(entry, stack_top);

    /* Should not reach here */
    return -1;
}

/*
 * Wait for child process to exit
 */
int process_wait(int* status) {
    process_t* parent = current_process;
    if (parent == NULL) {
        return -1;
    }

    while (1) {
        /* Look for zombie children */
        for (int i = 0; i < MAX_PROCESSES; i++) {
            process_t* child = &process_table[i];
            if (child->ppid == parent->pid &&
                child->state == PROCESS_STATE_ZOMBIE) {
                /* Found a zombie child */
                int child_pid = child->pid;

                if (status != NULL) {
                    *status = child->exit_code;
                }

                /* Free child's resources */
                if (child->kernel_stack_base) {
                    kfree((void*)child->kernel_stack_base);
                }
                if (child->page_directory) {
                    paging_free_directory((page_directory_t*)child->page_directory);
                }

                /* Mark slot as unused */
                child->state = PROCESS_STATE_UNUSED;
                child->pid = 0;
                child->ppid = 0;

                return child_pid;
            }
        }

        /* Check if we have any children at all */
        int has_children = 0;
        for (int i = 0; i < MAX_PROCESSES; i++) {
            if (process_table[i].ppid == parent->pid &&
                process_table[i].state != PROCESS_STATE_UNUSED) {
                has_children = 1;
                break;
            }
        }

        if (!has_children) {
            /* No children to wait for */
            return -1;
        }

        /* Block until a child exits */
        process_block(PROCESS_STATE_BLOCKED);
        extern void schedule(void);
        schedule();
    }
}

/*
 * Create a new user process from an ELF file and start it
 * This is used to launch the initial userland processes from the kernel
 */
int process_exec_elf(const char* path) {
    /* First, load the ELF to get the entry point */
    uint32_t entry;
    int result = elf_load_file(path, &entry);
    if (result < 0) {
        printk("exec_elf: failed to load '%s'\n", path);
        return -1;
    }

    /* Allocate a process slot */
    process_t* proc = alloc_process_slot();
    if (proc == NULL) {
        printk("exec_elf: No free process slots\n");
        return -1;
    }

    /* Allocate kernel stack */
    uint32_t kstack_base = (uint32_t)kmalloc(KERNEL_STACK_SIZE);
    if (kstack_base == 0) {
        printk("exec_elf: Cannot allocate kernel stack\n");
        return -1;
    }
    uint32_t kstack_top = kstack_base + KERNEL_STACK_SIZE;

    /* Allocate user stack - needs to be in user space */
    uint32_t ustack_base = USER_STACK_BASE - (USER_STACK_SIZE - PAGE_SIZE);
    for (uint32_t offset = 0; offset < USER_STACK_SIZE; offset += PAGE_SIZE) {
        uint32_t stack_frame = pmm_alloc_frame();
        if (stack_frame == 0) {
            printk("exec_elf: Cannot allocate user stack frame\n");
            kfree((void*)kstack_base);
            return -1;
        }
        paging_map_page(ustack_base + offset, stack_frame, PAGE_USER | PAGE_WRITE);
        memset((void*)(ustack_base + offset), 0, PAGE_SIZE);
    }
    uint32_t ustack_top = ustack_base + USER_STACK_SIZE - 16;

    /* Extract program name from path */
    const char* name = path;
    for (const char* p = path; *p; p++) {
        if (*p == '/') name = p + 1;
    }

    /* Initialize PCB */
    proc->pid = next_pid++;
    proc->ppid = 0;  /* No parent - kernel launched */
    strncpy(proc->name, name, sizeof(proc->name) - 1);
    proc->name[sizeof(proc->name) - 1] = '\0';

    proc->state = PROCESS_STATE_CREATED;
    proc->exit_code = 0;
    proc->is_user_mode = 1;

    /* Run as root */
    proc->uid = 0;
    proc->gid = 0;
    proc->euid = 0;
    proc->egid = 0;

    proc->kernel_stack = kstack_top;
    proc->kernel_stack_base = kstack_base;
    proc->user_stack = ustack_top;
    proc->user_stack_base = ustack_base;
    proc->user_entry = entry;

    /* Use kernel page directory for now */
    extern page_directory_t* kernel_directory;
    proc->page_directory = (uint32_t)kernel_directory;

    /* Initialize heap */
    proc->heap_start = USER_HEAP_START;
    proc->heap_break = USER_HEAP_START;

    /* Initialize file descriptor table */
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        proc->fd_table[i] = NULL;
    }

    /* Initialize signal handling */
    signal_init_process(proc);

    proc->priority = 1;
    proc->time_slice = 10;
    proc->total_ticks = 0;
    proc->next = NULL;
    proc->prev = NULL;

    /* Set up kernel stack for context switch */
    uint32_t* stack = (uint32_t*)kstack_top;
    *(--stack) = (uint32_t)user_mode_trampoline;
    *(--stack) = 0;  /* ebp */
    *(--stack) = 0;  /* ebx */
    *(--stack) = 0;  /* esi */
    *(--stack) = 0;  /* edi */
    proc->context = (cpu_state_t*)stack;

    /* Add to ready queue */
    process_ready(proc);

    printk("exec_elf: Started '%s' (PID %d, entry 0x%x)\n", name, proc->pid, entry);
    return proc->pid;
}

