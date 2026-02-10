/*
 * MiniOS PS/2 Mouse Driver
 *
 * Handles PS/2 mouse input via IRQ12.
 * Uses the standard PS/2 mouse protocol with 3-byte packets.
 */

#include "../include/mouse.h"
#include "../include/io.h"
#include "../include/isr.h"
#include "../include/pic.h"
#include "../include/stdio.h"
#include "../include/serial.h"

/* PS/2 controller ports */
#define PS2_DATA_PORT       0x60
#define PS2_STATUS_PORT     0x64
#define PS2_COMMAND_PORT    0x64

/* PS/2 controller commands */
#define PS2_CMD_READ_CONFIG     0x20
#define PS2_CMD_WRITE_CONFIG    0x60
#define PS2_CMD_DISABLE_PORT2   0xA7
#define PS2_CMD_ENABLE_PORT2    0xA8
#define PS2_CMD_TEST_PORT2      0xA9
#define PS2_CMD_WRITE_PORT2     0xD4

/* Mouse commands */
#define MOUSE_CMD_SET_DEFAULTS  0xF6
#define MOUSE_CMD_ENABLE        0xF4
#define MOUSE_CMD_DISABLE       0xF5
#define MOUSE_CMD_RESET         0xFF
#define MOUSE_CMD_GET_ID        0xF2
#define MOUSE_CMD_SET_RATE      0xF3

/* Mouse responses */
#define MOUSE_ACK               0xFA
#define MOUSE_RESEND            0xFE

/* Status register bits */
#define PS2_STATUS_OUTPUT_FULL  0x01
#define PS2_STATUS_INPUT_FULL   0x02
#define PS2_STATUS_MOUSE_DATA   0x20

/* Mouse state */
static mouse_state_t mouse_state;
static bool mouse_initialized = false;
static mouse_callback_t mouse_callback = NULL;

/* Screen bounds */
static int screen_max_x = 800;
static int screen_max_y = 600;

/* Packet assembly */
static uint8_t mouse_packet[3];
static int packet_index = 0;

/*
 * Wait for PS/2 controller input buffer to be empty
 */
static void ps2_wait_write(void) {
    int timeout = 100000;
    while (timeout-- > 0) {
        if (!(inb(PS2_STATUS_PORT) & PS2_STATUS_INPUT_FULL)) {
            return;
        }
    }
}

/*
 * Wait for PS/2 controller output buffer to have data
 */
static bool ps2_wait_read(void) {
    int timeout = 100000;
    while (timeout-- > 0) {
        if (inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL) {
            return true;
        }
    }
    return false;
}

/*
 * Send a command to the PS/2 controller
 */
static void ps2_send_command(uint8_t cmd) {
    ps2_wait_write();
    outb(PS2_COMMAND_PORT, cmd);
}

/*
 * Send a byte to the mouse (via PS/2 controller port 2)
 */
static void mouse_send(uint8_t data) {
    ps2_wait_write();
    outb(PS2_COMMAND_PORT, PS2_CMD_WRITE_PORT2);
    ps2_wait_write();
    outb(PS2_DATA_PORT, data);
}

/*
 * Read a byte from the PS/2 data port
 */
static uint8_t ps2_read(void) {
    if (ps2_wait_read()) {
        return inb(PS2_DATA_PORT);
    }
    return 0;
}

/*
 * Send a command to the mouse and wait for ACK
 */
static bool mouse_command(uint8_t cmd) {
    mouse_send(cmd);
    
    /* Wait for ACK */
    int retries = 3;
    while (retries-- > 0) {
        uint8_t response = ps2_read();
        if (response == MOUSE_ACK) {
            return true;
        }
        if (response == MOUSE_RESEND) {
            mouse_send(cmd);
        }
    }
    return false;
}

/*
 * Process a complete mouse packet
 */
static void mouse_process_packet(void) {
    uint8_t flags = mouse_packet[0];
    int8_t dx = mouse_packet[1];
    int8_t dy = mouse_packet[2];
    
    /* Check for overflow - discard packet if overflow */
    if (flags & 0x80 || flags & 0x40) {
        return;
    }
    
    /* Apply sign extension if needed */
    if (flags & 0x10) {
        dx |= 0xFFFFFF00;  /* Sign extend X */
    }
    if (flags & 0x20) {
        dy |= 0xFFFFFF00;  /* Sign extend Y */
    }
    
    /* Update button state */
    uint8_t old_buttons = mouse_state.buttons;
    mouse_state.buttons = flags & 0x07;
    
    if (mouse_state.buttons != old_buttons) {
        mouse_state.button_changed = true;
    }
    
    /* Update position (Y is inverted in PS/2 protocol) */
    if (dx != 0 || dy != 0) {
        mouse_state.dx = dx;
        mouse_state.dy = -dy;  /* Invert Y for screen coordinates */
        
        mouse_state.x += dx;
        mouse_state.y -= dy;  /* Invert Y */
        
        /* Clamp to screen bounds */
        if (mouse_state.x < 0) mouse_state.x = 0;
        if (mouse_state.y < 0) mouse_state.y = 0;
        if (mouse_state.x >= screen_max_x) mouse_state.x = screen_max_x - 1;
        if (mouse_state.y >= screen_max_y) mouse_state.y = screen_max_y - 1;
        
        mouse_state.moved = true;
    }
    
    /* Call callback if registered */
    if (mouse_callback && (mouse_state.moved || mouse_state.button_changed)) {
        mouse_callback(&mouse_state);
    }
}

/* Debug counter for IRQ12 */
static volatile uint32_t irq12_count = 0;

/*
 * Mouse interrupt handler (IRQ12)
 */
