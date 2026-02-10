/*
 * MiniOS XGUI Sticky Note
 *
 * Up to 3 independent sticky notes with different background colors.
 * State is persisted to /mnt/conf/note0.dat .. note2.dat so notes
 * survive reboots.  Every text change, move, minimize, and close is
 * saved immediately.
 *
 * File format (text):
 *   Line 0: x,y,open,minimized,line_count,cur_line,cur_col
 *   Lines 1..N: text content (one per line, may be empty)
 */

#include "xgui/xgui.h"
#include "xgui/wm.h"
#include "xgui/widget.h"
#include "string.h"
#include "stdio.h"
#include "keyboard.h"
#include "vfs.h"
#include "ext2.h"
#include "heap.h"
#include "serial.h"

/* Line-array text storage */
#define SN_MAX_NOTES    3
#define SN_MAX_LINES   64
#define SN_MAX_COLS    80
#define SN_VIS_LINES   13
#define SN_CHAR_W       8
#define SN_CHAR_H      16
#define SN_PAD          6
#define SN_WIN_W       240
#define SN_WIN_H       230
#define SN_FILE_MAX    8192

/* Sticky note colors: pale yellow, pale pink, pale green */
static const uint32_t sn_colors[SN_MAX_NOTES] = {
    XGUI_RGB(255, 255, 200),  /* Pale yellow */
    XGUI_RGB(255, 210, 210),  /* Pale pink */
    XGUI_RGB(210, 255, 210),  /* Pale green */
};

static const char* sn_titles[SN_MAX_NOTES] = {
    "Sticky Note",
    "Sticky Note 2",
    "Sticky Note 3",
};

static const char* sn_filenames[SN_MAX_NOTES] = {
    "note0.dat",
    "note1.dat",
    "note2.dat",
};

/* Spawn offsets so notes don't overlap exactly */
static const int sn_offsets[SN_MAX_NOTES][2] = {
    { 80, 60 },
    { 120, 90 },
    { 160, 120 },
};

/* Per-note state */
typedef struct {
    xgui_window_t* window;
    char lines[SN_MAX_LINES][SN_MAX_COLS + 1];
    int line_count;
    int cur_line;
    int cur_col;
    int scroll_y;
    uint32_t bg_color;
    int slot;           /* Which slot 0-2 */
    /* Selection state */
    int sel_start_line;
    int sel_start_col;
    int sel_end_line;
    int sel_end_col;
    bool selecting;     /* Mouse drag in progress */
} sticky_note_t;

static sticky_note_t notes[SN_MAX_NOTES];

/* ------------------------------------------------------------------ */
/*  Persistence helpers                                                */
/* ------------------------------------------------------------------ */

/*
 * Simple integer-to-string (signed)
 */
static int sn_itoa(int val, char* buf) {
    char tmp[16];
    int neg = 0, i = 0;
    if (val < 0) { neg = 1; val = -val; }
    do { tmp[i++] = '0' + (val % 10); val /= 10; } while (val);
    int pos = 0;
    if (neg) buf[pos++] = '-';
    while (i > 0) buf[pos++] = tmp[--i];
    buf[pos] = '\0';
    return pos;
}

/*
 * Simple string-to-int (signed)
 */
static int sn_atoi(const char* s) {
    int neg = 0, val = 0;
    if (*s == '-') { neg = 1; s++; }
    while (*s >= '0' && *s <= '9') { val = val * 10 + (*s - '0'); s++; }
    return neg ? -val : val;
}

/*
 * Build the file path for a note slot
 */
static void sn_filepath(int slot, char* buf, int bufsz) {
    snprintf(buf, bufsz, "/mnt/conf/%s", sn_filenames[slot]);
}

/*
 * Save a single note to disk
 */
