/*
 * MiniOS XGUI Terminal
 *
 * A ring 0 terminal emulator that runs shell commands in a GUI window.
 * Captures all output (printk, vga_puts, shell_output) via VGA redirect hook.
 */

#include "xgui/xgui.h"
#include "xgui/wm.h"
#include "shell.h"
#include "vga.h"
#include "vfs.h"
#include "string.h"
#include "stdio.h"
#include "keyboard.h"

/* Terminal configuration */
#define TERM_MAX_LINES   200
#define TERM_MAX_COLS    80
#define TERM_INPUT_MAX   256
#define TERM_HISTORY_MAX 16
#define TERM_CAPTURE_MAX 8192
#define TERM_CHAR_W      8
#define TERM_CHAR_H      16
#define TERM_PAD         4

/* Terminal colors */
#define TERM_FG          XGUI_GREEN
#define TERM_FG_BRIGHT   XGUI_RGB(80, 255, 80)
#define TERM_FG_DIM      XGUI_RGB(0, 180, 0)
#define TERM_BG          XGUI_RGB(0, 0, 0)
#define TERM_CURSOR_CLR  XGUI_GREEN
#define TERM_PROMPT_CLR  XGUI_RGB(80, 255, 80)

/* Terminal state */
static xgui_window_t* terminal_window = NULL;

/* Selection state for scrollback */
static int term_sel_start_line = -1;
static int term_sel_start_col = -1;
static int term_sel_end_line = -1;
static int term_sel_end_col = -1;
static bool term_selecting = false;

/* Scrollback buffer (extra space for inline ANSI escape codes) */
#define TERM_LINE_BUF  160
static char term_lines[TERM_MAX_LINES][TERM_LINE_BUF + 1];
static int term_line_count = 0;
static int term_scroll = 0;  /* Lines scrolled up from bottom (0 = latest visible) */

/* Current input */
static char term_input[TERM_INPUT_MAX];
static int term_input_len = 0;
static int term_cursor = 0;

/* Command history */
static char term_history[TERM_HISTORY_MAX][TERM_INPUT_MAX];
static int term_hist_count = 0;
static int term_hist_pos = -1;  /* -1 = not browsing history */
static char term_hist_save[TERM_INPUT_MAX];  /* Saves current input when browsing */

/* Output capture buffer */
static char capture_buf[TERM_CAPTURE_MAX];
static int capture_pos = 0;

/*
 * Add a line to the scrollback buffer
 */
static void term_add_line(const char* line) {
    if (term_line_count >= TERM_MAX_LINES) {
        /* Scroll buffer: discard oldest line */
        memmove(term_lines[0], term_lines[1],
                (TERM_MAX_LINES - 1) * (TERM_LINE_BUF + 1));
        term_line_count = TERM_MAX_LINES - 1;
    }
    strncpy(term_lines[term_line_count], line, TERM_LINE_BUF);
    term_lines[term_line_count][TERM_LINE_BUF] = '\0';
    term_line_count++;
}

/*
 * Map ANSI SGR color code to XGUI color
 */
static uint32_t ansi_to_xgui(int code) {
    switch (code) {
        case 30: return XGUI_RGB(0, 0, 0);         /* black */
        case 31: return XGUI_RGB(170, 0, 0);        /* red */
        case 32: return XGUI_RGB(0, 170, 0);        /* green */
        case 33: return XGUI_RGB(170, 85, 0);       /* brown/yellow */
        case 34: return XGUI_RGB(0, 0, 170);        /* blue */
        case 35: return XGUI_RGB(170, 0, 170);      /* magenta */
        case 36: return XGUI_RGB(0, 170, 170);      /* cyan */
        case 37: return XGUI_RGB(170, 170, 170);    /* light grey */
        case 90: return XGUI_RGB(85, 85, 85);       /* dark grey */
        case 91: return XGUI_RGB(255, 85, 85);      /* light red */
        case 92: return XGUI_RGB(85, 255, 85);      /* light green */
        case 93: return XGUI_RGB(255, 255, 85);     /* yellow */
        case 94: return XGUI_RGB(85, 85, 255);      /* light blue */
        case 95: return XGUI_RGB(255, 85, 255);     /* light magenta */
        case 96: return XGUI_RGB(85, 255, 255);     /* light cyan */
        case 97: return XGUI_RGB(255, 255, 255);    /* white */
        default: return TERM_FG;
    }
}

