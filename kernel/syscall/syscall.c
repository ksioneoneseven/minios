/*
 * MiniOS System Call Implementation
 * 
 * Handles INT 0x80 system calls from user space.
 */

#include "../include/syscall.h"
#include "../include/isr.h"
#include "../include/idt.h"
#include "../include/gdt.h"
#include "../include/stdio.h"
#include "../include/process.h"
#include "../include/scheduler.h"
#include "../include/vga.h"
#include "../include/keyboard.h"
#include "../include/timer.h"
#include "../include/vfs.h"
#include "../include/heap.h"
#include "../include/pmm.h"
#include "../include/user.h"
#include "../include/string.h"
#include "../include/ramfs.h"
#include "../include/ext2.h"
#include "../include/pipe.h"
#include "../include/signal.h"
#include "../include/uaccess.h"

/* External references to current directory from shell */
extern vfs_node_t* current_dir_node;
extern char current_dir[VFS_MAX_PATH];

/* System call table */
static syscall_handler_t syscall_table[NUM_SYSCALLS];

/*
 * System call interrupt handler
 */
static void syscall_handler(registers_t* regs) {
    /* Get syscall number from EAX */
    uint32_t syscall_num = regs->eax;

    if (syscall_num >= NUM_SYSCALLS) {
        printk("syscall: invalid syscall number %d\n", syscall_num);
        regs->eax = (uint32_t)-1;
        /* Ensure interrupts are enabled when returning to user mode */
        regs->eflags |= 0x200;
        return;
    }

    syscall_handler_t handler = syscall_table[syscall_num];
    if (handler == NULL) {
        printk("syscall: unimplemented syscall %d\n", syscall_num);
        regs->eax = (uint32_t)-1;
        /* Ensure interrupts are enabled when returning to user mode */
        regs->eflags |= 0x200;
        return;
    }

    /* Call handler with arguments from registers */
    /* Arguments: EBX, ECX, EDX, ESI, EDI */
    int32_t result = handler(regs->ebx, regs->ecx, regs->edx, regs->esi, regs->edi);

    /* Return value in EAX */
    regs->eax = (uint32_t)result;

    /* Ensure interrupts are enabled when returning to user mode.
     * When INT 0x80 is executed, the CPU clears the IF flag and saves EFLAGS.
     * Without this, IRET would restore EFLAGS with IF=0, keeping interrupts disabled. */
    regs->eflags |= 0x200;
}

/*
 * sys_exit - terminate current process
 */
int32_t sys_exit(uint32_t status, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5) {
    (void)a2; (void)a3; (void)a4; (void)a5;
    
    process_exit((int)status);
    
    /* Should not return */
    return 0;
}

/*
 * sys_write - write to file descriptor
 * For now, only supports stdout (fd=1) and stderr (fd=2)
 */
int32_t sys_write(uint32_t fd, uint32_t buf, uint32_t count, uint32_t a4, uint32_t a5) {
    (void)a4; (void)a5;
    
    if (fd != STDOUT_FILENO && fd != STDERR_FILENO) {
        return -1;  /* Invalid fd */
    }
    
    const char* str = (const char*)buf;
    
    /* Write each character to VGA */
    for (uint32_t i = 0; i < count; i++) {
        vga_putchar(str[i]);
    }
    
    return (int32_t)count;
}

/*
 * sys_read - read from file descriptor
 * For now, only supports stdin (fd=0)
 * Note: Does NOT echo - application is responsible for echoing if desired
 */
int32_t sys_read(uint32_t fd, uint32_t buf, uint32_t count, uint32_t a4, uint32_t a5) {
    (void)a4; (void)a5;

    if (fd != STDIN_FILENO) {
        return -1;  /* Invalid fd */
    }

    char* buffer = (char*)buf;
    uint32_t read_count = 0;

    /* Read characters from keyboard */
    while (read_count < count) {
        /* Enable interrupts so keyboard IRQ can fire */
        __asm__ volatile("sti");

        /* Busy-wait for keyboard input */
        while (!keyboard_has_key()) {
            /* Use hlt to save power - wakes on any interrupt */
            __asm__ volatile("hlt");
        }

        char c = keyboard_getchar_nonblock();
        buffer[read_count++] = c;

        /* Return after each character for line-buffered input
         * This allows the shell to handle echoing and line editing */
        break;
    }

    return (int32_t)read_count;
}

