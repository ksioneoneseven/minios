/*
 * MiniOS XGUI Spreadsheet
 *
 * A lightweight windowed spreadsheet with cell navigation, editing,
 * and basic formula support (SUM, numbers, cell references).
 */

#include "xgui/xgui.h"
#include "xgui/wm.h"
#include "xgui/widget.h"
#include "string.h"
#include "keyboard.h"
#include "stdio.h"
#include "vfs.h"
#include "ramfs.h"
#include "ext2.h"
#include "shell.h"
#include "heap.h"

/* Grid dimensions */
#define GS_COLS         26      /* A-Z */
#define GS_ROWS         100
#define GS_CELL_W       64      /* Pixel width per cell */
#define GS_CELL_H       16      /* Pixel height per cell */
#define GS_HDR_W        32      /* Row number column width */
#define GS_HDR_H        16      /* Column header height */
#define GS_MAX_CONTENT  32
#define GS_CHAR_W       8

/* Window size */
#define GS_WIN_W        560
#define GS_WIN_H        420

/* Toolbar height */
#define GS_TOOLBAR_H    26

/* Formula bar height */
#define GS_FBAR_H       20

/* Status bar height */
#define GS_STATUS_H     18

/* Filename max */
#define GS_FNAME_MAX    64

/* Colors */
#define GS_GRID_BG      XGUI_WHITE
#define GS_GRID_LINE    XGUI_RGB(200, 200, 200)
#define GS_HDR_BG       XGUI_RGB(220, 220, 220)
#define GS_HDR_FG       XGUI_BLACK
#define GS_CURSOR_BG    XGUI_RGB(180, 210, 255)
#define GS_CURSOR_BRD   XGUI_RGB(0, 100, 200)
#define GS_TEXT_FG       XGUI_BLACK
#define GS_NUM_FG        XGUI_RGB(0, 0, 160)
#define GS_FORMULA_FG    XGUI_RGB(128, 0, 128)
#define GS_FBAR_BG      XGUI_RGB(240, 240, 240)
#define GS_EDIT_BG       XGUI_RGB(255, 255, 220)
#define GS_TOOLBAR_BG    XGUI_RGB(235, 235, 235)
#define GS_STATUS_BG     XGUI_RGB(0, 100, 180)
#define GS_STATUS_FG     XGUI_WHITE

/* Cell types */
typedef enum {
    GS_EMPTY = 0,
    GS_TEXT,
    GS_NUMBER,
    GS_FORMULA
} gs_cell_type_t;

/* Cell */
typedef struct {
    gs_cell_type_t type;
    char content[GS_MAX_CONTENT];
    double value;
} gs_cell_t;

/* State */
static xgui_window_t* gs_window = NULL;
static gs_cell_t gs_cells[GS_ROWS][GS_COLS];
static int gs_cur_row = 0;
static int gs_cur_col = 0;
static int gs_view_row = 0;    /* First visible row */
static int gs_view_col = 0;    /* First visible column */
static int gs_editing = 0;
static char gs_edit_buf[GS_MAX_CONTENT];
static int gs_edit_pos = 0;

/* File state */
static char gs_filename[GS_FNAME_MAX];
static int gs_fname_mode = 0;     /* 0=none, 1=open, 2=save-as */
static char gs_fname_buf[GS_FNAME_MAX];
static int gs_fname_pos = 0;
static char gs_status_msg[64];
static int gs_status_ticks = 0;

/* Toolbar buttons */
static xgui_widget_t* gs_btn_open = NULL;
static xgui_widget_t* gs_btn_save = NULL;
static xgui_widget_t* gs_btn_saveas = NULL;

/* How many rows fit in the visible area */
static int gs_vis_rows(xgui_window_t* win) {
    int avail = win->client_height - GS_TOOLBAR_H - GS_FBAR_H - GS_HDR_H - GS_STATUS_H;
    int rows = avail / GS_CELL_H;
    if (rows > GS_ROWS) rows = GS_ROWS;
    if (rows < 1) rows = 1;
    return rows;
}

/* How many columns fit in the visible area */
static int gs_vis_cols(xgui_window_t* win) {
    int avail = win->client_width - GS_HDR_W;
    int cols = avail / GS_CELL_W;
    if (cols > GS_COLS) cols = GS_COLS;
    if (cols < 1) cols = 1;
    return cols;
}

/*
 * Parse a cell reference like "A1" -> row=0, col=0
 * Returns 0 on success, -1 on failure
 */
static int gs_parse_ref(const char* ref, int* row, int* col) {
    if (!ref || !ref[0]) return -1;
    char c = ref[0];
    if (c >= 'a' && c <= 'z') c -= 32;
    if (c < 'A' || c > 'A' + GS_COLS - 1) return -1;
    *col = c - 'A';
    int r = 0;
    for (int i = 1; ref[i]; i++) {
        if (ref[i] < '0' || ref[i] > '9') return -1;
        r = r * 10 + (ref[i] - '0');
    }
    if (r < 1 || r > GS_ROWS) return -1;
    *row = r - 1;
    return 0;
}

/*
 * Case-insensitive prefix match helper
 */