/*
 * Render a line with ANSI escape code parsing.
 * Draws characters one at a time, switching color on ESC[...m sequences.
 */
static void term_render_line(xgui_window_t* win, int px, int py,
                             const char* line, int max_cols) {
    uint32_t color = TERM_FG;
    int col = 0;
    char ch[2] = { 0, 0 };

    for (int i = 0; line[i] && col < max_cols; i++) {
        if (line[i] == '\x1b' && line[i + 1] == '[') {
            /* Parse ANSI escape: ESC [ <number> m */
            i += 2; /* skip ESC [ */
            int code = 0;
            while (line[i] >= '0' && line[i] <= '9') {
                code = code * 10 + (line[i] - '0');
                i++;
            }
            if (line[i] == 'm') {
                color = (code == 0) ? TERM_FG : ansi_to_xgui(code);
            } else {
                i--; /* back up if not 'm' */
            }
        } else {
            ch[0] = line[i];
            xgui_win_text(win, px + col * TERM_CHAR_W, py, ch, color, TERM_BG);
            col++;
        }
    }
}

/*
 * Get the visible column count based on the current window width
 */
static int term_wrap_cols(void) {
    if (terminal_window) {
        int cols = (terminal_window->client_width - TERM_PAD * 2) / TERM_CHAR_W;
        if (cols < 20) cols = 20;
        if (cols > TERM_MAX_COLS) cols = TERM_MAX_COLS;
        return cols;
    }
    return TERM_MAX_COLS;
}

/*
 * Write multi-line text to scrollback (handles \n)
 */
static void term_write_output(const char* text) {
    char buf[TERM_LINE_BUF + 1];
    int pos = 0;       /* Buffer position (includes escape codes) */
    int vcol = 0;      /* Visible column count */
    int wrap = term_wrap_cols();

    for (int i = 0; text[i]; i++) {
        if (text[i] == '\n') {
            buf[pos] = '\0';
            term_add_line(buf);
            pos = 0;
            vcol = 0;
        } else if (text[i] == '\r') {
            /* Ignore CR */
        } else if (text[i] == '\x1b' && text[i + 1] == '[') {
            /* Copy entire ANSI escape sequence without counting visible cols */
            buf[pos++] = text[i++]; /* ESC */
            buf[pos++] = text[i++]; /* [ */
            while (text[i] >= '0' && text[i] <= '9' && pos < TERM_LINE_BUF)
                buf[pos++] = text[i++];
            if (text[i] && pos < TERM_LINE_BUF)
                buf[pos++] = text[i]; /* 'm' or other terminator */
        } else if (text[i] == '\t') {
            /* Expand tab to spaces */
            int next_tab = (vcol + 8) & ~7;
            while (vcol < next_tab && vcol < wrap && pos < TERM_LINE_BUF) {
                buf[pos++] = ' ';
                vcol++;
            }
            if (vcol >= wrap) {
                buf[pos] = '\0';
                term_add_line(buf);
                pos = 0;
                vcol = 0;
            }
        } else {
            if (pos < TERM_LINE_BUF) {
                buf[pos++] = text[i];
                vcol++;
            }
            if (vcol >= wrap) {
                buf[pos] = '\0';
                term_add_line(buf);
                pos = 0;
                vcol = 0;
            }
        }
    }
    /* Flush remaining partial line */
    if (pos > 0) {
        buf[pos] = '\0';
        term_add_line(buf);
    }
}

/*
 * Tab completion
 *
 * First word: complete against shell commands.
 * Subsequent words: complete against filesystem paths.
 */
