/*
 * MiniOS System Daemon Implementation
 * 
 * Manages background system daemons.
 */

#include "../include/daemon.h"
#include "../include/process.h"
#include "../include/scheduler.h"
#include "../include/timer.h"
#include "../include/string.h"
#include "../include/stdio.h"
#include "../include/vga.h"
#include "../include/heap.h"

/* Daemon registry */
daemon_t daemons[MAX_DAEMONS];
int num_daemons = 0;

/* Kernel log buffer */
#define KLOG_BUFFER_SIZE 4096
static char klog_buffer[KLOG_BUFFER_SIZE];
static uint32_t klog_write_pos = 0;
static uint32_t klog_read_pos = 0;

/*
 * Initialize daemon subsystem
 */
void daemon_init(void) {
    memset(daemons, 0, sizeof(daemons));
    num_daemons = 0;
    
    /* Register built-in daemons */
    daemon_register("init", "System init process", init_daemon);
    daemon_register("klogd", "Kernel log daemon", klogd_daemon);
    daemon_register("crond", "Task scheduler daemon", crond_daemon);
    daemon_register("kswapd", "Memory management daemon", kswapd_daemon);
    
    printk("Daemon: Subsystem initialized (%d daemons registered)\n", num_daemons);
}

/*
 * Register a daemon
 */
int daemon_register(const char* name, const char* description, void (*entry)(void)) {
    if (num_daemons >= MAX_DAEMONS) {
        return -1;
    }
    
    daemon_t* d = &daemons[num_daemons];
    d->name = name;
    d->description = description;
    d->entry = entry;
    d->pid = -1;
    d->state = DAEMON_STOPPED;
    d->restart_count = 0;
    d->start_time = 0;
    
    return num_daemons++;
}

/*
 * Get daemon by name
 */
daemon_t* daemon_get(const char* name) {
    for (int i = 0; i < num_daemons; i++) {
        if (strcmp(daemons[i].name, name) == 0) {
            return &daemons[i];
        }
    }
    return NULL;
}

/*
 * Get daemon by PID
 */
daemon_t* daemon_get_by_pid(int pid) {
    for (int i = 0; i < num_daemons; i++) {
        if (daemons[i].pid == pid) {
            return &daemons[i];
        }
    }
    return NULL;
}

/*
 * Start a daemon by name
 */
int daemon_start(const char* name) {
    daemon_t* d = daemon_get(name);
    if (d == NULL) {
        return -1;
    }
    
    if (d->state == DAEMON_RUNNING) {
        return 0;  /* Already running */
    }
    
    d->state = DAEMON_STARTING;
    
    /* Create kernel process for daemon */
    int pid = process_create(d->entry, d->name);
    if (pid < 0) {
        d->state = DAEMON_FAILED;
        return -1;
    }
    
    d->pid = pid;
    d->state = DAEMON_RUNNING;
    d->start_time = timer_get_ticks();
    
    return 0;
}

/*
 * Stop a daemon by name
 */
int daemon_stop(const char* name) {
    daemon_t* d = daemon_get(name);
    if (d == NULL || d->pid < 0) {
        return -1;
    }
    
    d->state = DAEMON_STOPPING;
    
    /* Terminate the process */
    process_t* proc = process_get(d->pid);
    if (proc != NULL) {
        process_terminate(proc, 0);
    }
    
    d->pid = -1;
    d->state = DAEMON_STOPPED;
    
    return 0;
}

/*
 * Restart a daemon
 */
int daemon_restart(const char* name) {
    daemon_stop(name);
    return daemon_start(name);
}

/*
 * Start all registered daemons
 */
void daemon_start_all(void) {
    printk("Daemon: Starting all daemons...\n");
    for (int i = 0; i < num_daemons; i++) {
        if (daemon_start(daemons[i].name) == 0) {
            printk("  [OK] %s (PID %d)\n", daemons[i].name, daemons[i].pid);
        } else {
            printk("  [FAIL] %s\n", daemons[i].name);
        }
    }
}

/*
 * Print daemon status
 */
void daemon_print_status(void) {
    vga_puts("System Daemons:\n");
    vga_puts("  NAME       PID    STATE       DESCRIPTION\n");
    vga_puts("  ----       ---    -----       -----------\n");

    for (int i = 0; i < num_daemons; i++) {
        daemon_t* d = &daemons[i];

        /* Name (10 chars) */
        printk("  %-10s ", d->name);

        /* PID (6 chars) */
        if (d->pid >= 0) {
            printk("%-6d ", d->pid);
        } else {
            printk("-      ");
        }

        /* State */
        const char* state_str;
        switch (d->state) {
            case DAEMON_STOPPED:  state_str = "stopped"; break;
            case DAEMON_STARTING: state_str = "starting"; break;
            case DAEMON_RUNNING:  state_str = "running"; break;
            case DAEMON_STOPPING: state_str = "stopping"; break;
            case DAEMON_FAILED:   state_str = "failed"; break;
            default:              state_str = "unknown"; break;
        }
        printk("%-11s ", state_str);

        /* Description */
        printk("%s\n", d->description);
    }
}

/*
 * Add message to kernel log
 */
void klog_write(const char* msg) {
    while (*msg && klog_write_pos < KLOG_BUFFER_SIZE - 1) {
        klog_buffer[klog_write_pos++] = *msg++;
    }
}

/*
 * Read from kernel log
 */
int klog_read(char* buf, int size) {
    int count = 0;
    while (klog_read_pos < klog_write_pos && count < size - 1) {
        buf[count++] = klog_buffer[klog_read_pos++];
    }
    buf[count] = '\0';
    return count;
}

/* ============================================
 * Daemon Entry Points
 * ============================================ */

/*
 * init daemon - System initialization process (PID 1)
 * Responsible for system startup and orphan process reaping
 */
void init_daemon(void) {
    /* Init daemon main loop */
    while (1) {
        /* Check for zombie processes and reap them */
        for (int i = 0; i < MAX_PROCESSES; i++) {
            process_t* proc = &process_table[i];
            if (proc->state == PROCESS_STATE_ZOMBIE && proc->ppid == 1) {
                /* Reap orphaned zombie */
                proc->state = PROCESS_STATE_UNUSED;
            }
        }

        /* Sleep for a bit */
        yield();
    }
}

/*
 * klogd daemon - Kernel log daemon
 * Manages kernel message logging
 */
void klogd_daemon(void) {
    /* Log daemon main loop */
    while (1) {
        /* In a real OS, this would write logs to disk */
        /* For now, just yield and let other processes run */
        yield();
    }
}

/*
 * crond daemon - Cron/task scheduler daemon
 * Runs scheduled tasks (simplified implementation)
 */
void crond_daemon(void) {
    uint32_t last_minute = 0;

    while (1) {
        /* Check every ~60 seconds (6000 ticks at 100Hz) */
        uint32_t current_ticks = timer_get_ticks();
        uint32_t current_minute = current_ticks / 6000;

        if (current_minute > last_minute) {
            last_minute = current_minute;
            /* In a real OS, check crontab and run scheduled tasks */
        }

        yield();
    }
}

/*
 * kswapd daemon - Memory management daemon
 * Handles memory pressure and page swapping (placeholder)
 */
void kswapd_daemon(void) {
    while (1) {
        /* In a real OS, this would:
         * - Monitor memory pressure
         * - Swap out pages when memory is low
         * - Manage page cache
         */
        yield();
    }
}

