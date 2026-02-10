/*
 * MiniOS Spreadsheet Application
 * 
 * A simple spreadsheet with cell navigation, editing, formulas, and file I/O.
 * Runs as a shell command taking over the VGA display.
 */

#include "../include/spreadsheet.h"
#include "../include/vga.h"
#include "../include/keyboard.h"
#include "../include/string.h"
#include "../include/stdio.h"
#include "../include/heap.h"
#include "../include/vfs.h"
#include "../include/ramfs.h"
#include "../include/ext2.h"
#include "../include/shell.h"

/* Spreadsheet dimensions */
#define SS_MAX_COLS     26      /* A-Z */
#define SS_MAX_ROWS     100
#define SS_CELL_WIDTH   10      /* Display width per cell */
#define SS_MAX_CONTENT  64      /* Max content length per cell */

/* Display layout */
#define SS_HEADER_ROW   0       /* Column headers row */
#define SS_DATA_START   2       /* First data row on screen */
#define SS_ROW_NUM_WIDTH 4      /* Width for row numbers */
#define SS_VISIBLE_COLS 7       /* Columns visible on screen */
#define SS_VISIBLE_ROWS 20      /* Rows visible on screen */
#define SS_STATUS_ROW   23      /* Status bar row */
#define SS_INPUT_ROW    24      /* Input/formula bar row */

/* Cell types */
typedef enum {
    CELL_EMPTY = 0,
    CELL_TEXT,
    CELL_NUMBER,
    CELL_FORMULA
} cell_type_t;

/* Cell structure */
typedef struct {
    cell_type_t type;
    char content[SS_MAX_CONTENT];   /* Raw content (formula or value) */
    double value;                    /* Computed numeric value */
    char display[SS_CELL_WIDTH + 1]; /* Display string */
} cell_t;

/* Spreadsheet state */
static cell_t cells[SS_MAX_ROWS][SS_MAX_COLS];
static int cursor_row = 0;
static int cursor_col = 0;
static int view_row = 0;      /* First visible row */
static int view_col = 0;      /* First visible column */
static int editing = 0;       /* Currently editing a cell */
static char edit_buffer[SS_MAX_CONTENT];
static int edit_pos = 0;
static char filename[64] = "";
static int modified = 0;

/* Color scheme */
#define COLOR_HEADER    vga_entry_color(VGA_COLOR_BLACK, VGA_COLOR_LIGHT_GREY)
#define COLOR_CELL      vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE)
#define COLOR_CURSOR    vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_RED)
#define COLOR_NUMBER    vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLUE)
#define COLOR_FORMULA   vga_entry_color(VGA_COLOR_LIGHT_MAGENTA, VGA_COLOR_BLUE)
#define COLOR_STATUS    vga_entry_color(VGA_COLOR_BLACK, VGA_COLOR_WHITE)
#define COLOR_INPUT     vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK)
#define COLOR_BORDER    vga_entry_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLUE)

/* Forward declarations */
static void ss_draw_screen(void);
static void ss_draw_cell(int row, int col);
static void ss_draw_status(void);
static void ss_draw_input(void);
static void ss_move_cursor(int drow, int dcol);
static void ss_start_edit(void);
static void ss_end_edit(int save);
static void ss_evaluate_cell(int row, int col);
static void ss_evaluate_all(void);
static double ss_evaluate_formula(const char* formula);
static void ss_format_cell(int row, int col);
static int ss_save_file(const char* name);
static int ss_load_file(const char* name);
static void ss_clear_all(void);

/*
 * Initialize spreadsheet
 */
void spreadsheet_init(void) {
    memset(cells, 0, sizeof(cells));
    cursor_row = 0;
    cursor_col = 0;
    view_row = 0;
    view_col = 0;
    editing = 0;
    edit_pos = 0;
    filename[0] = '\0';
    modified = 0;
}

/*
 * Get column letter from index (0=A, 1=B, etc.)
 */
static char col_to_letter(int col) {
    return 'A' + col;
}

/*
 * Draw a horizontal line
 */
static void ss_draw_hline(int y, int x1, int x2, uint8_t color) {
    char line[VGA_WIDTH + 1];
    int len = x2 - x1 + 1;
    if (len > VGA_WIDTH) len = VGA_WIDTH;
    for (int i = 0; i < len; i++) {
        line[i] = '-';
    }
    line[len] = '\0';
    vga_write_at(line, color, x1, y);
}

/*
 * Clear a line
 */
static void ss_clear_line(int y, uint8_t color) {
    char spaces[VGA_WIDTH + 1];
    for (int i = 0; i < VGA_WIDTH; i++) spaces[i] = ' ';
    spaces[VGA_WIDTH] = '\0';
    vga_write_at(spaces, color, 0, y);
}

/*
 * Draw column headers (A, B, C, ...)
 */
static void ss_draw_headers(void) {
    char buf[16];

    /* Clear header row */
    ss_clear_line(SS_HEADER_ROW, COLOR_HEADER);

    /* Row number column header */
    vga_write_at("    ", COLOR_HEADER, 0, SS_HEADER_ROW);

    /* Column letters */
    for (int i = 0; i < SS_VISIBLE_COLS; i++) {
        int col = view_col + i;
        if (col >= SS_MAX_COLS) break;

        int x = SS_ROW_NUM_WIDTH + i * SS_CELL_WIDTH;

        /* Center the letter in the cell width */
        for (int j = 0; j < SS_CELL_WIDTH; j++) buf[j] = ' ';
        buf[SS_CELL_WIDTH / 2] = col_to_letter(col);
        buf[SS_CELL_WIDTH] = '\0';

        vga_write_at(buf, COLOR_HEADER, x, SS_HEADER_ROW);
    }

    /* Draw separator line */
    ss_draw_hline(1, 0, VGA_WIDTH - 1, COLOR_BORDER);
}

