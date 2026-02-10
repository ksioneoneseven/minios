/*
 * MiniOS XGUI File Explorer
 *
 * A graphical file browser for navigating the filesystem.
 */

#include "xgui/xgui.h"
#include "xgui/wm.h"
#include "xgui/widget.h"
#include "xgui/theme.h"
#include "vfs.h"
#include "ramfs.h"
#include "ext2.h"
#include "shell.h"
#include "string.h"
#include "stdio.h"
#include "heap.h"
#include "timer.h"

/* Explorer configuration */
#define EXPLORER_WIDTH      420
#define EXPLORER_HEIGHT     360
#define MAX_VISIBLE_ITEMS   14
#define MAX_DIR_ENTRIES     64
#define ITEM_HEIGHT         22
#define PATH_BAR_HEIGHT     28
#define TOOLBAR_HEIGHT      32

/* Directory entry for display */
typedef struct {
    char name[VFS_MAX_NAME];
    uint32_t size;
    uint32_t flags;
    bool is_dir;
} explorer_entry_t;

/* Explorer state */
static xgui_window_t* explorer_window = NULL;
static char current_path[VFS_MAX_PATH] = "/";
static explorer_entry_t entries[MAX_DIR_ENTRIES];
static int entry_count = 0;
static int selected_index = -1;
static int scroll_offset = 0;

/* Context menu state */
#define CTX_MENU_W      180
#define CTX_MENU_ITEM_H 20
#define CTX_MENU_ITEMS  6
#define CTX_MENU_SEP    3       /* separator after item index 2 (after Open with Paint) */
#define CTX_MENU_H      (CTX_MENU_ITEM_H * CTX_MENU_ITEMS + 4 + 4) /* +4 for separator line */

static int ctx_menu_visible = 0;
static int ctx_menu_x = 0;
static int ctx_menu_y = 0;
static int ctx_menu_index = -1;   /* entry index that was right-clicked */
static int ctx_menu_hover = -1;   /* hovered menu item */

/* Clipboard state for cut/copy/paste */
static char clipboard_path[VFS_MAX_PATH];  /* full path of copied/cut file */
static int  clipboard_is_cut = 0;          /* 1 = cut, 0 = copy */
static int  clipboard_valid = 0;           /* 1 = clipboard has content */

/* Widgets */
static xgui_widget_t* btn_up = NULL;
static xgui_widget_t* btn_home = NULL;
static xgui_widget_t* btn_refresh = NULL;

/*
 * Format file size for display
 */
static void format_size(uint32_t size, char* buf, int buf_size) {
    if (size < 1024) {
        snprintf(buf, buf_size, "%u B", size);
    } else if (size < 1024 * 1024) {
        snprintf(buf, buf_size, "%u KB", size / 1024);
    } else {
        snprintf(buf, buf_size, "%u MB", size / (1024 * 1024));
    }
}

/*
 * Compare entries for sorting (directories first, then alphabetical)
 */
static int compare_entries(const explorer_entry_t* a, const explorer_entry_t* b) {
    /* Directories come first */
    if (a->is_dir && !b->is_dir) return -1;
    if (!a->is_dir && b->is_dir) return 1;
    /* Then sort alphabetically */
    return strcmp(a->name, b->name);
}

/*
 * Simple bubble sort for entries
 */
static void sort_entries(void) {
    for (int i = 0; i < entry_count - 1; i++) {
        for (int j = 0; j < entry_count - i - 1; j++) {
            if (compare_entries(&entries[j], &entries[j + 1]) > 0) {
                explorer_entry_t temp = entries[j];
                entries[j] = entries[j + 1];
                entries[j + 1] = temp;
            }
        }
    }
}

/*
 * Load directory contents
 */
static void load_directory(const char* path) {
    entry_count = 0;
    selected_index = -1;
    scroll_offset = 0;

    /* Copy path */
    strncpy(current_path, path, VFS_MAX_PATH - 1);
    current_path[VFS_MAX_PATH - 1] = '\0';

    /* Normalize path - remove trailing slash unless root */
    int len = strlen(current_path);
    if (len > 1 && current_path[len - 1] == '/') {
        current_path[len - 1] = '\0';
    }

    /* Look up the directory node */
    vfs_node_t* dir = vfs_lookup(current_path);
    if (!dir) {
        /* Path not found, try root */
        strcpy(current_path, "/");
        dir = vfs_root;
    }

    /* Safety check - if still null, can't do anything */
    if (!dir) {
        if (explorer_window) explorer_window->dirty = true;
        return;
    }

    /* Check if it's actually a directory */
    if (!(dir->flags & VFS_DIRECTORY)) {
        strcpy(current_path, "/");
        dir = vfs_root;
        if (!dir) {
            if (explorer_window) explorer_window->dirty = true;
            return;
        }
    }

    /* Read directory entries */
    uint32_t index = 0;
    dirent_t* dirent;

    while ((dirent = vfs_readdir(dir, index)) != NULL && entry_count < MAX_DIR_ENTRIES) {
        /* Skip . and .. */
        if (strcmp(dirent->name, ".") == 0 || strcmp(dirent->name, "..") == 0) {
            index++;
            continue;
        }

        /* Copy entry info */
        strncpy(entries[entry_count].name, dirent->name, VFS_MAX_NAME - 1);
        entries[entry_count].name[VFS_MAX_NAME - 1] = '\0';

        /* Look up the node to get type and size */
        vfs_node_t* node = vfs_finddir(dir, dirent->name);
        if (node) {
            entries[entry_count].flags = node->flags;
            entries[entry_count].size = node->length;
            entries[entry_count].is_dir = (node->flags & VFS_DIRECTORY) != 0;
        } else {
            entries[entry_count].flags = 0;
            entries[entry_count].size = 0;
            entries[entry_count].is_dir = false;
        }

        entry_count++;
        index++;
    }

    /* Sort entries */
    sort_entries();

    if (explorer_window) explorer_window->dirty = true;
}

