/*
 * MiniOS Signal Implementation
 *
 * Unix-style signals for process control and inter-process communication.
 */

#ifndef _SIGNAL_H
#define _SIGNAL_H

#include "types.h"

/* Signal numbers (POSIX-compatible subset) */
#define SIGHUP      1   /* Hangup */
#define SIGINT      2   /* Interrupt (Ctrl+C) */
#define SIGQUIT     3   /* Quit */
#define SIGILL      4   /* Illegal instruction */
#define SIGTRAP     5   /* Trace trap */
#define SIGABRT     6   /* Abort */
#define SIGBUS      7   /* Bus error */
#define SIGFPE      8   /* Floating point exception */
#define SIGKILL     9   /* Kill (cannot be caught) */
#define SIGUSR1     10  /* User-defined signal 1 */
#define SIGSEGV     11  /* Segmentation fault */
#define SIGUSR2     12  /* User-defined signal 2 */
#define SIGPIPE     13  /* Broken pipe */
#define SIGALRM     14  /* Alarm clock */
#define SIGTERM     15  /* Termination */
#define SIGCHLD     17  /* Child process status changed */
#define SIGCONT     18  /* Continue if stopped */
#define SIGSTOP     19  /* Stop (cannot be caught) */
#define SIGTSTP     20  /* Terminal stop (Ctrl+Z) */
#define SIGTTIN     21  /* Background read from terminal */
#define SIGTTOU     22  /* Background write to terminal */

#define NSIG        32  /* Number of signals */

/* Signal handler types */
#define SIG_DFL     ((signal_handler_t)0)  /* Default action */
#define SIG_IGN     ((signal_handler_t)1)  /* Ignore signal */

/* Signal handler function type */
typedef void (*signal_handler_t)(int);

/* Signal action structure */
typedef struct sigaction {
    signal_handler_t sa_handler;    /* Signal handler function */
    uint32_t sa_mask;               /* Signals to block during handler */
    uint32_t sa_flags;              /* Signal flags */
} sigaction_t;

/* Forward declaration */
struct process;

/* Signal functions (kernel-internal) */
void signal_init(void);
int signal_send(uint32_t pid, int signum);
int signal_handle(uint32_t pid, int signum, signal_handler_t handler);
void signal_deliver(void);
void signal_deliver_to(struct process* proc);
bool signal_pending(uint32_t pid, int signum);
void signal_init_process(struct process* proc);

/* Default signal actions */
void signal_default_handler(int signum);

#endif /* _SIGNAL_H */