/*
 * Format a cell's display string based on its content
 */
static void ss_format_cell(int row, int col) {
    cell_t* cell = &cells[row][col];

    if (cell->type == CELL_EMPTY) {
        memset(cell->display, ' ', SS_CELL_WIDTH);
        cell->display[SS_CELL_WIDTH] = '\0';
        return;
    }

    /* Format based on type */
    char temp[SS_MAX_CONTENT];
    if (cell->type == CELL_NUMBER || cell->type == CELL_FORMULA) {
        /* Format number with up to 2 decimal places */
        int whole = (int)cell->value;
        int frac = (int)((cell->value - whole) * 100);
        if (frac < 0) frac = -frac;

        if (frac == 0) {
            sprintf(temp, "%d", whole);
        } else {
            sprintf(temp, "%d.%02d", whole, frac);
        }
    } else {
        strncpy(temp, cell->content, SS_CELL_WIDTH);
        temp[SS_CELL_WIDTH] = '\0';
    }

    /* Right-align numbers, left-align text */
    int len = strlen(temp);
    if (len > SS_CELL_WIDTH) len = SS_CELL_WIDTH;

    memset(cell->display, ' ', SS_CELL_WIDTH);
    if (cell->type == CELL_NUMBER || cell->type == CELL_FORMULA) {
        /* Right align */
        strncpy(cell->display + SS_CELL_WIDTH - len, temp, len);
    } else {
        /* Left align */
        strncpy(cell->display, temp, len);
    }
    cell->display[SS_CELL_WIDTH] = '\0';
}

/*
 * Draw a single cell
 */
static void ss_draw_cell(int row, int col) {
    int screen_row = SS_DATA_START + (row - view_row);
    int screen_col = SS_ROW_NUM_WIDTH + (col - view_col) * SS_CELL_WIDTH;

    if (screen_row < SS_DATA_START || screen_row >= SS_DATA_START + SS_VISIBLE_ROWS) return;
    if (col < view_col || col >= view_col + SS_VISIBLE_COLS) return;

    cell_t* cell = &cells[row][col];
    uint8_t color;

    /* Determine color */
    if (row == cursor_row && col == cursor_col) {
        color = COLOR_CURSOR;
    } else if (cell->type == CELL_NUMBER) {
        color = COLOR_NUMBER;
    } else if (cell->type == CELL_FORMULA) {
        color = COLOR_FORMULA;
    } else {
        color = COLOR_CELL;
    }

    vga_write_at(cell->display, color, screen_col, screen_row);
}

/*
 * Draw row numbers
 */
static void ss_draw_row_numbers(void) {
    char buf[8];

    for (int i = 0; i < SS_VISIBLE_ROWS; i++) {
        int row = view_row + i;
        if (row >= SS_MAX_ROWS) break;

        int y = SS_DATA_START + i;
        sprintf(buf, "%3d ", row + 1);

        uint8_t color = (row == cursor_row) ? COLOR_CURSOR : COLOR_HEADER;
        vga_write_at(buf, color, 0, y);
    }
}

/*
 * Draw the entire grid
 */
static void ss_draw_grid(void) {
    /* Clear data area with cell background */
    for (int y = SS_DATA_START; y < SS_DATA_START + SS_VISIBLE_ROWS; y++) {
        ss_clear_line(y, COLOR_CELL);
    }

    /* Draw row numbers */
    ss_draw_row_numbers();

    /* Draw all visible cells */
    for (int r = 0; r < SS_VISIBLE_ROWS; r++) {
        int row = view_row + r;
        if (row >= SS_MAX_ROWS) break;

        for (int c = 0; c < SS_VISIBLE_COLS; c++) {
            int col = view_col + c;
            if (col >= SS_MAX_COLS) break;

            ss_draw_cell(row, col);
        }
    }
}

/*
 * Draw status bar
 */
static void ss_draw_status(void) {
    char buf[VGA_WIDTH + 1];

    ss_clear_line(SS_STATUS_ROW, COLOR_STATUS);

    /* Current cell reference */
    char cell_ref[8];
    sprintf(cell_ref, "%c%d", col_to_letter(cursor_col), cursor_row + 1);

    /* Build status line */
    sprintf(buf, " %s | %s%s | Arrows:Move Enter:Edit ESC:Exit F2:Save F3:Load F5:Clear",
            cell_ref,
            filename[0] ? filename : "[New]",
            modified ? "*" : "");

    /* Truncate if too long */
    if (strlen(buf) > VGA_WIDTH) buf[VGA_WIDTH] = '\0';

    vga_write_at(buf, COLOR_STATUS, 0, SS_STATUS_ROW);
}

/*
 * Draw input/formula bar
 */
