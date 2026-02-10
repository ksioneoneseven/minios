/*
 * MiniOS XGUI Event System
 *
 * Unified event queue for keyboard, mouse, and window events.
 */

#include "xgui/event.h"
#include "keyboard.h"
#include "mouse.h"
#include "timer.h"
#include "string.h"

/* Event queue */
static xgui_event_t event_queue[XGUI_EVENT_QUEUE_SIZE];
static int queue_head = 0;
static int queue_tail = 0;
static int queue_count = 0;

/* Previous mouse state for detecting changes */
static int prev_mouse_x = 0;
static int prev_mouse_y = 0;
static uint8_t prev_mouse_buttons = 0;

/* Double-click detection */
static uint32_t last_click_time = 0;
static int last_click_x = 0;
static int last_click_y = 0;
#define DOUBLE_CLICK_TIME 500   /* ms */
#define DOUBLE_CLICK_DIST 5     /* pixels */

/*
 * Initialize the event system
 */
void xgui_event_init(void) {
    queue_head = 0;
    queue_tail = 0;
    queue_count = 0;
    
    prev_mouse_x = 0;
    prev_mouse_y = 0;
    prev_mouse_buttons = 0;
    
    last_click_time = 0;
    last_click_x = 0;
    last_click_y = 0;
}

/*
 * Push an event onto the queue
 */
bool xgui_event_push(xgui_event_t* event) {
    if (queue_count >= XGUI_EVENT_QUEUE_SIZE) {
        return false;  /* Queue full */
    }
    
    event->timestamp = timer_get_ticks();
    event_queue[queue_tail] = *event;
    queue_tail = (queue_tail + 1) % XGUI_EVENT_QUEUE_SIZE;
    queue_count++;
    
    return true;
}

/*
 * Pop an event from the queue
 */
static bool event_pop(xgui_event_t* event) {
    if (queue_count == 0) {
        return false;
    }
    
    *event = event_queue[queue_head];
    queue_head = (queue_head + 1) % XGUI_EVENT_QUEUE_SIZE;
    queue_count--;
    
    return true;
}

/*
 * Poll for the next event (non-blocking)
 */
bool xgui_event_poll(xgui_event_t* event) {
    /* First process any pending input */
    xgui_event_process_input();
    
    /* Then try to get an event from the queue */
    return event_pop(event);
}

/*
 * Wait for the next event (blocking)
 */
void xgui_event_wait(xgui_event_t* event) {
    while (1) {
        xgui_event_process_input();
        
        if (event_pop(event)) {
            return;
        }
        
        /* Small delay to avoid busy-waiting */
        __asm__ volatile("hlt");
    }
}

/*
 * Peek at the next event without removing it
 */
bool xgui_event_peek(xgui_event_t* event) {
    xgui_event_process_input();
    
    if (queue_count == 0) {
        return false;
    }
    
    *event = event_queue[queue_head];
    return true;
}

/*
 * Clear all pending events
 */
void xgui_event_clear(void) {
    queue_head = 0;
    queue_tail = 0;
    queue_count = 0;
}

/*
 * Check if there are pending events
 */
bool xgui_event_pending(void) {
    xgui_event_process_input();
    return queue_count > 0;
}

/*
 * Get current modifier key state
 */
uint8_t xgui_event_get_modifiers(void) {
    uint16_t kbd_mods = keyboard_get_modifiers();
    uint8_t mods = 0;
    
    if (kbd_mods & KEY_SHIFT) mods |= XGUI_MOD_SHIFT;
    if (kbd_mods & KEY_CTRL)  mods |= XGUI_MOD_CTRL;
    if (kbd_mods & KEY_ALT)   mods |= XGUI_MOD_ALT;
    
    return mods;
}

/*
 * Process mouse input and generate events
 */