/*
 * Navigate to parent directory
 */
static void go_up(void) {
    if (strcmp(current_path, "/") == 0) {
        return;  /* Already at root */
    }

    /* Find last slash */
    char* last_slash = strrchr(current_path, '/');
    if (last_slash == current_path) {
        /* Parent is root */
        load_directory("/");
    } else if (last_slash) {
        /* Truncate at last slash */
        *last_slash = '\0';
        load_directory(current_path);
    }
}

/*
 * Navigate into a directory
 */
static void enter_directory(const char* name) {
    char new_path[VFS_MAX_PATH];

    if (strcmp(current_path, "/") == 0) {
        snprintf(new_path, VFS_MAX_PATH, "/%s", name);
    } else {
        snprintf(new_path, VFS_MAX_PATH, "%s/%s", current_path, name);
    }

    load_directory(new_path);
}

/*
 * Handle item click (selection)
 */
static void handle_item_click(int item_index) {
    if (item_index < 0 || item_index >= entry_count) {
        return;
    }

    selected_index = item_index;
    if (explorer_window) explorer_window->dirty = true;
}

/*
 * Handle item double-click (open)
 */
static void handle_item_open(int item_index) {
    if (item_index < 0 || item_index >= entry_count) {
        return;
    }

    if (entries[item_index].is_dir) {
        enter_directory(entries[item_index].name);
    }
    /* For files, we could open with an appropriate application */
}

/*
 * Button click handlers
 */
static void btn_up_click(xgui_widget_t* widget) {
    (void)widget;
    go_up();
}

static void btn_home_click(xgui_widget_t* widget) {
    (void)widget;
    load_directory("/");
}

static void btn_refresh_click(xgui_widget_t* widget) {
    (void)widget;
    load_directory(current_path);
}

/*
 * Copy a file from src_path to dest_dir with the same filename.
 * Returns 0 on success, -1 on failure.
 */
static int explorer_copy_file(const char* src_path, const char* dest_dir) {
    vfs_node_t* src = vfs_lookup(src_path);
    if (!src || (src->flags & VFS_DIRECTORY)) return -1;

    /* Extract filename from source path */
    const char* fname = src_path;
    for (const char* p = src_path; *p; p++) {
        if (*p == '/') fname = p + 1;
    }
    if (!fname[0]) return -1;

    /* Read source file */
    uint32_t size = src->length;
    uint8_t* buf = NULL;
    if (size > 0) {
        buf = (uint8_t*)kmalloc(size);
        if (!buf) return -1;
        int32_t rd = vfs_read(src, 0, size, buf);
        if (rd < 0) { kfree(buf); return -1; }
        size = (uint32_t)rd;
    }

    /* Build destination path */
    char dest_path[VFS_MAX_PATH];
    if (strcmp(dest_dir, "/") == 0) {
        snprintf(dest_path, sizeof(dest_path), "/%s", fname);
    } else {
        snprintf(dest_path, sizeof(dest_path), "%s/%s", dest_dir, fname);
    }

    /* Check if destination already exists */
    vfs_node_t* dst = vfs_lookup(dest_path);
    if (dst) {
        /* Overwrite existing file */
        dst->length = 0;
    } else {
        /* Create new file in destination directory */
        vfs_node_t* parent = vfs_lookup(dest_dir);
        if (!parent) { if (buf) kfree(buf); return -1; }
        if (parent->readdir == ext2_vfs_readdir) {
            dst = ext2_create_file(parent, fname);
        } else {
            dst = ramfs_create_file_in(parent, fname, VFS_FILE);
        }
        if (!dst) { if (buf) kfree(buf); return -1; }
    }

    /* Write data */
    if (size > 0 && buf) {
        vfs_write(dst, 0, size, buf);
        kfree(buf);
    }
    return 0;
}

