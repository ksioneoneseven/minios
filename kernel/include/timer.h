/*
 * MiniOS Timer Driver Header
 * 
 * Programmable Interval Timer (PIT) driver using IRQ0.
 */

#ifndef _TIMER_H
#define _TIMER_H

#include "types.h"

/* PIT I/O ports */
#define PIT_CHANNEL0    0x40
#define PIT_CHANNEL1    0x41
#define PIT_CHANNEL2    0x42
#define PIT_COMMAND     0x43

/* PIT frequency (1.193182 MHz) */
#define PIT_FREQUENCY   1193182

/* Timer callback function type */
typedef void (*timer_callback_t)(uint32_t ticks);

/* Initialize timer with specified frequency (Hz) */
void timer_init(uint32_t frequency);

/* Get current tick count */
uint32_t timer_get_ticks(void);

/* Sleep for specified number of ticks */
void timer_wait(uint32_t ticks);

/* Sleep for specified number of milliseconds */
void timer_sleep_ms(uint32_t ms);

/* Register a timer callback */
void timer_set_callback(timer_callback_t callback);

#endif /* _TIMER_H */