static void ss_draw_input(void) {
    char buf[VGA_WIDTH + 1];

    ss_clear_line(SS_INPUT_ROW, COLOR_INPUT);

    cell_t* cell = &cells[cursor_row][cursor_col];

    if (editing) {
        /* Show edit buffer with cursor */
        sprintf(buf, " Edit: %s_", edit_buffer);
    } else if (cell->type != CELL_EMPTY) {
        /* Show cell content */
        sprintf(buf, " Content: %s", cell->content);
    } else {
        sprintf(buf, " (empty)");
    }

    if (strlen(buf) > VGA_WIDTH) buf[VGA_WIDTH] = '\0';
    vga_write_at(buf, COLOR_INPUT, 0, SS_INPUT_ROW);
}

/*
 * Draw entire screen
 */
static void ss_draw_screen(void) {
    vga_clear();
    ss_draw_headers();
    ss_draw_grid();
    ss_draw_status();
    ss_draw_input();
}

/*
 * Move cursor with scrolling
 */
static void ss_move_cursor(int drow, int dcol) {
    int old_row = cursor_row;
    int old_col = cursor_col;

    cursor_row += drow;
    cursor_col += dcol;

    /* Clamp to bounds */
    if (cursor_row < 0) cursor_row = 0;
    if (cursor_row >= SS_MAX_ROWS) cursor_row = SS_MAX_ROWS - 1;
    if (cursor_col < 0) cursor_col = 0;
    if (cursor_col >= SS_MAX_COLS) cursor_col = SS_MAX_COLS - 1;

    /* Scroll view if needed */
    int need_redraw = 0;

    if (cursor_row < view_row) {
        view_row = cursor_row;
        need_redraw = 1;
    } else if (cursor_row >= view_row + SS_VISIBLE_ROWS) {
        view_row = cursor_row - SS_VISIBLE_ROWS + 1;
        need_redraw = 1;
    }

    if (cursor_col < view_col) {
        view_col = cursor_col;
        need_redraw = 1;
    } else if (cursor_col >= view_col + SS_VISIBLE_COLS) {
        view_col = cursor_col - SS_VISIBLE_COLS + 1;
        need_redraw = 1;
    }

    if (need_redraw) {
        ss_draw_screen();
    } else {
        /* Just redraw affected cells */
        ss_draw_cell(old_row, old_col);
        ss_draw_cell(cursor_row, cursor_col);
        ss_draw_row_numbers();
        ss_draw_status();
        ss_draw_input();
    }
}

/*
 * Start editing current cell
 */
static void ss_start_edit(void) {
    cell_t* cell = &cells[cursor_row][cursor_col];

    editing = 1;
    strcpy(edit_buffer, cell->content);
    edit_pos = strlen(edit_buffer);

    ss_draw_input();
}

/*
 * End editing (save or cancel)
 */
static void ss_end_edit(int save) {
    if (!editing) return;

    editing = 0;

    if (save && edit_buffer[0] != '\0') {
        cell_t* cell = &cells[cursor_row][cursor_col];
        strcpy(cell->content, edit_buffer);

        /* Determine cell type */
        if (edit_buffer[0] == '=') {
            cell->type = CELL_FORMULA;
        } else {
            /* Check if it's a number */
            int is_num = 1;
            int has_dot = 0;
            for (int i = 0; edit_buffer[i]; i++) {
                char c = edit_buffer[i];
                if (c == '-' && i == 0) continue;
                if (c == '.' && !has_dot) { has_dot = 1; continue; }
                if (c < '0' || c > '9') { is_num = 0; break; }
            }

            if (is_num && edit_buffer[0] != '\0') {
                cell->type = CELL_NUMBER;
                /* Parse number */
                cell->value = 0;
                int neg = 0;
                int i = 0;
                if (edit_buffer[0] == '-') { neg = 1; i = 1; }

                double whole = 0, frac = 0, div = 1;
                int in_frac = 0;
                for (; edit_buffer[i]; i++) {
                    if (edit_buffer[i] == '.') { in_frac = 1; continue; }
                    if (in_frac) {
                        div *= 10;
                        frac = frac * 10 + (edit_buffer[i] - '0');
                    } else {
                        whole = whole * 10 + (edit_buffer[i] - '0');
                    }
                }
                cell->value = whole + frac / div;
                if (neg) cell->value = -cell->value;
            } else {
                cell->type = CELL_TEXT;
            }
        }

        /* Evaluate if formula */
        if (cell->type == CELL_FORMULA) {
            ss_evaluate_cell(cursor_row, cursor_col);
        }

        ss_format_cell(cursor_row, cursor_col);
        modified = 1;
    } else if (save) {
        /* Empty input - clear cell */
        cell_t* cell = &cells[cursor_row][cursor_col];
        cell->type = CELL_EMPTY;
        cell->content[0] = '\0';
        cell->value = 0;
        ss_format_cell(cursor_row, cursor_col);
        modified = 1;
    }

    edit_buffer[0] = '\0';
    edit_pos = 0;

    ss_draw_cell(cursor_row, cursor_col);
    ss_draw_status();
    ss_draw_input();
}

/*
 * Parse a cell reference like "A1" -> row=0, col=0
 * Returns 0 on success, -1 on failure
 */
static int ss_parse_ref(const char* ref, int* row, int* col) {
    if (!ref || !ref[0]) return -1;
    char c = ref[0];
    if (c >= 'a' && c <= 'z') c -= 32;
    if (c < 'A' || c > 'A' + SS_MAX_COLS - 1) return -1;
    *col = c - 'A';
    int r = 0;
    for (int i = 1; ref[i] >= '0' && ref[i] <= '9'; i++) {
        r = r * 10 + (ref[i] - '0');
    }
    if (r < 1 || r > SS_MAX_ROWS) return -1;
    *row = r - 1;
    return 0;
}

