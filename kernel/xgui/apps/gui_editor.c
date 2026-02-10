/*
 * MiniOS XGUI Text Editor
 *
 * A windowed text editor with file open/save via VFS.
 * Supports multi-line editing, scrolling, and basic file I/O.
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

/* Editor configuration */
#define ED_MAX_LINES    256
#define ED_MAX_COLS     512
#define ED_CHAR_W       8
#define ED_CHAR_H       16
#define ED_PAD          4
#define ED_TOOLBAR_H    26
#define ED_STATUS_H     18
#define ED_FNAME_MAX    64

/* Colors */
#define ED_TEXT_BG       XGUI_WHITE
#define ED_TEXT_FG       XGUI_BLACK
#define ED_CURSOR_CLR    XGUI_BLACK
#define ED_LINENUM_FG    XGUI_RGB(128, 128, 128)
#define ED_LINENUM_BG    XGUI_RGB(240, 240, 240)
#define ED_STATUS_BG     XGUI_RGB(0, 100, 180)
#define ED_STATUS_FG     XGUI_WHITE
#define ED_TOOLBAR_BG    XGUI_RGB(235, 235, 235)
#define ED_LINENUM_W     32

/* State */
static xgui_window_t* ed_window = NULL;

static char ed_lines[ED_MAX_LINES][ED_MAX_COLS + 1];
static int ed_line_count = 1;
static int ed_cur_line = 0;
static int ed_cur_col = 0;
static int ed_scroll_y = 0;
static int ed_scroll_x = 0;  /* unused with word wrap, kept for compat */
static char ed_filename[ED_FNAME_MAX];
static int ed_modified = 0;

/* Selection state (-1 = no selection) */
static int ed_sel_start_line = -1;
static int ed_sel_start_col = -1;
static int ed_sel_end_line = -1;
static int ed_sel_end_col = -1;
static bool ed_selecting = false;  /* Mouse drag in progress */

/* Filename input mode */
static int ed_fname_mode = 0;     /* 0=none, 1=open, 2=save-as */
static char ed_fname_buf[ED_FNAME_MAX];
static int ed_fname_pos = 0;

/* Toolbar buttons */
static xgui_widget_t* btn_new = NULL;
static xgui_widget_t* btn_open = NULL;
static xgui_widget_t* btn_save = NULL;
static xgui_widget_t* btn_saveas = NULL;

/* Status message (shown briefly after save etc.) */
static char ed_status_msg[64];
static int ed_status_ticks = 0;

/*
 * Get length of a line
 */
static int ed_line_len(int ln) {
    if (ln < 0 || ln >= ed_line_count) return 0;
    int len = 0;
    while (len < ED_MAX_COLS && ed_lines[ln][len]) len++;
    return len;
}

/*
 * How many text columns fit in the text area
 */
static int ed_wrap_cols(void) {
    if (!ed_window) return 60;
    int max_text_w = ed_window->client_width - ED_LINENUM_W - ED_PAD * 2;
    int cols = max_text_w / ED_CHAR_W;
    if (cols < 1) cols = 1;
    if (cols > ED_MAX_COLS) cols = ED_MAX_COLS;
    return cols;
}

/*
 * How many screen rows does buffer line 'ln' occupy with word wrap
 */
static int ed_line_screen_rows(int ln) {
    int len = ed_line_len(ln);
    int cols = ed_wrap_cols();
    if (len == 0) return 1;
    return (len + cols - 1) / cols;
}

/*
 * Visible rows in text area
 */
static int ed_vis_rows(void) {
    if (!ed_window) return 10;
    int avail = ed_window->client_height - ED_TOOLBAR_H - ED_STATUS_H;
    int rows = avail / ED_CHAR_H;
    if (rows < 1) rows = 1;
    return rows;
}

/*
 * Ensure cursor is visible (accounts for word wrap)
 */