static void sn_save(int slot) {
    sticky_note_t* sn = &notes[slot];

    /* Build file content into a temp buffer */
    uint8_t* buf = (uint8_t*)kmalloc(SN_FILE_MAX);
    if (!buf) return;
    int pos = 0;

    /* Header: x,y,open,minimized,line_count,cur_line,cur_col */
    int wx = 0, wy = 0, is_open = 0, is_min = 0;
    if (sn->window) {
        wx = sn->window->x;
        wy = sn->window->y;
        is_open = 1;
        is_min = (sn->window->flags & XGUI_WINDOW_MINIMIZED) ? 1 : 0;
    }

    pos += sn_itoa(wx, (char*)buf + pos);       buf[pos++] = ',';
    pos += sn_itoa(wy, (char*)buf + pos);       buf[pos++] = ',';
    pos += sn_itoa(is_open, (char*)buf + pos);  buf[pos++] = ',';
    pos += sn_itoa(is_min, (char*)buf + pos);   buf[pos++] = ',';
    pos += sn_itoa(sn->line_count, (char*)buf + pos); buf[pos++] = ',';
    pos += sn_itoa(sn->cur_line, (char*)buf + pos);   buf[pos++] = ',';
    pos += sn_itoa(sn->cur_col, (char*)buf + pos);
    buf[pos++] = '\n';

    /* Text lines */
    for (int i = 0; i < sn->line_count && pos < SN_FILE_MAX - SN_MAX_COLS - 2; i++) {
        int len = 0;
        while (len < SN_MAX_COLS && sn->lines[i][len]) len++;
        if (len > 0) memcpy(buf + pos, sn->lines[i], len);
        pos += len;
        buf[pos++] = '\n';
    }

    /* Write to disk */
    char path[64];
    sn_filepath(slot, path, sizeof(path));

    vfs_node_t* node = vfs_lookup(path);
    if (!node) {
        vfs_node_t* conf_dir = vfs_lookup("/mnt/conf");
        if (!conf_dir) { kfree(buf); return; }
        node = ext2_create_file(conf_dir, sn_filenames[slot]);
        if (!node) { kfree(buf); return; }
    }

    vfs_write(node, 0, pos, buf);
    /* Update node length in case file shrank */
    node->length = pos;

    kfree(buf);
}

/* Geometry cache for restore (populated by sn_load_with_geo) */
static int sn_restore_geo[SN_MAX_NOTES][4]; /* x, y, was_open, was_min */

/*
 * Load a single note — version that stores geometry in the shared cache
 */
static int sn_load_with_geo(int slot) {
    sticky_note_t* sn = &notes[slot];
    sn->window = NULL;
    sn->line_count = 1;
    sn->cur_line = 0;
    sn->cur_col = 0;
    sn->scroll_y = 0;
    sn->bg_color = sn_colors[slot];
    sn->slot = slot;
    memset(sn->lines, 0, sizeof(sn->lines));

    char path[64];
    sn_filepath(slot, path, sizeof(path));

    vfs_node_t* node = vfs_lookup(path);
    if (!node || node->length == 0) {
        sn_restore_geo[slot][2] = 0; /* not open */
        return 0;
    }

    uint32_t fsize = node->length;
    if (fsize > SN_FILE_MAX) fsize = SN_FILE_MAX;

    uint8_t* buf = (uint8_t*)kmalloc(fsize + 1);
    if (!buf) return 0;

    int32_t rd = vfs_read(node, 0, fsize, buf);
    if (rd <= 0) { kfree(buf); return 0; }
    buf[rd] = '\0';

    /* Parse header line */
    const char* p = (const char*)buf;
    int hdr[7] = {0};
    for (int i = 0; i < 7 && *p && *p != '\n'; i++) {
        hdr[i] = sn_atoi(p);
        while (*p && *p != ',' && *p != '\n') p++;
        if (*p == ',') p++;
    }
    if (*p == '\n') p++;

    sn_restore_geo[slot][0] = hdr[0]; /* x */
    sn_restore_geo[slot][1] = hdr[1]; /* y */
    sn_restore_geo[slot][2] = hdr[2]; /* was_open */
    sn_restore_geo[slot][3] = hdr[3]; /* was_min */

    int lcount = hdr[4];
    if (lcount < 1) lcount = 1;
    if (lcount > SN_MAX_LINES) lcount = SN_MAX_LINES;

    sn->line_count = lcount;
    sn->cur_line = hdr[5];
    sn->cur_col = hdr[6];

    /* Parse text lines */
    for (int i = 0; i < lcount && *p; i++) {
        int col = 0;
        while (*p && *p != '\n' && col < SN_MAX_COLS) {
            sn->lines[i][col++] = *p++;
        }
        sn->lines[i][col] = '\0';
        if (*p == '\n') p++;
    }

    /* Clamp cursor */
    if (sn->cur_line >= sn->line_count) sn->cur_line = sn->line_count - 1;
    int len = 0;
    while (len < SN_MAX_COLS && sn->lines[sn->cur_line][len]) len++;
    if (sn->cur_col > len) sn->cur_col = len;

    kfree(buf);
    return hdr[2]; /* was_open */
}

