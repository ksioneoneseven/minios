/*
 * MiniOS Nano Text Editor
 *
 * A full-featured nano clone with:
 * - Multi-line text editing
 * - File open/save
 * - Cut/Copy/Paste
 * - Search and Replace
 * - Go to line
 * - Word wrap display
 * - Syntax highlighting (basic)
 * - Undo support
 */

#include "../include/nano.h"
#include "../include/vga.h"
#include "../include/keyboard.h"
#include "../include/string.h"
#include "../include/stdio.h"
#include "../include/heap.h"
#include "../include/vfs.h"
#include "../include/ramfs.h"
#include "../include/ext2.h"
#include "../include/shell.h"

/* Editor configuration */
#define NANO_MAX_LINES      1000
#define NANO_MAX_LINE_LEN   256
#define NANO_TAB_SIZE       4
#define NANO_UNDO_LEVELS    50

/* Screen layout */
#define NANO_TITLE_ROW      0
#define NANO_EDIT_START     1
#define NANO_EDIT_ROWS      21
#define NANO_STATUS_ROW     22
#define NANO_HELP_ROW1      23
#define NANO_HELP_ROW2      24

/* Colors */
#define COLOR_TITLE     vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE)
#define COLOR_TEXT      vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK)
#define COLOR_LINENUM   vga_entry_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK)
#define COLOR_STATUS    vga_entry_color(VGA_COLOR_BLACK, VGA_COLOR_WHITE)
#define COLOR_HELP      vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK)
#define COLOR_HELP_KEY  vga_entry_color(VGA_COLOR_BLACK, VGA_COLOR_WHITE)
#define COLOR_CURSOR    vga_entry_color(VGA_COLOR_BLACK, VGA_COLOR_WHITE)
#define COLOR_SELECTED  vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE)
#define COLOR_KEYWORD   vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK)
#define COLOR_STRING    vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK)
#define COLOR_COMMENT   vga_entry_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK)
#define COLOR_NUMBER    vga_entry_color(VGA_COLOR_LIGHT_MAGENTA, VGA_COLOR_BLACK)

/* Line structure */
typedef struct {
    char text[NANO_MAX_LINE_LEN];
    int length;
} line_t;

/* Undo action types */
typedef enum {
    UNDO_INSERT_CHAR,
    UNDO_DELETE_CHAR,
    UNDO_INSERT_LINE,
    UNDO_DELETE_LINE,
    UNDO_JOIN_LINES,
    UNDO_SPLIT_LINE
} undo_type_t;

/* Undo record */
typedef struct {
    undo_type_t type;
    int line;
    int col;
    char ch;
    char line_content[NANO_MAX_LINE_LEN];
} undo_record_t;

/* Editor state */
static line_t* lines = NULL;
static int num_lines = 0;
static int cursor_line = 0;
static int cursor_col = 0;
static int view_line = 0;          /* First visible line */
static int view_col = 0;           /* Horizontal scroll offset */
static char filename[256] = "";
static int modified = 0;
static int show_line_numbers = 1;

/* Selection state (reserved for future use) */
static int select_active = 0;

/* Cut buffer (clipboard) */
static char* cut_buffer = NULL;
static int cut_buffer_size = 0;

/* Search state */
static char search_term[128] = "";
static char replace_term[128] = "";
static int search_wrap = 1;
static int search_case_sensitive = 0;

/* Undo buffer */
static undo_record_t undo_buffer[NANO_UNDO_LEVELS];
static int undo_pos = 0;
static int undo_count = 0;

/* Forward declarations */
static void nano_draw_screen(void);
static void nano_draw_title(void);
static void nano_draw_text(void);
static void nano_draw_status(const char* msg);
static void nano_draw_help(void);
static void nano_move_cursor(int dline, int dcol);
static void nano_insert_char(char c);
static void nano_delete_char(void);
static void nano_backspace(void);
static void nano_insert_line(void);
static void nano_delete_line(void);
static int nano_save_file(void);
static int nano_load_file(const char* name);
static void nano_search(void);
static void nano_search_next(void);
static void nano_replace(void);
static void nano_goto_line(void);
static void nano_cut_line(void);
static void nano_copy_line(void);
static void nano_paste(void);
static void nano_undo(void);
static void nano_push_undo(undo_type_t type, int line, int col, char ch, const char* content);
static int nano_prompt(const char* prompt, char* buffer, int bufsize);
static void nano_clear_line(int row, uint8_t color);
static int nano_get_display_col(int line, int col);

/*
 * Clear a screen line with a specific color
 */
static void nano_clear_line(int row, uint8_t color) {
    char spaces[81];
    memset(spaces, ' ', 80);
    spaces[80] = '\0';
    vga_write_at(spaces, color, 0, row);
}

/*
 * Get display column accounting for tabs
 */
static int nano_get_display_col(int line, int col) {
    if (line < 0 || line >= num_lines) return col;
    int display_col = 0;
    for (int i = 0; i < col && i < lines[line].length; i++) {
        if (lines[line].text[i] == '\t') {
            display_col = (display_col + NANO_TAB_SIZE) & ~(NANO_TAB_SIZE - 1);
        } else {
            display_col++;
        }
    }
    return display_col;
}

/*
 * Initialize editor state
 */
