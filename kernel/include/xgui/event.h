/*
 * MiniOS XGUI Event System
 *
 * Unified event queue for keyboard, mouse, and window events.
 */

#ifndef _XGUI_EVENT_H
#define _XGUI_EVENT_H

#include "types.h"

/* Event types */
typedef enum {
    XGUI_EVENT_NONE = 0,
    
    /* Mouse events */
    XGUI_EVENT_MOUSE_MOVE,
    XGUI_EVENT_MOUSE_DOWN,
    XGUI_EVENT_MOUSE_UP,
    XGUI_EVENT_MOUSE_CLICK,
    XGUI_EVENT_MOUSE_DBLCLICK,
    
    /* Keyboard events */
    XGUI_EVENT_KEY_DOWN,
    XGUI_EVENT_KEY_UP,
    XGUI_EVENT_KEY_CHAR,
    
    /* Window events */
    XGUI_EVENT_WINDOW_CLOSE,
    XGUI_EVENT_WINDOW_FOCUS,
    XGUI_EVENT_WINDOW_BLUR,
    XGUI_EVENT_WINDOW_RESIZE,
    XGUI_EVENT_WINDOW_MOVE,
    XGUI_EVENT_WINDOW_PAINT,
    
    /* Timer events */
    XGUI_EVENT_TIMER,
    
    /* System events */
    XGUI_EVENT_QUIT
} xgui_event_type_t;

/* Mouse button identifiers */
#define XGUI_MOUSE_LEFT     0x01
#define XGUI_MOUSE_RIGHT    0x02
#define XGUI_MOUSE_MIDDLE   0x04

/* Modifier key flags */
#define XGUI_MOD_SHIFT      0x01
#define XGUI_MOD_CTRL       0x02
#define XGUI_MOD_ALT        0x04

/* Mouse event data */
typedef struct {
    int x;              /* Mouse X position */
    int y;              /* Mouse Y position */
    int dx;             /* Delta X (for move events) */
    int dy;             /* Delta Y (for move events) */
    uint8_t button;     /* Button that triggered event */
    uint8_t buttons;    /* Current button state */
    uint8_t modifiers;  /* Modifier keys held */
} xgui_mouse_event_t;

/* Keyboard event data */
typedef struct {
    uint8_t keycode;    /* Raw keycode */
    char character;     /* ASCII character (for KEY_CHAR) */
    uint8_t modifiers;  /* Modifier keys held */
    bool repeat;        /* True if this is a key repeat */
} xgui_key_event_t;

/* Window event data */
typedef struct {
    int x;              /* New X position (for move) */
    int y;              /* New Y position (for move) */
    int width;          /* New width (for resize) */
    int height;         /* New height (for resize) */
} xgui_window_event_t;

/* Timer event data */
typedef struct {
    uint32_t timer_id;  /* Timer identifier */
    uint32_t interval;  /* Timer interval in ms */
} xgui_timer_event_t;

/* Generic event structure */
typedef struct {
    xgui_event_type_t type;     /* Event type */
    uint32_t timestamp;         /* Event timestamp (ticks) */
    uint32_t window_id;         /* Target window ID (0 for global) */
    
    union {
        xgui_mouse_event_t mouse;
        xgui_key_event_t key;
        xgui_window_event_t window;
        xgui_timer_event_t timer;
    };
} xgui_event_t;

/* Event queue size */
#define XGUI_EVENT_QUEUE_SIZE 64

/*
 * Initialize the event system
 */
void xgui_event_init(void);

/*
 * Poll for the next event (non-blocking)
 * Returns true if an event was available
 */
bool xgui_event_poll(xgui_event_t* event);

/*
 * Wait for the next event (blocking)
 */
void xgui_event_wait(xgui_event_t* event);

/*
 * Peek at the next event without removing it
 * Returns true if an event is available
 */
bool xgui_event_peek(xgui_event_t* event);

/*
 * Push an event onto the queue
 * Returns true on success, false if queue is full
 */
bool xgui_event_push(xgui_event_t* event);

/*
 * Clear all pending events
 */
void xgui_event_clear(void);

/*
 * Check if there are pending events
 */
bool xgui_event_pending(void);

/*
 * Get current modifier key state
 */
uint8_t xgui_event_get_modifiers(void);

/*
 * Process input devices and generate events
 * Called from main loop
 */
void xgui_event_process_input(void);

#endif /* _XGUI_EVENT_H */