static void term_tab_complete(void) {
    if (term_input_len == 0) return;

    /* Find the start of the current word */
    int word_start = term_cursor;
    while (word_start > 0 && term_input[word_start - 1] != ' ')
        word_start--;

    /* Is this the first word (command) or a later word (path)? */
    bool is_first_word = true;
    for (int i = 0; i < word_start; i++) {
        if (term_input[i] != ' ') { is_first_word = false; break; }
    }

    /* Extract the prefix to complete */
    int prefix_len = term_cursor - word_start;
    if (prefix_len <= 0) return;
    char prefix[TERM_INPUT_MAX];
    memcpy(prefix, &term_input[word_start], prefix_len);
    prefix[prefix_len] = '\0';

    /* Collect matches */
    #define TAB_MAX_MATCHES 64
    const char* matches[TAB_MAX_MATCHES];
    static char path_matches[TAB_MAX_MATCHES][VFS_MAX_NAME];
    int match_count = 0;

    if (is_first_word) {
        /* Complete against shell commands */
        int cmd_count = 0;
        const shell_command_t* cmds = shell_get_commands(&cmd_count);
        for (int i = 0; i < cmd_count && match_count < TAB_MAX_MATCHES; i++) {
            if (strncmp(cmds[i].name, prefix, prefix_len) == 0) {
                matches[match_count++] = cmds[i].name;
            }
        }
    } else {
        /* Complete against filesystem paths */
        /* Split prefix into directory part and name part */
        char dir_path[256];
        const char* name_part = prefix;
        int name_len = prefix_len;

        /* Find last slash */
        const char* last_slash = NULL;
        for (const char* p = prefix; *p; p++)
            if (*p == '/') last_slash = p;

        vfs_node_t* dir_node = NULL;
        if (last_slash) {
            int dlen = (int)(last_slash - prefix) + 1;
            memcpy(dir_path, prefix, dlen);
            dir_path[dlen] = '\0';
            name_part = last_slash + 1;
            name_len = prefix_len - dlen;

            /* Resolve the directory */
            char resolved[256];
            shell_resolve_path(dir_path, resolved, sizeof(resolved));
            dir_node = vfs_lookup(resolved);
        } else {
            /* No slash — complete in current directory */
            dir_node = shell_get_cwd_node();
        }

        if (dir_node) {
            uint32_t idx = 0;
            dirent_t* de;
            while ((de = vfs_readdir(dir_node, idx)) != NULL &&
                   match_count < TAB_MAX_MATCHES) {
                if (name_len == 0 ||
                    strncmp(de->name, name_part, name_len) == 0) {
                    strncpy(path_matches[match_count], de->name,
                            VFS_MAX_NAME - 1);
                    path_matches[match_count][VFS_MAX_NAME - 1] = '\0';
                    matches[match_count] = path_matches[match_count];
                    match_count++;
                }
                idx++;
            }
        }
    }

    if (match_count == 0) return;

    if (match_count == 1) {
        /* Single match — complete it */
        const char* m = matches[0];
        int mlen = strlen(m);
        int to_add;
        if (is_first_word) {
            to_add = mlen - prefix_len;
        } else {
            /* For paths, only complete the name part after last slash */
            const char* last_slash = NULL;
            for (const char* p = prefix; *p; p++)
                if (*p == '/') last_slash = p;
            int name_len = last_slash ? prefix_len - (int)(last_slash - prefix) - 1 : prefix_len;
            to_add = mlen - name_len;
        }
        if (to_add > 0 && term_input_len + to_add < TERM_INPUT_MAX - 1) {
            /* Insert the completion suffix at cursor */
            const char* suffix = m + (mlen - to_add);
            memmove(&term_input[term_cursor + to_add],
                    &term_input[term_cursor],
                    term_input_len - term_cursor + 1);
            memcpy(&term_input[term_cursor], suffix, to_add);
            term_input_len += to_add;
            term_cursor += to_add;

            /* Add trailing space for commands */
            if (is_first_word && term_input_len < TERM_INPUT_MAX - 1) {
                memmove(&term_input[term_cursor + 1],
                        &term_input[term_cursor],
                        term_input_len - term_cursor + 1);
                term_input[term_cursor] = ' ';
                term_input_len++;
                term_cursor++;
            }
        }
    } else {
        /* Multiple matches — find common prefix and show options */
        int common = strlen(matches[0]);
        for (int i = 1; i < match_count; i++) {
            int j = 0;
            while (j < common && matches[i][j] == matches[0][j]) j++;
            common = j;
        }

        /* Complete the common prefix */
        int already;
        if (is_first_word) {
            already = prefix_len;
        } else {
            const char* last_slash = NULL;
            for (const char* p = prefix; *p; p++)
                if (*p == '/') last_slash = p;
            already = last_slash ? prefix_len - (int)(last_slash - prefix) - 1 : prefix_len;
        }
        int to_add = common - already;
        if (to_add > 0 && term_input_len + to_add < TERM_INPUT_MAX - 1) {
            const char* suffix = matches[0] + already;
            memmove(&term_input[term_cursor + to_add],
                    &term_input[term_cursor],
                    term_input_len - term_cursor + 1);
            memcpy(&term_input[term_cursor], suffix, to_add);
            term_input_len += to_add;
            term_cursor += to_add;
        }

        /* Show all matches in scrollback */
        char line[TERM_MAX_COLS + 1];
        int col = 0;
        line[0] = '\0';
        for (int i = 0; i < match_count; i++) {
            int mlen = strlen(matches[i]);
            int padded = ((mlen / 16) + 1) * 16;
            if (col + padded > TERM_MAX_COLS) {
                term_add_line(line);
                col = 0;
                line[0] = '\0';
            }
            /* Append match name */
            int pos = col;
            for (int j = 0; j < mlen && pos < TERM_MAX_COLS; j++)
                line[pos++] = matches[i][j];
            /* Pad with spaces */
            while (pos < col + padded && pos < TERM_MAX_COLS)
                line[pos++] = ' ';
            line[pos] = '\0';
            col = pos;
        }
        if (col > 0) term_add_line(line);
    }
}