/*
 * sys_getpid - get current process ID
 */
int32_t sys_getpid(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
    
    process_t* current = process_current();
    if (current == NULL) {
        return -1;
    }
    
    return (int32_t)current->pid;
}

/*
 * sys_yield - yield CPU to another process
 */
int32_t sys_yield(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
    
    yield();
    return 0;
}

/*
 * sys_sleep - sleep for specified milliseconds
 */
int32_t sys_sleep(uint32_t ms, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5) {
    (void)a2; (void)a3; (void)a4; (void)a5;

    timer_sleep_ms(ms);
    return 0;
}

/*
 * sys_open - open a file
 * flags: 0=read, 1=create, 2=write
 * Returns file descriptor on success, -1 on error
 */
int32_t sys_open(uint32_t path_ptr, uint32_t flags, uint32_t mode, uint32_t a4, uint32_t a5) {
    (void)mode; (void)a4; (void)a5;

    process_t* proc = process_current();
    if (proc == NULL) {
        return -1;
    }

    /* Safely copy path from user space */
    char path[256];
    if (copyinstr(path, (const char*)path_ptr, sizeof(path)) < 0) {
        printk("sys_open: bad path pointer\n");
        return -1;
    }

    printk("sys_open: path='%s' flags=%d current_dir='%s'\n", path, flags, current_dir);

    /* Build full path for relative paths */
    char fullpath[256];
    const char* lookup_path = path;

    if (path[0] != '/') {
        /* Relative path - build full path */
        if (strcmp(current_dir, "/") == 0) {
            snprintf(fullpath, sizeof(fullpath), "/%s", path);
        } else {
            snprintf(fullpath, sizeof(fullpath), "%s/%s", current_dir, path);
        }
        lookup_path = fullpath;
    }

    printk("sys_open: lookup_path='%s'\n", lookup_path);

    /* Find the file in VFS */
    vfs_node_t* node = vfs_lookup(lookup_path);

    /* If file not found and O_CREAT flag is set, create it */
    if (node == NULL && (flags & 1)) {
        printk("sys_open: file not found, creating...\n");
        /* Extract parent directory and filename */
        char parent_path[256];
        strncpy(parent_path, lookup_path, sizeof(parent_path) - 1);
        parent_path[sizeof(parent_path) - 1] = '\0';

        char* last_slash = strrchr(parent_path, '/');
        const char* filename = NULL;
        vfs_node_t* parent = NULL;

        if (last_slash != NULL) {
            filename = last_slash + 1;
            if (last_slash == parent_path) {
                /* File in root directory */
                parent = vfs_root;
            } else {
                *last_slash = '\0';
                parent = vfs_lookup(parent_path);
            }
        }

        printk("sys_open: parent=%p filename='%s'\n", parent, filename ? filename : "(null)");

        if (parent != NULL && filename != NULL && filename[0] != '\0') {
            node = ramfs_create_file_in(parent, filename, 0);
            printk("sys_open: created node=%p\n", node);
        }
    }

    if (node == NULL) {
        return -1;  /* File not found and couldn't create */
    }

    /* Find a free file descriptor slot */
    int fd = -1;
    for (int i = 3; i < MAX_OPEN_FILES; i++) {  /* Skip stdin/stdout/stderr */
        if (proc->fd_table[i] == NULL) {
            fd = i;
            break;
        }
    }

    if (fd < 0) {
        return -1;  /* No free file descriptors */
    }

    /* Allocate file descriptor structure */
    file_descriptor_t* file_desc = (file_descriptor_t*)kmalloc(sizeof(file_descriptor_t));
    if (file_desc == NULL) {
        return -1;
    }

    /* Initialize file descriptor */
    file_desc->node = node;
    file_desc->offset = 0;
    file_desc->flags = flags;
    file_desc->refcount = 1;

    /* Open the file */
    if (vfs_open(node, flags) < 0) {
        kfree(file_desc);
        return -1;
    }

    /* Store in process fd table */
    proc->fd_table[fd] = (struct file_descriptor*)file_desc;

    return fd;
}