static void ed_ensure_visible(void) {
    int vis = ed_vis_rows();
    /* Scroll up if cursor line is above viewport */
    if (ed_cur_line < ed_scroll_y) {
        ed_scroll_y = ed_cur_line;
        return;
    }
    /* Count screen rows from ed_scroll_y to cursor line (inclusive) */
    int cols = ed_wrap_cols();
    int cursor_wrap_row = ed_cur_col / cols;  /* which wrap row the cursor is on */
    int screen_rows = 0;
    for (int ln = ed_scroll_y; ln <= ed_cur_line && ln < ed_line_count; ln++) {
        int rows = ed_line_screen_rows(ln);
        if (ln == ed_cur_line) {
            screen_rows += cursor_wrap_row + 1;
        } else {
            screen_rows += rows;
        }
    }
    /* Scroll down if cursor is past the visible area */
    while (screen_rows > vis && ed_scroll_y < ed_cur_line) {
        screen_rows -= ed_line_screen_rows(ed_scroll_y);
        ed_scroll_y++;
    }
    if (ed_scroll_y < 0) ed_scroll_y = 0;
}

/*
 * Insert character at cursor
 */
static void ed_insert_char(char c) {
    int len = ed_line_len(ed_cur_line);
    if (len >= ED_MAX_COLS) return;
    for (int i = len; i >= ed_cur_col; i--) {
        ed_lines[ed_cur_line][i + 1] = ed_lines[ed_cur_line][i];
    }
    ed_lines[ed_cur_line][ed_cur_col] = c;
    ed_cur_col++;
    ed_modified = 1;
}

/*
 * Insert newline at cursor (split line)
 */
static void ed_insert_newline(void) {
    if (ed_line_count >= ED_MAX_LINES) return;
    for (int i = ed_line_count; i > ed_cur_line + 1; i--) {
        memcpy(ed_lines[i], ed_lines[i - 1], ED_MAX_COLS + 1);
    }
    ed_line_count++;
    int len = ed_line_len(ed_cur_line);
    memcpy(ed_lines[ed_cur_line + 1], &ed_lines[ed_cur_line][ed_cur_col], len - ed_cur_col + 1);
    ed_lines[ed_cur_line][ed_cur_col] = '\0';
    ed_cur_line++;
    ed_cur_col = 0;
    ed_modified = 1;
    ed_ensure_visible();
}

/*
 * Backspace
 */
static void ed_backspace(void) {
    if (ed_cur_col > 0) {
        int len = ed_line_len(ed_cur_line);
        for (int i = ed_cur_col - 1; i < len; i++) {
            ed_lines[ed_cur_line][i] = ed_lines[ed_cur_line][i + 1];
        }
        ed_cur_col--;
        ed_modified = 1;
    } else if (ed_cur_line > 0) {
        int prev_len = ed_line_len(ed_cur_line - 1);
        int cur_len = ed_line_len(ed_cur_line);
        if (prev_len + cur_len <= ED_MAX_COLS) {
            memcpy(&ed_lines[ed_cur_line - 1][prev_len], ed_lines[ed_cur_line], cur_len + 1);
            for (int i = ed_cur_line; i < ed_line_count - 1; i++) {
                memcpy(ed_lines[i], ed_lines[i + 1], ED_MAX_COLS + 1);
            }
            ed_line_count--;
            ed_cur_line--;
            ed_cur_col = prev_len;
            ed_modified = 1;
        }
    }
    ed_ensure_visible();
}

/*
 * Delete character at cursor
 */
static void ed_delete_char(void) {
    int len = ed_line_len(ed_cur_line);
    if (ed_cur_col < len) {
        for (int i = ed_cur_col; i < len; i++) {
            ed_lines[ed_cur_line][i] = ed_lines[ed_cur_line][i + 1];
        }
        ed_modified = 1;
    } else if (ed_cur_line < ed_line_count - 1) {
        /* Join with next line */
        int next_len = ed_line_len(ed_cur_line + 1);
        if (len + next_len <= ED_MAX_COLS) {
            memcpy(&ed_lines[ed_cur_line][len], ed_lines[ed_cur_line + 1], next_len + 1);
            for (int i = ed_cur_line + 1; i < ed_line_count - 1; i++) {
                memcpy(ed_lines[i], ed_lines[i + 1], ED_MAX_COLS + 1);
            }
            ed_line_count--;
            ed_modified = 1;
        }
    }
}

/*
 * Load file from VFS
 */