/*
 * Case-insensitive prefix match helper
 */
static int ss_match_func(const char* p, const char* name) {
    int i;
    for (i = 0; name[i]; i++) {
        char c = p[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        if (c != name[i]) return 0;
    }
    return (p[i] == '(') ? i + 1 : 0;
}

/*
 * Parse a number literal from string, advance pointer
 */
static double ss_parse_number(const char** pp) {
    const char* p = *pp;
    double val = 0;
    int neg = 0;
    if (*p == '-') { neg = 1; p++; }
    while (*p >= '0' && *p <= '9') {
        val = val * 10 + (*p - '0');
        p++;
    }
    if (*p == '.') {
        p++;
        double frac = 0.1;
        while (*p >= '0' && *p <= '9') {
            val += (*p - '0') * frac;
            frac *= 0.1;
            p++;
        }
    }
    if (neg) val = -val;
    *pp = p;
    return val;
}

/*
 * Parse a range like "A1:B10" from pointer, advance past closing ')'
 */
static int ss_parse_range(const char** pp, int* r1, int* c1, int* r2, int* c2) {
    const char* p = *pp;
    char ref1[8], ref2[8];
    int i = 0;
    while (*p && *p != ':' && *p != ')' && *p != ',' && i < 7) ref1[i++] = *p++;
    ref1[i] = '\0';
    if (*p == ':') {
        p++;
        i = 0;
        while (*p && *p != ')' && *p != ',' && i < 7) ref2[i++] = *p++;
        ref2[i] = '\0';
    } else {
        strncpy(ref2, ref1, 8);
    }
    if (*p == ')') p++;
    *pp = p;
    if (ss_parse_ref(ref1, r1, c1) < 0) return -1;
    if (ss_parse_ref(ref2, r2, c2) < 0) return -1;
    return 0;
}

/* Forward declaration */
static double ss_eval_expr(const char** pp);

/*
 * Parse a single value: number, cell reference, or function call
 */
static double ss_parse_value(const char** pp) {
    const char* p = *pp;
    while (*p == ' ') p++;
    int skip;

    /* Parenthesized sub-expression */
    if (*p == '(') {
        p++;
        *pp = p;
        double val = ss_eval_expr(pp);
        p = *pp;
        while (*p == ' ') p++;
        if (*p == ')') p++;
        *pp = p;
        return val;
    }

    /* --- Range functions --- */
    if ((skip = ss_match_func(p, "SUM")) > 0) {
        p += skip;
        int r1, c1, r2, c2;
        if (ss_parse_range(&p, &r1, &c1, &r2, &c2) < 0) { *pp = p; return 0; }
        double sum = 0;
        for (int r = r1; r <= r2; r++)
            for (int c = c1; c <= c2; c++)
                if (r >= 0 && r < SS_MAX_ROWS && c >= 0 && c < SS_MAX_COLS)
                    sum += cells[r][c].value;
        *pp = p;
        return sum;
    }

    if ((skip = ss_match_func(p, "AVERAGE")) > 0) {
        p += skip;
        int r1, c1, r2, c2;
        if (ss_parse_range(&p, &r1, &c1, &r2, &c2) < 0) { *pp = p; return 0; }
        double sum = 0;
        int cnt = 0;
        for (int r = r1; r <= r2; r++)
            for (int c = c1; c <= c2; c++)
                if (r >= 0 && r < SS_MAX_ROWS && c >= 0 && c < SS_MAX_COLS) {
                    if (cells[r][c].type == CELL_NUMBER || cells[r][c].type == CELL_FORMULA) {
                        sum += cells[r][c].value;
                        cnt++;
                    }
                }
        *pp = p;
        return cnt > 0 ? sum / cnt : 0;
    }

    if ((skip = ss_match_func(p, "MIN")) > 0) {
        p += skip;
        int r1, c1, r2, c2;
        if (ss_parse_range(&p, &r1, &c1, &r2, &c2) < 0) { *pp = p; return 0; }
        double min_val = 1e30;
        int found = 0;
        for (int r = r1; r <= r2; r++)
            for (int c = c1; c <= c2; c++)
                if (r >= 0 && r < SS_MAX_ROWS && c >= 0 && c < SS_MAX_COLS) {
                    if (cells[r][c].type == CELL_NUMBER || cells[r][c].type == CELL_FORMULA) {
                        if (!found || cells[r][c].value < min_val)
                            min_val = cells[r][c].value;
                        found = 1;
                    }
                }
        *pp = p;
        return found ? min_val : 0;
    }

    if ((skip = ss_match_func(p, "MAX")) > 0) {
        p += skip;
        int r1, c1, r2, c2;
        if (ss_parse_range(&p, &r1, &c1, &r2, &c2) < 0) { *pp = p; return 0; }
        double max_val = -1e30;
        int found = 0;
        for (int r = r1; r <= r2; r++)
            for (int c = c1; c <= c2; c++)
                if (r >= 0 && r < SS_MAX_ROWS && c >= 0 && c < SS_MAX_COLS) {
                    if (cells[r][c].type == CELL_NUMBER || cells[r][c].type == CELL_FORMULA) {
                        if (!found || cells[r][c].value > max_val)
                            max_val = cells[r][c].value;
                        found = 1;
                    }
                }
        *pp = p;
        return found ? max_val : 0;
    }

    if ((skip = ss_match_func(p, "COUNTBLANK")) > 0) {
        p += skip;
        int r1, c1, r2, c2;
        if (ss_parse_range(&p, &r1, &c1, &r2, &c2) < 0) { *pp = p; return 0; }
        int cnt = 0;
        for (int r = r1; r <= r2; r++)
            for (int c = c1; c <= c2; c++)
                if (r >= 0 && r < SS_MAX_ROWS && c >= 0 && c < SS_MAX_COLS)
                    if (cells[r][c].type == CELL_EMPTY) cnt++;
        *pp = p;
        return cnt;
    }

    if ((skip = ss_match_func(p, "COUNTA")) > 0) {
        p += skip;
        int r1, c1, r2, c2;
        if (ss_parse_range(&p, &r1, &c1, &r2, &c2) < 0) { *pp = p; return 0; }
        int cnt = 0;
        for (int r = r1; r <= r2; r++)
            for (int c = c1; c <= c2; c++)
                if (r >= 0 && r < SS_MAX_ROWS && c >= 0 && c < SS_MAX_COLS)
                    if (cells[r][c].type != CELL_EMPTY) cnt++;
        *pp = p;
        return cnt;
    }

    if ((skip = ss_match_func(p, "COUNT")) > 0) {
        p += skip;
        int r1, c1, r2, c2;
        if (ss_parse_range(&p, &r1, &c1, &r2, &c2) < 0) { *pp = p; return 0; }
        int cnt = 0;
        for (int r = r1; r <= r2; r++)
            for (int c = c1; c <= c2; c++)
                if (r >= 0 && r < SS_MAX_ROWS && c >= 0 && c < SS_MAX_COLS)
                    if (cells[r][c].type == CELL_NUMBER || cells[r][c].type == CELL_FORMULA)
                        cnt++;
        *pp = p;
        return cnt;
    }

    /* --- Scalar functions --- */
    if ((skip = ss_match_func(p, "ABS")) > 0) {
        p += skip;
        *pp = p;
        double val = ss_eval_expr(pp);
        p = *pp;
        if (*p == ')') p++;
        *pp = p;
        return val < 0 ? -val : val;
    }

    if ((skip = ss_match_func(p, "ROUND")) > 0) {
        p += skip;
        *pp = p;
        double val = ss_eval_expr(pp);
        p = *pp;
        int digits = 0;
        if (*p == ',') {
            p++; *pp = p;
            digits = (int)ss_eval_expr(pp);
            p = *pp;
        }
        if (*p == ')') p++;
        *pp = p;
        double mult = 1;
        for (int i = 0; i < digits; i++) mult *= 10;
        return (int)(val * mult + (val >= 0 ? 0.5 : -0.5)) / mult;
    }

    if ((skip = ss_match_func(p, "INT")) > 0) {
        p += skip;
        *pp = p;
        double val = ss_eval_expr(pp);
        p = *pp;
        if (*p == ')') p++;
        *pp = p;
        return (int)val;
    }

    if ((skip = ss_match_func(p, "SQRT")) > 0) {
        p += skip;
        *pp = p;
        double val = ss_eval_expr(pp);
        p = *pp;
        if (*p == ')') p++;
        *pp = p;
        if (val < 0) return 0;
        double guess = val / 2;
        if (guess == 0) return 0;
        for (int i = 0; i < 20; i++) {
            guess = (guess + val / guess) / 2;
        }
        return guess;
    }

    if ((skip = ss_match_func(p, "MOD")) > 0) {
        p += skip;
        *pp = p;
        double num = ss_eval_expr(pp);
        p = *pp;
        double divisor = 1;
        if (*p == ',') {
            p++; *pp = p;
            divisor = ss_eval_expr(pp);
            p = *pp;
        }
        if (*p == ')') p++;
        *pp = p;
        if (divisor == 0) return 0;
        return num - (int)(num / divisor) * divisor;
    }

    if ((skip = ss_match_func(p, "POWER")) > 0) {
        p += skip;
        *pp = p;
        double base = ss_eval_expr(pp);
        p = *pp;
        double exp = 1;
        if (*p == ',') {
            p++; *pp = p;
            exp = ss_eval_expr(pp);
            p = *pp;
        }
        if (*p == ')') p++;
        *pp = p;
        double result = 1;
        int iexp = (int)exp;
        if (iexp >= 0) {
            for (int i = 0; i < iexp; i++) result *= base;
        } else {
            for (int i = 0; i < -iexp; i++) result *= base;
            result = 1.0 / result;
        }
        return result;
    }

    if ((skip = ss_match_func(p, "IF")) > 0) {
        p += skip;
        *pp = p;
        double cond = ss_eval_expr(pp);
        p = *pp;
        double true_val = 0, false_val = 0;
        if (*p == ',') {
            p++; *pp = p;
            true_val = ss_eval_expr(pp);
            p = *pp;
        }
        if (*p == ',') {
            p++; *pp = p;
            false_val = ss_eval_expr(pp);
            p = *pp;
        }
        if (*p == ')') p++;
        *pp = p;
        return cond != 0 ? true_val : false_val;
    }

    /* Cell reference */
    if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')) {
        char ref[8];
        int i = 0;
        ref[i++] = *p++;
        while (*p >= '0' && *p <= '9' && i < 7) ref[i++] = *p++;
        ref[i] = '\0';
        int row, col;
        if (ss_parse_ref(ref, &row, &col) == 0) {
            *pp = p;
            return cells[row][col].value;
        }
        *pp = p;
        return 0;
    }

    /* Number literal */
    *pp = p;
    return ss_parse_number(pp);
}