static void process_mouse_input(void) {
    if (!mouse_available()) {
        return;
    }
    
    mouse_state_t* state = mouse_get_state();
    xgui_event_t event;
    uint8_t mods = xgui_event_get_modifiers();
    
    /* Check for movement */
    if (state->x != prev_mouse_x || state->y != prev_mouse_y) {
        memset(&event, 0, sizeof(event));
        event.type = XGUI_EVENT_MOUSE_MOVE;
        event.mouse.x = state->x;
        event.mouse.y = state->y;
        event.mouse.dx = state->x - prev_mouse_x;
        event.mouse.dy = state->y - prev_mouse_y;
        event.mouse.buttons = state->buttons;
        event.mouse.modifiers = mods;
        
        xgui_event_push(&event);
        
        prev_mouse_x = state->x;
        prev_mouse_y = state->y;
    }
    
    /* Check for button changes */
    if (state->buttons != prev_mouse_buttons) {
        uint8_t changed = state->buttons ^ prev_mouse_buttons;
        
        /* Check each button */
        for (int btn = 0; btn < 3; btn++) {
            uint8_t mask = (1 << btn);
            if (changed & mask) {
                memset(&event, 0, sizeof(event));
                
                if (state->buttons & mask) {
                    /* Button pressed */
                    event.type = XGUI_EVENT_MOUSE_DOWN;
                } else {
                    /* Button released */
                    event.type = XGUI_EVENT_MOUSE_UP;
                    
                    /* Generate click event */
                    xgui_event_t click_event;
                    memset(&click_event, 0, sizeof(click_event));
                    click_event.type = XGUI_EVENT_MOUSE_CLICK;
                    click_event.mouse.x = state->x;
                    click_event.mouse.y = state->y;
                    click_event.mouse.button = mask;
                    click_event.mouse.buttons = state->buttons;
                    click_event.mouse.modifiers = mods;
                    xgui_event_push(&click_event);
                    
                    /* Check for double-click */
                    uint32_t now = timer_get_ticks();
                    int dx = state->x - last_click_x;
                    int dy = state->y - last_click_y;
                    if (dx < 0) dx = -dx;
                    if (dy < 0) dy = -dy;
                    
                    if ((now - last_click_time) < DOUBLE_CLICK_TIME &&
                        dx < DOUBLE_CLICK_DIST && dy < DOUBLE_CLICK_DIST) {
                        xgui_event_t dbl_event;
                        memset(&dbl_event, 0, sizeof(dbl_event));
                        dbl_event.type = XGUI_EVENT_MOUSE_DBLCLICK;
                        dbl_event.mouse.x = state->x;
                        dbl_event.mouse.y = state->y;
                        dbl_event.mouse.button = mask;
                        dbl_event.mouse.buttons = state->buttons;
                        dbl_event.mouse.modifiers = mods;
                        xgui_event_push(&dbl_event);
                        
                        last_click_time = 0;  /* Reset to prevent triple-click */
                    } else {
                        last_click_time = now;
                        last_click_x = state->x;
                        last_click_y = state->y;
                    }
                }
                
                event.mouse.x = state->x;
                event.mouse.y = state->y;
                event.mouse.button = mask;
                event.mouse.buttons = state->buttons;
                event.mouse.modifiers = mods;
                
                xgui_event_push(&event);
            }
        }
        
        prev_mouse_buttons = state->buttons;
    }
    
    mouse_clear_flags();
}

/*
 * Process keyboard input and generate events
 */
static void process_keyboard_input(void) {
    char c;
    xgui_event_t event;
    
    while ((c = keyboard_getchar_nonblock()) != 0) {
        /* Fetch modifiers AFTER getchar so they match this key press */
        uint8_t mods = xgui_event_get_modifiers();
        uint8_t uc = (uint8_t)c;
        
        memset(&event, 0, sizeof(event));
        event.type = XGUI_EVENT_KEY_DOWN;
        event.key.keycode = uc;
        event.key.modifiers = mods;
        event.key.repeat = false;
        
        /* For printable characters, also generate KEY_CHAR */
        if (c >= 32 && c < 127) {
            event.key.character = c;
            xgui_event_push(&event);
            
            /* Generate separate KEY_CHAR event */
            xgui_event_t char_event;
            memset(&char_event, 0, sizeof(char_event));
            char_event.type = XGUI_EVENT_KEY_CHAR;
            char_event.key.keycode = uc;
            char_event.key.character = c;
            char_event.key.modifiers = mods;
            xgui_event_push(&char_event);
        } else {
            /* Special key */
            event.key.character = 0;
            xgui_event_push(&event);
        }
    }
}

/*
 * Process input devices and generate events
 */
void xgui_event_process_input(void) {
    process_mouse_input();
    process_keyboard_input();
}