/*
 * sys_close - close a file descriptor
 */
int32_t sys_close(uint32_t fd, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5) {
    (void)a2; (void)a3; (void)a4; (void)a5;

    process_t* proc = process_current();

    if (proc == NULL || fd >= MAX_OPEN_FILES) {
        return -1;
    }

    /* Don't allow closing stdin/stdout/stderr for now */
    if (fd < 3) {
        return -1;
    }

    file_descriptor_t* file_desc = (file_descriptor_t*)proc->fd_table[fd];
    if (file_desc == NULL) {
        return -1;  /* Not open */
    }

    /* Close the file */
    vfs_close(file_desc->node);

    /* Free the file descriptor */
    kfree(file_desc);
    proc->fd_table[fd] = NULL;

    return 0;
}

/*
 * sys_sbrk - Increment program break (heap allocation)
 * Returns old break on success, -1 on failure
 */
int32_t sys_sbrk(uint32_t increment, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5) {
    (void)a2; (void)a3; (void)a4; (void)a5;

    process_t* proc = process_current();
    if (proc == NULL) {
        return -1;
    }

    /* Initialize heap if not set */
    if (proc->heap_break == 0) {
        proc->heap_start = USER_HEAP_START;
        proc->heap_break = USER_HEAP_START;
    }

    uint32_t old_break = proc->heap_break;
    int32_t inc = (int32_t)increment;

    /* Calculate new break */
    uint32_t new_break = old_break + inc;

    /* Check bounds */
    if (new_break < proc->heap_start || new_break >= USER_HEAP_MAX) {
        return -1;
    }

    /* For now, just update the break pointer.
     * In a full implementation, we would:
     * 1. Allocate physical pages for the new heap region
     * 2. Map them into the process's page table
     * 3. Zero the new memory
     */
    proc->heap_break = new_break;

    return (int32_t)old_break;
}

/*
 * sys_fork - Fork current process
 * Returns child PID to parent, 0 to child, -1 on error
 */
int32_t sys_fork(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;

    return process_fork();
}

/*
 * sys_exec - Execute a new program
 * path: path to executable
 * argv: argument vector (currently ignored)
 */
int32_t sys_exec(uint32_t path, uint32_t argv, uint32_t a3, uint32_t a4, uint32_t a5) {
    (void)a3; (void)a4; (void)a5;

    /* Safely copy path from user space */
    char pathname[256];
    if (copyinstr(pathname, (const char*)path, sizeof(pathname)) < 0) {
        return -1;  /* Bad path pointer */
    }

    char* const* argvec = (char* const*)argv;

    return process_exec(pathname, argvec);
}

/*
 * sys_wait - Wait for child process to exit
 * status_ptr: pointer to store exit status (can be NULL)
 */
int32_t sys_wait(uint32_t status_ptr, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5) {
    (void)a2; (void)a3; (void)a4; (void)a5;

    int* status = NULL;

    /* Validate status pointer if provided */
    if (status_ptr != 0) {
        if (!access_ok((void*)status_ptr, sizeof(int), true)) {
            return -1;  /* Bad status pointer */
        }
        status = (int*)status_ptr;
    }

    return process_wait(status);
}

/*
 * sys_readdir - Read directory entries
 * path: directory path
 * buf: buffer to store entries (array of dirent structures)
 * count: max number of entries to read
 * Returns number of entries read, -1 on error
 */
