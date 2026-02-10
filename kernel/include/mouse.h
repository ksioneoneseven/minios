/*
 * MiniOS PS/2 Mouse Driver
 *
 * Handles PS/2 mouse input via IRQ12.
 */

#ifndef _MOUSE_H
#define _MOUSE_H

#include "types.h"

/* Mouse button flags */
#define MOUSE_LEFT_BUTTON   0x01
#define MOUSE_RIGHT_BUTTON  0x02
#define MOUSE_MIDDLE_BUTTON 0x04

/* Mouse state structure */
typedef struct {
    int x;              /* Current X position */
    int y;              /* Current Y position */
    uint8_t buttons;    /* Button state (MOUSE_*_BUTTON flags) */
    int dx;             /* Delta X since last read */
    int dy;             /* Delta Y since last read */
    bool moved;         /* True if mouse moved since last read */
    bool button_changed;/* True if button state changed */
} mouse_state_t;

/* Mouse event callback type */
typedef void (*mouse_callback_t)(mouse_state_t* state);

/*
 * Initialize the PS/2 mouse driver
 * Returns 0 on success, -1 on failure
 */
int mouse_init(void);

/*
 * Check if mouse is available
 */
bool mouse_available(void);

/*
 * Get IRQ12 count for debugging
 */
uint32_t mouse_get_irq_count(void);

/*
 * Get current mouse state
 */
mouse_state_t* mouse_get_state(void);

/*
 * Get current mouse position
 */
void mouse_get_position(int* x, int* y);

/*
 * Set mouse position (for bounds clamping or warping)
 */
void mouse_set_position(int x, int y);

/*
 * Set mouse bounds (screen dimensions)
 */
void mouse_set_bounds(int max_x, int max_y);

/*
 * Check if a button is currently pressed
 */
bool mouse_button_down(uint8_t button);

/*
 * Register a callback for mouse events
 */
void mouse_set_callback(mouse_callback_t callback);

/*
 * Clear the moved/changed flags after processing
 */
void mouse_clear_flags(void);

#endif /* _MOUSE_H */
