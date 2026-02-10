/*
 * MiniOS XGUI Calculator App
 *
 * A simple calculator demonstrating the XGUI widget toolkit.
 */

#include "xgui/xgui.h"
#include "xgui/widget.h"
#include "string.h"
#include "stdio.h"

/* Calculator state */
static xgui_window_t* calc_window = NULL;
static xgui_widget_t* display_field = NULL;
static double accumulator = 0;
static double current_value = 0;
static char pending_op = 0;
static bool new_number = true;
static char display_text[32] = "0";

/* Button layout */
static const char* button_labels[] = {
    "C", "CE", "%", "/",
    "7", "8", "9", "*",
    "4", "5", "6", "-",
    "1", "2", "3", "+",
    "0", ".", "=", ""
};

static xgui_widget_t* buttons[20];

/*
 * Update the display
 */
static void update_display(void) {
    xgui_widget_set_text(display_field, display_text);
}

/*
 * Parse display to number
 */
static double parse_display(void) {
    double result = 0;
    double fraction = 0;
    bool negative = false;
    bool in_fraction = false;
    double fraction_mult = 0.1;
    
    const char* p = display_text;
    
    if (*p == '-') {
        negative = true;
        p++;
    }
    
    while (*p) {
        if (*p == '.') {
            in_fraction = true;
        } else if (*p >= '0' && *p <= '9') {
            if (in_fraction) {
                fraction += (*p - '0') * fraction_mult;
                fraction_mult *= 0.1;
            } else {
                result = result * 10 + (*p - '0');
            }
        }
        p++;
    }
    
    result += fraction;
    return negative ? -result : result;
}

/*
 * Format number for display (supports fractional values)
 */
static void format_number(double num) {
    int pos = 0;

    /* Handle negative */
    if (num < 0) {
        display_text[pos++] = '-';
        num = -num;
    }

    /* Split into integer and fractional parts */
    /* Use fixed-point: multiply by 10^8 to get 8 decimal digits */
    uint64_t int_part = (uint64_t)num;
    double frac = num - (double)int_part;
    /* Round the fractional part to 8 digits */
    uint64_t frac_digits = (uint64_t)(frac * 100000000.0 + 0.5);
    /* Handle rounding overflow (e.g. 0.9999999999 -> 1.0) */
    if (frac_digits >= 100000000ULL) {
        int_part++;
        frac_digits = 0;
    }

    /* Format integer part */
    char temp[24];
    int ti = 0;
    if (int_part == 0) {
        temp[ti++] = '0';
    } else {
        while (int_part > 0 && ti < 20) {
            temp[ti++] = '0' + (int)(int_part % 10);
            int_part /= 10;
        }
    }
    /* Reverse integer digits into display */
    while (ti > 0 && pos < 28) {
        display_text[pos++] = temp[--ti];
    }

    /* Format fractional part (if nonzero) */
    if (frac_digits > 0) {
        display_text[pos++] = '.';
        /* Write exactly 8 fractional digits */
        char frac_buf[9];
        for (int i = 7; i >= 0; i--) {
            frac_buf[i] = '0' + (int)(frac_digits % 10);
            frac_digits /= 10;
        }
        frac_buf[8] = '\0';
        /* Copy fractional digits */
        for (int i = 0; i < 8 && pos < 30; i++) {
            display_text[pos++] = frac_buf[i];
        }
        /* Trim trailing zeros */
        while (pos > 1 && display_text[pos - 1] == '0') pos--;
        if (pos > 1 && display_text[pos - 1] == '.') pos--;
    }

    display_text[pos] = '\0';
}

/*
 * Perform calculation
 */
static void calculate(void) {
    current_value = parse_display();
    
    switch (pending_op) {
        case '+':
            accumulator += current_value;
            break;
        case '-':
            accumulator -= current_value;
            break;
        case '*':
            accumulator *= current_value;
            break;
        case '/':
            if (current_value != 0) {
                accumulator /= current_value;
            } else {
                strcpy(display_text, "Error");
                update_display();
                return;
            }
            break;
        case '%':
            accumulator = (int)accumulator % (int)current_value;
            break;
        default:
            accumulator = current_value;
            break;
    }
    
    format_number(accumulator);
    update_display();
}

/*
 * Button click handler
 */