int32_t sys_readdir(uint32_t path_ptr, uint32_t buf_ptr, uint32_t count, uint32_t a4, uint32_t a5) {
    (void)a4; (void)a5;

    const char* path = (const char*)path_ptr;
    dirent_t* entries = (dirent_t*)buf_ptr;

    /* Always use vfs_lookup for consistency - don't use cached current_dir_node */
    vfs_node_t* dir = NULL;
    char fullpath[256];

    if (strcmp(path, ".") == 0 || strcmp(path, "") == 0) {
        /* Current directory - use current_dir string path */
        if (strcmp(current_dir, "/") == 0) {
            dir = vfs_lookup("/");
        } else {
            dir = vfs_lookup(current_dir);
        }
    } else if (path[0] == '/') {
        /* Absolute path */
        dir = vfs_lookup(path);
    } else {
        /* Relative path - build full path */
        if (strcmp(current_dir, "/") == 0) {
            snprintf(fullpath, sizeof(fullpath), "/%s", path);
        } else {
            snprintf(fullpath, sizeof(fullpath), "%s/%s", current_dir, path);
        }
        dir = vfs_lookup(fullpath);
    }

    printk("sys_readdir: path='%s' current_dir='%s' dir=%p vfs_root=%p\n", path, current_dir, dir, vfs_root);

    if (dir == NULL || !(dir->flags & VFS_DIRECTORY)) {
        return -1;
    }

    /* Read entries */
    uint32_t i = 0;
    for (i = 0; i < count; i++) {
        dirent_t* entry = vfs_readdir(dir, i);
        if (entry == NULL) break;

        /* Copy entry to user buffer */
        strncpy(entries[i].name, entry->name, VFS_MAX_NAME - 1);
        entries[i].name[VFS_MAX_NAME - 1] = '\0';
        entries[i].inode = entry->inode;
    }

    return (int32_t)i;
}

/*
 * sys_stat - Get file information
 * path: file path
 * buf: buffer to store stat info (we'll use a simple format)
 * Returns 0 on success, -1 on error
 */
int32_t sys_stat(uint32_t path_ptr, uint32_t buf_ptr, uint32_t a3, uint32_t a4, uint32_t a5) {
    (void)a3; (void)a4; (void)a5;

    const char* path = (const char*)path_ptr;
    uint32_t* buf = (uint32_t*)buf_ptr;

    vfs_node_t* node = vfs_lookup(path);
    if (node == NULL) {
        return -1;
    }

    /* Simple stat structure: flags, size, uid, gid, mode */
    buf[0] = node->flags;
    buf[1] = node->length;
    buf[2] = node->uid;
    buf[3] = node->gid;
    buf[4] = node->mode;

    return 0;
}

/*
 * sys_chdir - Change working directory
 * path: new directory path
 * Returns 0 on success, -1 on error
 */
int32_t sys_chdir(uint32_t path_ptr, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5) {
    (void)a2; (void)a3; (void)a4; (void)a5;

    const char* path = (const char*)path_ptr;
    char fullpath[256];
    vfs_node_t* node = NULL;

    /* Handle special paths */
    if (strcmp(path, ".") == 0) {
        /* Stay in current directory */
        return 0;
    } else if (strcmp(path, "..") == 0) {
        /* Go to parent directory */
        if (strcmp(current_dir, "/") == 0) {
            return 0;  /* Already at root */
        }
        /* Find last slash and truncate */
        strncpy(fullpath, current_dir, sizeof(fullpath) - 1);
        fullpath[sizeof(fullpath) - 1] = '\0';
        char* last_slash = strrchr(fullpath, '/');
        if (last_slash != NULL && last_slash != fullpath) {
            *last_slash = '\0';
        } else {
            strcpy(fullpath, "/");
        }
        node = vfs_lookup(fullpath);
    } else if (path[0] == '/') {
        /* Absolute path */
        strncpy(fullpath, path, sizeof(fullpath) - 1);
        fullpath[sizeof(fullpath) - 1] = '\0';
        node = vfs_lookup(path);
    } else {
        /* Relative path */
        if (strcmp(current_dir, "/") == 0) {
            snprintf(fullpath, sizeof(fullpath), "/%s", path);
        } else {
            snprintf(fullpath, sizeof(fullpath), "%s/%s", current_dir, path);
        }
        node = vfs_lookup(fullpath);
    }

    if (node == NULL || !(node->flags & VFS_DIRECTORY)) {
        return -1;
    }

    /* Update current directory */
    current_dir_node = node;
    strncpy(current_dir, fullpath, VFS_MAX_PATH - 1);
    current_dir[VFS_MAX_PATH - 1] = '\0';

    return 0;
}

/*
 * sys_getcwd - Get current working directory
 * buf: buffer to store path
 * size: buffer size
 * Returns 0 on success, -1 on error
 */