/*
 * Parse multiplication and division (higher precedence)
 */
static double ss_eval_term(const char** pp) {
    double result = ss_parse_value(pp);
    const char* p = *pp;
    while (*p == ' ') p++;
    while (*p == '*' || *p == '/') {
        char op = *p++;
        *pp = p;
        double right = ss_parse_value(pp);
        p = *pp;
        if (op == '*') result *= right;
        else if (right != 0) result /= right;
        while (*p == ' ') p++;
    }
    *pp = p;
    return result;
}

/*
 * Parse addition and subtraction (lower precedence)
 */
static double ss_eval_expr(const char** pp) {
    const char* p = *pp;
    while (*p == ' ') p++;
    *pp = p;
    double result = ss_eval_term(pp);
    p = *pp;
    while (*p == ' ') p++;
    while (*p == '+' || *p == '-') {
        char op = *p++;
        *pp = p;
        double right = ss_eval_term(pp);
        p = *pp;
        if (op == '+') result += right;
        else result -= right;
        while (*p == ' ') p++;
    }
    *pp = p;
    return result;
}

/*
 * Evaluate a formula (recursive-descent expression parser)
 * Supports: cell refs, numbers, +, -, *, /, parentheses,
 * SUM, AVERAGE, MIN, MAX, COUNT, COUNTA, COUNTBLANK,
 * ABS, ROUND, INT, SQRT, MOD, POWER, IF
 */
