/*
 * MiniOS Scheduler Header
 * 
 * Round-robin scheduler with timer-based preemption.
 */

#ifndef _SCHEDULER_H
#define _SCHEDULER_H

#include "types.h"
#include "process.h"

/* Time slice in timer ticks (100 Hz = 10ms per tick) */
#define DEFAULT_TIME_SLICE  10  /* 100ms */

/*
 * Initialize the scheduler
 */
void scheduler_init(void);

/*
 * Schedule next process
 * Called by timer interrupt or when current process blocks/exits
 */
void schedule(void);

/*
 * Yield CPU to next process
 * Called voluntarily by current process
 */
void yield(void);

/*
 * Context switch to a process
 * Implemented in assembly
 */
extern void switch_to(process_t* prev, process_t* next);

/*
 * Start the scheduler (called once, doesn't return)
 */
void scheduler_start(void);

/*
 * Check if scheduler is running
 */
bool scheduler_running(void);

/*
 * Disable scheduler preemption (for critical sections)
 */
void scheduler_disable(void);

/*
 * Enable scheduler preemption
 */
void scheduler_enable(void);

/*
 * Get scheduler statistics
 */
typedef struct {
    uint64_t total_switches;    /* Total context switches */
    uint64_t idle_ticks;        /* Ticks spent in idle */
    uint32_t num_ready;         /* Number of ready processes */
} scheduler_stats_t;

void scheduler_get_stats(scheduler_stats_t* stats);

/*
 * Timer tick handler (called by IRQ0)
 */
void scheduler_tick(void);

#endif /* _SCHEDULER_H */