static int ed_load_file(const char* path) {
    /* Resolve path */
    char resolved[256];
    shell_resolve_path(path, resolved, sizeof(resolved));

    vfs_node_t* node = vfs_lookup(resolved);
    if (!node) {
        strncpy(ed_status_msg, "File not found", sizeof(ed_status_msg));
        ed_status_ticks = 100;
        return -1;
    }

    uint32_t size = node->length;
    if (size == 0) {
        /* Empty file */
        memset(ed_lines, 0, sizeof(ed_lines));
        ed_line_count = 1;
        strncpy(ed_filename, resolved, ED_FNAME_MAX - 1);
        ed_filename[ED_FNAME_MAX - 1] = '\0';
        ed_modified = 0;
        return 0;
    }

    uint8_t* buf = (uint8_t*)kmalloc(size + 1);
    if (!buf) return -1;

    int32_t read = vfs_read(node, 0, size, buf);
    if (read <= 0) {
        kfree(buf);
        return -1;
    }
    buf[read] = '\0';

    /* Parse into lines */
    memset(ed_lines, 0, sizeof(ed_lines));
    ed_line_count = 0;
    int col = 0;
    for (int i = 0; i < read && ed_line_count < ED_MAX_LINES; i++) {
        if (buf[i] == '\n') {
            ed_lines[ed_line_count][col] = '\0';
            ed_line_count++;
            col = 0;
        } else if (buf[i] != '\r') {
            if (col < ED_MAX_COLS) {
                ed_lines[ed_line_count][col++] = buf[i];
            }
        }
    }
    /* Flush last line if no trailing newline */
    if (col > 0 || ed_line_count == 0) {
        ed_lines[ed_line_count][col] = '\0';
        ed_line_count++;
    }
    if (ed_line_count == 0) ed_line_count = 1;

    kfree(buf);
    strncpy(ed_filename, resolved, ED_FNAME_MAX - 1);
    ed_filename[ED_FNAME_MAX - 1] = '\0';
    ed_modified = 0;
    ed_cur_line = 0;
    ed_cur_col = 0;
    ed_scroll_y = 0;
    return 0;
}

/*
 * Save file to VFS
 */
static int ed_save_file(const char* path) {
    /* Resolve path */
    char resolved[256];
    shell_resolve_path(path, resolved, sizeof(resolved));

    /* Build content buffer */
    int total = 0;
    for (int i = 0; i < ed_line_count; i++) {
        total += ed_line_len(i) + 1;  /* +1 for newline */
    }

    uint8_t* buf = (uint8_t*)kmalloc(total + 1);
    if (!buf) {
        strncpy(ed_status_msg, "Out of memory", sizeof(ed_status_msg));
        ed_status_ticks = 100;
        return -1;
    }

    int pos = 0;
    for (int i = 0; i < ed_line_count; i++) {
        int len = ed_line_len(i);
        memcpy(&buf[pos], ed_lines[i], len);
        pos += len;
        buf[pos++] = '\n';
    }
    buf[pos] = '\0';

    /* Create or overwrite file */
    vfs_node_t* node = vfs_lookup(resolved);
    if (node) {
        node->length = 0;
    } else {
        /* Find parent directory from resolved path */
        const char* fname = resolved;
        const char* last_slash = NULL;
        for (const char* p = resolved; *p; p++) {
            if (*p == '/') last_slash = p;
        }
        vfs_node_t* parent;
        if (!last_slash || last_slash == resolved) {
            fname = (last_slash == resolved) ? resolved + 1 : resolved;
            parent = vfs_lookup("/");
        } else {
            static char parent_path[256];
            int plen = (int)(last_slash - resolved);
            if (plen >= 256) plen = 255;
            memcpy(parent_path, resolved, plen);
            parent_path[plen] = '\0';
            fname = last_slash + 1;
            parent = vfs_lookup(parent_path);
        }
        if (!parent) {
            kfree(buf);
            strncpy(ed_status_msg, "Directory not found", sizeof(ed_status_msg));
            ed_status_ticks = 100;
            return -1;
        }
        if (parent->readdir == ext2_vfs_readdir) {
            node = ext2_create_file(parent, fname);
        } else {
            node = ramfs_create_file_in(parent, fname, VFS_FILE);
        }
        if (!node) {
            kfree(buf);
            strncpy(ed_status_msg, "Failed to create file", sizeof(ed_status_msg));
            ed_status_ticks = 100;
            return -1;
        }
    }

    int32_t written = vfs_write(node, 0, pos, buf);
    kfree(buf);

    if (written < 0) {
        strncpy(ed_status_msg, "Write failed", sizeof(ed_status_msg));
        ed_status_ticks = 100;
        return -1;
    }

    strncpy(ed_filename, resolved, ED_FNAME_MAX - 1);
    ed_filename[ED_FNAME_MAX - 1] = '\0';
    ed_modified = 0;
    snprintf(ed_status_msg, sizeof(ed_status_msg), "Saved %d bytes", pos);
    ed_status_ticks = 100;
    return 0;
}