static int gs_match_func(const char* p, const char* name) {
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
static double gs_parse_number(const char** pp) {
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
 * Returns 0 on success, -1 on failure
 */
static int gs_parse_range(const char** pp, int* r1, int* c1, int* r2, int* c2) {
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
        /* Single cell, not a range */
        strncpy(ref2, ref1, 8);
    }
    if (*p == ')') p++;
    *pp = p;
    if (gs_parse_ref(ref1, r1, c1) < 0) return -1;
    if (gs_parse_ref(ref2, r2, c2) < 0) return -1;
    return 0;
}

/* Forward declaration */
static double gs_eval_expr(const char** pp);

/*
 * Parse a single value: number, cell reference, or function call
 */
static double gs_parse_value(const char** pp) {
    const char* p = *pp;
    while (*p == ' ') p++;
    int skip;

    /* Parenthesized sub-expression */
    if (*p == '(') {
        p++;
        *pp = p;
        double val = gs_eval_expr(pp);
        p = *pp;
        while (*p == ' ') p++;
        if (*p == ')') p++;
        *pp = p;
        return val;
    }

    /* --- Range functions: SUM, AVERAGE, MIN, MAX, COUNT, COUNTA, COUNTBLANK --- */
    if ((skip = gs_match_func(p, "SUM")) > 0) {
        p += skip;
        int r1, c1, r2, c2;
        if (gs_parse_range(&p, &r1, &c1, &r2, &c2) < 0) { *pp = p; return 0; }
        double sum = 0;
        for (int r = r1; r <= r2; r++)
            for (int c = c1; c <= c2; c++)
                if (r >= 0 && r < GS_ROWS && c >= 0 && c < GS_COLS)
                    sum += gs_cells[r][c].value;
        *pp = p;
        return sum;
    }

    if ((skip = gs_match_func(p, "AVERAGE")) > 0) {
        p += skip;
        int r1, c1, r2, c2;
        if (gs_parse_range(&p, &r1, &c1, &r2, &c2) < 0) { *pp = p; return 0; }
        double sum = 0;
        int cnt = 0;
        for (int r = r1; r <= r2; r++)
            for (int c = c1; c <= c2; c++)
                if (r >= 0 && r < GS_ROWS && c >= 0 && c < GS_COLS) {
                    if (gs_cells[r][c].type == GS_NUMBER || gs_cells[r][c].type == GS_FORMULA) {
                        sum += gs_cells[r][c].value;
                        cnt++;
                    }
                }
        *pp = p;
        return cnt > 0 ? sum / cnt : 0;
    }

    if ((skip = gs_match_func(p, "MIN")) > 0) {
        p += skip;
        int r1, c1, r2, c2;
        if (gs_parse_range(&p, &r1, &c1, &r2, &c2) < 0) { *pp = p; return 0; }
        double min_val = 1e30;
        int found = 0;
        for (int r = r1; r <= r2; r++)
            for (int c = c1; c <= c2; c++)
                if (r >= 0 && r < GS_ROWS && c >= 0 && c < GS_COLS) {
                    if (gs_cells[r][c].type == GS_NUMBER || gs_cells[r][c].type == GS_FORMULA) {
                        if (!found || gs_cells[r][c].value < min_val)
                            min_val = gs_cells[r][c].value;
                        found = 1;
                    }
                }
        *pp = p;
        return found ? min_val : 0;
    }

    if ((skip = gs_match_func(p, "MAX")) > 0) {
        p += skip;
        int r1, c1, r2, c2;
        if (gs_parse_range(&p, &r1, &c1, &r2, &c2) < 0) { *pp = p; return 0; }
        double max_val = -1e30;
        int found = 0;
        for (int r = r1; r <= r2; r++)
            for (int c = c1; c <= c2; c++)
                if (r >= 0 && r < GS_ROWS && c >= 0 && c < GS_COLS) {
                    if (gs_cells[r][c].type == GS_NUMBER || gs_cells[r][c].type == GS_FORMULA) {
                        if (!found || gs_cells[r][c].value > max_val)
                            max_val = gs_cells[r][c].value;
                        found = 1;
                    }
                }
        *pp = p;
        return found ? max_val : 0;
    }

    if ((skip = gs_match_func(p, "COUNTBLANK")) > 0) {
        p += skip;
        int r1, c1, r2, c2;
        if (gs_parse_range(&p, &r1, &c1, &r2, &c2) < 0) { *pp = p; return 0; }
        int cnt = 0;
        for (int r = r1; r <= r2; r++)
            for (int c = c1; c <= c2; c++)
                if (r >= 0 && r < GS_ROWS && c >= 0 && c < GS_COLS)
                    if (gs_cells[r][c].type == GS_EMPTY) cnt++;
        *pp = p;
        return cnt;
    }

    if ((skip = gs_match_func(p, "COUNTA")) > 0) {
        p += skip;
        int r1, c1, r2, c2;
        if (gs_parse_range(&p, &r1, &c1, &r2, &c2) < 0) { *pp = p; return 0; }
        int cnt = 0;
        for (int r = r1; r <= r2; r++)
            for (int c = c1; c <= c2; c++)
                if (r >= 0 && r < GS_ROWS && c >= 0 && c < GS_COLS)
                    if (gs_cells[r][c].type != GS_EMPTY) cnt++;
        *pp = p;
        return cnt;
    }

    if ((skip = gs_match_func(p, "COUNT")) > 0) {
        p += skip;
        int r1, c1, r2, c2;
        if (gs_parse_range(&p, &r1, &c1, &r2, &c2) < 0) { *pp = p; return 0; }
        int cnt = 0;
        for (int r = r1; r <= r2; r++)
            for (int c = c1; c <= c2; c++)
                if (r >= 0 && r < GS_ROWS && c >= 0 && c < GS_COLS)
                    if (gs_cells[r][c].type == GS_NUMBER || gs_cells[r][c].type == GS_FORMULA)
                        cnt++;
        *pp = p;
        return cnt;
    }

    /* --- Scalar functions: ABS, ROUND, INT, SQRT, MOD, POWER, IF --- */
    if ((skip = gs_match_func(p, "ABS")) > 0) {
        p += skip;
        *pp = p;
        double val = gs_eval_expr(pp);
        p = *pp;
        if (*p == ')') p++;
        *pp = p;
        return val < 0 ? -val : val;
    }

    if ((skip = gs_match_func(p, "ROUND")) > 0) {
        p += skip;
        *pp = p;
        double val = gs_eval_expr(pp);
        p = *pp;
        int digits = 0;
        if (*p == ',') {
            p++; *pp = p;
            digits = (int)gs_eval_expr(pp);
            p = *pp;
        }
        if (*p == ')') p++;
        *pp = p;
        double mult = 1;
        for (int i = 0; i < digits; i++) mult *= 10;
        return (int)(val * mult + (val >= 0 ? 0.5 : -0.5)) / mult;
    }

    if ((skip = gs_match_func(p, "INT")) > 0) {
        p += skip;
        *pp = p;
        double val = gs_eval_expr(pp);
        p = *pp;
        if (*p == ')') p++;
        *pp = p;
        return (int)val;
    }

    if ((skip = gs_match_func(p, "SQRT")) > 0) {
        p += skip;
        *pp = p;
        double val = gs_eval_expr(pp);
        p = *pp;
        if (*p == ')') p++;
        *pp = p;
        if (val < 0) return 0;
        /* Newton's method for sqrt */
        double guess = val / 2;
        if (guess == 0) return 0;
        for (int i = 0; i < 20; i++) {
            guess = (guess + val / guess) / 2;
        }
        return guess;
    }

    if ((skip = gs_match_func(p, "MOD")) > 0) {
        p += skip;
        *pp = p;
        double num = gs_eval_expr(pp);
        p = *pp;
        double divisor = 1;
        if (*p == ',') {
            p++; *pp = p;
            divisor = gs_eval_expr(pp);
            p = *pp;
        }
        if (*p == ')') p++;
        *pp = p;
        if (divisor == 0) return 0;
        return num - (int)(num / divisor) * divisor;
    }

    if ((skip = gs_match_func(p, "POWER")) > 0) {
        p += skip;
        *pp = p;
        double base = gs_eval_expr(pp);
        p = *pp;
        double exp = 1;
        if (*p == ',') {
            p++; *pp = p;
            exp = gs_eval_expr(pp);
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

    if ((skip = gs_match_func(p, "IF")) > 0) {
        p += skip;
        *pp = p;
        double cond = gs_eval_expr(pp);
        p = *pp;
        double true_val = 0, false_val = 0;
        if (*p == ',') {
            p++; *pp = p;
            true_val = gs_eval_expr(pp);
            p = *pp;
        }
        if (*p == ',') {
            p++; *pp = p;
            false_val = gs_eval_expr(pp);
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
        if (gs_parse_ref(ref, &row, &col) == 0) {
            *pp = p;
            return gs_cells[row][col].value;
        }
        *pp = p;
        return 0;
    }

    /* Number literal */
    *pp = p;
    return gs_parse_number(pp);
}

/*
 * Parse multiplication and division (higher precedence)
 */
static double gs_eval_term(const char** pp) {
    double result = gs_parse_value(pp);
    const char* p = *pp;
    while (*p == ' ') p++;
    while (*p == '*' || *p == '/') {
        char op = *p++;
        *pp = p;
        double right = gs_parse_value(pp);
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
static double gs_eval_expr(const char** pp) {
    const char* p = *pp;
    while (*p == ' ') p++;
    *pp = p;
    double result = gs_eval_term(pp);
    p = *pp;
    while (*p == ' ') p++;
    while (*p == '+' || *p == '-') {
        char op = *p++;
        *pp = p;
        double right = gs_eval_term(pp);
        p = *pp;
        if (op == '+') result += right;
        else result -= right;
        while (*p == ' ') p++;
    }
    *pp = p;
    return result;
}

/*
 * Formula evaluator entry point
 * Supports: cell refs, numbers, +, -, *, /, parentheses,
 * SUM, AVERAGE, MIN, MAX, COUNT, COUNTA, COUNTBLANK,
 * ABS, ROUND, INT, SQRT, MOD, POWER, IF
 */
static double gs_eval_formula(const char* formula) {
    if (!formula || formula[0] != '=') return 0;
    const char* p = formula + 1;
    return gs_eval_expr(&p);
}

/*
 * Evaluate a single cell
 */
static void gs_eval_cell(int r, int c) {
    gs_cell_t* cell = &gs_cells[r][c];
    if (cell->content[0] == '\0') {
        cell->type = GS_EMPTY;
        cell->value = 0;
        return;
    }
    if (cell->content[0] == '=') {
        cell->type = GS_FORMULA;
        cell->value = gs_eval_formula(cell->content);
    } else {
        /* Try to parse as number */
        const char* s = cell->content;
        int is_num = 1;
        int has_digit = 0;
        if (*s == '-') s++;
        for (; *s; s++) {
            if (*s == '.') continue;
            if (*s < '0' || *s > '9') { is_num = 0; break; }
            has_digit = 1;
        }
        if (is_num && has_digit) {
            cell->type = GS_NUMBER;
            /* Simple atof */
            cell->value = 0;
            int neg = 0;
            s = cell->content;
            if (*s == '-') { neg = 1; s++; }
            int dot = 0;
            double frac = 0.1;
            for (; *s; s++) {
                if (*s == '.') { dot = 1; continue; }
                if (dot) { cell->value += (*s - '0') * frac; frac *= 0.1; }
                else { cell->value = cell->value * 10 + (*s - '0'); }
            }
            if (neg) cell->value = -cell->value;
        } else {
            cell->type = GS_TEXT;
            cell->value = 0;
        }
    }
}

/*
 * Evaluate all cells
 */
static void gs_eval_all(void) {
    for (int r = 0; r < GS_ROWS; r++)
        for (int c = 0; c < GS_COLS; c++)
            gs_eval_cell(r, c);
}

/*
 * Commit edit buffer to current cell
 */
static void gs_commit_edit(void) {
    gs_cell_t* cell = &gs_cells[gs_cur_row][gs_cur_col];
    if (gs_edit_buf[0] == '\0') {
        cell->type = GS_EMPTY;
        cell->content[0] = '\0';
        cell->value = 0;
    } else {
        strncpy(cell->content, gs_edit_buf, GS_MAX_CONTENT - 1);
        cell->content[GS_MAX_CONTENT - 1] = '\0';
    }
    gs_eval_all();
    gs_editing = 0;
}

/*
 * Ensure cursor is visible
 */
static void gs_ensure_visible(void) {
    int vis = gs_window ? gs_vis_rows(gs_window) : 10;
    if (gs_cur_row < gs_view_row) gs_view_row = gs_cur_row;
    if (gs_cur_row >= gs_view_row + vis) gs_view_row = gs_cur_row - vis + 1;
    if (gs_view_row < 0) gs_view_row = 0;

    int vcols = gs_window ? gs_vis_cols(gs_window) : 8;
    if (gs_cur_col < gs_view_col) gs_view_col = gs_cur_col;
    if (gs_cur_col >= gs_view_col + vcols) gs_view_col = gs_cur_col - vcols + 1;
    if (gs_view_col < 0) gs_view_col = 0;
}

/*
 * Helper: find parent directory node and extract filename from a resolved path.
 * E.g. "/home/docs/test.csv" -> parent="/home/docs", fname="test.csv"
 */
static vfs_node_t* gs_find_parent(const char* resolved, const char** out_fname) {
    /* Find last slash */
    const char* last_slash = NULL;
    for (const char* p = resolved; *p; p++) {
        if (*p == '/') last_slash = p;
    }
    if (!last_slash || last_slash == resolved) {
        /* Root or no slash */
        *out_fname = (last_slash == resolved) ? resolved + 1 : resolved;
        return vfs_lookup("/");
    }
    /* Build parent path */
    static char parent_path[256];
    int plen = (int)(last_slash - resolved);
    if (plen >= 256) plen = 255;
    memcpy(parent_path, resolved, plen);
    parent_path[plen] = '\0';
    *out_fname = last_slash + 1;
    return vfs_lookup(parent_path);
}

/*
 * Save spreadsheet as CSV
 */
static int gs_save_file(const char* path) {
    char resolved[256];
    shell_resolve_path(path, resolved, sizeof(resolved));

    /* Build CSV content */
    /* Find last non-empty row */
    int last_row = 0;
    for (int r = 0; r < GS_ROWS; r++) {
        for (int c = 0; c < GS_COLS; c++) {
            if (gs_cells[r][c].content[0]) last_row = r + 1;
        }
    }
    if (last_row == 0) last_row = 1;

    /* Estimate size: each cell up to GS_MAX_CONTENT + comma + newline */
    int bufsize = last_row * GS_COLS * (GS_MAX_CONTENT + 2) + 1;
    uint8_t* buf = (uint8_t*)kmalloc(bufsize);
    if (!buf) {
        strncpy(gs_status_msg, "Out of memory", sizeof(gs_status_msg));
        gs_status_ticks = 100;
        return -1;
    }

    int pos = 0;
    for (int r = 0; r < last_row; r++) {
        for (int c = 0; c < GS_COLS; c++) {
            if (c > 0) buf[pos++] = ',';
            const char* s = gs_cells[r][c].content;
            int len = strlen(s);
            memcpy(&buf[pos], s, len);
            pos += len;
        }
        buf[pos++] = '\n';
    }
    buf[pos] = '\0';

    /* Create or overwrite file */
    vfs_node_t* node = vfs_lookup(resolved);
    if (node) {
        node->length = 0;
    } else {
        const char* fname;
        vfs_node_t* parent = gs_find_parent(resolved, &fname);
        if (!parent) {
            kfree(buf);
            strncpy(gs_status_msg, "Directory not found", sizeof(gs_status_msg));
            gs_status_ticks = 100;
            return -1;
        }
        if (parent->readdir == ext2_vfs_readdir) {
            node = ext2_create_file(parent, fname);
        } else {
            node = ramfs_create_file_in(parent, fname, VFS_FILE);
        }
        if (!node) {
            kfree(buf);
            strncpy(gs_status_msg, "Failed to create file", sizeof(gs_status_msg));
            gs_status_ticks = 100;
            return -1;
        }
    }

    int32_t written = vfs_write(node, 0, pos, buf);
    kfree(buf);

    if (written < 0) {
        strncpy(gs_status_msg, "Write failed", sizeof(gs_status_msg));
        gs_status_ticks = 100;
        return -1;
    }

    strncpy(gs_filename, resolved, GS_FNAME_MAX - 1);
    gs_filename[GS_FNAME_MAX - 1] = '\0';
    snprintf(gs_status_msg, sizeof(gs_status_msg), "Saved %d bytes", pos);
    gs_status_ticks = 100;
    return 0;
}

/*
 * Load spreadsheet from CSV
 */
static int gs_load_file(const char* path) {
    char resolved[256];
    shell_resolve_path(path, resolved, sizeof(resolved));

    vfs_node_t* node = vfs_lookup(resolved);
    if (!node) {
        strncpy(gs_status_msg, "File not found", sizeof(gs_status_msg));
        gs_status_ticks = 100;
        return -1;
    }

    uint32_t size = node->length;
    if (size == 0) {
        memset(gs_cells, 0, sizeof(gs_cells));
        strncpy(gs_filename, resolved, GS_FNAME_MAX - 1);
        gs_filename[GS_FNAME_MAX - 1] = '\0';
        gs_eval_all();
        return 0;
    }

    uint8_t* buf = (uint8_t*)kmalloc(size + 1);
    if (!buf) {
        strncpy(gs_status_msg, "Out of memory", sizeof(gs_status_msg));
        gs_status_ticks = 100;
        return -1;
    }

    int32_t rd = vfs_read(node, 0, size, buf);
    if (rd <= 0) {
        kfree(buf);
        strncpy(gs_status_msg, "Read failed", sizeof(gs_status_msg));
        gs_status_ticks = 100;
        return -1;
    }
    buf[rd] = '\0';

    /* Parse CSV */
    memset(gs_cells, 0, sizeof(gs_cells));
    int row = 0, col = 0, ci = 0;
    for (int i = 0; i <= rd && row < GS_ROWS; i++) {
        char ch = (i < rd) ? (char)buf[i] : '\n';
        if (ch == ',' || ch == '\n') {
            if (col < GS_COLS) {
                gs_cells[row][col].content[ci] = '\0';
            }
            ci = 0;
            if (ch == ',') {
                col++;
            } else {
                row++;
                col = 0;
            }
        } else if (ch != '\r') {
            if (col < GS_COLS && ci < GS_MAX_CONTENT - 1) {
                gs_cells[row][col].content[ci++] = ch;
            }
        }
    }

    kfree(buf);
    strncpy(gs_filename, resolved, GS_FNAME_MAX - 1);
    gs_filename[GS_FNAME_MAX - 1] = '\0';
    gs_cur_row = 0;
    gs_cur_col = 0;
    gs_view_row = 0;
    gs_view_col = 0;
    gs_eval_all();
    snprintf(gs_status_msg, sizeof(gs_status_msg), "Loaded %s", gs_filename);
    gs_status_ticks = 100;
    return 0;
}

/*
 * Toolbar button callbacks
 */
static void gs_on_open(xgui_widget_t* w) {
    (void)w;
    if (gs_editing) gs_commit_edit();
    gs_fname_mode = 1;
    gs_fname_buf[0] = '\0';
    gs_fname_pos = 0;
    if (gs_window) gs_window->dirty = true;
}

static void gs_on_save(xgui_widget_t* w) {
    (void)w;
    if (gs_editing) gs_commit_edit();
    if (gs_filename[0]) {
        gs_save_file(gs_filename);
        if (gs_window) gs_window->dirty = true;
    } else {
        gs_fname_mode = 2;
        gs_fname_buf[0] = '\0';
        gs_fname_pos = 0;
        if (gs_window) gs_window->dirty = true;
    }
}

static void gs_on_save_as(xgui_widget_t* w) {
    (void)w;
    if (gs_editing) gs_commit_edit();
    gs_fname_mode = 2;
    gs_fname_buf[0] = '\0';
    gs_fname_pos = 0;
    if (gs_window) gs_window->dirty = true;
}

/*
 * Paint callback
 */
static void gs_paint(xgui_window_t* win) {
    int cw = win->client_width;
    int ch = win->client_height;
    int vis = gs_vis_rows(win);

    /* Toolbar background */
    xgui_win_rect_filled(win, 0, 0, cw, GS_TOOLBAR_H, GS_TOOLBAR_BG);
    xgui_win_hline(win, 0, GS_TOOLBAR_H - 1, cw, XGUI_RGB(180, 180, 180));

    /* Draw toolbar buttons */
    xgui_widgets_draw(win);

    /* Formula bar */
    int fbar_y = GS_TOOLBAR_H;
    xgui_win_rect_filled(win, 0, fbar_y, cw, GS_FBAR_H, GS_FBAR_BG);
    {
        /* Cell reference (e.g., "A1" or "Z100") */
        char ref[8];
        ref[0] = 'A' + gs_cur_col;
        snprintf(ref + 1, sizeof(ref) - 1, "%d", gs_cur_row + 1);
        xgui_win_text_transparent(win, 4, fbar_y + 2, ref, XGUI_BLACK);
        xgui_win_vline(win, 40, fbar_y, GS_FBAR_H, GS_GRID_LINE);

        /* Show content or edit buffer - clip at window edge naturally */
        const char* show = gs_editing ? gs_edit_buf : gs_cells[gs_cur_row][gs_cur_col].content;
        if (show && show[0]) {
            /* Calculate how many chars fit before clipping */
            int avail_pixels = cw - 46 - 4; /* Leave 4px margin on right */
            int max_chars = avail_pixels / GS_CHAR_W;
            int len = strlen(show);

            if (len <= max_chars) {
                /* Show full text */
                xgui_win_text_transparent(win, 46, fbar_y + 2, show, XGUI_BLACK);
            } else {
                /* Show with ellipsis indicator */
                char display[GS_MAX_CONTENT + 1];
                if (max_chars > 3) {
                    strncpy(display, show, max_chars - 3);
                    strcpy(display + max_chars - 3, "...");
                } else {
                    strncpy(display, show, max_chars);
                    display[max_chars] = '\0';
                }
                xgui_win_text_transparent(win, 46, fbar_y + 2, display, XGUI_BLACK);
            }
        }
    }
    xgui_win_hline(win, 0, fbar_y + GS_FBAR_H - 1, cw, GS_GRID_LINE);

    int grid_y = GS_TOOLBAR_H + GS_FBAR_H;

    /* Column headers */
    int vcols = gs_vis_cols(win);
    xgui_win_rect_filled(win, 0, grid_y, cw, GS_HDR_H, GS_HDR_BG);
    xgui_win_hline(win, 0, grid_y + GS_HDR_H - 1, cw, GS_GRID_LINE);
    for (int ci = 0; ci < vcols; ci++) {
        int c = gs_view_col + ci;
        if (c >= GS_COLS) break;
        int x = GS_HDR_W + ci * GS_CELL_W;
        if (x >= cw) break;
        char lbl[2] = { 'A' + c, '\0' };
        int tx = x + GS_CELL_W / 2 - GS_CHAR_W / 2;
        xgui_win_text_transparent(win, tx, grid_y + 1, lbl,
                                  c == gs_cur_col ? GS_CURSOR_BRD : GS_HDR_FG);
        xgui_win_vline(win, x, grid_y, GS_HDR_H, GS_GRID_LINE);
    }

    /* Row headers + cells */
    for (int vi = 0; vi < vis; vi++) {
        int r = gs_view_row + vi;
        if (r >= GS_ROWS) break;
        int y = grid_y + GS_HDR_H + vi * GS_CELL_H;

        /* Row number */
        xgui_win_rect_filled(win, 0, y, GS_HDR_W, GS_CELL_H, GS_HDR_BG);
        char rnum[4];
        snprintf(rnum, sizeof(rnum), "%d", r + 1);
        xgui_win_text_transparent(win, 4, y + 1, rnum,
                                  r == gs_cur_row ? GS_CURSOR_BRD : GS_HDR_FG);

        /* Cells */
        for (int ci = 0; ci < vcols; ci++) {
            int c = gs_view_col + ci;
            if (c >= GS_COLS) break;
            int x = GS_HDR_W + ci * GS_CELL_W;
            if (x >= cw) break;
            gs_cell_t* cell = &gs_cells[r][c];

            /* Background */
            uint32_t bg = GS_GRID_BG;
            if (r == gs_cur_row && c == gs_cur_col) {
                bg = gs_editing ? GS_EDIT_BG : GS_CURSOR_BG;
            }
            xgui_win_rect_filled(win, x, y, GS_CELL_W, GS_CELL_H, bg);

            /* Cell border */
            xgui_win_vline(win, x, y, GS_CELL_H, GS_GRID_LINE);
            xgui_win_hline(win, x, y + GS_CELL_H - 1, GS_CELL_W, GS_GRID_LINE);

            /* Cursor highlight */
            if (r == gs_cur_row && c == gs_cur_col) {
                xgui_win_rect(win, x, y, GS_CELL_W, GS_CELL_H, GS_CURSOR_BRD);
            }

            /* Cell text */
            if (r == gs_cur_row && c == gs_cur_col && gs_editing) {
                /* Show edit buffer with cursor */
                if (gs_edit_buf[0]) {
                    char disp[GS_MAX_CONTENT];
                    int maxch = (GS_CELL_W - 4) / GS_CHAR_W;
                    strncpy(disp, gs_edit_buf, maxch);
                    disp[maxch] = '\0';
                    xgui_win_text_transparent(win, x + 2, y + 1, disp, GS_TEXT_FG);
                }
                /* Edit cursor */
                int cx = x + 2 + gs_edit_pos * GS_CHAR_W;
                if (cx < x + GS_CELL_W - 2) {
                    xgui_win_rect_filled(win, cx, y + 1, 2, GS_CELL_H - 2, XGUI_BLACK);
                }
            } else if (cell->type != GS_EMPTY) {
                char disp[GS_MAX_CONTENT];
                int maxch = (GS_CELL_W - 4) / GS_CHAR_W;

                if (cell->type == GS_NUMBER || cell->type == GS_FORMULA) {
                    int whole = (int)cell->value;
                    int frac = (int)((cell->value - whole) * 100);
                    if (frac < 0) frac = -frac;
                    if (frac == 0) snprintf(disp, maxch + 1, "%d", whole);
                    else snprintf(disp, maxch + 1, "%d.%02d", whole, frac);
                    /* Right-align numbers */
                    int len = strlen(disp);
                    int tx = x + GS_CELL_W - 2 - len * GS_CHAR_W;
                    if (tx < x + 2) tx = x + 2;
                    uint32_t fg = cell->type == GS_FORMULA ? GS_FORMULA_FG : GS_NUM_FG;
                    xgui_win_text_transparent(win, tx, y + 1, disp, fg);
                } else {
                    strncpy(disp, cell->content, maxch);
                    disp[maxch] = '\0';
                    xgui_win_text_transparent(win, x + 2, y + 1, disp, GS_TEXT_FG);
                }
            }
        }
    }

    /* Right border */
    {
        int drawn_cols = vcols;
        if (gs_view_col + drawn_cols > GS_COLS) drawn_cols = GS_COLS - gs_view_col;
        int rx = GS_HDR_W + drawn_cols * GS_CELL_W;
        if (rx < cw) {
            xgui_win_vline(win, rx, grid_y, GS_HDR_H + vis * GS_CELL_H, GS_GRID_LINE);
        }
    }

    /* Draw selected/editing cell overlay with full text (after all cells, so it appears on top) */
    if (gs_cur_row >= gs_view_row && gs_cur_row < gs_view_row + vis &&
        gs_cur_col >= gs_view_col && gs_cur_col < gs_view_col + vcols) {

        /* Determine what text to show and whether to expand */
        const char* display_text = NULL;
        int text_len = 0;
        int cell_chars = (GS_CELL_W - 4) / GS_CHAR_W;
        uint32_t bg_color;
        bool show_cursor = false;

        if (gs_editing) {
            /* Editing mode: show edit buffer */
            display_text = gs_edit_buf;
            text_len = strlen(gs_edit_buf);
            bg_color = GS_EDIT_BG;
            show_cursor = true;
        } else {
            /* Selection mode: show cell content if text type */
            gs_cell_t* sel_cell = &gs_cells[gs_cur_row][gs_cur_col];
            if (sel_cell->type == GS_TEXT && sel_cell->content[0]) {
                display_text = sel_cell->content;
                text_len = strlen(sel_cell->content);
                bg_color = GS_CURSOR_BG;
            }
        }

        /* Only draw overlay if text is longer than fits in cell */
        if (display_text && text_len > cell_chars) {
            int ci = gs_cur_col - gs_view_col;
            int vi = gs_cur_row - gs_view_row;
            int x = GS_HDR_W + ci * GS_CELL_W;
            int y = grid_y + GS_HDR_H + vi * GS_CELL_H;

            /* Calculate how many cells we need to span */
            int needed_width = text_len * GS_CHAR_W + 4;
            int cells_needed = (needed_width + GS_CELL_W - 1) / GS_CELL_W;
            int overlay_width = cells_needed * GS_CELL_W;

            /* Clip to window edge */
            if (x + overlay_width > cw) {
                overlay_width = cw - x;
            }

            /* Draw overlay background */
            xgui_win_rect_filled(win, x, y, overlay_width, GS_CELL_H, bg_color);

            /* Draw border around extended area */
            xgui_win_rect(win, x, y, overlay_width, GS_CELL_H, GS_CURSOR_BRD);

            /* Draw grid lines that were covered */
            for (int i = 1; i < cells_needed; i++) {
                int line_x = x + i * GS_CELL_W;
                if (line_x < x + overlay_width) {
                    xgui_win_vline(win, line_x, y, GS_CELL_H,
                                  XGUI_RGB(180, 200, 230)); /* Lighter grid line */
                }
            }

            /* Draw full text */
            xgui_win_text_transparent(win, x + 2, y + 1, display_text, GS_TEXT_FG);

            /* Draw edit cursor if in edit mode */
            if (show_cursor) {
                int cx = x + 2 + gs_edit_pos * GS_CHAR_W;
                if (cx < x + overlay_width - 2) {
                    xgui_win_rect_filled(win, cx, y + 1, 2, GS_CELL_H - 2, XGUI_BLACK);
                }
            }
        }
    }

    /* Status bar */
    int sy = ch - GS_STATUS_H;
    xgui_win_rect_filled(win, 0, sy, cw, GS_STATUS_H, GS_STATUS_BG);

    if (gs_fname_mode) {
        const char* prompt = gs_fname_mode == 1 ? "Open: " : "Save as: ";
        char status[128];
        snprintf(status, sizeof(status), "%s%s_", prompt, gs_fname_buf);
        xgui_win_text_transparent(win, 4, sy + 2, status, GS_STATUS_FG);
    } else if (gs_status_ticks > 0) {
        xgui_win_text_transparent(win, 4, sy + 2, gs_status_msg, XGUI_RGB(255, 255, 100));
        gs_status_ticks--;
        if (gs_status_ticks > 0) win->dirty = true;
    } else {
        char status[128];
        snprintf(status, sizeof(status), " %s  Cell %c%d",
                 gs_filename[0] ? gs_filename : "[No File]",
                 'A' + gs_cur_col, gs_cur_row + 1);
        xgui_win_text_transparent(win, 4, sy + 2, status, GS_STATUS_FG);
    }
}

/*
 * Event handler
 */
static void gs_handler(xgui_window_t* win, xgui_event_t* event) {
    /* Let widgets handle toolbar button clicks */
    if (xgui_widgets_handle_event(win, event)) {
        return;
    }

    if (event->type == XGUI_EVENT_WINDOW_CLOSE) {
        xgui_window_destroy(win);
        gs_window = NULL;
        gs_btn_open = NULL;
        gs_btn_save = NULL;
        gs_btn_saveas = NULL;
        return;
    }

    /* Filename input mode */
    if (gs_fname_mode) {
        if (event->type == XGUI_EVENT_KEY_CHAR) {
            char c = event->key.character;
            if (c >= 32 && c < 127 && gs_fname_pos < GS_FNAME_MAX - 1) {
                gs_fname_buf[gs_fname_pos++] = c;
                gs_fname_buf[gs_fname_pos] = '\0';
                win->dirty = true;
            }
            return;
        }
        if (event->type == XGUI_EVENT_KEY_DOWN) {
            uint8_t key = event->key.keycode;
            if (key == '\n' || key == '\r') {
                if (gs_fname_pos > 0) {
                    if (gs_fname_mode == 1) {
                        gs_load_file(gs_fname_buf);
                    } else {
                        gs_save_file(gs_fname_buf);
                    }
                }
                gs_fname_mode = 0;
                win->dirty = true;
            } else if (key == KEY_ESCAPE) {
                gs_fname_mode = 0;
                win->dirty = true;
            } else if (key == '\b') {
                if (gs_fname_pos > 0) {
                    gs_fname_pos--;
                    gs_fname_buf[gs_fname_pos] = '\0';
                    win->dirty = true;
                }
            }
        }
        return;
    }

    /* Mouse click on grid to select cell */
    if (event->type == XGUI_EVENT_MOUSE_DOWN) {
        int mx = event->mouse.x;
        int my = event->mouse.y;
        int grid_y = GS_TOOLBAR_H + GS_FBAR_H + GS_HDR_H;

        if (mx >= GS_HDR_W && my >= grid_y) {
            int new_col = gs_view_col + (mx - GS_HDR_W) / GS_CELL_W;
            int new_row = gs_view_row + (my - grid_y) / GS_CELL_H;
            if (new_col >= 0 && new_col < GS_COLS &&
                new_row >= 0 && new_row < GS_ROWS) {
                if (gs_editing) gs_commit_edit();
                gs_cur_col = new_col;
                gs_cur_row = new_row;
                win->dirty = true;
            }
        }
        return;
    }

    /* Printable characters via KEY_CHAR */
    if (event->type == XGUI_EVENT_KEY_CHAR) {
        char c = event->key.character;
        if (c >= 32 && c < 127) {
            if (!gs_editing) {
                /* Start editing with this character */
                gs_editing = 1;
                gs_edit_buf[0] = c;
                gs_edit_buf[1] = '\0';
                gs_edit_pos = 1;
            } else if (gs_edit_pos < GS_MAX_CONTENT - 1) {
                gs_edit_buf[gs_edit_pos++] = c;
                gs_edit_buf[gs_edit_pos] = '\0';
            }
            win->dirty = true;
        }
        return;
    }

    if (event->type != XGUI_EVENT_KEY_DOWN) return;

    uint8_t key = event->key.keycode;

    if (gs_editing) {
        switch (key) {
            case '\n':
            case '\r':
                gs_commit_edit();
                /* Move down after enter */
                if (gs_cur_row < GS_ROWS - 1) gs_cur_row++;
                gs_ensure_visible();
                win->dirty = true;
                return;
            case KEY_ESCAPE:
                gs_editing = 0;
                win->dirty = true;
                return;
            case '\b':
                if (gs_edit_pos > 0) {
                    gs_edit_pos--;
                    gs_edit_buf[gs_edit_pos] = '\0';
                    win->dirty = true;
                }
                return;
            case '\t':
                gs_commit_edit();
                if (gs_cur_col < GS_COLS - 1) {
                    gs_cur_col++;
                } else if (gs_cur_row < GS_ROWS - 1) {
                    gs_cur_col = 0;
                    gs_cur_row++;
                }
                gs_ensure_visible();
                win->dirty = true;
                return;
            default:
                return;
        }
    }

    /* Navigation mode */
    switch (key) {
        case KEY_UP:
            if (gs_cur_row > 0) gs_cur_row--;
            gs_ensure_visible();
            break;
        case KEY_DOWN:
            if (gs_cur_row < GS_ROWS - 1) gs_cur_row++;
            gs_ensure_visible();
            break;
        case KEY_LEFT:
            if (gs_cur_col > 0) gs_cur_col--;
            gs_ensure_visible();
            break;
        case KEY_RIGHT:
            if (gs_cur_col < GS_COLS - 1) gs_cur_col++;
            gs_ensure_visible();
            break;
        case '\t':
            if (gs_cur_col < GS_COLS - 1) gs_cur_col++;
            else if (gs_cur_row < GS_ROWS - 1) { gs_cur_col = 0; gs_cur_row++; }
            gs_ensure_visible();
            break;
        case '\n':
        case '\r':
            /* Enter: start editing current cell */
            gs_editing = 1;
            strncpy(gs_edit_buf, gs_cells[gs_cur_row][gs_cur_col].content, GS_MAX_CONTENT - 1);
            gs_edit_buf[GS_MAX_CONTENT - 1] = '\0';
            gs_edit_pos = strlen(gs_edit_buf);
            break;
        case KEY_DELETE:
            /* Delete cell contents */
            gs_cells[gs_cur_row][gs_cur_col].type = GS_EMPTY;
            gs_cells[gs_cur_row][gs_cur_col].content[0] = '\0';
            gs_cells[gs_cur_row][gs_cur_col].value = 0;
            gs_eval_all();
            break;
        case KEY_HOME:
            gs_cur_col = 0;
            gs_ensure_visible();
            break;
        case KEY_END:
            gs_cur_col = GS_COLS - 1;
            gs_ensure_visible();
            break;
        case KEY_PAGEUP:
            gs_cur_row -= gs_vis_rows(win);
            if (gs_cur_row < 0) gs_cur_row = 0;
            gs_ensure_visible();
            break;
        case KEY_PAGEDOWN:
            gs_cur_row += gs_vis_rows(win);
            if (gs_cur_row >= GS_ROWS) gs_cur_row = GS_ROWS - 1;
            gs_ensure_visible();
            break;
        default:
            return;
    }
    win->dirty = true;
}

/*
 * Create the GUI Spreadsheet window
 */
void xgui_gui_spreadsheet_create(void) {
    if (gs_window) {
        xgui_window_focus(gs_window);
        return;
    }

    /* Reset state */
    memset(gs_cells, 0, sizeof(gs_cells));
    gs_cur_row = 0;
    gs_cur_col = 0;
    gs_view_row = 0;
    gs_view_col = 0;
    gs_editing = 0;
    gs_edit_pos = 0;
    gs_edit_buf[0] = '\0';
    gs_filename[0] = '\0';
    gs_fname_mode = 0;
    gs_fname_pos = 0;
    gs_status_msg[0] = '\0';
    gs_status_ticks = 0;

    gs_window = xgui_window_create("Spreadsheet", 30, 20, GS_WIN_W, GS_WIN_H,
                                    XGUI_WINDOW_DEFAULT | XGUI_WINDOW_MAXIMIZABLE);
    if (!gs_window) return;

    xgui_window_set_paint(gs_window, gs_paint);
    xgui_window_set_handler(gs_window, gs_handler);

    /* Create toolbar buttons */
    gs_btn_open   = xgui_button_create(gs_window, 4, 2, 50, 20, "Open");
    gs_btn_save   = xgui_button_create(gs_window, 58, 2, 50, 20, "Save");
    gs_btn_saveas = xgui_button_create(gs_window, 112, 2, 60, 20, "Save As");

    if (gs_btn_open)   xgui_widget_set_onclick(gs_btn_open, gs_on_open);
    if (gs_btn_save)   xgui_widget_set_onclick(gs_btn_save, gs_on_save);
    if (gs_btn_saveas) xgui_widget_set_onclick(gs_btn_saveas, gs_on_save_as);
}

/*
 * Create the GUI Spreadsheet and open a specific file
 */
void xgui_gui_spreadsheet_open_file(const char* path) {
    xgui_gui_spreadsheet_create();
    if (gs_window && path && path[0]) {
        gs_load_file(path);
        gs_window->dirty = true;
    }
}
