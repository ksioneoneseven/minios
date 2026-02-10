/*
 * MiniOS Signal Implementation
 */

#include "../include/signal.h"
#include "../include/process.h"
#include "../include/string.h"
#include "../include/stdio.h"

/*
 * Initialize signal subsystem
 */
void signal_init(void) {
    /* Signal handlers are initialized per-process in process_init */
}

/*
 * Default signal handler - performs default action for signal
 */
void signal_default_handler(int signum) {
    process_t* proc = process_current();
    if (!proc) return;

    switch (signum) {
        case SIGCHLD:
        case SIGCONT:
            /* Ignore by default */
            break;

        case SIGSTOP:
        case SIGTSTP:
            /* Stop process */
            proc->state = PROCESS_STATE_BLOCKED;
            break;

        case SIGKILL:
        case SIGTERM:
        case SIGINT:
        case SIGQUIT:
        case SIGHUP:
        case SIGABRT:
        case SIGSEGV:
        case SIGILL:
        case SIGFPE:
        case SIGBUS:
        case SIGPIPE:
            /* Terminate process */
            printk("Process %d (%s) terminated by signal %d\n",
                   proc->pid, proc->name, signum);
            process_exit(128 + signum);
            break;

        default:
            /* Unknown signal - ignore */
            break;
    }
}

/*
 * Send a signal to a process
 * Returns: 0 on success, -1 on error
 */
int signal_send(uint32_t pid, int signum) {
    if (signum < 1 || signum >= NSIG) {
        return -1;  /* Invalid signal */
    }

    process_t* proc = process_get(pid);
    if (!proc || proc->state == PROCESS_STATE_UNUSED) {
        return -1;  /* Process not found */
    }

    /* Check if signal is blocked */
    if (proc->blocked_signals & (1 << signum)) {
        /* Signal is blocked, just mark it pending */
        proc->pending_signals |= (1 << signum);
        return 0;
    }

    /* SIGKILL and SIGSTOP cannot be caught or ignored */
    if (signum == SIGKILL || signum == SIGSTOP) {
        proc->pending_signals |= (1 << signum);
        /* Force delivery immediately */
        signal_deliver_to(proc);
        return 0;
    }

    /* Mark signal as pending */
    proc->pending_signals |= (1 << signum);

    /* If process is blocked, unblock it to handle signal */
    if (proc->state == PROCESS_STATE_BLOCKED) {
        process_unblock(proc);
    }

    return 0;
}

/*
 * Set signal handler for a specific signal
 * Returns: 0 on success, -1 on error
 */
int signal_handle(uint32_t pid, int signum, signal_handler_t handler) {
    if (signum < 1 || signum >= NSIG) {
        return -1;  /* Invalid signal */
    }

    /* SIGKILL and SIGSTOP cannot be caught */
    if (signum == SIGKILL || signum == SIGSTOP) {
        return -1;
    }

    process_t* proc = process_get(pid);
    if (!proc || proc->state == PROCESS_STATE_UNUSED) {
        return -1;  /* Process not found */
    }

    proc->signal_handlers[signum] = handler;
    return 0;
}

/*
 * Check if a specific signal is pending
 */
bool signal_pending(uint32_t pid, int signum) {
    if (signum < 1 || signum >= NSIG) {
        return false;
    }

    process_t* proc = process_get(pid);
    if (!proc || proc->state == PROCESS_STATE_UNUSED) {
        return false;
    }

    return (proc->pending_signals & (1 << signum)) != 0;
}

/*
 * Deliver pending signals to a specific process
 * Called before returning to user mode
 */
void signal_deliver_to(process_t* proc) {
    if (!proc || proc->state == PROCESS_STATE_UNUSED) {
        return;
    }

    /* Check each signal */
    for (int sig = 1; sig < NSIG; sig++) {
        /* Skip if not pending */
        if (!(proc->pending_signals & (1 << sig))) {
            continue;
        }

        /* Skip if blocked (unless SIGKILL/SIGSTOP) */
        if ((proc->blocked_signals & (1 << sig)) &&
            sig != SIGKILL && sig != SIGSTOP) {
            continue;
        }

        /* Clear pending bit */
        proc->pending_signals &= ~(1 << sig);

        /* Get handler */
        signal_handler_t handler = proc->signal_handlers[sig];

        /* Handle signal */
        if (handler == SIG_IGN) {
            /* Ignore signal */
            continue;
        } else if (handler == SIG_DFL) {
            /* Default action */
            signal_default_handler(sig);
        } else {
            /* User-defined handler */
            /* TODO: This requires setting up a proper user-mode signal frame */
            /* For now, just call the handler in kernel context */
            /* In a real OS, we would:
             * 1. Save current user context on user stack
             * 2. Set up return trampoline
             * 3. Set EIP to handler address
             * 4. Return to user mode to execute handler
             * 5. Handler calls sigreturn() to restore context
             */

            /* Simplified version: call handler if kernel mode */
            if (!proc->is_user_mode) {
                handler(sig);
            } else {
                /* For user mode, just do default action for now */
                signal_default_handler(sig);
            }
        }
    }
}

/*
 * Deliver pending signals to current process
 * Called from scheduler before returning to user mode
 */
void signal_deliver(void) {
    process_t* proc = process_current();
    if (proc) {
        signal_deliver_to(proc);
    }
}

/*
 * Initialize signal handlers for a new process
 * Called from process creation
 */
void signal_init_process(process_t* proc) {
    proc->pending_signals = 0;
    proc->blocked_signals = 0;

    /* Set all handlers to default */
    for (int i = 0; i < NSIG; i++) {
        proc->signal_handlers[i] = SIG_DFL;
    }
}