/* ------------------------------------------------------------------ */
/*  Core note helpers                                                  */
/* ------------------------------------------------------------------ */

static sticky_note_t* sn_from_win(xgui_window_t* win) {
    for (int i = 0; i < SN_MAX_NOTES; i++) {
        if (notes[i].window == win) return &notes[i];
    }
    return NULL;
}

static int sn_line_len(sticky_note_t* sn, int ln) {
    if (ln < 0 || ln >= sn->line_count) return 0;
    int len = 0;
    while (len < SN_MAX_COLS && sn->lines[ln][len]) len++;
    return len;
}

/* Word wrap: how many columns fit in the note width */
static int sn_wrap_cols(sticky_note_t* sn) {
    if (!sn->window) return 28;
    int cols = (sn->window->client_width - SN_PAD * 2) / SN_CHAR_W;
    return cols > 0 ? cols : 1;
}

/* How many screen rows does a logical line occupy with word wrap */
static int sn_line_screen_rows(sticky_note_t* sn, int ln) {
    int len = sn_line_len(sn, ln);
    int cols = sn_wrap_cols(sn);
    if (len == 0) return 1;
    return (len + cols - 1) / cols;
}

static void sn_ensure_visible(sticky_note_t* sn) {
    int vis = SN_VIS_LINES;
    if (sn->cur_line < sn->scroll_y) sn->scroll_y = sn->cur_line;
    /* Count screen rows from scroll_y to cur_line */
    int rows = 0;
    for (int ln = sn->scroll_y; ln <= sn->cur_line && ln < sn->line_count; ln++) {
        rows += sn_line_screen_rows(sn, ln);
    }
    while (rows > vis && sn->scroll_y < sn->cur_line) {
        rows -= sn_line_screen_rows(sn, sn->scroll_y);
        sn->scroll_y++;
    }
    if (sn->scroll_y < 0) sn->scroll_y = 0;
}

static void sn_insert_char(sticky_note_t* sn, char c) {
    int len = sn_line_len(sn, sn->cur_line);
    if (len >= SN_MAX_COLS) return;
    for (int i = len; i >= sn->cur_col; i--) {
        sn->lines[sn->cur_line][i + 1] = sn->lines[sn->cur_line][i];
    }
    sn->lines[sn->cur_line][sn->cur_col] = c;
    sn->cur_col++;
}

static void sn_insert_newline(sticky_note_t* sn) {
    if (sn->line_count >= SN_MAX_LINES) return;
    for (int i = sn->line_count; i > sn->cur_line + 1; i--) {
        memcpy(sn->lines[i], sn->lines[i - 1], SN_MAX_COLS + 1);
    }
    sn->line_count++;
    int len = sn_line_len(sn, sn->cur_line);
    memcpy(sn->lines[sn->cur_line + 1], &sn->lines[sn->cur_line][sn->cur_col], len - sn->cur_col + 1);
    sn->lines[sn->cur_line][sn->cur_col] = '\0';
    sn->cur_line++;
    sn->cur_col = 0;
    sn_ensure_visible(sn);
}

