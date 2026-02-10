/*
 * MiniOS Scheduler Implementation
 * 
 * Simple round-robin scheduler with timer preemption.
 */

#include "../include/scheduler.h"
#include "../include/process.h"
#include "../include/stdio.h"
#include "../include/string.h"

/* Scheduler state */
static bool scheduler_active = false;
static uint64_t total_switches = 0;
static uint64_t idle_ticks = 0;

/* Idle process - runs when nothing else is ready */
static void idle_process(void) {
    while (1) {
        /* Enable interrupts and halt until next interrupt */
        __asm__ volatile("sti; hlt");
        idle_ticks++;
    }
}

/*
 * Initialize the scheduler
 */
void scheduler_init(void) {
    scheduler_active = false;
    total_switches = 0;
    idle_ticks = 0;
    
    /* Initialize process subsystem */
    process_init();
    
    /* Create idle process (PID 0) */
    int idle_pid = process_create(idle_process, "idle");
    if (idle_pid < 0) {
        printk("Scheduler: Failed to create idle process!\n");
        return;
    }
    
    printk("Scheduler: Initialized\n");
}

/*
 * Get next process to run
 */
static process_t* get_next_process(void) {
    /* Round-robin: get first ready process that is not idle */
    process_t* p = ready_queue;
    while (p != NULL) {
        if (p->pid != 0) {
            return p;
        }
        p = p->next;
    }
    
    /* Only idle is ready, or queue is empty - return idle (PID 0) */
    if (ready_queue != NULL && ready_queue->pid == 0) {
        return ready_queue;
    }
    return process_get(0);
}

/*
 * Schedule next process
 */
void schedule(void) {
    if (!scheduler_active) {
        return;
    }
    
    process_t* prev = current_process;
    process_t* next = get_next_process();
    
    if (next == NULL) {
        /* Should never happen - idle is always available */
        printk("Scheduler: No process to run!\n");
        return;
    }
    
    if (prev == next) {
        /* Same process, no switch needed */
        return;
    }
    
    /* Update states */
    if (prev != NULL && prev->state == PROCESS_STATE_RUNNING) {
        prev->state = PROCESS_STATE_READY;
        /* Move to end of ready queue for round-robin */
        process_unready(prev);
        process_ready(prev);
    }
    
    /* Remove next from ready queue and mark as running */
    process_unready(next);
    next->state = PROCESS_STATE_RUNNING;
    next->time_slice = DEFAULT_TIME_SLICE;
    
    current_process = next;
    total_switches++;
    
    /* Perform context switch */
    if (prev != NULL) {
        switch_to(prev, next);
    } else {
        /* First switch - just jump to next */
        switch_to(next, next);
    }
}

/*
 * Yield CPU to next process
 */
void yield(void) {
    schedule();
}

/*
 * Timer tick handler
 */
void scheduler_tick(void) {
    if (!scheduler_active || current_process == NULL) {
        return;
    }
    
    current_process->total_ticks++;
    
    /* Decrement time slice */
    if (current_process->time_slice > 0) {
        current_process->time_slice--;
    }
    
    /* Preempt if time slice expired */
    if (current_process->time_slice == 0) {
        schedule();
    }
}

/*
 * Start the scheduler
 */
void scheduler_start(void) {
    printk("Scheduler: Starting...\n");
    
    scheduler_active = true;
    
    /* Get first process to run */
    process_t* first = get_next_process();
    if (first == NULL) {
        printk("Scheduler: No process to start!\n");
        return;
    }
    
    /* Remove from ready queue and mark as running */
    process_unready(first);
    first->state = PROCESS_STATE_RUNNING;
    first->time_slice = DEFAULT_TIME_SLICE;
    current_process = first;
    
    printk("Scheduler: Running '%s' (PID %d)\n", first->name, first->pid);
    
    /* Jump to first process */
    switch_to(NULL, first);
}

bool scheduler_running(void) {
    return scheduler_active;
}

void scheduler_disable(void) {
    scheduler_active = false;
}

void scheduler_enable(void) {
    scheduler_active = true;
}

void scheduler_get_stats(scheduler_stats_t* stats) {
    stats->total_switches = total_switches;
    stats->idle_ticks = idle_ticks;

    /* Count ready processes */
    uint32_t count = 0;
    process_t* p = ready_queue;
    while (p != NULL) {
        count++;
        p = p->next;
    }
    stats->num_ready = count;
}