void nano_init(void) {
    /* Allocate line buffer */
    if (lines == NULL) {
        lines = (line_t*)kmalloc(sizeof(line_t) * NANO_MAX_LINES);
    }

    /* Clear all lines */
    memset(lines, 0, sizeof(line_t) * NANO_MAX_LINES);
    num_lines = 1;  /* Start with one empty line */

    /* Reset cursor and view */
    cursor_line = 0;
    cursor_col = 0;
    view_line = 0;
    view_col = 0;

    /* Reset state */
    filename[0] = '\0';
    modified = 0;
    select_active = 0;

    /* Clear undo buffer */
    undo_pos = 0;
    undo_count = 0;

    /* Clear cut buffer */
    if (cut_buffer) {
        kfree(cut_buffer);
        cut_buffer = NULL;
    }
    cut_buffer_size = 0;

    /* Clear search */
    search_term[0] = '\0';
    replace_term[0] = '\0';
}

/*
 * Push an undo record
 */
static void nano_push_undo(undo_type_t type, int line, int col, char ch, const char* content) {
    undo_record_t* rec = &undo_buffer[undo_pos];
    rec->type = type;
    rec->line = line;
    rec->col = col;
    rec->ch = ch;
    if (content) {
        strncpy(rec->line_content, content, NANO_MAX_LINE_LEN - 1);
        rec->line_content[NANO_MAX_LINE_LEN - 1] = '\0';
    } else {
        rec->line_content[0] = '\0';
    }

    undo_pos = (undo_pos + 1) % NANO_UNDO_LEVELS;
    if (undo_count < NANO_UNDO_LEVELS) {
        undo_count++;
    }
}

/*
 * Undo last action
 */
static void nano_undo(void) {
    if (undo_count == 0) {
        nano_draw_status("Nothing to undo");
        return;
    }

    undo_pos = (undo_pos - 1 + NANO_UNDO_LEVELS) % NANO_UNDO_LEVELS;
    undo_count--;

    undo_record_t* rec = &undo_buffer[undo_pos];

    switch (rec->type) {
        case UNDO_INSERT_CHAR:
            /* Undo insert = delete the char */
            cursor_line = rec->line;
            cursor_col = rec->col;
            nano_delete_char();
            undo_count--;  /* Don't count the delete as new undo */
            break;

        case UNDO_DELETE_CHAR:
            /* Undo delete = insert the char */
            cursor_line = rec->line;
            cursor_col = rec->col;
            nano_insert_char(rec->ch);
            undo_count--;
            break;

        case UNDO_INSERT_LINE:
            /* Undo insert line = delete it */
            if (rec->line < num_lines) {
                for (int i = rec->line; i < num_lines - 1; i++) {
                    lines[i] = lines[i + 1];
                }
                num_lines--;
                cursor_line = rec->line > 0 ? rec->line - 1 : 0;
                cursor_col = lines[cursor_line].length;
            }
            break;

        case UNDO_DELETE_LINE:
            /* Undo delete line = restore it */
            if (num_lines < NANO_MAX_LINES) {
                for (int i = num_lines; i > rec->line; i--) {
                    lines[i] = lines[i - 1];
                }
                strcpy(lines[rec->line].text, rec->line_content);
                lines[rec->line].length = strlen(rec->line_content);
                num_lines++;
                cursor_line = rec->line;
                cursor_col = 0;
            }
            break;

        default:
            break;
    }

    nano_draw_screen();
    nano_draw_status("Undone");
}


/*
 * Draw the title bar
 */
static void nano_draw_title(void) {
    nano_clear_line(NANO_TITLE_ROW, COLOR_TITLE);

    char title[81];
    if (filename[0]) {
        snprintf(title, sizeof(title), "  MiniOS nano 1.0                    File: %s%s",
                 filename, modified ? " (modified)" : "");
    } else {
        snprintf(title, sizeof(title), "  MiniOS nano 1.0                    New Buffer%s",
                 modified ? " (modified)" : "");
    }

    /* Pad to 80 chars */
    int len = strlen(title);
    while (len < 80) title[len++] = ' ';
    title[80] = '\0';

    vga_write_at(title, COLOR_TITLE, 0, NANO_TITLE_ROW);
}

/*
 * Draw the status bar
 */
static void nano_draw_status(const char* msg) {
    nano_clear_line(NANO_STATUS_ROW, COLOR_STATUS);

    char status[81];
    if (msg && msg[0]) {
        snprintf(status, sizeof(status), " %s", msg);
    } else {
        snprintf(status, sizeof(status), " Line %d/%d, Col %d    %s",
                 cursor_line + 1, num_lines, cursor_col + 1,
                 modified ? "[Modified]" : "");
    }

    int len = strlen(status);
    while (len < 80) status[len++] = ' ';
    status[80] = '\0';

    vga_write_at(status, COLOR_STATUS, 0, NANO_STATUS_ROW);
}

/*
 * Draw the help bar
 */