static void sn_do_backspace(sticky_note_t* sn) {
    if (sn->cur_col > 0) {
        int len = sn_line_len(sn, sn->cur_line);
        for (int i = sn->cur_col - 1; i < len; i++) {
            sn->lines[sn->cur_line][i] = sn->lines[sn->cur_line][i + 1];
        }
        sn->cur_col--;
    } else if (sn->cur_line > 0) {
        int prev_len = sn_line_len(sn, sn->cur_line - 1);
        int cur_len = sn_line_len(sn, sn->cur_line);
        if (prev_len + cur_len <= SN_MAX_COLS) {
            memcpy(&sn->lines[sn->cur_line - 1][prev_len], sn->lines[sn->cur_line], cur_len + 1);
            for (int i = sn->cur_line; i < sn->line_count - 1; i++) {
                memcpy(sn->lines[i], sn->lines[i + 1], SN_MAX_COLS + 1);
            }
            sn->line_count--;
            sn->cur_line--;
            sn->cur_col = prev_len;
        }
    }
    sn_ensure_visible(sn);
}

/* ------------------------------------------------------------------ */
/*  Selection helpers                                                   */
/* ------------------------------------------------------------------ */

static void sn_clear_selection(sticky_note_t* sn) {
    sn->sel_start_line = -1;
    sn->sel_start_col = -1;
    sn->sel_end_line = -1;
    sn->sel_end_col = -1;
    sn->selecting = false;
}

static bool sn_has_selection(sticky_note_t* sn) {
    return sn->sel_start_line >= 0 && sn->sel_end_line >= 0 &&
           (sn->sel_start_line != sn->sel_end_line || sn->sel_start_col != sn->sel_end_col);
}

static void sn_get_selection(sticky_note_t* sn, int* sl, int* sc, int* el, int* ec) {
    if (sn->sel_start_line < sn->sel_end_line ||
        (sn->sel_start_line == sn->sel_end_line && sn->sel_start_col <= sn->sel_end_col)) {
        *sl = sn->sel_start_line; *sc = sn->sel_start_col;
        *el = sn->sel_end_line;   *ec = sn->sel_end_col;
    } else {
        *sl = sn->sel_end_line;   *sc = sn->sel_end_col;
        *el = sn->sel_start_line; *ec = sn->sel_start_col;
    }
}

static bool sn_in_selection(sticky_note_t* sn, int line, int col) {
    if (!sn_has_selection(sn)) return false;
    int sl, sc, el, ec;
    sn_get_selection(sn, &sl, &sc, &el, &ec);
    if (line < sl || line > el) return false;
    if (line == sl && col < sc) return false;
    if (line == el && col >= ec) return false;
    if (line == sl && line == el) return col >= sc && col < ec;
    return true;
}

static void sn_copy_selection(sticky_note_t* sn) {
    if (!sn_has_selection(sn)) return;
    int sl, sc, el, ec;
    sn_get_selection(sn, &sl, &sc, &el, &ec);

    char buf[XGUI_CLIPBOARD_MAX];
    int pos = 0;
    for (int ln = sl; ln <= el && pos < XGUI_CLIPBOARD_MAX - 2; ln++) {
        int start = (ln == sl) ? sc : 0;
        int end = (ln == el) ? ec : sn_line_len(sn, ln);
        for (int c = start; c < end && pos < XGUI_CLIPBOARD_MAX - 2; c++) {
            buf[pos++] = sn->lines[ln][c];
        }
        if (ln < el && pos < XGUI_CLIPBOARD_MAX - 2) {
            buf[pos++] = '\n';
        }
    }
    buf[pos] = '\0';
    xgui_clipboard_set(buf, pos);
}

static void sn_delete_selection(sticky_note_t* sn) {
    if (!sn_has_selection(sn)) return;
    int sl, sc, el, ec;
    sn_get_selection(sn, &sl, &sc, &el, &ec);

    if (sl == el) {
        int len = sn_line_len(sn, sl);
        memmove(&sn->lines[sl][sc], &sn->lines[sl][ec], len - ec + 1);
    } else {
        int end_len = sn_line_len(sn, el);
        int keep = end_len - ec;
        if (keep > 0) {
            memcpy(&sn->lines[sl][sc], &sn->lines[el][ec], keep);
        }
        sn->lines[sl][sc + keep] = '\0';
        int remove_count = el - sl;
        for (int i = sl + 1; i + remove_count < sn->line_count; i++) {
            memcpy(sn->lines[i], sn->lines[i + remove_count], SN_MAX_COLS + 1);
        }
        sn->line_count -= remove_count;
    }
    sn->cur_line = sl;
    sn->cur_col = sc;
    sn_clear_selection(sn);
    sn_ensure_visible(sn);
}