static double ss_evaluate_formula(const char* formula) {
    if (formula[0] != '=') return 0;
    const char* p = formula + 1;
    return ss_eval_expr(&p);
}

/*
 * Evaluate a single cell
 */
static void ss_evaluate_cell(int row, int col) {
    cell_t* cell = &cells[row][col];

    if (cell->type == CELL_FORMULA) {
        cell->value = ss_evaluate_formula(cell->content);
    }
}

/*
 * Evaluate all cells (for recalculation)
 */
static void ss_evaluate_all(void) {
    for (int r = 0; r < SS_MAX_ROWS; r++) {
        for (int c = 0; c < SS_MAX_COLS; c++) {
            if (cells[r][c].type == CELL_FORMULA) {
                ss_evaluate_cell(r, c);
                ss_format_cell(r, c);
            }
        }
    }
}

/*
 * Clear all cells
 */
static void ss_clear_all(void) {
    memset(cells, 0, sizeof(cells));
    for (int r = 0; r < SS_MAX_ROWS; r++) {
        for (int c = 0; c < SS_MAX_COLS; c++) {
            ss_format_cell(r, c);
        }
    }
    cursor_row = 0;
    cursor_col = 0;
    view_row = 0;
    view_col = 0;
    modified = 0;
    filename[0] = '\0';
}

/*
 * Save spreadsheet to CSV file
 */
static int ss_save_file(const char* name) {
    /* Resolve path relative to current directory */
    char resolved_path[256];
    shell_resolve_path(name, resolved_path, sizeof(resolved_path));

    /* Find last used row and column */
    int max_row = 0, max_col = 0;
    for (int r = 0; r < SS_MAX_ROWS; r++) {
        for (int c = 0; c < SS_MAX_COLS; c++) {
            if (cells[r][c].type != CELL_EMPTY) {
                if (r > max_row) max_row = r;
                if (c > max_col) max_col = c;
            }
        }
    }

    /* Build CSV content */
    char* csv = (char*)kmalloc(32768);  /* 32KB buffer */
    if (!csv) return -1;

    int pos = 0;
    for (int r = 0; r <= max_row; r++) {
        for (int c = 0; c <= max_col; c++) {
            if (c > 0) csv[pos++] = ',';

            cell_t* cell = &cells[r][c];
            if (cell->type != CELL_EMPTY) {
                /* Copy content, escaping commas */
                for (int i = 0; cell->content[i] && pos < 32700; i++) {
                    csv[pos++] = cell->content[i];
                }
            }
        }
        csv[pos++] = '\n';
    }
    csv[pos] = '\0';

    /* Create or overwrite file */
    vfs_node_t* file = vfs_lookup(resolved_path);
    if (file) {
        /* Clear existing content */
        file->length = 0;
    } else {
        /* Create in current directory using shell's cwd node */
        vfs_node_t* cwd = shell_get_cwd_node();
        if (cwd->readdir == ext2_vfs_readdir) {
            file = ext2_create_file(cwd, name);
        } else {
            file = ramfs_create_file_in(cwd, name, VFS_FILE);
        }
        if (!file) {
            kfree(csv);
            return -1;
        }
    }

    /* Write content */
    vfs_write(file, 0, pos, (uint8_t*)csv);

    kfree(csv);
    strncpy(filename, name, sizeof(filename) - 1);
    modified = 0;

    return 0;
}

/*
 * Load spreadsheet from CSV file
 */