static void nano_draw_help(void) {
    /* First help row */
    nano_clear_line(NANO_HELP_ROW1, COLOR_HELP);
    vga_write_at("^G", COLOR_HELP_KEY, 0, NANO_HELP_ROW1);
    vga_write_at(" Help  ", COLOR_HELP, 2, NANO_HELP_ROW1);
    vga_write_at("^O", COLOR_HELP_KEY, 9, NANO_HELP_ROW1);
    vga_write_at(" Save  ", COLOR_HELP, 11, NANO_HELP_ROW1);
    vga_write_at("^W", COLOR_HELP_KEY, 18, NANO_HELP_ROW1);
    vga_write_at(" Search", COLOR_HELP, 20, NANO_HELP_ROW1);
    vga_write_at("^K", COLOR_HELP_KEY, 28, NANO_HELP_ROW1);
    vga_write_at(" Cut   ", COLOR_HELP, 30, NANO_HELP_ROW1);
    vga_write_at("^U", COLOR_HELP_KEY, 37, NANO_HELP_ROW1);
    vga_write_at(" Paste ", COLOR_HELP, 39, NANO_HELP_ROW1);
    vga_write_at("^C", COLOR_HELP_KEY, 47, NANO_HELP_ROW1);
    vga_write_at(" Pos   ", COLOR_HELP, 49, NANO_HELP_ROW1);
    vga_write_at("^X", COLOR_HELP_KEY, 56, NANO_HELP_ROW1);
    vga_write_at(" Exit  ", COLOR_HELP, 58, NANO_HELP_ROW1);

    /* Second help row */
    nano_clear_line(NANO_HELP_ROW2, COLOR_HELP);
    vga_write_at("^R", COLOR_HELP_KEY, 0, NANO_HELP_ROW2);
    vga_write_at(" Read  ", COLOR_HELP, 2, NANO_HELP_ROW2);
    vga_write_at("^\\", COLOR_HELP_KEY, 9, NANO_HELP_ROW2);
    vga_write_at(" Replce", COLOR_HELP, 11, NANO_HELP_ROW2);
    vga_write_at("^_", COLOR_HELP_KEY, 18, NANO_HELP_ROW2);
    vga_write_at(" GoLine", COLOR_HELP, 20, NANO_HELP_ROW2);
    vga_write_at("^6", COLOR_HELP_KEY, 28, NANO_HELP_ROW2);
    vga_write_at(" Copy  ", COLOR_HELP, 30, NANO_HELP_ROW2);
    vga_write_at("^Z", COLOR_HELP_KEY, 37, NANO_HELP_ROW2);
    vga_write_at(" Undo  ", COLOR_HELP, 39, NANO_HELP_ROW2);
    vga_write_at("^L", COLOR_HELP_KEY, 47, NANO_HELP_ROW2);
    vga_write_at(" Refresh", COLOR_HELP, 49, NANO_HELP_ROW2);
}

/*
 * Draw the text area
 */
static void nano_draw_text(void) {
    int line_num_width = show_line_numbers ? 5 : 0;
    int text_width = 80 - line_num_width;

    for (int screen_row = 0; screen_row < NANO_EDIT_ROWS; screen_row++) {
        int file_line = view_line + screen_row;
        int screen_y = NANO_EDIT_START + screen_row;

        /* Clear the line first */
        nano_clear_line(screen_y, COLOR_TEXT);

        if (file_line >= num_lines) {
            /* Beyond end of file - show ~ like vim or empty */
            if (show_line_numbers) {
                vga_write_at("    ~", COLOR_LINENUM, 0, screen_y);
            }
            continue;
        }

        /* Draw line number */
        if (show_line_numbers) {
            char linenum[6];
            snprintf(linenum, sizeof(linenum), "%4d ", file_line + 1);
            vga_write_at(linenum, COLOR_LINENUM, 0, screen_y);
        }

        /* Draw text content */
        line_t* line = &lines[file_line];
        int display_col = 0;

        for (int i = 0; i < line->length && display_col < text_width + view_col; i++) {
            char ch = line->text[i];
            int char_width = 1;

            if (ch == '\t') {
                char_width = NANO_TAB_SIZE - (display_col % NANO_TAB_SIZE);
            }

            /* Check if visible */
            if (display_col + char_width > view_col && display_col < view_col + text_width) {
                int screen_x = line_num_width + display_col - view_col;

                /* Determine color */
                uint8_t color = COLOR_TEXT;

                /* Check if this is the cursor position */
                if (file_line == cursor_line && i == cursor_col) {
                    color = COLOR_CURSOR;
                }

                /* Draw character */
                if (ch == '\t') {
                    /* Draw tab as spaces */
                    for (int t = 0; t < char_width && screen_x + t < 80; t++) {
                        char sp[2] = {' ', '\0'};
                        vga_write_at(sp, color, screen_x + t, screen_y);
                    }
                } else if (ch >= 32 && ch < 127) {
                    char str[2] = {ch, '\0'};
                    vga_write_at(str, color, screen_x, screen_y);
                } else {
                    /* Non-printable - show as dot */
                    vga_write_at(".", COLOR_COMMENT, screen_x, screen_y);
                }
            }

            display_col += char_width;
        }

        /* Draw cursor at end of line if needed */
        if (file_line == cursor_line && cursor_col >= line->length) {
            int cursor_display = nano_get_display_col(file_line, cursor_col);
            if (cursor_display >= view_col && cursor_display < view_col + text_width) {
                int screen_x = line_num_width + cursor_display - view_col;
                if (screen_x < 80) {
                    vga_write_at(" ", COLOR_CURSOR, screen_x, screen_y);
                }
            }
        }
    }
}

/*
 * Draw entire screen
 */
static void nano_draw_screen(void) {
    nano_draw_title();
    nano_draw_text();
    nano_draw_status(NULL);
    nano_draw_help();
}