int32_t sys_getcwd(uint32_t buf_ptr, uint32_t size, uint32_t a3, uint32_t a4, uint32_t a5) {
    (void)a3; (void)a4; (void)a5;

    char* buf = (char*)buf_ptr;

    if (size == 0 || buf == NULL) {
        return -1;
    }

    strncpy(buf, current_dir, size - 1);
    buf[size - 1] = '\0';

    return 0;
}

/*
 * sys_getuid - Get current user ID
 * Returns current user ID
 */
int32_t sys_getuid(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
    return (int32_t)current_uid;
}

/*
 * sys_ps - Get process list
 * buf: buffer to store process info (simple format)
 * count: max entries
 * Returns number of processes
 */
int32_t sys_ps(uint32_t buf_ptr, uint32_t count, uint32_t a3, uint32_t a4, uint32_t a5) {
    (void)a3; (void)a4; (void)a5;

    /* Process info: pid (4), state (4), name (24) = 32 bytes per entry */
    uint8_t* buf = (uint8_t*)buf_ptr;
    uint32_t num_procs = 0;

    for (uint32_t i = 0; i < MAX_PROCESSES && num_procs < count; i++) {
        if (process_table[i].pid > 0) {
            uint32_t* entry = (uint32_t*)(buf + num_procs * 32);
            entry[0] = process_table[i].pid;
            entry[1] = (uint32_t)process_table[i].state;
            strncpy((char*)&entry[2], process_table[i].name, 23);
            ((char*)&entry[2])[23] = '\0';
            num_procs++;
        }
    }

    return (int32_t)num_procs;
}

/*
 * sys_uptime - Get system uptime in ticks
 * Returns uptime in timer ticks
 */
int32_t sys_uptime(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
    return (int32_t)timer_get_ticks();
}

/*
 * sys_uname - Get system information
 * buf: buffer to store system info (5 strings of 32 bytes each)
 * Returns 0 on success
 */
int32_t sys_uname(uint32_t buf_ptr, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5) {
    (void)a2; (void)a3; (void)a4; (void)a5;

    char* buf = (char*)buf_ptr;

    /* sysname */
    strncpy(buf, "MiniOS", 31);
    buf[31] = '\0';
    /* nodename */
    strncpy(buf + 32, "minios", 31);
    buf[63] = '\0';
    /* release */
    strncpy(buf + 64, "0.3.0", 31);
    buf[95] = '\0';
    /* version */
    strncpy(buf + 96, "Stage3", 31);
    buf[127] = '\0';
    /* machine */
    strncpy(buf + 128, "i686", 31);
    buf[159] = '\0';

    return 0;
}

/*
 * sys_scroll - Scroll the VGA display
 * direction: 0 = up (show older), 1 = down (show newer), 2 = to bottom
 * lines: number of lines to scroll
 */
int32_t sys_scroll(uint32_t direction, uint32_t lines, uint32_t a3, uint32_t a4, uint32_t a5) {
    (void)a3; (void)a4; (void)a5;

    if (direction == 0) {
        vga_scroll_up(lines);
    } else if (direction == 1) {
        vga_scroll_down(lines);
    } else if (direction == 2) {
        vga_scroll_to_bottom();
    }
    return 0;
}

/*
 * sys_mkdir - Create a directory
 */
int32_t sys_mkdir(uint32_t path_ptr, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5) {
    (void)a2; (void)a3; (void)a4; (void)a5;

    const char* path = (const char*)path_ptr;

    /* Build full path for relative paths */
    char fullpath[256];
    const char* lookup_path = path;
    vfs_node_t* parent = NULL;
    const char* dirname = NULL;

    if (path[0] == '/') {
        /* Absolute path */
        strncpy(fullpath, path, sizeof(fullpath) - 1);
        fullpath[sizeof(fullpath) - 1] = '\0';
        lookup_path = fullpath;
    } else {
        /* Relative path - build full path */
        if (strcmp(current_dir, "/") == 0) {
            snprintf(fullpath, sizeof(fullpath), "/%s", path);
        } else {
            snprintf(fullpath, sizeof(fullpath), "%s/%s", current_dir, path);
        }
        lookup_path = fullpath;
    }

    /* Extract parent directory and dirname */
    char parent_path[256];
    strncpy(parent_path, lookup_path, sizeof(parent_path) - 1);
    parent_path[sizeof(parent_path) - 1] = '\0';

    char* last_slash = strrchr(parent_path, '/');
    if (last_slash != NULL) {
        dirname = last_slash + 1;
        if (last_slash == parent_path) {
            /* Directory in root */
            parent = vfs_root;
        } else {
            *last_slash = '\0';
            parent = vfs_lookup(parent_path);
        }
    }

    if (parent == NULL || dirname == NULL || dirname[0] == '\0') {
        return -1;
    }

    if (ramfs_create_dir_in(parent, dirname) == NULL) {
        return -1;
    }
    return 0;
}