/*
 * Delete a file by name from its parent directory (ramfs only).
 * Returns 0 on success, -1 on failure.
 */
static int explorer_delete_file(const char* path) {
    /* Find parent dir and filename */
    const char* fname = path;
    const char* last_slash = NULL;
    for (const char* p = path; *p; p++) {
        if (*p == '/') last_slash = p;
    }
    if (!last_slash) return -1;

    char parent_path[VFS_MAX_PATH];
    if (last_slash == path) {
        strcpy(parent_path, "/");
        fname = path + 1;
    } else {
        int plen = (int)(last_slash - path);
        if (plen >= (int)sizeof(parent_path)) plen = sizeof(parent_path) - 1;
        memcpy(parent_path, path, plen);
        parent_path[plen] = '\0';
        fname = last_slash + 1;
    }

    vfs_node_t* parent = vfs_lookup(parent_path);
    if (!parent) return -1;

    /* Only support ramfs deletion for now */
    if (parent->readdir != ramfs_readdir) return -1;

    return ramfs_delete(parent, fname);
}

/*
 * Build full path for an entry
 */
static void build_entry_path(int idx, char* out, int out_size) {
    if (strcmp(current_path, "/") == 0) {
        snprintf(out, out_size, "/%s", entries[idx].name);
    } else {
        snprintf(out, out_size, "%s/%s", current_path, entries[idx].name);
    }
}

/*
 * Draw a small folder icon (12x10) at (x, y)
 */
static void draw_folder_icon(xgui_window_t* win, int x, int y) {
    uint32_t tab  = XGUI_RGB(220, 180, 60);
    uint32_t body = XGUI_RGB(240, 200, 80);
    uint32_t edge = XGUI_RGB(180, 140, 40);
    uint32_t hi   = XGUI_RGB(255, 230, 140);

    /* Tab */
    xgui_win_rect_filled(win, x, y, 5, 2, tab);
    /* Body */
    xgui_win_rect_filled(win, x, y + 2, 12, 8, body);
    /* Highlight */
    xgui_win_hline(win, x + 1, y + 3, 10, hi);
    /* Edge */
    xgui_win_rect(win, x, y + 2, 12, 8, edge);
    xgui_win_hline(win, x, y, 5, edge);
    xgui_win_vline(win, x, y, 2, edge);
    xgui_win_vline(win, x + 5, y, 2, edge);
}

/*
 * Draw a small file icon (10x12) at (x, y)
 */
static void draw_file_icon(xgui_window_t* win, int x, int y) {
    uint32_t paper = XGUI_WHITE;
    uint32_t edge  = XGUI_RGB(140, 140, 140);
    uint32_t fold  = XGUI_RGB(200, 200, 200);
    uint32_t line  = XGUI_RGB(180, 200, 220);

    /* Paper body */
    xgui_win_rect_filled(win, x, y, 10, 12, paper);
    xgui_win_rect(win, x, y, 10, 12, edge);
    /* Dog-ear fold */
    xgui_win_rect_filled(win, x + 7, y, 3, 3, fold);
    xgui_win_hline(win, x + 7, y + 3, 3, edge);
    xgui_win_vline(win, x + 7, y, 3, edge);
    /* Text lines */
    xgui_win_hline(win, x + 2, y + 4, 5, line);
    xgui_win_hline(win, x + 2, y + 6, 6, line);
    xgui_win_hline(win, x + 2, y + 8, 4, line);
}

/*
 * Window paint callback
 * All coordinates are client-relative (0,0 = top-left of client area).
 * Draws into the window's own pixel buffer via xgui_win_* functions.
 */