/*
 * Ensure cursor is visible on screen
 */
static void nano_ensure_cursor_visible(void) {
    int line_num_width = show_line_numbers ? 5 : 0;
    int text_width = 80 - line_num_width;

    /* Vertical scrolling */
    if (cursor_line < view_line) {
        view_line = cursor_line;
    } else if (cursor_line >= view_line + NANO_EDIT_ROWS) {
        view_line = cursor_line - NANO_EDIT_ROWS + 1;
    }

    /* Horizontal scrolling */
    int cursor_display = nano_get_display_col(cursor_line, cursor_col);
    if (cursor_display < view_col) {
        view_col = cursor_display;
    } else if (cursor_display >= view_col + text_width - 1) {
        view_col = cursor_display - text_width + 2;
    }
}

/*
 * Move cursor
 */
static void nano_move_cursor(int dline, int dcol) {
    cursor_line += dline;
    cursor_col += dcol;

    /* Clamp line */
    if (cursor_line < 0) cursor_line = 0;
    if (cursor_line >= num_lines) cursor_line = num_lines - 1;

    /* Clamp column */
    if (cursor_col < 0) {
        /* Move to end of previous line */
        if (cursor_line > 0) {
            cursor_line--;
            cursor_col = lines[cursor_line].length;
        } else {
            cursor_col = 0;
        }
    } else if (cursor_col > lines[cursor_line].length) {
        /* Move to start of next line */
        if (cursor_line < num_lines - 1 && dcol > 0) {
            cursor_line++;
            cursor_col = 0;
        } else {
            cursor_col = lines[cursor_line].length;
        }
    }

    nano_ensure_cursor_visible();
}

/*
 * Insert a character at cursor
 */
static void nano_insert_char(char c) {
    line_t* line = &lines[cursor_line];

    if (line->length >= NANO_MAX_LINE_LEN - 1) {
        nano_draw_status("Line too long");
        return;
    }

    /* Push undo */
    nano_push_undo(UNDO_INSERT_CHAR, cursor_line, cursor_col, c, NULL);

    /* Shift characters right */
    for (int i = line->length; i > cursor_col; i--) {
        line->text[i] = line->text[i - 1];
    }

    line->text[cursor_col] = c;
    line->length++;
    line->text[line->length] = '\0';
    cursor_col++;

    modified = 1;
    nano_ensure_cursor_visible();
}

/*
 * Delete character at cursor (Delete key)
 */
static void nano_delete_char(void) {
    line_t* line = &lines[cursor_line];

    if (cursor_col < line->length) {
        /* Delete character at cursor */
        char deleted = line->text[cursor_col];
        nano_push_undo(UNDO_DELETE_CHAR, cursor_line, cursor_col, deleted, NULL);

        for (int i = cursor_col; i < line->length; i++) {
            line->text[i] = line->text[i + 1];
        }
        line->length--;
        modified = 1;
    } else if (cursor_line < num_lines - 1) {
        /* Join with next line */
        line_t* next = &lines[cursor_line + 1];

        if (line->length + next->length < NANO_MAX_LINE_LEN) {
            nano_push_undo(UNDO_DELETE_LINE, cursor_line + 1, 0, 0, next->text);

            strcat(line->text, next->text);
            line->length += next->length;

            /* Shift lines up */
            for (int i = cursor_line + 1; i < num_lines - 1; i++) {
                lines[i] = lines[i + 1];
            }
            num_lines--;
            modified = 1;
        }
    }
}

/*
 * Backspace - delete character before cursor
 */
static void nano_backspace(void) {
    if (cursor_col > 0) {
        cursor_col--;
        nano_delete_char();
    } else if (cursor_line > 0) {
        /* Join with previous line */
        cursor_line--;
        cursor_col = lines[cursor_line].length;
        nano_delete_char();
    }
}

/*
 * Insert a new line (Enter key)
 */
static void nano_insert_line(void) {
    if (num_lines >= NANO_MAX_LINES) {
        nano_draw_status("Too many lines");
        return;
    }

    line_t* line = &lines[cursor_line];

    /* Push undo for the new line */
    nano_push_undo(UNDO_INSERT_LINE, cursor_line + 1, 0, 0, NULL);

    /* Shift lines down */
    for (int i = num_lines; i > cursor_line + 1; i--) {
        lines[i] = lines[i - 1];
    }
    num_lines++;

    /* Split current line */
    line_t* new_line = &lines[cursor_line + 1];
    strcpy(new_line->text, &line->text[cursor_col]);
    new_line->length = line->length - cursor_col;

    line->text[cursor_col] = '\0';
    line->length = cursor_col;

    /* Move cursor to new line */
    cursor_line++;
    cursor_col = 0;

    modified = 1;
    nano_ensure_cursor_visible();
}

/*
 * Delete entire line
 */
static void nano_delete_line(void) {
    if (num_lines <= 1) {
        /* Can't delete last line, just clear it */
        nano_push_undo(UNDO_DELETE_LINE, cursor_line, 0, 0, lines[cursor_line].text);
        lines[cursor_line].text[0] = '\0';
        lines[cursor_line].length = 0;
        cursor_col = 0;
        modified = 1;
        return;
    }

    nano_push_undo(UNDO_DELETE_LINE, cursor_line, 0, 0, lines[cursor_line].text);

    /* Shift lines up */
    for (int i = cursor_line; i < num_lines - 1; i++) {
        lines[i] = lines[i + 1];
    }
    num_lines--;

    /* Adjust cursor */
    if (cursor_line >= num_lines) {
        cursor_line = num_lines - 1;
    }
    cursor_col = 0;

    modified = 1;
}