/*
 * Reset editor to new file
 */
static void ed_new_file(void) {
    memset(ed_lines, 0, sizeof(ed_lines));
    ed_line_count = 1;
    ed_cur_line = 0;
    ed_cur_col = 0;
    ed_scroll_y = 0;
    ed_scroll_x = 0;
    ed_filename[0] = '\0';
    ed_modified = 0;
}

/*
 * Toolbar button callbacks
 */
static void on_new(xgui_widget_t* w) {
    (void)w;
    ed_new_file();
    if (ed_window) ed_window->dirty = true;
}

static void on_open(xgui_widget_t* w) {
    (void)w;
    ed_fname_mode = 1;
    ed_fname_buf[0] = '\0';
    ed_fname_pos = 0;
    if (ed_window) ed_window->dirty = true;
}

static void on_save(xgui_widget_t* w) {
    (void)w;
    if (ed_filename[0]) {
        ed_save_file(ed_filename);
        if (ed_window) ed_window->dirty = true;
    } else {
        /* No filename yet — prompt like Save As */
        ed_fname_mode = 2;
        ed_fname_buf[0] = '\0';
        ed_fname_pos = 0;
        if (ed_window) ed_window->dirty = true;
    }
}

static void on_save_as(xgui_widget_t* w) {
    (void)w;
    ed_fname_mode = 2;
    ed_fname_buf[0] = '\0';
    ed_fname_pos = 0;
    if (ed_window) ed_window->dirty = true;
}

/*
 * Clear selection
 */
static void ed_clear_selection(void) {
    ed_sel_start_line = -1;
    ed_sel_start_col = -1;
    ed_sel_end_line = -1;
    ed_sel_end_col = -1;
    ed_selecting = false;
}

/*
 * Check if there is an active selection
 */
static bool ed_has_selection(void) {
    return ed_sel_start_line >= 0 && ed_sel_end_line >= 0 &&
           (ed_sel_start_line != ed_sel_end_line || ed_sel_start_col != ed_sel_end_col);
}

/*
 * Get normalized selection (start <= end)
 */
static void ed_get_selection(int* sl, int* sc, int* el, int* ec) {
    if (ed_sel_start_line < ed_sel_end_line ||
        (ed_sel_start_line == ed_sel_end_line && ed_sel_start_col <= ed_sel_end_col)) {
        *sl = ed_sel_start_line; *sc = ed_sel_start_col;
        *el = ed_sel_end_line;   *ec = ed_sel_end_col;
    } else {
        *sl = ed_sel_end_line;   *sc = ed_sel_end_col;
        *el = ed_sel_start_line; *ec = ed_sel_start_col;
    }
}

/*
 * Check if a character position is inside the selection
 */
static bool ed_in_selection(int line, int col) {
    if (!ed_has_selection()) return false;
    int sl, sc, el, ec;
    ed_get_selection(&sl, &sc, &el, &ec);
    if (line < sl || line > el) return false;
    if (line == sl && col < sc) return false;
    if (line == el && col >= ec) return false;
    if (line == sl && line == el) return col >= sc && col < ec;
    return true;
}

/*
 * Copy selection to clipboard
 */
static void ed_copy_selection(void) {
    if (!ed_has_selection()) return;
    int sl, sc, el, ec;
    ed_get_selection(&sl, &sc, &el, &ec);

    char buf[XGUI_CLIPBOARD_MAX];
    int pos = 0;

    for (int ln = sl; ln <= el && pos < XGUI_CLIPBOARD_MAX - 2; ln++) {
        int start = (ln == sl) ? sc : 0;
        int end = (ln == el) ? ec : ed_line_len(ln);
        for (int c = start; c < end && pos < XGUI_CLIPBOARD_MAX - 2; c++) {
            buf[pos++] = ed_lines[ln][c];
        }
        if (ln < el && pos < XGUI_CLIPBOARD_MAX - 2) {
            buf[pos++] = '\n';
        }
    }
    buf[pos] = '\0';
    xgui_clipboard_set(buf, pos);
}

/*
 * Delete the selected text
 */