static void sn_paste(sticky_note_t* sn) {
    const char* text = xgui_clipboard_get();
    if (!text) return;
    if (sn_has_selection(sn)) {
        sn_delete_selection(sn);
    }
    for (int i = 0; text[i]; i++) {
        if (text[i] == '\n') {
            sn_insert_newline(sn);
        } else if (text[i] >= 32 && text[i] < 127) {
            sn_insert_char(sn, text[i]);
        }
    }
    sn_ensure_visible(sn);
}

static void sn_select_all(sticky_note_t* sn) {
    sn->sel_start_line = 0;
    sn->sel_start_col = 0;
    sn->sel_end_line = sn->line_count - 1;
    sn->sel_end_col = sn_line_len(sn, sn->line_count - 1);
}

static void sn_screen_to_pos(sticky_note_t* sn, int mx, int my, int* out_line, int* out_col) {
    int click_row = (my - SN_PAD) / SN_CHAR_H;
    int click_col = (mx - SN_PAD) / SN_CHAR_W;
    if (click_col < 0) click_col = 0;
    int cols = sn_wrap_cols(sn);
    int screen_row = 0;

    for (int ln = sn->scroll_y; ln < sn->line_count; ln++) {
        int wrap_rows = sn_line_screen_rows(sn, ln);
        if (click_row < screen_row + wrap_rows) {
            int wr = click_row - screen_row;
            int new_col = wr * cols + click_col;
            int len = sn_line_len(sn, ln);
            if (new_col > len) new_col = len;
            if (new_col < 0) new_col = 0;
            *out_line = ln;
            *out_col = new_col;
            return;
        }
        screen_row += wrap_rows;
    }
    /* Past end of text */
    *out_line = sn->line_count - 1;
    *out_col = sn_line_len(sn, sn->line_count - 1);
}

/* ------------------------------------------------------------------ */
/*  Paint                                                              */
/* ------------------------------------------------------------------ */