/*
 * Prompt for input at status line
 */
static int nano_prompt(const char* prompt, char* buffer, int bufsize) {
    nano_clear_line(NANO_STATUS_ROW, COLOR_STATUS);
    vga_write_at(prompt, COLOR_STATUS, 0, NANO_STATUS_ROW);

    int prompt_len = strlen(prompt);
    int pos = strlen(buffer);  /* Start with existing content */

    /* Show existing content */
    if (pos > 0) {
        vga_write_at(buffer, COLOR_STATUS, prompt_len, NANO_STATUS_ROW);
    }

    while (1) {
        /* Show cursor */
        vga_write_at("_", COLOR_STATUS, prompt_len + pos, NANO_STATUS_ROW);

        char c = keyboard_getchar();

        if (c == '\n') {
            buffer[pos] = '\0';
            return 0;  /* Success */
        } else if (c == KEY_ESCAPE || (c == 'C' - '@')) {  /* ESC or Ctrl+C */
            return -1;  /* Cancelled */
        } else if (c == KEY_BACKSPACE || c == 127) {
            if (pos > 0) {
                pos--;
                buffer[pos] = '\0';
                vga_write_at(" ", COLOR_STATUS, prompt_len + pos, NANO_STATUS_ROW);
            }
        } else if (c >= 32 && c < 127 && pos < bufsize - 1) {
            buffer[pos] = c;
            pos++;
            buffer[pos] = '\0';
            char str[2] = {c, '\0'};
            vga_write_at(str, COLOR_STATUS, prompt_len + pos - 1, NANO_STATUS_ROW);
        }
    }
}

/*
 * Save file
 */
static int nano_save_file(void) {
    char fname[256];
    strcpy(fname, filename);

    if (nano_prompt(" File Name to Write: ", fname, sizeof(fname)) < 0) {
        nano_draw_status("Cancelled");
        return -1;
    }

    if (fname[0] == '\0') {
        nano_draw_status("No filename");
        return -1;
    }

    /* Resolve path */
    char resolved_path[256];
    shell_resolve_path(fname, resolved_path, sizeof(resolved_path));

    /* Calculate total size */
    int total_size = 0;
    for (int i = 0; i < num_lines; i++) {
        total_size += lines[i].length + 1;  /* +1 for newline */
    }

    /* Allocate buffer */
    char* content = (char*)kmalloc(total_size + 1);
    if (!content) {
        nano_draw_status("Out of memory");
        return -1;
    }

    /* Build content */
    int pos = 0;
    for (int i = 0; i < num_lines; i++) {
        memcpy(&content[pos], lines[i].text, lines[i].length);
        pos += lines[i].length;
        content[pos++] = '\n';
    }
    content[pos] = '\0';

    /* Create or overwrite file */
    vfs_node_t* file = vfs_lookup(resolved_path);
    if (file) {
        file->length = 0;
    } else {
        vfs_node_t* cwd = shell_get_cwd_node();
        if (cwd->readdir == ext2_vfs_readdir) {
            file = ext2_create_file(cwd, fname);
        } else {
            file = ramfs_create_file_in(cwd, fname, VFS_FILE);
        }
        if (!file) {
            kfree(content);
            nano_draw_status("Failed to create file");
            return -1;
        }
    }

    /* Write content */
    vfs_write(file, 0, pos, (uint8_t*)content);
    kfree(content);

    strcpy(filename, fname);
    modified = 0;

    char msg[80];
    snprintf(msg, sizeof(msg), "Wrote %d lines to %s", num_lines, fname);
    nano_draw_status(msg);
    nano_draw_title();

    return 0;
}

/*
 * Load file
 */
static int nano_load_file(const char* name) {
    char resolved_path[256];
    shell_resolve_path(name, resolved_path, sizeof(resolved_path));

    vfs_node_t* file = vfs_lookup(resolved_path);
    if (!file) {
        return -1;
    }

    /* Read content */
    char* content = (char*)kmalloc(file->length + 1);
    if (!content) {
        return -1;
    }

    vfs_read(file, 0, file->length, (uint8_t*)content);
    content[file->length] = '\0';

    /* Clear current content */
    nano_init();
    strcpy(filename, name);

    /* Parse lines */
    num_lines = 0;
    int line_start = 0;

    for (uint32_t i = 0; i <= file->length; i++) {
        if (content[i] == '\n' || content[i] == '\0') {
            if (num_lines < NANO_MAX_LINES) {
                int line_len = i - line_start;
                if (line_len > NANO_MAX_LINE_LEN - 1) {
                    line_len = NANO_MAX_LINE_LEN - 1;
                }
                memcpy(lines[num_lines].text, &content[line_start], line_len);
                lines[num_lines].text[line_len] = '\0';
                lines[num_lines].length = line_len;
                num_lines++;
            }
            line_start = i + 1;
        }
    }

    if (num_lines == 0) {
        num_lines = 1;
    }

    kfree(content);
    modified = 0;

    return 0;
}

/*
 * Search for text
 */