/*
 * sys_unlink - Remove a file
 */
int32_t sys_unlink(uint32_t path_ptr, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5) {
    (void)a2; (void)a3; (void)a4; (void)a5;

    const char* path = (const char*)path_ptr;
    if (!path) return -1;

    /* Find the file */
    vfs_node_t* node = vfs_lookup(path);
    if (!node) return -1;  /* File not found */

    /* Find parent directory */
    vfs_node_t* parent = node->parent;
    if (!parent) {
        /* Can't unlink root or file without parent */
        return -1;
    }

    /* Extract filename from path */
    const char* filename = path;
    const char* last_slash = NULL;
    for (const char* p = path; *p; p++) {
        if (*p == '/') last_slash = p;
    }
    if (last_slash) filename = last_slash + 1;

    /* Check if it's an ext2 filesystem by checking function pointer */
    /* This is a simple check - ext2 directories have ext2_vfs_readdir */
    if (parent->readdir == ext2_vfs_readdir) {
        /* ext2 filesystem - use ext2_unlink */
        if (ext2_unlink(parent, filename)) {
            return 0;
        }
        return -1;
    }

    /* Not an ext2 filesystem or deletion failed */
    return -1;
}

/*
 * sys_fread - Read from file descriptor
 * Returns: number of bytes read, 0 on EOF, -1 on error
 */
int32_t sys_fread(uint32_t fd, uint32_t buf_ptr, uint32_t count, uint32_t a4, uint32_t a5) {
    (void)a4; (void)a5;

    process_t* proc = process_current();
    if (proc == NULL || buf_ptr == 0 || count == 0) {
        return -1;
    }

    if (fd >= MAX_OPEN_FILES) {
        return -1;
    }

    file_descriptor_t* file_desc = (file_descriptor_t*)proc->fd_table[fd];
    if (file_desc == NULL) {
        return -1;  /* Not open */
    }

    uint8_t* buffer = (uint8_t*)buf_ptr;
    int32_t bytes_read = 0;

    /* Check if this is a pipe */
    if (file_desc->is_pipe && file_desc->pipe) {
        if (!file_desc->is_read_end) {
            return -1;  /* Can't read from write end */
        }
        bytes_read = pipe_read(file_desc->pipe, buffer, count);
    } else if (file_desc->node) {
        /* Regular file - read from VFS */
        bytes_read = vfs_read(file_desc->node, file_desc->offset, count, buffer);
        if (bytes_read > 0) {
            file_desc->offset += bytes_read;
        }
    } else {
        return -1;  /* Invalid descriptor */
    }

    return bytes_read;
}

/*
 * sys_fwrite - Write to file descriptor
 * Returns: number of bytes written, -1 on error
 */
int32_t sys_fwrite(uint32_t fd, uint32_t buf_ptr, uint32_t count, uint32_t a4, uint32_t a5) {
    (void)a4; (void)a5;

    process_t* proc = process_current();
    if (proc == NULL || buf_ptr == 0 || count == 0) {
        return -1;
    }

    if (fd >= MAX_OPEN_FILES) {
        return -1;
    }

    file_descriptor_t* file_desc = (file_descriptor_t*)proc->fd_table[fd];
    if (file_desc == NULL) {
        return -1;  /* Not open */
    }

    uint8_t* buffer = (uint8_t*)buf_ptr;
    int32_t bytes_written = 0;

    /* Check if this is a pipe */
    if (file_desc->is_pipe && file_desc->pipe) {
        if (file_desc->is_read_end) {
            return -1;  /* Can't write to read end */
        }
        bytes_written = pipe_write(file_desc->pipe, buffer, count);
    } else if (file_desc->node) {
        /* Regular file - write to VFS */
        bytes_written = vfs_write(file_desc->node, file_desc->offset, count, buffer);
        if (bytes_written > 0) {
            file_desc->offset += bytes_written;
        }
    } else {
        return -1;  /* Invalid descriptor */
    }

    return bytes_written;
}