static int ss_load_file(const char* name) {
    /* Resolve path relative to current directory */
    char resolved_path[256];
    shell_resolve_path(name, resolved_path, sizeof(resolved_path));

    vfs_node_t* file = vfs_lookup(resolved_path);
    if (!file) return -1;

    /* Read file content */
    char* csv = (char*)kmalloc(file->length + 1);
    if (!csv) return -1;

    vfs_read(file, 0, file->length, (uint8_t*)csv);
    csv[file->length] = '\0';

    /* Clear current data */
    ss_clear_all();

    /* Parse CSV */
    int row = 0, col = 0;
    char cell_buf[SS_MAX_CONTENT];
    int buf_pos = 0;

    for (int i = 0; csv[i] && row < SS_MAX_ROWS; i++) {
        if (csv[i] == ',' || csv[i] == '\n') {
            cell_buf[buf_pos] = '\0';

            if (buf_pos > 0 && col < SS_MAX_COLS) {
                cell_t* cell = &cells[row][col];
                strcpy(cell->content, cell_buf);

                /* Determine type */
                if (cell_buf[0] == '=') {
                    cell->type = CELL_FORMULA;
                    ss_evaluate_cell(row, col);
                } else {
                    /* Check if number */
                    int is_num = 1;
                    for (int j = 0; cell_buf[j]; j++) {
                        char c = cell_buf[j];
                        if (c == '-' && j == 0) continue;
                        if (c == '.') continue;
                        if (c < '0' || c > '9') { is_num = 0; break; }
                    }
                    if (is_num) {
                        cell->type = CELL_NUMBER;
                        /* Simple atof */
                        cell->value = 0;
                        int neg = 0, j = 0;
                        if (cell_buf[0] == '-') { neg = 1; j = 1; }
                        double whole = 0, frac = 0, div = 1;
                        int in_frac = 0;
                        for (; cell_buf[j]; j++) {
                            if (cell_buf[j] == '.') { in_frac = 1; continue; }
                            if (in_frac) {
                                div *= 10;
                                frac = frac * 10 + (cell_buf[j] - '0');
                            } else {
                                whole = whole * 10 + (cell_buf[j] - '0');
                            }
                        }
                        cell->value = whole + frac / div;
                        if (neg) cell->value = -cell->value;
                    } else {
                        cell->type = CELL_TEXT;
                    }
                }
                ss_format_cell(row, col);
            }

            buf_pos = 0;
            if (csv[i] == '\n') {
                row++;
                col = 0;
            } else {
                col++;
            }
        } else if (buf_pos < SS_MAX_CONTENT - 1) {
            cell_buf[buf_pos++] = csv[i];
        }
    }

    kfree(csv);
    strncpy(filename, name, sizeof(filename) - 1);
    modified = 0;

    /* Re-evaluate all formulas */
    ss_evaluate_all();

    return 0;
}

/*
 * Prompt for filename (simple inline input)
 */
static int ss_prompt_filename(const char* prompt, char* buf, int bufsize) {
    ss_clear_line(SS_INPUT_ROW, COLOR_INPUT);
    vga_write_at(prompt, COLOR_INPUT, 0, SS_INPUT_ROW);

    int pos = 0;
    int prompt_len = strlen(prompt);

    while (1) {
        char c = keyboard_getchar();

        if (c == '\n') {
            buf[pos] = '\0';
            return (pos > 0) ? 0 : -1;
        } else if (c == KEY_ESCAPE) {
            return -1;
        } else if (c == '\b') {
            if (pos > 0) {
                pos--;
                buf[pos] = '\0';
                vga_write_at(" ", COLOR_INPUT, prompt_len + pos, SS_INPUT_ROW);
            }
        } else if (c >= 32 && c < 127 && pos < bufsize - 1) {
            buf[pos++] = c;
            buf[pos] = '\0';
            char tmp[2] = {c, '\0'};
            vga_write_at(tmp, COLOR_INPUT, prompt_len + pos - 1, SS_INPUT_ROW);
        }
    }
}

/*
 * Main spreadsheet loop
 */