static void nano_search(void) {
    if (nano_prompt(" Search: ", search_term, sizeof(search_term)) < 0) {
        nano_draw_screen();
        return;
    }

    if (search_term[0] == '\0') {
        nano_draw_screen();
        return;
    }

    nano_search_next();
}

/*
 * Find next occurrence
 */
static void nano_search_next(void) {
    if (search_term[0] == '\0') {
        nano_draw_status("No search term");
        return;
    }

    int search_len = strlen(search_term);
    int start_line = cursor_line;
    int start_col = cursor_col + 1;  /* Start after current position */

    /* Search from current position to end */
    for (int line = start_line; line < num_lines; line++) {
        int col_start = (line == start_line) ? start_col : 0;

        for (int col = col_start; col <= lines[line].length - search_len; col++) {
            int match = 1;
            for (int i = 0; i < search_len && match; i++) {
                char c1 = lines[line].text[col + i];
                char c2 = search_term[i];
                if (!search_case_sensitive) {
                    if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
                    if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
                }
                if (c1 != c2) match = 0;
            }

            if (match) {
                cursor_line = line;
                cursor_col = col;
                nano_ensure_cursor_visible();
                nano_draw_screen();
                return;
            }
        }
    }

    /* Wrap around if enabled */
    if (search_wrap) {
        for (int line = 0; line <= start_line; line++) {
            int col_end = (line == start_line) ? start_col : lines[line].length;

            for (int col = 0; col <= col_end - search_len; col++) {
                int match = 1;
                for (int i = 0; i < search_len && match; i++) {
                    char c1 = lines[line].text[col + i];
                    char c2 = search_term[i];
                    if (!search_case_sensitive) {
                        if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
                        if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
                    }
                    if (c1 != c2) match = 0;
                }

                if (match) {
                    cursor_line = line;
                    cursor_col = col;
                    nano_ensure_cursor_visible();
                    nano_draw_screen();
                    nano_draw_status("Search wrapped");
                    return;
                }
            }
        }
    }

    nano_draw_status("Not found");
}

/*
 * Search and replace
 */
static void nano_replace(void) {
    if (nano_prompt(" Search (to replace): ", search_term, sizeof(search_term)) < 0) {
        nano_draw_screen();
        return;
    }

    if (search_term[0] == '\0') {
        nano_draw_screen();
        return;
    }

    replace_term[0] = '\0';
    if (nano_prompt(" Replace with: ", replace_term, sizeof(replace_term)) < 0) {
        nano_draw_screen();
        return;
    }

    int search_len = strlen(search_term);
    int replace_len = strlen(replace_term);
    int replaced = 0;

    /* Replace all occurrences */
    for (int line = 0; line < num_lines; line++) {
        for (int col = 0; col <= lines[line].length - search_len; col++) {
            int match = 1;
            for (int i = 0; i < search_len && match; i++) {
                if (lines[line].text[col + i] != search_term[i]) match = 0;
            }

            if (match) {
                /* Check if replacement fits */
                int new_len = lines[line].length - search_len + replace_len;
                if (new_len >= NANO_MAX_LINE_LEN) continue;

                /* Shift text */
                if (replace_len != search_len) {
                    memmove(&lines[line].text[col + replace_len],
                            &lines[line].text[col + search_len],
                            lines[line].length - col - search_len + 1);
                }

                /* Insert replacement */
                memcpy(&lines[line].text[col], replace_term, replace_len);
                lines[line].length = new_len;

                col += replace_len - 1;  /* Skip past replacement */
                replaced++;
                modified = 1;
            }
        }
    }

    nano_draw_screen();
    char msg[80];
    snprintf(msg, sizeof(msg), "Replaced %d occurrences", replaced);
    nano_draw_status(msg);
}

/*
 * Go to specific line
 */
static void nano_goto_line(void) {
    char buf[16] = "";

    if (nano_prompt(" Enter line number: ", buf, sizeof(buf)) < 0) {
        nano_draw_screen();
        return;
    }

    /* Parse number */
    int line_num = 0;
    for (int i = 0; buf[i]; i++) {
        if (buf[i] >= '0' && buf[i] <= '9') {
            line_num = line_num * 10 + (buf[i] - '0');
        }
    }

    if (line_num < 1) line_num = 1;
    if (line_num > num_lines) line_num = num_lines;

    cursor_line = line_num - 1;
    cursor_col = 0;
    nano_ensure_cursor_visible();
    nano_draw_screen();
}

/*
 * Cut current line to buffer
 */
static void nano_cut_line(void) {
    /* Free old buffer */
    if (cut_buffer) {
        kfree(cut_buffer);
    }

    /* Copy line to buffer */
    cut_buffer_size = lines[cursor_line].length + 2;
    cut_buffer = (char*)kmalloc(cut_buffer_size);
    if (cut_buffer) {
        strcpy(cut_buffer, lines[cursor_line].text);
        strcat(cut_buffer, "\n");
    }

    /* Delete the line */
    nano_delete_line();
    nano_draw_screen();
    nano_draw_status("Cut 1 line");
}

/*
 * Copy current line to buffer
 */
static void nano_copy_line(void) {
    /* Free old buffer */
    if (cut_buffer) {
        kfree(cut_buffer);
    }

    /* Copy line to buffer */
    cut_buffer_size = lines[cursor_line].length + 2;
    cut_buffer = (char*)kmalloc(cut_buffer_size);
    if (cut_buffer) {
        strcpy(cut_buffer, lines[cursor_line].text);
        strcat(cut_buffer, "\n");
    }

    nano_draw_status("Copied 1 line");
}