/*
 * sys_meminfo - Get memory information
 * buf: array of 4 uint32_t: [total, used, free, heap_used]
 */
int32_t sys_meminfo(uint32_t buf_ptr, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5) {
    (void)a2; (void)a3; (void)a4; (void)a5;

    uint32_t* buf = (uint32_t*)buf_ptr;

    /* Use pmm_get_stats and heap_get_stats to get information */
    pmm_stats_t pmm_stats;
    heap_stats_t heap_stats;
    pmm_get_stats(&pmm_stats);
    heap_get_stats(&heap_stats);

    buf[0] = pmm_stats.total_memory;
    buf[1] = pmm_stats.total_memory - pmm_stats.free_memory;
    buf[2] = pmm_stats.free_memory;
    buf[3] = heap_stats.used_size;

    return 0;
}

/*
 * sys_date - Get current date/time (simulated)
 * buf: array of 6 uint32_t: [year, month, day, hour, min, sec]
 */
int32_t sys_date(uint32_t buf_ptr, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5) {
    (void)a2; (void)a3; (void)a4; (void)a5;

    uint32_t* buf = (uint32_t*)buf_ptr;

    /* Simulated date based on uptime */
    uint32_t ticks = timer_get_ticks();
    uint32_t seconds = ticks / 100;

    buf[0] = 2026;  /* year */
    buf[1] = 1;     /* month */
    buf[2] = 31;    /* day */
    buf[3] = (seconds / 3600) % 24;  /* hour */
    buf[4] = (seconds / 60) % 60;    /* minute */
    buf[5] = seconds % 60;           /* second */

    return 0;
}

/*
 * sys_pipe - Create a pipe
 * pipefd: array of 2 integers to store read and write file descriptors
 */
int32_t sys_pipe(uint32_t pipefd_ptr, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5) {
    (void)a2; (void)a3; (void)a4; (void)a5;

    int32_t* pipefd = (int32_t*)pipefd_ptr;
    if (!pipefd) return -1;

    /* Create pipe */
    pipe_t* pipe = pipe_create();
    if (!pipe) return -1;

    /* Allocate file descriptors for read and write ends */
    int32_t read_fd = fd_alloc();
    if (read_fd < 0) {
        pipe_destroy(pipe);
        return -1;
    }

    int32_t write_fd = fd_alloc();
    if (write_fd < 0) {
        fd_free(read_fd);
        pipe_destroy(pipe);
        return -1;
    }

    /* Set up read end */
    file_descriptor_t* read_desc = fd_get(read_fd);
    read_desc->pipe = pipe;
    read_desc->is_pipe = true;
    read_desc->is_read_end = true;
    read_desc->flags = O_RDONLY;
    pipe->readers++;

    /* Set up write end */
    file_descriptor_t* write_desc = fd_get(write_fd);
    write_desc->pipe = pipe;
    write_desc->is_pipe = true;
    write_desc->is_read_end = false;
    write_desc->flags = O_WRONLY;
    pipe->writers++;

    /* Return file descriptors */
    pipefd[0] = read_fd;
    pipefd[1] = write_fd;

    return 0;
}

/*
 * sys_dup2 - Duplicate file descriptor
 */
int32_t sys_dup2(uint32_t oldfd, uint32_t newfd, uint32_t a3, uint32_t a4, uint32_t a5) {
    (void)a3; (void)a4; (void)a5;

    return fd_dup2(oldfd, newfd);
}

/*
 * Send a signal to a process
 * SYS_KILL(pid, sig)
 */