static void explorer_paint(xgui_window_t* win) {
    int cw = win->client_width;
    int ch = win->client_height;
    uint32_t accent = xgui_theme_current()->selection;

    /* --- Toolbar: subtle gradient background --- */
    for (int row = 0; row < TOOLBAR_HEIGHT; row++) {
        int t = (row * 60) / TOOLBAR_HEIGHT;
        uint32_t c = XGUI_RGB(245 - t / 4, 245 - t / 4, 248 - t / 4);
        xgui_win_hline(win, 0, row, cw, c);
    }
    /* Toolbar bottom edge */
    xgui_win_hline(win, 0, TOOLBAR_HEIGHT - 2, cw, XGUI_RGB(200, 200, 200));
    xgui_win_hline(win, 0, TOOLBAR_HEIGHT - 1, cw, XGUI_RGB(220, 220, 220));

    /* Draw widgets (buttons) */
    xgui_widgets_draw(win);

    /* --- Path bar: modern inset with subtle border --- */
    int path_y = TOOLBAR_HEIGHT;
    /* Path bar background */
    xgui_win_rect_filled(win, 0, path_y, cw, PATH_BAR_HEIGHT, XGUI_RGB(240, 240, 240));
    /* Path input field */
    int pf_x = 6, pf_y = path_y + 4, pf_w = cw - 12, pf_h = PATH_BAR_HEIGHT - 8;
    xgui_win_rect_filled(win, pf_x, pf_y, pf_w, pf_h, XGUI_WHITE);
    xgui_win_rect(win, pf_x, pf_y, pf_w, pf_h, XGUI_RGB(170, 170, 170));
    /* Inner highlight on bottom/right */
    xgui_win_hline(win, pf_x + 1, pf_y + pf_h - 1, pf_w - 2, XGUI_RGB(240, 240, 240));
    /* Path text */
    xgui_win_text(win, pf_x + 6, pf_y + 3, current_path, XGUI_RGB(40, 40, 40), XGUI_WHITE);
    /* Bottom separator */
    xgui_win_hline(win, 0, path_y + PATH_BAR_HEIGHT - 1, cw, XGUI_RGB(210, 210, 210));

    /* --- File list area --- */
    int list_y = path_y + PATH_BAR_HEIGHT;
    int list_h = ch - TOOLBAR_HEIGHT - PATH_BAR_HEIGHT - 22;

    /* List background */
    xgui_win_rect_filled(win, 0, list_y, cw, list_h, XGUI_WHITE);

    /* --- Column header: gradient --- */
    int header_y = list_y;
    int header_h = ITEM_HEIGHT;
    for (int row = 0; row < header_h; row++) {
        int t = (row * 40) / header_h;
        uint32_t c = XGUI_RGB(248 - t, 248 - t, 250 - t);
        xgui_win_hline(win, 0, header_y + row, cw, c);
    }
    xgui_win_hline(win, 0, header_y + header_h - 1, cw, XGUI_RGB(200, 200, 200));
    /* Header text */
    xgui_win_text_transparent(win, 30, header_y + 4, "Name", XGUI_RGB(80, 80, 80));
    xgui_win_text_transparent(win, cw - 80, header_y + 4, "Size", XGUI_RGB(80, 80, 80));

    /* --- Draw entries --- */
    int item_y = header_y + header_h;
    int visible_items = (list_h - header_h) / ITEM_HEIGHT;
    if (visible_items > MAX_VISIBLE_ITEMS) visible_items = MAX_VISIBLE_ITEMS;

    uint32_t row_even = XGUI_WHITE;
    uint32_t row_odd  = XGUI_RGB(247, 249, 252);

    for (int i = 0; i < visible_items && (i + scroll_offset) < entry_count; i++) {
        int idx = i + scroll_offset;
        explorer_entry_t* entry = &entries[idx];
        int y = item_y + i * ITEM_HEIGHT;

        /* Row background: alternating or selected */
        uint32_t row_bg = (i % 2 == 0) ? row_even : row_odd;
        bool selected = (idx == selected_index);

        if (selected) {
            /* Selection highlight with subtle gradient */
            for (int row = 0; row < ITEM_HEIGHT; row++) {
                int t = (row * 255) / ITEM_HEIGHT;
                int r1 = (accent >> 16) & 0xFF, g1 = (accent >> 8) & 0xFF, b1 = accent & 0xFF;
                int r = r1 + (20 - row * 40 / ITEM_HEIGHT);
                int g = g1 + (20 - row * 40 / ITEM_HEIGHT);
                int b = b1 + (20 - row * 40 / ITEM_HEIGHT);
                if (r > 255) r = 255;
                if (r < 0) r = 0;
                if (g > 255) g = 255;
                if (g < 0) g = 0;
                if (b > 255) b = 255;
                if (b < 0) b = 0;
                (void)t;
                xgui_win_hline(win, 0, y + row, cw - 16, XGUI_RGB(r, g, b));
            }
        } else {
            xgui_win_rect_filled(win, 0, y, cw - 16, ITEM_HEIGHT, row_bg);
        }

        uint32_t text_color = selected ? XGUI_WHITE : XGUI_RGB(30, 30, 30);

        /* Icon */
        int icon_x = 8;
        int icon_y = y + (ITEM_HEIGHT - 10) / 2;
        if (entry->is_dir) {
            if (!selected) draw_folder_icon(win, icon_x, icon_y);
            else {
                /* Bright folder on selection */
                xgui_win_rect_filled(win, icon_x, icon_y, 5, 2, XGUI_RGB(255, 240, 160));
                xgui_win_rect_filled(win, icon_x, icon_y + 2, 12, 8, XGUI_RGB(255, 240, 160));
                xgui_win_rect(win, icon_x, icon_y + 2, 12, 8, XGUI_RGB(255, 255, 200));
            }
        } else {
            if (!selected) draw_file_icon(win, icon_x + 1, icon_y);
            else {
                xgui_win_rect_filled(win, icon_x + 1, icon_y, 10, 12, XGUI_RGB(255, 255, 255));
                xgui_win_rect(win, icon_x + 1, icon_y, 10, 12, XGUI_RGB(200, 220, 255));
            }
        }

        /* Name */
        char name_buf[40];
        strncpy(name_buf, entry->name, 35);
        name_buf[35] = '\0';
        if (strlen(entry->name) > 35) strcat(name_buf, "...");
        xgui_win_text_transparent(win, 26, y + 4, name_buf, text_color);

        /* Size */
        if (!entry->is_dir) {
            char size_buf[16];
            format_size(entry->size, size_buf, sizeof(size_buf));
            xgui_win_text_transparent(win, cw - 80, y + 4, size_buf,
                                      selected ? XGUI_RGB(220, 230, 255) : XGUI_RGB(100, 100, 100));
        } else {
            xgui_win_text_transparent(win, cw - 80, y + 4, "<DIR>",
                                      selected ? XGUI_RGB(220, 230, 255) : XGUI_RGB(140, 140, 140));
        }

        /* Subtle bottom border per row */
        if (!selected) {
            xgui_win_hline(win, 0, y + ITEM_HEIGHT - 1, cw - 16, XGUI_RGB(235, 235, 235));
        }
    }

    /* --- Scrollbar with arrow buttons --- */
    int sb_x = cw - 14;
    int sb_full_y = item_y;
    int sb_full_h = visible_items * ITEM_HEIGHT;
    int sb_btn_h = 14;  /* Arrow button height */

    if (entry_count > visible_items && visible_items > 0) {
        /* Full scrollbar background */
        xgui_win_rect_filled(win, sb_x, sb_full_y, 14, sb_full_h, XGUI_RGB(240, 240, 240));
        xgui_win_vline(win, sb_x, sb_full_y, sb_full_h, XGUI_RGB(210, 210, 210));

        /* Up arrow button */
        xgui_win_rect_filled(win, sb_x, sb_full_y, 14, sb_btn_h, XGUI_RGB(225, 225, 228));
        xgui_win_rect(win, sb_x, sb_full_y, 14, sb_btn_h, XGUI_RGB(190, 190, 190));
        xgui_win_hline(win, sb_x + 1, sb_full_y + 1, 12, XGUI_RGB(245, 245, 248));
        /* Up triangle */
        for (int r = 0; r < 4; r++) {
            xgui_win_hline(win, sb_x + 7 - r, sb_full_y + 4 + r, r * 2 + 1, XGUI_RGB(80, 80, 80));
        }

        /* Down arrow button */
        int dn_y = sb_full_y + sb_full_h - sb_btn_h;
        xgui_win_rect_filled(win, sb_x, dn_y, 14, sb_btn_h, XGUI_RGB(225, 225, 228));
        xgui_win_rect(win, sb_x, dn_y, 14, sb_btn_h, XGUI_RGB(190, 190, 190));
        xgui_win_hline(win, sb_x + 1, dn_y + 1, 12, XGUI_RGB(245, 245, 248));
        /* Down triangle */
        for (int r = 0; r < 4; r++) {
            xgui_win_hline(win, sb_x + 7 - r, dn_y + 9 - r, r * 2 + 1, XGUI_RGB(80, 80, 80));
        }

        /* Track area (between buttons) */
        int track_y = sb_full_y + sb_btn_h;
        int track_h = sb_full_h - sb_btn_h * 2;

        /* Thumb */
        if (track_h > 4) {
            int thumb_h = (visible_items * track_h) / entry_count;
            if (thumb_h < 16) thumb_h = 16;
            int scroll_range = entry_count - visible_items;
            int thumb_y = track_y;
            if (scroll_range > 0)
                thumb_y = track_y + (scroll_offset * (track_h - thumb_h)) / scroll_range;

            xgui_win_rect_filled(win, sb_x + 1, thumb_y, 12, thumb_h, XGUI_RGB(180, 180, 185));
            xgui_win_hline(win, sb_x + 2, thumb_y, 10, XGUI_RGB(210, 210, 215));
            xgui_win_hline(win, sb_x + 2, thumb_y + thumb_h - 1, 10, XGUI_RGB(155, 155, 160));
            /* Grip lines */
            int grip_y = thumb_y + thumb_h / 2 - 3;
            for (int g = 0; g < 3; g++) {
                xgui_win_hline(win, sb_x + 4, grip_y + g * 3, 6, XGUI_RGB(155, 155, 160));
                xgui_win_hline(win, sb_x + 4, grip_y + g * 3 + 1, 6, XGUI_RGB(210, 210, 215));
            }
        }
    }

    /* --- Status bar --- */
    int status_y = ch - 22;
    /* Gradient status bar */
    for (int row = 0; row < 22; row++) {
        int t = (row * 20) / 22;
        xgui_win_hline(win, 0, status_y + row, cw, XGUI_RGB(240 - t, 240 - t, 242 - t));
    }
    xgui_win_hline(win, 0, status_y, cw, XGUI_RGB(200, 200, 200));

    char status[80];
    snprintf(status, sizeof(status), " %d items", entry_count);
    xgui_win_text_transparent(win, 6, status_y + 5, status, XGUI_RGB(80, 80, 80));

    /* Context menu (drawn last, on top of everything) */
    if (ctx_menu_visible && ctx_menu_index >= 0 && ctx_menu_index < entry_count) {
        int mx = ctx_menu_x;
        int my = ctx_menu_y;

        /* Clamp to window bounds */
        if (mx + CTX_MENU_W > cw) mx = cw - CTX_MENU_W;
        if (my + CTX_MENU_H > ch) my = ch - CTX_MENU_H;

        /* Menu background with shadow */
        xgui_win_rect_filled(win, mx + 2, my + 2, CTX_MENU_W, CTX_MENU_H, XGUI_RGB(80, 80, 80));
        xgui_win_rect_filled(win, mx, my, CTX_MENU_W, CTX_MENU_H, XGUI_RGB(240, 240, 240));
        xgui_win_rect(win, mx, my, CTX_MENU_W, CTX_MENU_H, XGUI_DARK_GRAY);

        /* Menu items */
        const char* labels[CTX_MENU_ITEMS] = {
            "Open with Text Editor",
            "Open with Spreadsheet",
            "Open with Paint",
            "Cut",
            "Copy",
            "Paste"
        };
        int sep_extra = 0;  /* extra Y offset after separator */
        for (int i = 0; i < CTX_MENU_ITEMS; i++) {
            if (i == CTX_MENU_SEP) {
                /* Draw separator line */
                int sy = my + 2 + i * CTX_MENU_ITEM_H + sep_extra;
                xgui_win_hline(win, mx + 4, sy, CTX_MENU_W - 8, XGUI_DARK_GRAY);
                sep_extra += 4;
            }
            int iy = my + 2 + i * CTX_MENU_ITEM_H + sep_extra;
            /* Gray out Paste if clipboard is empty */
            int grayed = (i == 5 && !clipboard_valid);
            if (i == ctx_menu_hover && !grayed) {
                xgui_win_rect_filled(win, mx + 1, iy, CTX_MENU_W - 2, CTX_MENU_ITEM_H,
                                     XGUI_SELECTION);
                xgui_win_text(win, mx + 8, iy + 3, labels[i], XGUI_WHITE, XGUI_SELECTION);
            } else {
                uint32_t fg = grayed ? XGUI_GRAY : XGUI_BLACK;
                xgui_win_text(win, mx + 8, iy + 3, labels[i], fg,
                              XGUI_RGB(240, 240, 240));
            }
        }
    }
}