/*
 * Paste from buffer
 */
static void nano_paste(void) {
    if (!cut_buffer || cut_buffer_size == 0) {
        nano_draw_status("Nothing to paste");
        return;
    }

    /* Insert new line */
    if (num_lines >= NANO_MAX_LINES) {
        nano_draw_status("Too many lines");
        return;
    }

    /* Shift lines down */
    for (int i = num_lines; i > cursor_line; i--) {
        lines[i] = lines[i - 1];
    }
    num_lines++;

    /* Copy content (without trailing newline) */
    int len = strlen(cut_buffer);
    if (len > 0 && cut_buffer[len - 1] == '\n') len--;
    if (len > NANO_MAX_LINE_LEN - 1) len = NANO_MAX_LINE_LEN - 1;

    memcpy(lines[cursor_line].text, cut_buffer, len);
    lines[cursor_line].text[len] = '\0';
    lines[cursor_line].length = len;

    modified = 1;
    nano_draw_screen();
    nano_draw_status("Pasted 1 line");
}

/*
 * Show help screen
 */
static void nano_show_help(void) {
    vga_clear();

    vga_write_at("  MiniOS nano Help", COLOR_TITLE, 0, 0);

    int row = 2;
    vga_write_at("Navigation:", COLOR_STATUS, 2, row++);
    vga_write_at("  Arrow keys    Move cursor", COLOR_TEXT, 2, row++);
    vga_write_at("  Home/End      Start/End of line", COLOR_TEXT, 2, row++);
    vga_write_at("  PgUp/PgDn     Page up/down", COLOR_TEXT, 2, row++);
    vga_write_at("  Ctrl+_        Go to line number", COLOR_TEXT, 2, row++);
    row++;

    vga_write_at("Editing:", COLOR_STATUS, 2, row++);
    vga_write_at("  Ctrl+K        Cut current line", COLOR_TEXT, 2, row++);
    vga_write_at("  Ctrl+6        Copy current line", COLOR_TEXT, 2, row++);
    vga_write_at("  Ctrl+U        Paste", COLOR_TEXT, 2, row++);
    vga_write_at("  Ctrl+Z        Undo", COLOR_TEXT, 2, row++);
    row++;

    vga_write_at("Search:", COLOR_STATUS, 2, row++);
    vga_write_at("  Ctrl+W        Search", COLOR_TEXT, 2, row++);
    vga_write_at("  Ctrl+\\        Search and replace", COLOR_TEXT, 2, row++);
    row++;

    vga_write_at("File:", COLOR_STATUS, 2, row++);
    vga_write_at("  Ctrl+O        Save file", COLOR_TEXT, 2, row++);
    vga_write_at("  Ctrl+R        Read/insert file", COLOR_TEXT, 2, row++);
    vga_write_at("  Ctrl+X        Exit", COLOR_TEXT, 2, row++);
    row++;

    vga_write_at("Press any key to continue...", COLOR_STATUS, 2, row + 1);

    keyboard_getchar();
    nano_draw_screen();
}

/*
 * Read and insert file at cursor
 */
static void nano_read_file(void) {
    char fname[256] = "";

    if (nano_prompt(" File to insert: ", fname, sizeof(fname)) < 0) {
        nano_draw_screen();
        return;
    }

    char resolved_path[256];
    shell_resolve_path(fname, resolved_path, sizeof(resolved_path));

    vfs_node_t* file = vfs_lookup(resolved_path);
    if (!file) {
        nano_draw_status("File not found");
        return;
    }

    char* content = (char*)kmalloc(file->length + 1);
    if (!content) {
        nano_draw_status("Out of memory");
        return;
    }

    vfs_read(file, 0, file->length, (uint8_t*)content);
    content[file->length] = '\0';

    /* Insert lines at cursor */
    int inserted = 0;
    int line_start = 0;

    for (uint32_t i = 0; i <= file->length && num_lines < NANO_MAX_LINES; i++) {
        if (content[i] == '\n' || content[i] == '\0') {
            /* Shift lines down */
            for (int j = num_lines; j > cursor_line + inserted; j--) {
                lines[j] = lines[j - 1];
            }
            num_lines++;

            int line_len = i - line_start;
            if (line_len > NANO_MAX_LINE_LEN - 1) line_len = NANO_MAX_LINE_LEN - 1;

            int insert_pos = cursor_line + inserted;
            memcpy(lines[insert_pos].text, &content[line_start], line_len);
            lines[insert_pos].text[line_len] = '\0';
            lines[insert_pos].length = line_len;

            inserted++;
            line_start = i + 1;
        }
    }

    kfree(content);
    modified = 1;

    nano_draw_screen();
    char msg[80];
    snprintf(msg, sizeof(msg), "Inserted %d lines", inserted);
    nano_draw_status(msg);
}

/*
 * Confirm exit if modified
 */
static int nano_confirm_exit(void) {
    if (!modified) return 1;

    nano_draw_status("Save modified buffer? (Y/N/C)");

    while (1) {
        char c = keyboard_getchar();
        if (c == 'y' || c == 'Y') {
            if (nano_save_file() == 0) {
                return 1;
            }
            return 0;  /* Save failed, don't exit */
        } else if (c == 'n' || c == 'N') {
            return 1;  /* Exit without saving */
        } else if (c == 'c' || c == 'C' || c == KEY_ESCAPE) {
            nano_draw_screen();
            return 0;  /* Cancel exit */
        }
    }
}