int32_t sys_kill(uint32_t pid, uint32_t sig, uint32_t a3, uint32_t a4, uint32_t a5) {
    (void)a3; (void)a4; (void)a5;

    /* Check signal validity */
    if (sig >= NSIG) {
        return -1;  /* Invalid signal */
    }

    /* Check if process exists */
    process_t* target = process_get(pid);
    if (!target || target->state == PROCESS_STATE_UNUSED) {
        return -1;  /* Process not found */
    }

    /* Permission checking: sender must be root or owner of target */
    process_t* sender = process_current();
    if (sender) {
        /* Root (UID 0) can signal any process */
        /* Otherwise, must own the target process */
        if (sender->uid != 0 && sender->uid != target->uid) {
            return -1;  /* Permission denied */
        }
    }

    /* Send the signal */
    return signal_send(pid, sig);
}

/*
 * Set a signal handler
 * SYS_SIGNAL(signum, handler)
 */
int32_t sys_signal(uint32_t signum, uint32_t handler, uint32_t a3, uint32_t a4, uint32_t a5) {
    (void)a3; (void)a4; (void)a5;

    /* Check signal validity */
    if (signum >= NSIG) {
        return -1;  /* Invalid signal */
    }

    /* Get current process */
    process_t* proc = process_current();
    if (!proc) {
        return -1;
    }

    /* Set the handler */
    signal_handler_t old_handler = proc->signal_handlers[signum];

    int result = signal_handle(proc->pid, signum, (signal_handler_t)handler);
    if (result == 0) {
        /* Return old handler (as integer) */
        return (int32_t)old_handler;
    }

    return -1;
}

/*
 * Register a system call handler
 */
void syscall_register(uint32_t num, syscall_handler_t handler) {
    if (num < NUM_SYSCALLS) {
        syscall_table[num] = handler;
    }
}

/*
 * Initialize system call interface
 */
void syscall_init(void) {
    /* Clear syscall table */
    for (int i = 0; i < NUM_SYSCALLS; i++) {
        syscall_table[i] = NULL;
    }

    /* Register built-in syscalls */
    syscall_register(SYS_EXIT, sys_exit);
    syscall_register(SYS_WRITE, sys_write);
    syscall_register(SYS_READ, sys_read);
    syscall_register(SYS_GETPID, sys_getpid);
    syscall_register(SYS_FORK, sys_fork);
    syscall_register(SYS_EXEC, sys_exec);
    syscall_register(SYS_WAIT, sys_wait);
    syscall_register(SYS_YIELD, sys_yield);
    syscall_register(SYS_SLEEP, sys_sleep);
    syscall_register(SYS_OPEN, sys_open);
    syscall_register(SYS_CLOSE, sys_close);
    syscall_register(SYS_SBRK, sys_sbrk);
    syscall_register(SYS_READDIR, sys_readdir);
    syscall_register(SYS_STAT, sys_stat);
    syscall_register(SYS_CHDIR, sys_chdir);
    syscall_register(SYS_GETCWD, sys_getcwd);
    syscall_register(SYS_GETUID, sys_getuid);
    syscall_register(SYS_PS, sys_ps);
    syscall_register(SYS_UPTIME, sys_uptime);
    syscall_register(SYS_UNAME, sys_uname);
    syscall_register(SYS_SCROLL, sys_scroll);
    syscall_register(SYS_MKDIR, sys_mkdir);
    syscall_register(SYS_UNLINK, sys_unlink);
    syscall_register(SYS_FREAD, sys_fread);
    syscall_register(SYS_FWRITE, sys_fwrite);
    syscall_register(SYS_MEMINFO, sys_meminfo);
    syscall_register(SYS_DATE, sys_date);
    syscall_register(SYS_PIPE, sys_pipe);
    syscall_register(SYS_DUP2, sys_dup2);
    syscall_register(SYS_KILL, sys_kill);
    syscall_register(SYS_SIGNAL, sys_signal);

    /* Initialize file descriptor table */
    fd_init();

    /* Register INT 0x80 handler */
    /* Note: We need to set this up with ring 3 access so user mode can call it */
    extern void isr128(void);  /* Defined in interrupts.asm */
    extern void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags);

    idt_set_gate(0x80, (uint32_t)isr128, GDT_KERNEL_CODE,
                 IDT_FLAG_PRESENT | IDT_FLAG_RING3 | IDT_GATE_INT32);

    /* Register the C handler for ISR 128 */
    isr_register_handler(0x80, syscall_handler);

    printk("Syscall: Initialized (%d syscalls)\n", NUM_SYSCALLS);
}