/*
 * Window event handler
 */
static void explorer_handler(xgui_window_t* win, xgui_event_t* event) {
    /* Let widgets handle events first */
    if (xgui_widgets_handle_event(win, event)) {
        win->dirty = true;
        return;
    }

    /* Handle window close */
    if (event->type == XGUI_EVENT_WINDOW_CLOSE) {
        xgui_window_destroy(win);
        explorer_window = NULL;
        btn_up = NULL;
        btn_home = NULL;
        btn_refresh = NULL;
        return;
    }

    /* Calculate list area bounds (must match paint layout) */
    int list_y = TOOLBAR_HEIGHT + PATH_BAR_HEIGHT + ITEM_HEIGHT; /* after header */
    int list_h = win->client_height - TOOLBAR_HEIGHT - PATH_BAR_HEIGHT - 22 - ITEM_HEIGHT;
    int visible_items = list_h / ITEM_HEIGHT;
    if (visible_items > MAX_VISIBLE_ITEMS) visible_items = MAX_VISIBLE_ITEMS;

    /* Handle mouse move for context menu hover */
    if (event->type == XGUI_EVENT_MOUSE_MOVE && ctx_menu_visible) {
        int mx = event->mouse.x;
        int my = event->mouse.y;
        int cmx = ctx_menu_x;
        int cmy = ctx_menu_y;
        int cw = win->client_width;
        int ch = win->client_height;
        if (cmx + CTX_MENU_W > cw) cmx = cw - CTX_MENU_W;
        if (cmy + CTX_MENU_H > ch) cmy = ch - CTX_MENU_H;

        int old_hover = ctx_menu_hover;
        ctx_menu_hover = -1;
        if (mx >= cmx && mx < cmx + CTX_MENU_W && my >= cmy + 2) {
            int rel_y = my - cmy - 2;
            /* Items 0-2 are before separator, items 3-5 are after (+4px gap) */
            if (rel_y < CTX_MENU_SEP * CTX_MENU_ITEM_H) {
                int idx = rel_y / CTX_MENU_ITEM_H;
                if (idx >= 0 && idx < CTX_MENU_SEP) ctx_menu_hover = idx;
            } else {
                int adj_y = rel_y - 4; /* subtract separator gap */
                if (adj_y >= CTX_MENU_SEP * CTX_MENU_ITEM_H) {
                    int idx = adj_y / CTX_MENU_ITEM_H;
                    if (idx >= CTX_MENU_SEP && idx < CTX_MENU_ITEMS) ctx_menu_hover = idx;
                }
            }
        }
        if (ctx_menu_hover != old_hover) win->dirty = true;
        return;
    }

    /* Handle mouse clicks in list area */
    /* Note: WM already converts mouse coords to client-relative */
    if (event->type == XGUI_EVENT_MOUSE_DOWN) {
        int mx = event->mouse.x;
        int my = event->mouse.y;

        /* If context menu is visible, handle it first */
        if (ctx_menu_visible) {
            int cmx = ctx_menu_x;
            int cmy = ctx_menu_y;
            int cw2 = win->client_width;
            int ch2 = win->client_height;
            if (cmx + CTX_MENU_W > cw2) cmx = cw2 - CTX_MENU_W;
            if (cmy + CTX_MENU_H > ch2) cmy = ch2 - CTX_MENU_H;

            if (mx >= cmx && mx < cmx + CTX_MENU_W && my >= cmy + 2 &&
                event->mouse.button == XGUI_MOUSE_LEFT) {
                /* Determine which item was clicked (separator-aware) */
                int rel_y = my - cmy - 2;
                int item = -1;
                if (rel_y < CTX_MENU_SEP * CTX_MENU_ITEM_H) {
                    int idx = rel_y / CTX_MENU_ITEM_H;
                    if (idx >= 0 && idx < CTX_MENU_SEP) item = idx;
                } else {
                    int adj_y = rel_y - 4;
                    if (adj_y >= CTX_MENU_SEP * CTX_MENU_ITEM_H) {
                        int idx = adj_y / CTX_MENU_ITEM_H;
                        if (idx >= CTX_MENU_SEP && idx < CTX_MENU_ITEMS) item = idx;
                    }
                }
                if (item >= 0 && ctx_menu_index >= 0 && ctx_menu_index < entry_count) {
                    char path[VFS_MAX_PATH];
                    build_entry_path(ctx_menu_index, path, sizeof(path));
                    switch (item) {
                        case 0: /* Open with Text Editor */
                            xgui_gui_editor_open_file(path);
                            break;
                        case 1: /* Open with Spreadsheet */
                            xgui_gui_spreadsheet_open_file(path);
                            break;
                        case 2: /* Open with Paint */
                            xgui_paint_open_file(path);
                            break;
                        case 3: /* Cut */
                            strncpy(clipboard_path, path, VFS_MAX_PATH - 1);
                            clipboard_path[VFS_MAX_PATH - 1] = '\0';
                            clipboard_is_cut = 1;
                            clipboard_valid = 1;
                            break;
                        case 4: /* Copy */
                            strncpy(clipboard_path, path, VFS_MAX_PATH - 1);
                            clipboard_path[VFS_MAX_PATH - 1] = '\0';
                            clipboard_is_cut = 0;
                            clipboard_valid = 1;
                            break;
                        case 5: /* Paste */
                            if (clipboard_valid) {
                                if (explorer_copy_file(clipboard_path, current_path) == 0) {
                                    if (clipboard_is_cut) {
                                        explorer_delete_file(clipboard_path);
                                        clipboard_valid = 0;
                                    }
                                    load_directory(current_path);
                                }
                            }
                            break;
                    }
                }
            }
            /* Dismiss context menu on any click */
            ctx_menu_visible = 0;
            ctx_menu_hover = -1;
            win->dirty = true;
            return;
        }

        /* Scrollbar arrow button clicks */
        if (event->mouse.button == XGUI_MOUSE_LEFT) {
            int sb_x = win->client_width - 14;
            int sb_top = list_y;
            int sb_h = visible_items * ITEM_HEIGHT;
            int sb_btn = 14;
            int scroll_max = entry_count - visible_items;
            if (scroll_max < 0) scroll_max = 0;

            if (mx >= sb_x && mx < sb_x + 14) {
                /* Up arrow button */
                if (my >= sb_top && my < sb_top + sb_btn) {
                    if (scroll_offset > 0) scroll_offset--;
                    win->dirty = true;
                    return;
                }
                /* Down arrow button */
                if (my >= sb_top + sb_h - sb_btn && my < sb_top + sb_h) {
                    if (scroll_offset < scroll_max) scroll_offset++;
                    win->dirty = true;
                    return;
                }
            }
        }

        /* Right-click on a file: show context menu */
        if (event->mouse.button == XGUI_MOUSE_RIGHT) {
            if (mx >= 6 && mx < win->client_width - 20 &&
                my >= list_y && my < list_y + visible_items * ITEM_HEIGHT) {
                int clicked_item = (my - list_y) / ITEM_HEIGHT + scroll_offset;
                if (clicked_item >= 0 && clicked_item < entry_count &&
                    !entries[clicked_item].is_dir) {
                    selected_index = clicked_item;
                    ctx_menu_visible = 1;
                    ctx_menu_x = mx;
                    ctx_menu_y = my;
                    ctx_menu_index = clicked_item;
                    ctx_menu_hover = -1;
                    win->dirty = true;
                }
            }
            return;
        }

        /* Left-click: check if click is in list area */
        if (mx >= 6 && mx < win->client_width - 20 &&
            my >= list_y && my < list_y + visible_items * ITEM_HEIGHT) {
            int clicked_item = (my - list_y) / ITEM_HEIGHT + scroll_offset;
            if (clicked_item >= 0 && clicked_item < entry_count) {
                /* Double-click detection using timer ticks */
                static int last_clicked = -1;
                static uint32_t last_click_ticks = 0;

                uint32_t now = timer_get_ticks();

                if (clicked_item == last_clicked && (now - last_click_ticks) < 50) {
                    /* Double-click - open item */
                    handle_item_open(clicked_item);
                    last_clicked = -1;
                    last_click_ticks = 0;
                } else {
                    /* Single click - select item */
                    handle_item_click(clicked_item);
                    last_clicked = clicked_item;
                    last_click_ticks = now;
                }
            }
        }
    }

    /* Handle keyboard navigation */
    if (event->type == XGUI_EVENT_KEY_DOWN) {
        /* Dismiss context menu on any key */
        if (ctx_menu_visible) {
            ctx_menu_visible = 0;
            ctx_menu_hover = -1;
            win->dirty = true;
            return;
        }
        switch (event->key.keycode) {
            case 0x80: /* Up arrow */
                if (selected_index > 0) {
                    selected_index--;
                    if (selected_index < scroll_offset) {
                        scroll_offset = selected_index;
                    }
                    win->dirty = true;
                }
                break;

            case 0x81: /* Down arrow */
                if (selected_index < entry_count - 1) {
                    selected_index++;
                    if (selected_index >= scroll_offset + visible_items) {
                        scroll_offset = selected_index - visible_items + 1;
                    }
                    win->dirty = true;
                }
                break;

            case 0x96: /* Page Up */
                scroll_offset -= visible_items;
                if (scroll_offset < 0) scroll_offset = 0;
                if (selected_index > scroll_offset + visible_items - 1) {
                    selected_index = scroll_offset + visible_items - 1;
                }
                win->dirty = true;
                break;

            case 0x97: /* Page Down */
                scroll_offset += visible_items;
                if (scroll_offset > entry_count - visible_items) {
                    scroll_offset = entry_count - visible_items;
                    if (scroll_offset < 0) scroll_offset = 0;
                }
                if (selected_index < scroll_offset) {
                    selected_index = scroll_offset;
                }
                win->dirty = true;
                break;

            case 0x94: /* Home */
                selected_index = 0;
                scroll_offset = 0;
                win->dirty = true;
                break;

            case 0x95: /* End */
                selected_index = entry_count - 1;
                if (selected_index < 0) selected_index = 0;
                scroll_offset = entry_count - visible_items;
                if (scroll_offset < 0) scroll_offset = 0;
                win->dirty = true;
                break;
        }
    }

    /* Handle Enter key to open selected item */
    if (event->type == XGUI_EVENT_KEY_CHAR) {
        if (event->key.character == '\n' || event->key.character == '\r') {
            if (selected_index >= 0 && selected_index < entry_count) {
                handle_item_open(selected_index);
            }
        }
        /* Backspace goes up */
        if (event->key.character == '\b') {
            go_up();
        }
    }
}

/*
 * Create the File Explorer window
 */
void xgui_explorer_create(void) {
    if (explorer_window) {
        xgui_window_focus(explorer_window);
        return;
    }

    /* Create window */
    explorer_window = xgui_window_create("File Explorer", 80, 40,
                                          EXPLORER_WIDTH, EXPLORER_HEIGHT,
                                          XGUI_WINDOW_DEFAULT);
    if (!explorer_window) {
        return;
    }

    xgui_window_set_paint(explorer_window, explorer_paint);
    xgui_window_set_handler(explorer_window, explorer_handler);
    xgui_window_set_bgcolor(explorer_window, XGUI_LIGHT_GRAY);

    /* Create toolbar buttons */
    btn_up = xgui_button_create(explorer_window, 6, 4, 40, 24, "Up");
    xgui_widget_set_onclick(btn_up, btn_up_click);

    btn_home = xgui_button_create(explorer_window, 50, 4, 50, 24, "Home");
    xgui_widget_set_onclick(btn_home, btn_home_click);

    btn_refresh = xgui_button_create(explorer_window, 104, 4, 64, 24, "Refresh");
    xgui_widget_set_onclick(btn_refresh, btn_refresh_click);

    /* Load initial directory */
    load_directory("/");
}