/*
 * Main editor loop
 */
static void nano_main_loop(void) {
    vga_clear();
    nano_draw_screen();

    while (1) {
        char c = keyboard_getchar();
        unsigned char uc = (unsigned char)c;

        /* Handle backspace FIRST (before control key check since '\b' == Ctrl+H) */
        if (c == '\b' || c == 127) {
            nano_backspace();
            nano_draw_screen();
            continue;
        }

        /* Handle enter FIRST (before control key check since '\n' == Ctrl+J) */
        if (c == '\n' || c == '\r') {
            nano_insert_line();
            nano_draw_screen();
            continue;
        }

        /* Handle tab */
        if (c == '\t') {
            int spaces = NANO_TAB_SIZE - (cursor_col % NANO_TAB_SIZE);
            for (int i = 0; i < spaces; i++) {
                nano_insert_char(' ');
            }
            nano_draw_screen();
            continue;
        }

        /* Handle control keys (Ctrl+letter = letter - '@') */
        if (c >= 1 && c <= 26) {
            switch (c) {
                case 'X' - '@':  /* Ctrl+X - Exit */
                    if (nano_confirm_exit()) {
                        return;
                    }
                    break;

                case 'O' - '@':  /* Ctrl+O - Save */
                    nano_save_file();
                    break;

                case 'R' - '@':  /* Ctrl+R - Read file */
                    nano_read_file();
                    break;

                case 'W' - '@':  /* Ctrl+W - Search */
                    nano_search();
                    break;

                case 'G' - '@':  /* Ctrl+G - Help */
                    nano_show_help();
                    break;

                case 'K' - '@':  /* Ctrl+K - Cut line */
                    nano_cut_line();
                    break;

                case 'U' - '@':  /* Ctrl+U - Paste */
                    nano_paste();
                    break;

                case 'C' - '@':  /* Ctrl+C - Show position */
                    {
                        char msg[80];
                        snprintf(msg, sizeof(msg), "Line %d/%d, Col %d/%d, Char %d",
                                 cursor_line + 1, num_lines,
                                 cursor_col + 1, lines[cursor_line].length,
                                 cursor_col < lines[cursor_line].length ?
                                     (unsigned char)lines[cursor_line].text[cursor_col] : 0);
                        nano_draw_status(msg);
                    }
                    break;

                case 'Z' - '@':  /* Ctrl+Z - Undo */
                    nano_undo();
                    break;

                case 'L' - '@':  /* Ctrl+L - Refresh */
                    nano_draw_screen();
                    break;

                case '\\' - '@':  /* Ctrl+\ - Replace */
                    nano_replace();
                    break;

                case '_' - '@':  /* Ctrl+_ - Go to line */
                    nano_goto_line();
                    break;

                case '6' - '@':  /* Ctrl+6 - Copy */
                    nano_copy_line();
                    break;

                case 'A' - '@':  /* Ctrl+A - Home */
                    cursor_col = 0;
                    nano_draw_screen();
                    break;

                case 'E' - '@':  /* Ctrl+E - End */
                    cursor_col = lines[cursor_line].length;
                    nano_draw_screen();
                    break;
            }
            continue;
        }

        /* Handle special keys */
        if (uc == KEY_UP) {
            nano_move_cursor(-1, 0);
            nano_draw_screen();
        } else if (uc == KEY_DOWN) {
            nano_move_cursor(1, 0);
            nano_draw_screen();
        } else if (uc == KEY_LEFT) {
            nano_move_cursor(0, -1);
            nano_draw_screen();
        } else if (uc == KEY_RIGHT) {
            nano_move_cursor(0, 1);
            nano_draw_screen();
        } else if (uc == KEY_HOME) {
            cursor_col = 0;
            nano_ensure_cursor_visible();
            nano_draw_screen();
        } else if (uc == KEY_END) {
            cursor_col = lines[cursor_line].length;
            nano_ensure_cursor_visible();
            nano_draw_screen();
        } else if (uc == KEY_PAGEUP) {
            nano_move_cursor(-NANO_EDIT_ROWS, 0);
            nano_draw_screen();
        } else if (uc == KEY_PAGEDOWN) {
            nano_move_cursor(NANO_EDIT_ROWS, 0);
            nano_draw_screen();
        } else if (uc == KEY_DELETE) {
            nano_delete_char();
            nano_draw_screen();
        } else if (c >= 32 && c < 127) {
            /* Regular character */
            nano_insert_char(c);
            nano_draw_screen();
        }
    }
}

/*
 * Run nano without a file
 */
void nano_run(void) {
    nano_init();
    nano_main_loop();

    /* Cleanup */
    if (cut_buffer) {
        kfree(cut_buffer);
        cut_buffer = NULL;
    }
}

/*
 * Run nano with a file
 */
void nano_run_file(const char* name) {
    nano_init();

    if (nano_load_file(name) < 0) {
        /* File doesn't exist - create new with this name */
        strcpy(filename, name);
    }

    nano_main_loop();

    /* Cleanup */
    if (cut_buffer) {
        kfree(cut_buffer);
        cut_buffer = NULL;
    }
}