static void ed_delete_selection(void) {
    if (!ed_has_selection()) return;
    int sl, sc, el, ec;
    ed_get_selection(&sl, &sc, &el, &ec);

    if (sl == el) {
        /* Single line: remove chars [sc, ec) */
        int len = ed_line_len(sl);
        memmove(&ed_lines[sl][sc], &ed_lines[sl][ec], len - ec + 1);
    } else {
        /* Multi-line: keep start of first line + end of last line */
        int end_len = ed_line_len(el);
        int keep = end_len - ec;
        if (keep > 0) {
            memcpy(&ed_lines[sl][sc], &ed_lines[el][ec], keep);
        }
        ed_lines[sl][sc + keep] = '\0';

        /* Remove lines sl+1 through el */
        int remove_count = el - sl;
        for (int i = sl + 1; i + remove_count < ed_line_count; i++) {
            memcpy(ed_lines[i], ed_lines[i + remove_count], ED_MAX_COLS + 1);
        }
        ed_line_count -= remove_count;
    }

    ed_cur_line = sl;
    ed_cur_col = sc;
    ed_modified = 1;
    ed_clear_selection();
    ed_ensure_visible();
}

/*
 * Paste from clipboard at cursor
 */
static void ed_paste(void) {
    const char* text = xgui_clipboard_get();
    if (!text) return;

    /* Delete selection first if any */
    if (ed_has_selection()) {
        ed_delete_selection();
    }

    for (int i = 0; text[i]; i++) {
        if (text[i] == '\n') {
            ed_insert_newline();
        } else if (text[i] >= 32 && text[i] < 127) {
            ed_insert_char(text[i]);
        }
    }
    ed_ensure_visible();
}

/*
 * Select all text
 */
static void ed_select_all(void) {
    ed_sel_start_line = 0;
    ed_sel_start_col = 0;
    ed_sel_end_line = ed_line_count - 1;
    ed_sel_end_col = ed_line_len(ed_line_count - 1);
}

/*
 * Convert screen click to line/col (accounts for word wrap)
 */
static void ed_screen_to_pos(xgui_window_t* win, int mx, int my, int* out_line, int* out_col) {
    (void)win;
    int text_y = ED_TOOLBAR_H;
    int cols = ed_wrap_cols();
    int click_row = (my - text_y) / ED_CHAR_H;
    int screen_row = 0;

    for (int ln = ed_scroll_y; ln < ed_line_count; ln++) {
        int wrap_rows = ed_line_screen_rows(ln);
        if (click_row < screen_row + wrap_rows) {
            int wr = click_row - screen_row;
            int new_col = wr * cols + (mx - ED_LINENUM_W - ED_PAD) / ED_CHAR_W;
            int len = ed_line_len(ln);
            if (new_col > len) new_col = len;
            if (new_col < 0) new_col = 0;
            *out_line = ln;
            *out_col = new_col;
            return;
        }
        screen_row += wrap_rows;
    }
    /* Past end of document */
    *out_line = ed_line_count - 1;
    *out_col = ed_line_len(ed_line_count - 1);
}

/*
 * Paint callback
 */