/*
 * VGA output capture callback
 */
static void capture_char(char c) {
    if (capture_pos < TERM_CAPTURE_MAX - 1) {
        capture_buf[capture_pos++] = c;
        capture_buf[capture_pos] = '\0';
    }
}

/*
 * Execute a command and capture output
 */
static void term_execute(void) {
    /* Add prompt + input to scrollback */
    char prompt_line[TERM_MAX_COLS + 1];
    snprintf(prompt_line, sizeof(prompt_line), "$ %s", term_input);
    term_add_line(prompt_line);

    /* Save to history (skip empty and duplicate) */
    if (term_input_len > 0) {
        bool dup = (term_hist_count > 0 &&
                    strcmp(term_history[term_hist_count - 1], term_input) == 0);
        if (!dup) {
            if (term_hist_count >= TERM_HISTORY_MAX) {
                memmove(term_history[0], term_history[1],
                        (TERM_HISTORY_MAX - 1) * TERM_INPUT_MAX);
                term_hist_count = TERM_HISTORY_MAX - 1;
            }
            strncpy(term_history[term_hist_count], term_input, TERM_INPUT_MAX - 1);
            term_history[term_hist_count][TERM_INPUT_MAX - 1] = '\0';
            term_hist_count++;
        }
    }

    /* Handle built-in commands */
    if (strcmp(term_input, "clear") == 0) {
        term_line_count = 0;
        term_scroll = 0;
        goto done;
    }

    if (strcmp(term_input, "exit") == 0) {
        if (terminal_window) {
            xgui_window_destroy(terminal_window);
            terminal_window = NULL;
        }
        goto done;
    }

    /* Block VGA text-mode apps that use blocking input loops */
    {
        /* Extract first word (command name) */
        char cmd[32];
        int ci = 0;
        for (int i = 0; term_input[i] && term_input[i] != ' ' && ci < 31; i++) {
            cmd[ci++] = term_input[i];
        }
        cmd[ci] = '\0';

        if (strcmp(cmd, "spreadsheet") == 0 || strcmp(cmd, "nano") == 0 ||
            strcmp(cmd, "basic") == 0) {
            term_add_line("This app requires text mode and cannot run");
            term_add_line("inside the GUI terminal. Use the text shell");
            term_add_line("or launch the GUI version from the start menu.");
            goto done;
        }
    }

    /* Set up VGA output capture */
    capture_pos = 0;
    capture_buf[0] = '\0';
    vga_set_output_redirect(capture_char);

    /* Execute via shell */
    shell_execute_command(term_input);

    /* Remove capture hook */
    vga_set_output_redirect(NULL);

    /* Write captured output to scrollback */
    if (capture_buf[0]) {
        term_write_output(capture_buf);
    }

done:
    /* Reset input */
    term_input[0] = '\0';
    term_input_len = 0;
    term_cursor = 0;
    term_scroll = 0;
    term_hist_pos = -1;
}