static void mouse_handler(registers_t* regs) {
    (void)regs;
    
    irq12_count++;
    
    uint8_t status = inb(PS2_STATUS_PORT);
    
    /* Check if this is mouse data */
    if (!(status & PS2_STATUS_OUTPUT_FULL)) {
        return;
    }
    
    uint8_t data = inb(PS2_DATA_PORT);
    
    /* Synchronization: first byte always has bit 3 set */
    if (packet_index == 0 && !(data & 0x08)) {
        /* Out of sync - discard and wait for valid first byte */
        return;
    }
    
    mouse_packet[packet_index++] = data;
    
    if (packet_index >= 3) {
        mouse_process_packet();
        packet_index = 0;
    }
}

/*
 * Initialize the PS/2 mouse driver
 */
int mouse_init(void) {
    if (mouse_initialized) {
        return 0;
    }
    
    serial_write_string("Mouse: init starting\n");
    
    /* Initialize state */
    mouse_state.x = screen_max_x / 2;
    mouse_state.y = screen_max_y / 2;
    mouse_state.buttons = 0;
    mouse_state.dx = 0;
    mouse_state.dy = 0;
    mouse_state.moved = false;
    mouse_state.button_changed = false;
    
    packet_index = 0;
    
    /* Disable interrupts during PS/2 controller configuration to prevent
     * the keyboard ISR from reading the config byte off port 0x60 */
    __asm__ volatile("cli");

    /* Drain any stale data from the PS/2 output buffer */
    while (inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL) {
        inb(PS2_DATA_PORT);
    }

    /* Enable the auxiliary (mouse) PS/2 port */
    ps2_send_command(PS2_CMD_ENABLE_PORT2);
    
    /* Read controller configuration */
    ps2_send_command(PS2_CMD_READ_CONFIG);
    uint8_t config = ps2_read();
    
    /* Enable both keyboard IRQ (bit 0) and mouse IRQ (bit 1),
     * and enable mouse clock (clear bit 5) */
    config |= 0x03;   /* Enable IRQ1 (keyboard) + IRQ12 (mouse) */
    config &= ~0x20;  /* Enable mouse clock */
    
    /* Write back configuration */
    ps2_send_command(PS2_CMD_WRITE_CONFIG);
    ps2_wait_write();
    outb(PS2_DATA_PORT, config);

    __asm__ volatile("sti");
    
    /* Reset the mouse */
    if (!mouse_command(MOUSE_CMD_RESET)) {
        printk("Mouse: Reset failed\n");
        return -1;
    }
    
    /* Wait for self-test result (0xAA) and mouse ID (0x00) */
    ps2_read();  /* 0xAA */
    ps2_read();  /* 0x00 */
    
    /* Set defaults */
    if (!mouse_command(MOUSE_CMD_SET_DEFAULTS)) {
        printk("Mouse: Set defaults failed\n");
        return -1;
    }
    
    /* Set sample rate to 100 samples/second */
    if (mouse_command(MOUSE_CMD_SET_RATE)) {
        mouse_send(100);
        ps2_read();  /* ACK */
    }
    
    /* Enable data reporting */
    if (!mouse_command(MOUSE_CMD_ENABLE)) {
        printk("Mouse: Enable failed\n");
        return -1;
    }
    
    /* Register IRQ12 handler (IRQ 12, not interrupt 44) */
    irq_register_handler(12, mouse_handler);
    
    /* Enable IRQ12 in PIC */
    pic_enable_irq(12);
    
    mouse_initialized = true;
    printk("Mouse: PS/2 mouse initialized\n");
    serial_write_string("Mouse: init OK\n");
    
    return 0;
}

/*
 * Check if mouse is available
 */
bool mouse_available(void) {
    return mouse_initialized;
}

/*
 * Get IRQ12 count for debugging
 */
uint32_t mouse_get_irq_count(void) {
    return irq12_count;
}

/*
 * Get current mouse state
 */
mouse_state_t* mouse_get_state(void) {
    return &mouse_state;
}

/*
 * Get current mouse position
 */
void mouse_get_position(int* x, int* y) {
    if (x) *x = mouse_state.x;
    if (y) *y = mouse_state.y;
}

/*
 * Set mouse position
 */
void mouse_set_position(int x, int y) {
    mouse_state.x = x;
    mouse_state.y = y;
    
    /* Clamp to bounds */
    if (mouse_state.x < 0) mouse_state.x = 0;
    if (mouse_state.y < 0) mouse_state.y = 0;
    if (mouse_state.x >= screen_max_x) mouse_state.x = screen_max_x - 1;
    if (mouse_state.y >= screen_max_y) mouse_state.y = screen_max_y - 1;
}

/*
 * Set mouse bounds
 */
void mouse_set_bounds(int max_x, int max_y) {
    screen_max_x = max_x;
    screen_max_y = max_y;
    
    /* Re-clamp current position */
    if (mouse_state.x >= screen_max_x) mouse_state.x = screen_max_x - 1;
    if (mouse_state.y >= screen_max_y) mouse_state.y = screen_max_y - 1;
}

/*
 * Check if a button is currently pressed
 */
bool mouse_button_down(uint8_t button) {
    return (mouse_state.buttons & button) != 0;
}

/*
 * Register a callback for mouse events
 */
void mouse_set_callback(mouse_callback_t callback) {
    mouse_callback = callback;
}

/*
 * Clear the moved/changed flags
 */
void mouse_clear_flags(void) {
    mouse_state.moved = false;
    mouse_state.button_changed = false;
    mouse_state.dx = 0;
    mouse_state.dy = 0;
}