static void ed_paint(xgui_window_t* win) {
    int cw = win->client_width;
    int ch = win->client_height;
    int vis = ed_vis_rows();

    /* Toolbar background */
    xgui_win_rect_filled(win, 0, 0, cw, ED_TOOLBAR_H, ED_TOOLBAR_BG);
    xgui_win_hline(win, 0, ED_TOOLBAR_H - 1, cw, XGUI_RGB(180, 180, 180));

    /* Draw toolbar buttons */
    xgui_widgets_draw(win);

    /* Text area */
    int text_y = ED_TOOLBAR_H;
    int text_h = ch - ED_TOOLBAR_H - ED_STATUS_H;

    /* Line number gutter */
    xgui_win_rect_filled(win, 0, text_y, ED_LINENUM_W, text_h, ED_LINENUM_BG);
    xgui_win_vline(win, ED_LINENUM_W, text_y, text_h, XGUI_RGB(200, 200, 200));

    /* Text background */
    xgui_win_rect_filled(win, ED_LINENUM_W + 1, text_y, cw - ED_LINENUM_W - 1, text_h, ED_TEXT_BG);

    /* Draw lines with word wrap */
    int cols = ed_wrap_cols();
    int screen_row = 0;
    int cursor_sx = -1, cursor_sy = -1;

    for (int ln = ed_scroll_y; ln < ed_line_count && screen_row < vis; ln++) {
        int len = ed_line_len(ln);
        int wrap_rows = ed_line_screen_rows(ln);

        for (int wr = 0; wr < wrap_rows && screen_row < vis; wr++) {
            int y = text_y + screen_row * ED_CHAR_H;

            /* Line number on first wrap row only */
            if (wr == 0) {
                char lnum[6];
                snprintf(lnum, sizeof(lnum), "%3d", ln + 1);
                xgui_win_text_transparent(win, 2, y, lnum, ED_LINENUM_FG);
            }

            /* Draw this segment of the line (with selection highlighting) */
            int seg_start = wr * cols;
            int seg_len = len - seg_start;
            if (seg_len > cols) seg_len = cols;
            if (seg_len > 0) {
                for (int ci = 0; ci < seg_len; ci++) {
                    int abs_col = seg_start + ci;
                    int cx = ED_LINENUM_W + ED_PAD + ci * ED_CHAR_W;
                    char ch[2] = { ed_lines[ln][abs_col], '\0' };
                    if (ed_in_selection(ln, abs_col)) {
                        xgui_win_rect_filled(win, cx, y, ED_CHAR_W, ED_CHAR_H, XGUI_SELECTION);
                        xgui_win_text_transparent(win, cx, y, ch, XGUI_WHITE);
                    } else {
                        xgui_win_text_transparent(win, cx, y, ch, ED_TEXT_FG);
                    }
                }
            } else if (ed_in_selection(ln, 0)) {
                /* Empty line inside selection — show selection bar */
                xgui_win_rect_filled(win, ED_LINENUM_W + ED_PAD, y, ED_CHAR_W, ED_CHAR_H, XGUI_SELECTION);
            }

            /* Track cursor position */
            if (ln == ed_cur_line && !ed_fname_mode) {
                int cur_wrap = ed_cur_col / cols;
                int cur_col_in_wrap = ed_cur_col % cols;
                if (cur_wrap == wr) {
                    cursor_sx = ED_LINENUM_W + ED_PAD + cur_col_in_wrap * ED_CHAR_W;
                    cursor_sy = y;
                }
            }

            screen_row++;
        }
    }

    /* Cursor */
    if (cursor_sx >= 0 && cursor_sy >= 0) {
        xgui_win_rect_filled(win, cursor_sx, cursor_sy, 2, ED_CHAR_H, ED_CURSOR_CLR);
    }

    /* Status bar */
    int sy = ch - ED_STATUS_H;
    xgui_win_rect_filled(win, 0, sy, cw, ED_STATUS_H, ED_STATUS_BG);

    if (ed_fname_mode) {
        /* Filename input prompt */
        const char* prompt = ed_fname_mode == 1 ? "Open: " : "Save as: ";
        char status[128];
        snprintf(status, sizeof(status), "%s%s_", prompt, ed_fname_buf);
        xgui_win_text_transparent(win, 4, sy + 2, status, ED_STATUS_FG);
    } else if (ed_status_ticks > 0) {
        /* Show status message */
        xgui_win_text_transparent(win, 4, sy + 2, ed_status_msg, XGUI_RGB(255, 255, 100));
        ed_status_ticks--;
        if (ed_status_ticks > 0) win->dirty = true;
    } else {
        /* Normal status */
        char status[128];
        snprintf(status, sizeof(status), " %s%s  Ln %d, Col %d  %d lines",
                 ed_filename[0] ? ed_filename : "[New File]",
                 ed_modified ? " *" : "",
                 ed_cur_line + 1, ed_cur_col + 1, ed_line_count);
        xgui_win_text_transparent(win, 4, sy + 2, status, ED_STATUS_FG);
    }
}

/*
 * Event handler
 */