/* ------------------------------------------------------------------ */
/*  Selection helpers                                                   */
/* ------------------------------------------------------------------ */

static void term_clear_selection(void) {
    term_sel_start_line = -1;
    term_sel_start_col = -1;
    term_sel_end_line = -1;
    term_sel_end_col = -1;
    term_selecting = false;
}

static bool term_has_selection(void) {
    return term_sel_start_line >= 0 && term_sel_end_line >= 0 &&
           (term_sel_start_line != term_sel_end_line || term_sel_start_col != term_sel_end_col);
}

static void term_get_selection(int* sl, int* sc, int* el, int* ec) {
    if (term_sel_start_line < term_sel_end_line ||
        (term_sel_start_line == term_sel_end_line && term_sel_start_col <= term_sel_end_col)) {
        *sl = term_sel_start_line; *sc = term_sel_start_col;
        *el = term_sel_end_line;   *ec = term_sel_end_col;
    } else {
        *sl = term_sel_end_line;   *sc = term_sel_end_col;
        *el = term_sel_start_line; *ec = term_sel_start_col;
    }
}

static bool term_in_selection(int line, int col) {
    if (!term_has_selection()) return false;
    int sl, sc, el, ec;
    term_get_selection(&sl, &sc, &el, &ec);
    if (line < sl || line > el) return false;
    if (line == sl && col < sc) return false;
    if (line == el && col >= ec) return false;
    if (line == sl && line == el) return col >= sc && col < ec;
    return true;
}

/*
 * Get visible text length of a scrollback line (strips ANSI escapes)
 */
static int term_visible_len(int line_idx) {
    if (line_idx < 0 || line_idx >= term_line_count) return 0;
    const char* s = term_lines[line_idx];
    int len = 0;
    for (int i = 0; s[i]; i++) {
        if (s[i] == '\x1b' && s[i + 1] == '[') {
            i += 2;
            while (s[i] >= '0' && s[i] <= '9') i++;
            /* i now points to 'm' or whatever terminator */
            continue;
        }
        len++;
    }
    return len;
}

/*
 * Get the nth visible character from a scrollback line (skipping ANSI)
 */
static char term_visible_char(int line_idx, int vis_col) {
    if (line_idx < 0 || line_idx >= term_line_count) return 0;
    const char* s = term_lines[line_idx];
    int col = 0;
    for (int i = 0; s[i]; i++) {
        if (s[i] == '\x1b' && s[i + 1] == '[') {
            i += 2;
            while (s[i] >= '0' && s[i] <= '9') i++;
            continue;
        }
        if (col == vis_col) return s[i];
        col++;
    }
    return 0;
}

static void term_copy_selection(void) {
    if (!term_has_selection()) return;
    int sl, sc, el, ec;
    term_get_selection(&sl, &sc, &el, &ec);

    char buf[XGUI_CLIPBOARD_MAX];
    int pos = 0;
    for (int ln = sl; ln <= el && pos < XGUI_CLIPBOARD_MAX - 2; ln++) {
        int start = (ln == sl) ? sc : 0;
        int end = (ln == el) ? ec : term_visible_len(ln);
        for (int c = start; c < end && pos < XGUI_CLIPBOARD_MAX - 2; c++) {
            char ch = term_visible_char(ln, c);
            if (ch) buf[pos++] = ch;
        }
        if (ln < el && pos < XGUI_CLIPBOARD_MAX - 2) {
            buf[pos++] = '\n';
        }
    }
    buf[pos] = '\0';
    xgui_clipboard_set(buf, pos);
}

static void term_paste(void) {
    const char* text = xgui_clipboard_get();
    if (!text) return;
    for (int i = 0; text[i]; i++) {
        if (text[i] >= 32 && text[i] < 127 && term_input_len < TERM_INPUT_MAX - 1) {
            memmove(&term_input[term_cursor + 1], &term_input[term_cursor],
                    term_input_len - term_cursor + 1);
            term_input[term_cursor] = text[i];
            term_input_len++;
            term_cursor++;
        }
    }
}

static void term_select_all(void) {
    term_sel_start_line = 0;
    term_sel_start_col = 0;
    term_sel_end_line = term_line_count - 1;
    term_sel_end_col = term_visible_len(term_line_count - 1);
}

