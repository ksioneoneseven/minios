/*
 * MiniOS System Daemon Header
 * 
 * Defines system daemons that run as background kernel processes.
 */

#ifndef _DAEMON_H
#define _DAEMON_H

#include "types.h"
#include "process.h"

/* Maximum number of daemons */
#define MAX_DAEMONS 16

/* Daemon states */
typedef enum {
    DAEMON_STOPPED = 0,
    DAEMON_STARTING,
    DAEMON_RUNNING,
    DAEMON_STOPPING,
    DAEMON_FAILED
} daemon_state_t;

/* Daemon descriptor */
typedef struct daemon {
    const char* name;           /* Daemon name */
    const char* description;    /* Description */
    void (*entry)(void);        /* Entry point function */
    int pid;                    /* Process ID (-1 if not running) */
    daemon_state_t state;       /* Current state */
    uint32_t restart_count;     /* Number of restarts */
    uint32_t start_time;        /* Tick count when started */
} daemon_t;

/* Daemon registry */
extern daemon_t daemons[MAX_DAEMONS];
extern int num_daemons;

/*
 * Initialize daemon subsystem
 */
void daemon_init(void);

/*
 * Register a daemon
 * Returns daemon index or -1 on error
 */
int daemon_register(const char* name, const char* description, void (*entry)(void));

/*
 * Start a daemon by name
 * Returns 0 on success, -1 on error
 */
int daemon_start(const char* name);

/*
 * Stop a daemon by name
 * Returns 0 on success, -1 on error
 */
int daemon_stop(const char* name);

/*
 * Restart a daemon by name
 * Returns 0 on success, -1 on error
 */
int daemon_restart(const char* name);

/*
 * Get daemon by name
 * Returns daemon pointer or NULL
 */
daemon_t* daemon_get(const char* name);

/*
 * Get daemon by PID
 * Returns daemon pointer or NULL
 */
daemon_t* daemon_get_by_pid(int pid);

/*
 * Start all registered daemons
 */
void daemon_start_all(void);

/*
 * Print daemon status
 */
void daemon_print_status(void);

/*
 * Built-in daemon entry points
 */
void init_daemon(void);      /* PID 1 - init process */
void klogd_daemon(void);     /* Kernel log daemon */
void crond_daemon(void);     /* Cron daemon */
void kswapd_daemon(void);    /* Memory management daemon */

#endif /* _DAEMON_H */