static void ed_handler(xgui_window_t* win, xgui_event_t* event) {
    /* Let widgets handle toolbar button clicks */
    if (xgui_widgets_handle_event(win, event)) {
        return;
    }

    if (event->type == XGUI_EVENT_WINDOW_CLOSE) {
        xgui_window_destroy(win);
        ed_window = NULL;
        btn_new = NULL;
        btn_open = NULL;
        btn_save = NULL;
        btn_saveas = NULL;
        return;
    }

    /* Filename input mode */
    if (ed_fname_mode) {
        if (event->type == XGUI_EVENT_KEY_CHAR) {
            char c = event->key.character;
            if (c >= 32 && c < 127 && ed_fname_pos < ED_FNAME_MAX - 1) {
                ed_fname_buf[ed_fname_pos++] = c;
                ed_fname_buf[ed_fname_pos] = '\0';
                win->dirty = true;
            }
            return;
        }
        if (event->type == XGUI_EVENT_KEY_DOWN) {
            uint8_t key = event->key.keycode;
            if (key == '\n' || key == '\r') {
                if (ed_fname_pos > 0) {
                    if (ed_fname_mode == 1) {
                        ed_load_file(ed_fname_buf);
                    } else {
                        ed_save_file(ed_fname_buf);
                    }
                }
                ed_fname_mode = 0;
                win->dirty = true;
            } else if (key == KEY_ESCAPE) {
                ed_fname_mode = 0;
                win->dirty = true;
            } else if (key == '\b') {
                if (ed_fname_pos > 0) {
                    ed_fname_pos--;
                    ed_fname_buf[ed_fname_pos] = '\0';
                    win->dirty = true;
                }
            }
        }
        return;
    }

    /* Right-click: show context menu at screen position */
    if (event->type == XGUI_EVENT_MOUSE_DOWN && (event->mouse.button & XGUI_MOUSE_RIGHT)) {
        int sx = win->x + win->client_x + event->mouse.x;
        int sy = win->y + win->client_y + event->mouse.y;
        xgui_contextmenu_show(sx, sy, ed_has_selection(), xgui_clipboard_has_content());
        return;
    }

    /* Left mouse down: start selection or place cursor */
    if (event->type == XGUI_EVENT_MOUSE_DOWN && (event->mouse.button & XGUI_MOUSE_LEFT)) {
        int mx = event->mouse.x;
        int my = event->mouse.y;
        int text_y = ED_TOOLBAR_H;

        if (mx > ED_LINENUM_W && my >= text_y && my < (int)win->client_height - ED_STATUS_H) {
            int new_line, new_col;
            ed_screen_to_pos(win, mx, my, &new_line, &new_col);
            ed_cur_line = new_line;
            ed_cur_col = new_col;

            /* Start selection */
            ed_sel_start_line = new_line;
            ed_sel_start_col = new_col;
            ed_sel_end_line = new_line;
            ed_sel_end_col = new_col;
            ed_selecting = true;
            win->dirty = true;
        }
        return;
    }

    /* Mouse move while dragging: extend selection */
    if (event->type == XGUI_EVENT_MOUSE_MOVE && ed_selecting) {
        int mx = event->mouse.x;
        int my = event->mouse.y;
        int text_y = ED_TOOLBAR_H;

        if (mx > ED_LINENUM_W && my >= text_y && my < (int)win->client_height - ED_STATUS_H) {
            int new_line, new_col;
            ed_screen_to_pos(win, mx, my, &new_line, &new_col);
            ed_sel_end_line = new_line;
            ed_sel_end_col = new_col;
            ed_cur_line = new_line;
            ed_cur_col = new_col;
            win->dirty = true;
        }
        return;
    }

    /* Mouse up: end selection */
    if (event->type == XGUI_EVENT_MOUSE_UP) {
        ed_selecting = false;
        return;
    }

    /* Printable characters */
    if (event->type == XGUI_EVENT_KEY_CHAR) {
        char c = event->key.character;
        if (c >= 32 && c < 127) {
            /* Delete selection if any, then insert */
            if (ed_has_selection()) {
                ed_delete_selection();
            }
            ed_insert_char(c);
            win->dirty = true;
        }
        return;
    }

    if (event->type != XGUI_EVENT_KEY_DOWN) return;

    uint8_t key = event->key.keycode;

    /* Ctrl+key shortcuts: detect by control char value (1-26) to avoid modifier race */
    switch (key) {
        case 3:   /* Ctrl+C */
            ed_copy_selection();
            win->dirty = true;
            return;
        case 24:  /* Ctrl+X */
            ed_copy_selection();
            ed_delete_selection();
            win->dirty = true;
            return;
        case 22:  /* Ctrl+V */
            ed_paste();
            win->dirty = true;
            return;
        case 1:   /* Ctrl+A */
            ed_select_all();
            win->dirty = true;
            return;
        case 19:  /* Ctrl+S */
            if (ed_filename[0]) {
                ed_save_file(ed_filename);
            } else {
                ed_fname_mode = 2;
                ed_fname_buf[0] = '\0';
                ed_fname_pos = 0;
            }
            win->dirty = true;
            return;
    }

    switch (key) {
        case '\b':
            if (ed_has_selection()) {
                ed_delete_selection();
            } else {
                ed_backspace();
            }
            break;
        case '\n':
        case '\r':
            if (ed_has_selection()) {
                ed_delete_selection();
            }
            ed_insert_newline();
            break;
        case KEY_LEFT:
            ed_clear_selection();
            if (ed_cur_col > 0) {
                ed_cur_col--;
            } else if (ed_cur_line > 0) {
                ed_cur_line--;
                ed_cur_col = ed_line_len(ed_cur_line);
            }
            ed_ensure_visible();
            break;
        case KEY_RIGHT:
            ed_clear_selection();
            if (ed_cur_col < ed_line_len(ed_cur_line)) {
                ed_cur_col++;
            } else if (ed_cur_line < ed_line_count - 1) {
                ed_cur_line++;
                ed_cur_col = 0;
            }
            ed_ensure_visible();
            break;
        case KEY_UP:
            ed_clear_selection();
            if (ed_cur_line > 0) {
                ed_cur_line--;
                int len = ed_line_len(ed_cur_line);
                if (ed_cur_col > len) ed_cur_col = len;
            }
            ed_ensure_visible();
            break;
        case KEY_DOWN:
            ed_clear_selection();
            if (ed_cur_line < ed_line_count - 1) {
                ed_cur_line++;
                int len = ed_line_len(ed_cur_line);
                if (ed_cur_col > len) ed_cur_col = len;
            }
            ed_ensure_visible();
            break;
        case KEY_HOME:
            ed_clear_selection();
            ed_cur_col = 0;
            break;
        case KEY_END:
            ed_clear_selection();
            ed_cur_col = ed_line_len(ed_cur_line);
            break;
        case KEY_DELETE:
            if (ed_has_selection()) {
                ed_delete_selection();
            } else {
                ed_delete_char();
            }
            break;
        case KEY_PAGEUP:
            ed_clear_selection();
            ed_cur_line -= ed_vis_rows();
            if (ed_cur_line < 0) ed_cur_line = 0;
            ed_ensure_visible();
            break;
        case KEY_PAGEDOWN:
            ed_clear_selection();
            ed_cur_line += ed_vis_rows();
            if (ed_cur_line >= ed_line_count) ed_cur_line = ed_line_count - 1;
            ed_ensure_visible();
            break;
        case '\t':
            if (ed_has_selection()) {
                ed_delete_selection();
            }
            for (int i = 0; i < 4; i++) ed_insert_char(' ');
            break;
        default:
            return;
    }
    win->dirty = true;
}

