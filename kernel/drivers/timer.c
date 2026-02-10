/*
 * MiniOS Timer Driver (PIT - Programmable Interval Timer)
 *
 * Uses IRQ0 to provide system timing.
 */

#include "../include/timer.h"
#include "../include/io.h"
#include "../include/isr.h"
#include "../include/pic.h"
#include "../include/scheduler.h"
#include "../include/vga.h"
#include "../include/serial.h"

/* Current tick count */
static volatile uint32_t tick_count = 0;

/* Timer frequency in Hz */
static uint32_t timer_frequency = 0;

/* Timer callback */
static timer_callback_t timer_callback = NULL;

/*
 * Timer interrupt handler
 */
static void timer_handler(registers_t* regs) {
    (void)regs;  /* Unused */

    tick_count++;

    vga_cursor_blink_tick(tick_count);

    /* Call scheduler tick for preemption */
    scheduler_tick();

    /* Call callback if registered */
    if (timer_callback) {
        timer_callback(tick_count);
    }
}

/*
 * Initialize timer with specified frequency
 */
void timer_init(uint32_t frequency) {
    timer_frequency = frequency;
    tick_count = 0;
    timer_callback = NULL;
    
    /* Calculate divisor for PIT */
    uint32_t divisor = PIT_FREQUENCY / frequency;
    
    /* Make sure divisor fits in 16 bits */
    if (divisor > 65535) divisor = 65535;
    if (divisor < 1) divisor = 1;
    
    /* Send command byte: channel 0, lobyte/hibyte, rate generator */
    outb(PIT_COMMAND, 0x36);
    
    /* Send divisor */
    outb(PIT_CHANNEL0, (uint8_t)(divisor & 0xFF));         /* Low byte */
    outb(PIT_CHANNEL0, (uint8_t)((divisor >> 8) & 0xFF));  /* High byte */
    
    /* Register IRQ0 handler */
    irq_register_handler(0, timer_handler);
    
    /* Enable timer IRQ */
    pic_enable_irq(0);
}

/*
 * Get current tick count
 */
uint32_t timer_get_ticks(void) {
    return tick_count;
}

/*
 * Wait for specified number of ticks
 */
void timer_wait(uint32_t ticks) {
    uint32_t target = tick_count + ticks;
    while (tick_count < target) {
        __asm__ volatile("hlt");
    }
}

/*
 * Sleep for specified number of milliseconds
 */
void timer_sleep_ms(uint32_t ms) {
    if (timer_frequency == 0) return;
    
    uint32_t ticks = (ms * timer_frequency) / 1000;
    if (ticks == 0) ticks = 1;
    timer_wait(ticks);
}

/*
 * Register a timer callback
 */
void timer_set_callback(timer_callback_t callback) {
    timer_callback = callback;
}