static void sn_paint(xgui_window_t* win) {
    sticky_note_t* sn = sn_from_win(win);
    if (!sn) return;

    int cw = win->client_width;
    int ch = win->client_height;

    /* Fill with sticky note color */
    xgui_win_rect_filled(win, 0, 0, cw, ch, sn->bg_color);

    /* Draw visible lines with word wrap and selection highlighting */
    int cols = sn_wrap_cols(sn);
    int screen_row = 0;
    for (int ln = sn->scroll_y; ln < sn->line_count && screen_row < SN_VIS_LINES; ln++) {
        int len = sn_line_len(sn, ln);
        int wrap_rows = sn_line_screen_rows(sn, ln);

        for (int wr = 0; wr < wrap_rows && screen_row < SN_VIS_LINES; wr++, screen_row++) {
            int y = SN_PAD + screen_row * SN_CHAR_H;
            int start_col = wr * cols;
            int end_col = start_col + cols;
            if (end_col > len) end_col = len;

            for (int ci = start_col; ci < end_col; ci++) {
                int cx = SN_PAD + (ci - start_col) * SN_CHAR_W;
                char tc[2] = { sn->lines[ln][ci], '\0' };
                if (sn_in_selection(sn, ln, ci)) {
                    xgui_win_rect_filled(win, cx, y, SN_CHAR_W, SN_CHAR_H, XGUI_SELECTION);
                    xgui_win_text_transparent(win, cx, y, tc, XGUI_WHITE);
                } else {
                    xgui_win_text_transparent(win, cx, y, tc, XGUI_BLACK);
                }
            }
            /* Show selection highlight on empty wrapped rows */
            if (start_col >= len && sn_in_selection(sn, ln, start_col)) {
                xgui_win_rect_filled(win, SN_PAD, y, SN_CHAR_W, SN_CHAR_H, XGUI_SELECTION);
            }
        }
    }

    /* Draw cursor (accounting for word wrap) */
    if (sn->cur_line >= sn->scroll_y) {
        int cur_screen_row = 0;
        for (int ln = sn->scroll_y; ln < sn->cur_line && ln < sn->line_count; ln++) {
            cur_screen_row += sn_line_screen_rows(sn, ln);
        }
        int wr = sn->cur_col / cols;
        int col_in_row = sn->cur_col % cols;
        cur_screen_row += wr;
        if (cur_screen_row < SN_VIS_LINES) {
            int cx = SN_PAD + col_in_row * SN_CHAR_W;
            int cy = SN_PAD + cur_screen_row * SN_CHAR_H;
            xgui_win_rect_filled(win, cx, cy, 2, SN_CHAR_H, XGUI_BLACK);
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Event handler                                                      */
/* ------------------------------------------------------------------ */

static void sn_handler(xgui_window_t* win, xgui_event_t* event) {
    sticky_note_t* sn = sn_from_win(win);
    if (!sn) return;

    if (event->type == XGUI_EVENT_WINDOW_CLOSE) {
        sn->window = NULL;
        sn_save(sn->slot);
        xgui_window_destroy(win);
        return;
    }

    if (event->type == XGUI_EVENT_WINDOW_MOVE) {
        sn_save(sn->slot);
        return;
    }

    /* Right-click: show context menu at screen position */
    if (event->type == XGUI_EVENT_MOUSE_DOWN && (event->mouse.button & XGUI_MOUSE_RIGHT)) {
        int sx = win->x + win->client_x + event->mouse.x;
        int sy = win->y + win->client_y + event->mouse.y;
        xgui_contextmenu_show(sx, sy, sn_has_selection(sn), xgui_clipboard_has_content());
        return;
    }

    /* Left mouse down: start selection */
    if (event->type == XGUI_EVENT_MOUSE_DOWN && (event->mouse.button & XGUI_MOUSE_LEFT)) {
        int new_line, new_col;
        sn_screen_to_pos(sn, event->mouse.x, event->mouse.y, &new_line, &new_col);
        sn->cur_line = new_line;
        sn->cur_col = new_col;
        sn->sel_start_line = new_line;
        sn->sel_start_col = new_col;
        sn->sel_end_line = new_line;
        sn->sel_end_col = new_col;
        sn->selecting = true;
        win->dirty = true;
        return;
    }

    /* Mouse move while dragging: extend selection */
    if (event->type == XGUI_EVENT_MOUSE_MOVE && sn->selecting) {
        int new_line, new_col;
        sn_screen_to_pos(sn, event->mouse.x, event->mouse.y, &new_line, &new_col);
        sn->sel_end_line = new_line;
        sn->sel_end_col = new_col;
        sn->cur_line = new_line;
        sn->cur_col = new_col;
        win->dirty = true;
        return;
    }

    /* Mouse up: end selection */
    if (event->type == XGUI_EVENT_MOUSE_UP) {
        sn->selecting = false;
        return;
    }

    /* Printable characters via KEY_CHAR */
    if (event->type == XGUI_EVENT_KEY_CHAR) {
        char c = event->key.character;
        if (c >= 32 && c < 127) {
            if (sn_has_selection(sn)) {
                sn_delete_selection(sn);
            }
            sn_insert_char(sn, c);
            sn_save(sn->slot);
        }
        return;
    }

    if (event->type != XGUI_EVENT_KEY_DOWN) return;

    uint8_t key = event->key.keycode;

    /* Ctrl+key shortcuts: detect by control char value (1-26) to avoid modifier race */
    switch (key) {
        case 3:   /* Ctrl+C */
            sn_copy_selection(sn);
            win->dirty = true;
            return;
        case 24:  /* Ctrl+X */
            sn_copy_selection(sn);
            sn_delete_selection(sn);
            sn_save(sn->slot);
            win->dirty = true;
            return;
        case 22:  /* Ctrl+V */
            sn_paste(sn);
            sn_save(sn->slot);
            win->dirty = true;
            return;
        case 1:   /* Ctrl+A */
            sn_select_all(sn);
            win->dirty = true;
            return;
    }

    switch (key) {
        case '\b':
            if (sn_has_selection(sn)) {
                sn_delete_selection(sn);
            } else {
                sn_do_backspace(sn);
            }
            break;

        case '\n':
        case '\r':
            if (sn_has_selection(sn)) {
                sn_delete_selection(sn);
            }
            sn_insert_newline(sn);
            break;

        case KEY_LEFT:
            sn_clear_selection(sn);
            if (sn->cur_col > 0) {
                sn->cur_col--;
            } else if (sn->cur_line > 0) {
                sn->cur_line--;
                sn->cur_col = sn_line_len(sn, sn->cur_line);
            }
            sn_ensure_visible(sn);
            break;

        case KEY_RIGHT:
            sn_clear_selection(sn);
            if (sn->cur_col < sn_line_len(sn, sn->cur_line)) {
                sn->cur_col++;
            } else if (sn->cur_line < sn->line_count - 1) {
                sn->cur_line++;
                sn->cur_col = 0;
            }
            sn_ensure_visible(sn);
            break;

        case KEY_UP:
            sn_clear_selection(sn);
            if (sn->cur_line > 0) {
                sn->cur_line--;
                int len = sn_line_len(sn, sn->cur_line);
                if (sn->cur_col > len) sn->cur_col = len;
            }
            sn_ensure_visible(sn);
            break;

        case KEY_DOWN:
            sn_clear_selection(sn);
            if (sn->cur_line < sn->line_count - 1) {
                sn->cur_line++;
                int len = sn_line_len(sn, sn->cur_line);
                if (sn->cur_col > len) sn->cur_col = len;
            }
            sn_ensure_visible(sn);
            break;

        case KEY_HOME:
            sn_clear_selection(sn);
            sn->cur_col = 0;
            break;

        case KEY_END:
            sn_clear_selection(sn);
            sn->cur_col = sn_line_len(sn, sn->cur_line);
            break;

        default:
            return;
    }

    sn_save(sn->slot);
}

/* ------------------------------------------------------------------ */
/*  Open a note window for a given slot (used by create and restore)   */
/* ------------------------------------------------------------------ */

static void sn_open_window(int slot, int wx, int wy, int minimized) {
    sticky_note_t* sn = &notes[slot];
    if (sn->window) return;

    sn->window = xgui_window_create(sn_titles[slot], wx, wy,
                                     SN_WIN_W, SN_WIN_H,
                                     XGUI_WINDOW_DEFAULT);
    if (!sn->window) return;

    sn->window->bg_color = sn->bg_color;
    xgui_window_set_paint(sn->window, sn_paint);
    xgui_window_set_handler(sn->window, sn_handler);

    if (minimized) {
        xgui_window_minimize(sn->window);
    }
}

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

/*
 * Create a new Sticky Note (from start menu)
 */
void xgui_notepad_create(void) {
    /* Find first free slot */
    int slot = -1;
    for (int i = 0; i < SN_MAX_NOTES; i++) {
        if (!notes[i].window) {
            slot = i;
            break;
        }
    }

    if (slot < 0) {
        xgui_window_focus(notes[0].window);
        return;
    }

    sticky_note_t* sn = &notes[slot];

    /* Reset state */
    memset(sn->lines, 0, sizeof(sn->lines));
    sn->line_count = 1;
    sn->cur_line = 0;
    sn->cur_col = 0;
    sn->scroll_y = 0;
    sn->bg_color = sn_colors[slot];
    sn->slot = slot;
    sn_clear_selection(sn);

    sn_open_window(slot, sn_offsets[slot][0], sn_offsets[slot][1], 0);
    sn_save(slot);
}

/*
 * Restore all sticky notes from disk on GUI boot.
 * Called once from xgui_run().
 */
void xgui_stickynotes_restore(void) {
    for (int i = 0; i < SN_MAX_NOTES; i++) {
        notes[i].slot = i;
        int was_open = sn_load_with_geo(i);
        if (was_open) {
            sn_open_window(i,
                           sn_restore_geo[i][0],
                           sn_restore_geo[i][1],
                           sn_restore_geo[i][3]);
        }
    }
}