/*
 * Create the GUI Text Editor window
 */
void xgui_gui_editor_create(void) {
    if (ed_window) {
        xgui_window_focus(ed_window);
        return;
    }

    /* Reset state */
    ed_new_file();
    ed_fname_mode = 0;

    ed_window = xgui_window_create("Text Editor", 60, 30, 520, 380,
                                    XGUI_WINDOW_DEFAULT | XGUI_WINDOW_MAXIMIZABLE);
    if (!ed_window) return;

    xgui_window_set_paint(ed_window, ed_paint);
    xgui_window_set_handler(ed_window, ed_handler);

    /* Reset status */
    ed_status_msg[0] = '\0';
    ed_status_ticks = 0;

    /* Create toolbar buttons */
    btn_new    = xgui_button_create(ed_window, 4, 2, 50, 20, "New");
    btn_open   = xgui_button_create(ed_window, 58, 2, 50, 20, "Open");
    btn_save   = xgui_button_create(ed_window, 112, 2, 50, 20, "Save");
    btn_saveas = xgui_button_create(ed_window, 166, 2, 60, 20, "Save As");

    if (btn_new)    xgui_widget_set_onclick(btn_new, on_new);
    if (btn_open)   xgui_widget_set_onclick(btn_open, on_open);
    if (btn_save)   xgui_widget_set_onclick(btn_save, on_save);
    if (btn_saveas) xgui_widget_set_onclick(btn_saveas, on_save_as);
}

/*
 * Create the GUI Text Editor and open a specific file
 */
void xgui_gui_editor_open_file(const char* path) {
    xgui_gui_editor_create();
    if (ed_window && path && path[0]) {
        ed_load_file(path);
        ed_window->dirty = true;
    }
}