static void button_click(xgui_widget_t* widget) {
    const char* label = xgui_widget_get_text(widget);
    
    if (!label || !label[0]) return;
    
    char c = label[0];
    
    /* Digit buttons */
    if (c >= '0' && c <= '9') {
        if (new_number) {
            display_text[0] = c;
            display_text[1] = '\0';
            new_number = false;
        } else {
            int len = strlen(display_text);
            if (len < 15) {
                display_text[len] = c;
                display_text[len + 1] = '\0';
            }
        }
        update_display();
        return;
    }
    
    /* Decimal point */
    if (c == '.') {
        if (new_number) {
            strcpy(display_text, "0.");
            new_number = false;
        } else if (!strchr(display_text, '.')) {
            int len = strlen(display_text);
            if (len < 15) {
                display_text[len] = '.';
                display_text[len + 1] = '\0';
            }
        }
        update_display();
        return;
    }
    
    /* Clear */
    if (c == 'C' && label[1] == '\0') {
        accumulator = 0;
        pending_op = 0;
        strcpy(display_text, "0");
        new_number = true;
        update_display();
        return;
    }
    
    /* Clear Entry */
    if (strcmp(label, "CE") == 0) {
        strcpy(display_text, "0");
        new_number = true;
        update_display();
        return;
    }
    
    /* Operators */
    if (c == '+' || c == '-' || c == '*' || c == '/' || c == '%') {
        if (!new_number) {
            calculate();
        }
        pending_op = c;
        new_number = true;
        return;
    }
    
    /* Equals */
    if (c == '=') {
        calculate();
        pending_op = 0;
        new_number = true;
        return;
    }
}

/*
 * Window paint callback
 */
static void calc_paint(xgui_window_t* win) {
    xgui_widgets_draw(win);
}

/*
 * Window event handler
 */
static void calc_handler(xgui_window_t* win, xgui_event_t* event) {
    /* Let widgets handle events first */
    if (xgui_widgets_handle_event(win, event)) {
        return;
    }
    
    /* Handle window close */
    if (event->type == XGUI_EVENT_WINDOW_CLOSE) {
        xgui_window_destroy(win);
        calc_window = NULL;
        display_field = NULL;
        for (int i = 0; i < 20; i++) {
            buttons[i] = NULL;
        }
    }
    
    /* Handle keyboard input */
    if (event->type == XGUI_EVENT_KEY_CHAR) {
        char c = event->key.character;
        
        /* Simulate button press for keyboard input */
        if ((c >= '0' && c <= '9') || c == '.' || 
            c == '+' || c == '-' || c == '*' || c == '/') {
            /* Find matching button and trigger it */
            for (int i = 0; i < 20; i++) {
                if (buttons[i]) {
                    const char* label = xgui_widget_get_text(buttons[i]);
                    if (label && label[0] == c && label[1] == '\0') {
                        button_click(buttons[i]);
                        break;
                    }
                }
            }
        }
        
        /* Enter = equals */
        if (c == '\n' || c == '=') {
            for (int i = 0; i < 20; i++) {
                if (buttons[i]) {
                    const char* label = xgui_widget_get_text(buttons[i]);
                    if (label && label[0] == '=') {
                        button_click(buttons[i]);
                        break;
                    }
                }
            }
        }
    }
}

/*
 * Create the calculator window
 */
void xgui_calculator_create(void) {
    if (calc_window) {
        xgui_window_focus(calc_window);
        return;
    }
    
    /* Reset state */
    accumulator = 0;
    current_value = 0;
    pending_op = 0;
    new_number = true;
    strcpy(display_text, "0");
    
    /* Create window */
    calc_window = xgui_window_create("Calculator", 100, 100, 180, 240, XGUI_WINDOW_DEFAULT);
    if (!calc_window) return;
    
    xgui_window_set_paint(calc_window, calc_paint);
    xgui_window_set_handler(calc_window, calc_handler);
    
    /* Create display field */
    display_field = xgui_textfield_create(calc_window, 10, 10, 154, 15);
    xgui_widget_set_text(display_field, "0");
    xgui_widget_set_enabled(display_field, false);  /* Read-only */
    
    /* Create buttons */
    int btn_w = 35;
    int btn_h = 30;
    int start_x = 10;
    int start_y = 40;
    int gap = 4;
    
    for (int i = 0; i < 20; i++) {
        int row = i / 4;
        int col = i % 4;
        int x = start_x + col * (btn_w + gap);
        int y = start_y + row * (btn_h + gap);
        
        if (button_labels[i][0] != '\0') {
            buttons[i] = xgui_button_create(calc_window, x, y, btn_w, btn_h, button_labels[i]);
            xgui_widget_set_onclick(buttons[i], button_click);
        } else {
            buttons[i] = NULL;
        }
    }
    
    /* Make the 0 button wider (spans 2 columns) */
    if (buttons[16]) {
        xgui_widget_set_size(buttons[16], btn_w * 2 + gap, btn_h);
    }
    /* Adjust decimal point position */
    if (buttons[17]) {
        xgui_widget_set_position(buttons[17], start_x + 2 * (btn_w + gap), start_y + 4 * (btn_h + gap));
    }
    /* Adjust equals position */
    if (buttons[18]) {
        xgui_widget_set_position(buttons[18], start_x + 3 * (btn_w + gap), start_y + 4 * (btn_h + gap));
    }
}