/*
 * Convert screen click to scrollback line/col
 */
static void term_screen_to_pos(xgui_window_t* win, int mx, int my,
                                int* out_line, int* out_col) {
    int cw = win->client_width;
    int ch = win->client_height;
    int max_cols = (cw - TERM_PAD * 2) / TERM_CHAR_W;
    if (max_cols > TERM_MAX_COLS) max_cols = TERM_MAX_COLS;
    int total_rows = (ch - TERM_PAD * 2) / TERM_CHAR_H;
    if (total_rows < 2) total_rows = 2;
    int output_rows = total_rows - 1;

    int first_line = term_line_count - output_rows - term_scroll;
    if (first_line < 0) first_line = 0;

    int row = (my - TERM_PAD) / TERM_CHAR_H;
    int col = (mx - TERM_PAD) / TERM_CHAR_W;
    if (col < 0) col = 0;
    if (col > max_cols) col = max_cols;

    int ln = first_line + row;
    if (ln < 0) ln = 0;
    if (ln >= term_line_count) ln = term_line_count - 1;
    int vlen = term_visible_len(ln);
    if (col > vlen) col = vlen;

    *out_line = ln;
    *out_col = col;
}

/*
 * Window paint callback
 */
static void terminal_paint(xgui_window_t* win) {
    int cw = win->client_width;
    int ch = win->client_height;

    /* Calculate visible area */
    int max_cols = (cw - TERM_PAD * 2) / TERM_CHAR_W;
    if (max_cols > TERM_MAX_COLS) max_cols = TERM_MAX_COLS;
    int total_rows = (ch - TERM_PAD * 2) / TERM_CHAR_H;
    if (total_rows < 2) total_rows = 2;
    int output_rows = total_rows - 1;  /* Reserve 1 row for input line */

    /* Auto-scroll: compute which lines to show */
    int first_line = term_line_count - output_rows - term_scroll;
    if (first_line < 0) first_line = 0;

    /* Draw scrollback lines (with selection highlighting) */
    for (int i = 0; i < output_rows; i++) {
        int line_idx = first_line + i;
        if (line_idx >= term_line_count) break;

        int y = TERM_PAD + i * TERM_CHAR_H;
        if (term_has_selection()) {
            /* Render character by character for selection highlight */
            const char* s = term_lines[line_idx];
            uint32_t color = TERM_FG;
            int col = 0;
            for (int si = 0; s[si] && col < max_cols; si++) {
                if (s[si] == '\x1b' && s[si + 1] == '[') {
                    si += 2;
                    int code = 0;
                    while (s[si] >= '0' && s[si] <= '9') {
                        code = code * 10 + (s[si] - '0');
                        si++;
                    }
                    if (s[si] == 'm') {
                        color = (code == 0) ? TERM_FG : ansi_to_xgui(code);
                    } else {
                        si--;
                    }
                    continue;
                }
                int cx = TERM_PAD + col * TERM_CHAR_W;
                char ch[2] = { s[si], '\0' };
                if (term_in_selection(line_idx, col)) {
                    xgui_win_rect_filled(win, cx, y, TERM_CHAR_W, TERM_CHAR_H, XGUI_SELECTION);
                    xgui_win_text_transparent(win, cx, y, ch, XGUI_WHITE);
                } else {
                    xgui_win_text_transparent(win, cx, y, ch, color);
                }
                col++;
            }
        } else if (term_lines[line_idx][0]) {
            term_render_line(win, TERM_PAD, y, term_lines[line_idx], max_cols);
        }
    }

    /* Draw input line at bottom */
    int input_y = TERM_PAD + output_rows * TERM_CHAR_H;

    /* Separator line above input */
    xgui_win_hline(win, TERM_PAD, input_y - 2, cw - TERM_PAD * 2, TERM_FG_DIM);

    /* Build display string: "$ " + input */
    char input_display[TERM_MAX_COLS + 4];
    snprintf(input_display, sizeof(input_display), "$ %s", term_input);

    /* Truncate to visible */
    if ((int)strlen(input_display) > max_cols) {
        input_display[max_cols] = '\0';
    }

    xgui_win_text(win, TERM_PAD, input_y, input_display, TERM_FG_BRIGHT, TERM_BG);

    /* Draw cursor */
    int cursor_x = TERM_PAD + (term_cursor + 2) * TERM_CHAR_W;  /* +2 for "$ " */
    if (cursor_x < cw - TERM_PAD) {
        xgui_win_rect_filled(win, cursor_x, input_y, 2, TERM_CHAR_H, TERM_CURSOR_CLR);
    }

    /* Scroll indicator */
    if (term_scroll > 0) {
        char scroll_ind[16];
        snprintf(scroll_ind, sizeof(scroll_ind), "[-%d]", term_scroll);
        int ind_x = cw - TERM_PAD - (int)strlen(scroll_ind) * TERM_CHAR_W;
        xgui_win_text(win, ind_x, TERM_PAD, scroll_ind, TERM_FG_DIM, TERM_BG);
    }
}