void spreadsheet_run(void) {
    spreadsheet_init();

    /* Format all cells initially */
    for (int r = 0; r < SS_MAX_ROWS; r++) {
        for (int c = 0; c < SS_MAX_COLS; c++) {
            ss_format_cell(r, c);
        }
    }

    ss_draw_screen();

    while (1) {
        char c = keyboard_getchar();
        uint8_t uc = (uint8_t)c;

        if (editing) {
            /* Handle editing input */
            if (c == '\n') {
                ss_end_edit(1);  /* Save */
                ss_evaluate_all();
                ss_draw_grid();
            } else if (c == KEY_ESCAPE) {
                ss_end_edit(0);  /* Cancel */
            } else if (c == '\b') {
                if (edit_pos > 0) {
                    edit_pos--;
                    edit_buffer[edit_pos] = '\0';
                    ss_draw_input();
                }
            } else if (c >= 32 && c < 127 && edit_pos < SS_MAX_CONTENT - 1) {
                edit_buffer[edit_pos++] = c;
                edit_buffer[edit_pos] = '\0';
                ss_draw_input();
            }
        } else {
            /* Handle navigation and commands */
            if (uc == KEY_UP) {
                ss_move_cursor(-1, 0);
            } else if (uc == KEY_DOWN) {
                ss_move_cursor(1, 0);
            } else if (uc == KEY_LEFT) {
                ss_move_cursor(0, -1);
            } else if (uc == KEY_RIGHT) {
                ss_move_cursor(0, 1);
            } else if (uc == KEY_PAGEUP) {
                ss_move_cursor(-SS_VISIBLE_ROWS, 0);
            } else if (uc == KEY_PAGEDOWN) {
                ss_move_cursor(SS_VISIBLE_ROWS, 0);
            } else if (uc == KEY_HOME) {
                cursor_col = 0;
                view_col = 0;
                ss_draw_screen();
            } else if (uc == KEY_END) {
                cursor_col = SS_MAX_COLS - 1;
                if (cursor_col >= SS_VISIBLE_COLS) {
                    view_col = cursor_col - SS_VISIBLE_COLS + 1;
                }
                ss_draw_screen();
            } else if (c == '\n') {
                /* Enter - start editing */
                ss_start_edit();
            } else if (c == KEY_ESCAPE) {
                /* ESC - exit spreadsheet */
                break;
            } else if (uc == KEY_F2) {
                /* F2 - Save */
                char fname[64];
                if (filename[0]) {
                    strcpy(fname, filename);
                } else {
                    if (ss_prompt_filename(" Save as: ", fname, sizeof(fname)) < 0) {
                        ss_draw_screen();
                        continue;
                    }
                }
                if (ss_save_file(fname) == 0) {
                    ss_draw_status();
                    ss_clear_line(SS_INPUT_ROW, COLOR_INPUT);
                    vga_write_at(" Saved!", COLOR_INPUT, 0, SS_INPUT_ROW);
                } else {
                    ss_clear_line(SS_INPUT_ROW, COLOR_INPUT);
                    vga_write_at(" Save failed!", COLOR_INPUT, 0, SS_INPUT_ROW);
                }
            } else if (uc == KEY_F3) {
                /* F3 - Load */
                char fname[64];
                if (ss_prompt_filename(" Load file: ", fname, sizeof(fname)) < 0) {
                    ss_draw_screen();
                    continue;
                }
                if (ss_load_file(fname) == 0) {
                    ss_draw_screen();
                } else {
                    ss_clear_line(SS_INPUT_ROW, COLOR_INPUT);
                    vga_write_at(" File not found!", COLOR_INPUT, 0, SS_INPUT_ROW);
                }
            } else if (uc == KEY_F5) {
                /* F5 - Clear all */
                ss_clear_all();
                for (int r = 0; r < SS_MAX_ROWS; r++) {
                    for (int c = 0; c < SS_MAX_COLS; c++) {
                        ss_format_cell(r, c);
                    }
                }
                ss_draw_screen();
            } else if (c >= 32 && c < 127) {
                /* Start editing with typed character */
                ss_start_edit();
                edit_buffer[0] = c;
                edit_buffer[1] = '\0';
                edit_pos = 1;
                ss_draw_input();
            }
        }
    }
}

/*
 * Run spreadsheet with a file
 */
void spreadsheet_run_file(const char* file) {
    spreadsheet_init();

    /* Format all cells initially */
    for (int r = 0; r < SS_MAX_ROWS; r++) {
        for (int c = 0; c < SS_MAX_COLS; c++) {
            ss_format_cell(r, c);
        }
    }

    if (file && file[0]) {
        ss_load_file(file);
    }

    ss_draw_screen();

    /* Use the same main loop */
    while (1) {
        char c = keyboard_getchar();
        uint8_t uc = (uint8_t)c;

        if (editing) {
            if (c == '\n') {
                ss_end_edit(1);
                ss_evaluate_all();
                ss_draw_grid();
            } else if (c == KEY_ESCAPE) {
                ss_end_edit(0);
            } else if (c == '\b') {
                if (edit_pos > 0) {
                    edit_pos--;
                    edit_buffer[edit_pos] = '\0';
                    ss_draw_input();
                }
            } else if (c >= 32 && c < 127 && edit_pos < SS_MAX_CONTENT - 1) {
                edit_buffer[edit_pos++] = c;
                edit_buffer[edit_pos] = '\0';
                ss_draw_input();
            }
        } else {
            if (uc == KEY_UP) ss_move_cursor(-1, 0);
            else if (uc == KEY_DOWN) ss_move_cursor(1, 0);
            else if (uc == KEY_LEFT) ss_move_cursor(0, -1);
            else if (uc == KEY_RIGHT) ss_move_cursor(0, 1);
            else if (uc == KEY_PAGEUP) ss_move_cursor(-SS_VISIBLE_ROWS, 0);
            else if (uc == KEY_PAGEDOWN) ss_move_cursor(SS_VISIBLE_ROWS, 0);
            else if (c == '\n') ss_start_edit();
            else if (c == KEY_ESCAPE) break;
            else if (uc == KEY_F2) {
                char fname[64];
                if (filename[0]) strcpy(fname, filename);
                else if (ss_prompt_filename(" Save as: ", fname, sizeof(fname)) < 0) {
                    ss_draw_screen();
                    continue;
                }
                ss_save_file(fname);
                ss_draw_screen();
            } else if (uc == KEY_F3) {
                char fname[64];
                if (ss_prompt_filename(" Load: ", fname, sizeof(fname)) == 0) {
                    ss_load_file(fname);
                }
                ss_draw_screen();
            } else if (uc == KEY_F5) {
                ss_clear_all();
                for (int r = 0; r < SS_MAX_ROWS; r++) {
                    for (int c = 0; c < SS_MAX_COLS; c++) {
                        ss_format_cell(r, c);
                    }
                }
                ss_draw_screen();
            } else if (c >= 32 && c < 127) {
                ss_start_edit();
                edit_buffer[0] = c;
                edit_buffer[1] = '\0';
                edit_pos = 1;
                ss_draw_input();
            }
        }
    }
}