/*
 * Window event handler
 */
static void terminal_handler(xgui_window_t* win, xgui_event_t* event) {
    if (event->type == XGUI_EVENT_WINDOW_CLOSE) {
        xgui_window_destroy(win);
        terminal_window = NULL;
        return;
    }

    /* Right-click: show context menu at screen position */
    if (event->type == XGUI_EVENT_MOUSE_DOWN && (event->mouse.button & XGUI_MOUSE_RIGHT)) {
        int sx = win->x + win->client_x + event->mouse.x;
        int sy = win->y + win->client_y + event->mouse.y;
        xgui_contextmenu_show(sx, sy, term_has_selection(), xgui_clipboard_has_content());
        return;
    }

    /* Left mouse down: start scrollback selection */
    if (event->type == XGUI_EVENT_MOUSE_DOWN && (event->mouse.button & XGUI_MOUSE_LEFT)) {
        int mx = event->mouse.x, my = event->mouse.y;
        int ch = win->client_height;
        int total_rows = (ch - TERM_PAD * 2) / TERM_CHAR_H;
        if (total_rows < 2) total_rows = 2;
        int output_rows = total_rows - 1;
        int input_y = TERM_PAD + output_rows * TERM_CHAR_H;

        /* Only select in scrollback area, not input line */
        if (my < input_y && term_line_count > 0) {
            int new_line, new_col;
            term_screen_to_pos(win, mx, my, &new_line, &new_col);
            term_sel_start_line = new_line;
            term_sel_start_col = new_col;
            term_sel_end_line = new_line;
            term_sel_end_col = new_col;
            term_selecting = true;
            win->dirty = true;
        } else {
            term_clear_selection();
            win->dirty = true;
        }
        return;
    }

    /* Mouse move while dragging: extend selection */
    if (event->type == XGUI_EVENT_MOUSE_MOVE && term_selecting) {
        int new_line, new_col;
        term_screen_to_pos(win, event->mouse.x, event->mouse.y, &new_line, &new_col);
        term_sel_end_line = new_line;
        term_sel_end_col = new_col;
        win->dirty = true;
        return;
    }

    /* Mouse up: end selection */
    if (event->type == XGUI_EVENT_MOUSE_UP) {
        term_selecting = false;
        return;
    }

    /* Printable characters via KEY_CHAR */
    if (event->type == XGUI_EVENT_KEY_CHAR) {
        char c = event->key.character;
        if (c >= 32 && c < 127 && term_input_len < TERM_INPUT_MAX - 1) {
            memmove(&term_input[term_cursor + 1], &term_input[term_cursor],
                    term_input_len - term_cursor + 1);
            term_input[term_cursor] = c;
            term_input_len++;
            term_cursor++;
            win->dirty = true;
        }
        return;
    }

    if (event->type != XGUI_EVENT_KEY_DOWN) return;

    uint8_t key = event->key.keycode;
    uint8_t mods = event->key.modifiers;

    /* Ctrl shortcuts: keycode 1-26 means Ctrl was held (control chars).
     * For terminal, Ctrl+Shift+C/V = copy/paste (check Shift in mods).
     * Ctrl+C without Shift is just swallowed (SIGINT handled by keyboard driver).
     * Ctrl+A = select all. */
    if (key == 3 && (mods & XGUI_MOD_SHIFT)) {  /* Ctrl+Shift+C */
        term_copy_selection();
        win->dirty = true;
        return;
    }
    if (key == 22 && (mods & XGUI_MOD_SHIFT)) {  /* Ctrl+Shift+V */
        term_paste();
        win->dirty = true;
        return;
    }
    if (key == 1) {  /* Ctrl+A */
        term_select_all();
        win->dirty = true;
        return;
    }

    switch (key) {
        case '\b':
            if (term_cursor > 0) {
                memmove(&term_input[term_cursor - 1], &term_input[term_cursor],
                        term_input_len - term_cursor + 1);
                term_cursor--;
                term_input_len--;
                win->dirty = true;
            }
            break;

        case '\n':
        case '\r':
            term_clear_selection();
            term_execute();
            if (terminal_window) {
                win->dirty = true;
            }
            break;

        case KEY_LEFT:
            if (term_cursor > 0) {
                term_cursor--;
                win->dirty = true;
            }
            break;

        case KEY_RIGHT:
            if (term_cursor < term_input_len) {
                term_cursor++;
                win->dirty = true;
            }
            break;

        case KEY_UP:
            if (term_hist_count > 0) {
                if (term_hist_pos < 0) {
                    strncpy(term_hist_save, term_input, TERM_INPUT_MAX);
                    term_hist_pos = term_hist_count - 1;
                } else if (term_hist_pos > 0) {
                    term_hist_pos--;
                } else {
                    break;
                }
                strncpy(term_input, term_history[term_hist_pos], TERM_INPUT_MAX - 1);
                term_input[TERM_INPUT_MAX - 1] = '\0';
                term_input_len = strlen(term_input);
                term_cursor = term_input_len;
                win->dirty = true;
            }
            break;

        case KEY_DOWN:
            if (term_hist_pos >= 0) {
                if (term_hist_pos < term_hist_count - 1) {
                    term_hist_pos++;
                    strncpy(term_input, term_history[term_hist_pos], TERM_INPUT_MAX - 1);
                    term_input[TERM_INPUT_MAX - 1] = '\0';
                } else {
                    term_hist_pos = -1;
                    strncpy(term_input, term_hist_save, TERM_INPUT_MAX);
                }
                term_input_len = strlen(term_input);
                term_cursor = term_input_len;
                win->dirty = true;
            }
            break;

        case KEY_HOME:
            term_cursor = 0;
            win->dirty = true;
            break;

        case KEY_END:
            term_cursor = term_input_len;
            win->dirty = true;
            break;

        case KEY_DELETE:
            if (term_cursor < term_input_len) {
                memmove(&term_input[term_cursor], &term_input[term_cursor + 1],
                        term_input_len - term_cursor);
                term_input_len--;
                win->dirty = true;
            }
            break;

        case '\t':
            term_tab_complete();
            win->dirty = true;
            break;

        case KEY_PAGEUP:
            term_scroll += 5;
            {
                int max_scroll = term_line_count;
                if (term_scroll > max_scroll) term_scroll = max_scroll;
            }
            win->dirty = true;
            break;

        case KEY_PAGEDOWN:
            term_scroll -= 5;
            if (term_scroll < 0) term_scroll = 0;
            win->dirty = true;
            break;

        default:
            return;
    }
}

/*
 * Create the Terminal window
 */
void xgui_terminal_create(void) {
    if (terminal_window) {
        xgui_window_focus(terminal_window);
        return;
    }

    /* Reset state */
    term_line_count = 0;
    term_input[0] = '\0';
    term_input_len = 0;
    term_cursor = 0;
    term_scroll = 0;
    term_hist_pos = -1;
    memset(term_hist_save, 0, sizeof(term_hist_save));

    /* Welcome message */
    term_add_line("MiniOS Terminal v1.0");
    term_add_line("Type 'help' for commands, 'clear' to clear, 'exit' to close.");
    term_add_line("");

    /* Create window */
    terminal_window = xgui_window_create("Terminal", 40, 25, 530, 380,
                                         XGUI_WINDOW_DEFAULT | XGUI_WINDOW_MAXIMIZABLE);
    if (!terminal_window) return;

    terminal_window->bg_color = TERM_BG;
    xgui_window_set_paint(terminal_window, terminal_paint);
    xgui_window_set_handler(terminal_window, terminal_handler);
}
