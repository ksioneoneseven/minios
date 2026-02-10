/*
 * MiniOS XGUI Paint App — MSPaint-inspired
 *
 * Tools: Pencil, Brush, Eraser, Color Picker, Flood Fill,
 *        Line, Rectangle, Filled Rectangle, Ellipse, Filled Ellipse.
 * 28-color palette with FG/BG colors. BMP file support.
 */

#include "xgui/xgui.h"
#include "xgui/wm.h"
#include "xgui/widget.h"
#include "xgui/theme.h"
#include "string.h"
#include "keyboard.h"
#include "stdio.h"
#include "vfs.h"
#include "ramfs.h"
#include "ext2.h"
#include "shell.h"
#include "heap.h"
#include "serial.h"

/* ---- Layout constants ---- */
#define CANVAS_W      400
#define CANVAS_H      280

#define TOOLBAR_H     26       /* File buttons bar at top */
#define TOOLBOX_W     56       /* Tool palette sidebar */
#define COLORBAR_H    36       /* Color palette at bottom */
#define STATUSBAR_H   18       /* Status bar */

#define CANVAS_X      (TOOLBOX_W + 2)                   /* 58 */
#define CANVAS_Y      (TOOLBAR_H + 2)                   /* 28 */
#define COLORBAR_Y    (CANVAS_Y + CANVAS_H + 4)         /* 312 */
#define STATUS_Y      (COLORBAR_Y + COLORBAR_H)         /* 348 */
#define WIN_CW        (CANVAS_X + CANVAS_W + 8 + 2 * XGUI_BORDER_WIDTH)
#define WIN_CH        (STATUS_Y + STATUSBAR_H + XGUI_TITLE_HEIGHT + XGUI_BORDER_WIDTH)

/* Tool button grid */
#define TOOL_BTN      24       /* tool button size */
#define TOOL_GAP      2
#define TOOL_PAD      3        /* left/top padding in toolbox */
#define TOOL_COLS     2
#define TOOL_ROWS     5

/* Color palette swatch */
#define PAL_SZ        14
#define PAL_GAP       1
#define PAL_COLS      14
#define PAL_ROWS      2
#define PAL_X_START   38

/* ---- Tools ---- */
typedef enum {
    TOOL_PENCIL, TOOL_BRUSH, TOOL_ERASER, TOOL_PICKER,
    TOOL_FILL,   TOOL_LINE,  TOOL_RECT,   TOOL_FILLED_RECT,
    TOOL_ELLIPSE, TOOL_FILLED_ELLIPSE,
    TOOL_COUNT
} paint_tool_t;

static const char* tool_names[TOOL_COUNT] = {
    "Pencil", "Brush", "Eraser", "Color Picker",
    "Fill", "Line", "Rectangle", "Filled Rect",
    "Ellipse", "Filled Ellipse"
};

/* ---- 28-color palette (MSPaint-style, 2 rows of 14) ---- */
static const uint32_t palette[] = {
    /* Row 1: darker shades */
    0xFF000000, 0xFF808080, 0xFF800000, 0xFF808000,
    0xFF008000, 0xFF008080, 0xFF000080, 0xFF800080,
    0xFF808040, 0xFF004040, 0xFF0080FF, 0xFF004080,
    0xFF4000FF, 0xFF804000,
    /* Row 2: brighter shades */
    0xFFFFFFFF, 0xFFC0C0C0, 0xFFFF0000, 0xFFFFFF00,
    0xFF00FF00, 0xFF00FFFF, 0xFF0000FF, 0xFFFF00FF,
    0xFFFFFF80, 0xFF00FF80, 0xFF80FFFF, 0xFF8080FF,
    0xFFFF0080, 0xFFFF8040
};
#define PALETTE_SIZE 28

/* ---- State ---- */
static xgui_window_t* paint_window = NULL;
static uint32_t canvas[CANVAS_W * CANVAS_H];
static uint32_t fg_color = 0xFF000000;   /* black */
static uint32_t bg_color = 0xFFFFFFFF;   /* white */
static paint_tool_t current_tool = TOOL_PENCIL;
static int brush_size = 3;
static bool drawing = false;
static int last_x = -1, last_y = -1;
static uint32_t draw_color;   /* Active color during current drag */

/* Shape tool drag state */
static bool shape_dragging = false;
static int shape_x0, shape_y0, shape_x1, shape_y1;

/* File I/O state */
#define PT_FNAME_MAX 64
static char pt_filename[PT_FNAME_MAX];
static int  pt_fname_mode = 0;       /* 0=none, 1=open, 2=save-as */
static char pt_fname_buf[PT_FNAME_MAX];
static int  pt_fname_pos = 0;
static char pt_status_msg[64];
static int  pt_status_ticks = 0;

/* Toolbar widgets */
static xgui_widget_t* pt_btn_new    = NULL;
static xgui_widget_t* pt_btn_open   = NULL;
static xgui_widget_t* pt_btn_save   = NULL;
static xgui_widget_t* pt_btn_saveas = NULL;

/* ---- Utility helpers ---- */
static inline int pt_abs(int x) { return x < 0 ? -x : x; }
static inline int pt_min(int a, int b) { return a < b ? a : b; }
static inline int pt_max(int a, int b) { return a > b ? a : b; }

static int isqrt32(uint32_t n) {
    if (n == 0) return 0;
    uint32_t lo = 1, hi = 0xFFFF;
    if (hi > n) hi = n;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo + 1) / 2;
        if (mid <= n / mid) lo = mid;
        else hi = mid - 1;
    }
    return (int)lo;
}

/* ---- Canvas primitives ---- */

static inline void canvas_set(int x, int y, uint32_t c) {
    if (x >= 0 && x < CANVAS_W && y >= 0 && y < CANVAS_H)
        canvas[y * CANVAS_W + x] = c;
}

static inline uint32_t canvas_get(int x, int y) {
    if (x >= 0 && x < CANVAS_W && y >= 0 && y < CANVAS_H)
        return canvas[y * CANVAS_W + x];
    return 0;
}

static void clear_canvas(void) {
    for (int i = 0; i < CANVAS_W * CANVAS_H; i++)
        canvas[i] = 0xFFFFFFFF;
}

/* ---- Canvas drawing algorithms ---- */

/* Filled circle brush */
static void canvas_brush(int cx, int cy, int r, uint32_t color) {
    for (int y = -r; y <= r; y++)
        for (int x = -r; x <= r; x++)
            if (x * x + y * y <= r * r)
                canvas_set(cx + x, cy + y, color);
}

/* Bresenham line with brush at each point (thick freehand) */
static void canvas_brush_line(int x0, int y0, int x1, int y1,
                              int r, uint32_t color) {
    int dx = pt_abs(x1 - x0), dy = pt_abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    while (1) {
        canvas_brush(x0, y0, r, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx)  { err += dx; y0 += sy; }
    }
}

/* 1-pixel Bresenham line */
static void canvas_line(int x0, int y0, int x1, int y1, uint32_t color) {
    int dx = pt_abs(x1 - x0), dy = pt_abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    while (1) {
        canvas_set(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx)  { err += dx; y0 += sy; }
    }
}

/* Rectangle outline on canvas */
static void canvas_rect(int x, int y, int w, int h, uint32_t color) {
    if (w <= 0 || h <= 0) return;
    for (int i = 0; i < w; i++) {
        canvas_set(x + i, y, color);
        canvas_set(x + i, y + h - 1, color);
    }
    for (int i = 1; i < h - 1; i++) {
        canvas_set(x, y + i, color);
        canvas_set(x + w - 1, y + i, color);
    }
}

/* Filled rectangle on canvas */
static void canvas_rect_fill(int x, int y, int w, int h, uint32_t color) {
    if (w <= 0 || h <= 0) return;
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            canvas_set(x + i, y + j, color);
}

/* Ellipse outline — scan both axes for gap-free rendering */
static void canvas_ellipse(int cx, int cy, int rx, int ry, uint32_t color) {
    if (rx <= 0 || ry <= 0) return;
    uint32_t rx2 = (uint32_t)(rx * rx), ry2 = (uint32_t)(ry * ry);
    /* Scan Y axis: draw leftmost/rightmost pixel per row */
    for (int y = -ry; y <= ry; y++) {
        uint32_t x2 = rx2 * (ry2 - (uint32_t)(y * y)) / ry2;
        int xm = isqrt32(x2);
        canvas_set(cx + xm, cy + y, color);
        canvas_set(cx - xm, cy + y, color);
    }
    /* Scan X axis: draw topmost/bottommost pixel per column */
    for (int x = -rx; x <= rx; x++) {
        uint32_t y2 = ry2 * (rx2 - (uint32_t)(x * x)) / rx2;
        int ym = isqrt32(y2);
        canvas_set(cx + x, cy + ym, color);
        canvas_set(cx + x, cy - ym, color);
    }
}

/* Filled ellipse on canvas */
static void canvas_ellipse_fill(int cx, int cy, int rx, int ry, uint32_t color) {
    if (rx <= 0 || ry <= 0) return;
    uint32_t rx2 = (uint32_t)(rx * rx), ry2 = (uint32_t)(ry * ry);
    for (int y = -ry; y <= ry; y++) {
        uint32_t x2 = rx2 * (ry2 - (uint32_t)(y * y)) / ry2;
        int xm = isqrt32(x2);
        for (int x = -xm; x <= xm; x++)
            canvas_set(cx + x, cy + y, color);
    }
}

/* Scanline flood fill */
static void canvas_flood_fill(int sx, int sy, uint32_t fill) {
    if (sx < 0 || sx >= CANVAS_W || sy < 0 || sy >= CANVAS_H) return;
    uint32_t target = canvas_get(sx, sy);
    if (target == fill) return;

    #define FILL_MAX 8192
    int16_t* stk_x = (int16_t*)kmalloc(FILL_MAX * sizeof(int16_t));
    int16_t* stk_y = (int16_t*)kmalloc(FILL_MAX * sizeof(int16_t));
    if (!stk_x || !stk_y) { kfree(stk_x); kfree(stk_y); return; }

    int sp = 0;
    stk_x[sp] = (int16_t)sx;
    stk_y[sp] = (int16_t)sy;
    sp++;

    while (sp > 0) {
        sp--;
        int x = stk_x[sp], y = stk_y[sp];
        if (canvas_get(x, y) != target) continue;

        /* Find left edge of this scanline */
        int left = x;
        while (left > 0 && canvas_get(left - 1, y) == target) left--;

        /* Fill rightward, seeding rows above and below */
        int right = left;
        bool a_add = false, b_add = false;
        while (right < CANVAS_W && canvas_get(right, y) == target) {
            canvas_set(right, y, fill);
            if (y > 0) {
                if (canvas_get(right, y - 1) == target) {
                    if (!a_add && sp < FILL_MAX) {
                        stk_x[sp] = (int16_t)right;
                        stk_y[sp] = (int16_t)(y - 1);
                        sp++;
                        a_add = true;
                    }
                } else {
                    a_add = false;
                }
            }
            if (y < CANVAS_H - 1) {
                if (canvas_get(right, y + 1) == target) {
                    if (!b_add && sp < FILL_MAX) {
                        stk_x[sp] = (int16_t)right;
                        stk_y[sp] = (int16_t)(y + 1);
                        sp++;
                        b_add = true;
                    }
                } else {
                    b_add = false;
                }
            }
            right++;
        }
    }
    kfree(stk_x);
    kfree(stk_y);
}

/* Commit a finished shape from drag coordinates onto canvas */
static void commit_shape(paint_tool_t tool, int x0, int y0, int x1, int y1,
                         uint32_t color) {
    switch (tool) {
    case TOOL_LINE:
        canvas_line(x0, y0, x1, y1, color);
        break;
    case TOOL_RECT: {
        int rx = pt_min(x0, x1), ry = pt_min(y0, y1);
        int rw = pt_abs(x1 - x0) + 1, rh = pt_abs(y1 - y0) + 1;
        canvas_rect(rx, ry, rw, rh, color);
        break;
    }
    case TOOL_FILLED_RECT: {
        int rx = pt_min(x0, x1), ry = pt_min(y0, y1);
        int rw = pt_abs(x1 - x0) + 1, rh = pt_abs(y1 - y0) + 1;
        canvas_rect_fill(rx, ry, rw, rh, color);
        break;
    }
    case TOOL_ELLIPSE: {
        int cx = (x0 + x1) / 2, cy = (y0 + y1) / 2;
        int erx = pt_abs(x1 - x0) / 2, ery = pt_abs(y1 - y0) / 2;
        canvas_ellipse(cx, cy, erx, ery, color);
        break;
    }
    case TOOL_FILLED_ELLIPSE: {
        int cx = (x0 + x1) / 2, cy = (y0 + y1) / 2;
        int erx = pt_abs(x1 - x0) / 2, ery = pt_abs(y1 - y0) / 2;
        canvas_ellipse_fill(cx, cy, erx, ery, color);
        break;
    }
    default:
        break;
    }
}

/* ---- BMP file helpers ---- */

static void put_le16(uint8_t* p, uint16_t v) {
    p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF;
}
static void put_le32(uint8_t* p, uint32_t v) {
    p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF;
    p[2] = (v >> 16) & 0xFF; p[3] = (v >> 24) & 0xFF;
}
static uint16_t get_le16(const uint8_t* p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}
static uint32_t get_le32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static int32_t get_le32s(const uint8_t* p) {
    return (int32_t)get_le32(p);
}

/* Find parent directory and filename from a resolved path */
static vfs_node_t* pt_find_parent(const char* resolved, const char** out_fname) {
    const char* last_slash = NULL;
    for (const char* p = resolved; *p; p++)
        if (*p == '/') last_slash = p;
    if (!last_slash || last_slash == resolved) {
        *out_fname = (last_slash == resolved) ? resolved + 1 : resolved;
        return vfs_lookup("/");
    }
    static char ppath[256];
    int plen = (int)(last_slash - resolved);
    if (plen >= 256) plen = 255;
    memcpy(ppath, resolved, plen);
    ppath[plen] = '\0';
    *out_fname = last_slash + 1;
    return vfs_lookup(ppath);
}

/* Save canvas as 24-bit BMP */
static int pt_save_bmp(const char* path) {
    char resolved[256];
    shell_resolve_path(path, resolved, sizeof(resolved));

    serial_write_string("PAINT: save_bmp path='");
    serial_write_string(resolved);
    serial_write_string("'\n");

    int row_bytes = CANVAS_W * 3;
    int pad = (4 - (row_bytes % 4)) % 4;
    int padded_row = row_bytes + pad;
    uint32_t pixel_size = (uint32_t)padded_row * CANVAS_H;
    uint32_t file_size = 54 + pixel_size;

    uint8_t* buf = (uint8_t*)kmalloc(file_size);
    if (!buf) {
        serial_write_string("PAINT: kmalloc failed\n");
        strncpy(pt_status_msg, "Out of memory", sizeof(pt_status_msg));
        pt_status_ticks = 100;
        return -1;
    }
    memset(buf, 0, file_size);

    buf[0] = 'B'; buf[1] = 'M';
    put_le32(buf + 2, file_size);
    put_le32(buf + 10, 54);
    put_le32(buf + 14, 40);
    put_le32(buf + 18, (uint32_t)CANVAS_W);
    put_le32(buf + 22, (uint32_t)CANVAS_H);
    put_le16(buf + 26, 1);
    put_le16(buf + 28, 24);
    put_le32(buf + 34, pixel_size);

    /* Pixel data: bottom-up, BGR */
    uint8_t* pix = buf + 54;
    for (int y = CANVAS_H - 1; y >= 0; y--) {
        uint8_t* row = pix + (CANVAS_H - 1 - y) * padded_row;
        for (int x = 0; x < CANVAS_W; x++) {
            uint32_t c = canvas[y * CANVAS_W + x];
            row[x * 3 + 0] = c & 0xFF;
            row[x * 3 + 1] = (c >> 8) & 0xFF;
            row[x * 3 + 2] = (c >> 16) & 0xFF;
        }
    }

    vfs_node_t* node = vfs_lookup(resolved);
    if (!node) {
        serial_write_string("PAINT: file not found, creating...\n");
        const char* fname;
        vfs_node_t* parent = pt_find_parent(resolved, &fname);
        if (!parent) {
            serial_write_string("PAINT: parent dir not found\n");
            kfree(buf);
            strncpy(pt_status_msg, "Dir not found", sizeof(pt_status_msg));
            pt_status_ticks = 100;
            return -1;
        }
        serial_write_string("PAINT: parent='\n");
        serial_write_string(parent->name);
        serial_write_string("' readdir=");
        serial_write_hex((uint32_t)parent->readdir);
        serial_write_string(" ext2_vfs_readdir=");
        serial_write_hex((uint32_t)ext2_vfs_readdir);
        serial_write_string("\n");
        if (parent->readdir == ext2_vfs_readdir)
            node = ext2_create_file(parent, fname);
        else
            node = ramfs_create_file_in(parent, fname, VFS_FILE);
        if (!node) {
            serial_write_string("PAINT: create file failed\n");
            kfree(buf);
            strncpy(pt_status_msg, "Create failed", sizeof(pt_status_msg));
            pt_status_ticks = 100;
            return -1;
        }
        serial_write_string("PAINT: file created, write=");
        serial_write_hex((uint32_t)node->write);
        serial_write_string("\n");
    } else {
        serial_write_string("PAINT: file exists, write=");
        serial_write_hex((uint32_t)node->write);
        serial_write_string("\n");
    }

    int32_t written = vfs_write(node, 0, file_size, buf);
    serial_write_string("PAINT: vfs_write returned ");
    serial_write_hex((uint32_t)written);
    serial_write_string("\n");
    kfree(buf);
    if (written < 0) {
        strncpy(pt_status_msg, "Write failed", sizeof(pt_status_msg));
        pt_status_ticks = 100;
        return -1;
    }

    strncpy(pt_filename, resolved, PT_FNAME_MAX - 1);
    pt_filename[PT_FNAME_MAX - 1] = '\0';
    snprintf(pt_status_msg, sizeof(pt_status_msg), "Saved %u bytes",
             (unsigned)file_size);
    pt_status_ticks = 100;
    return 0;
}

/* Load a 24-bit BMP file into the canvas */
static int pt_load_bmp(const char* path) {
    char resolved[256];
    shell_resolve_path(path, resolved, sizeof(resolved));

    vfs_node_t* node = vfs_lookup(resolved);
    if (!node) {
        strncpy(pt_status_msg, "File not found", sizeof(pt_status_msg));
        pt_status_ticks = 100;
        return -1;
    }
    if (node->length < 54) {
        strncpy(pt_status_msg, "Not a BMP file", sizeof(pt_status_msg));
        pt_status_ticks = 100;
        return -1;
    }

    uint8_t* buf = (uint8_t*)kmalloc(node->length);
    if (!buf) {
        strncpy(pt_status_msg, "Out of memory", sizeof(pt_status_msg));
        pt_status_ticks = 100;
        return -1;
    }

    int32_t rd = vfs_read(node, 0, node->length, buf);
    if (rd < 54) {
        kfree(buf);
        strncpy(pt_status_msg, "Read failed", sizeof(pt_status_msg));
        pt_status_ticks = 100;
        return -1;
    }
    if (buf[0] != 'B' || buf[1] != 'M') {
        kfree(buf);
        strncpy(pt_status_msg, "Not a BMP file", sizeof(pt_status_msg));
        pt_status_ticks = 100;
        return -1;
    }

    uint16_t bpp = get_le16(buf + 28);
    uint32_t comp = get_le32(buf + 30);
    if (bpp != 24 || comp != 0) {
        kfree(buf);
        strncpy(pt_status_msg, "Only 24-bit BMP", sizeof(pt_status_msg));
        pt_status_ticks = 100;
        return -1;
    }

    int32_t bmp_w = get_le32s(buf + 18);
    int32_t bmp_h_raw = get_le32s(buf + 22);
    int bmp_h = bmp_h_raw < 0 ? -bmp_h_raw : bmp_h_raw;
    int bottom_up = bmp_h_raw > 0;
    int row_bytes = bmp_w * 3;
    int pad = (4 - (row_bytes % 4)) % 4;
    int padded_row = row_bytes + pad;
    uint32_t pix_offset = get_le32(buf + 10);
    uint8_t* pix = buf + pix_offset;

    clear_canvas();
    int cw = bmp_w < CANVAS_W ? bmp_w : CANVAS_W;
    int ch = bmp_h < CANVAS_H ? bmp_h : CANVAS_H;
    for (int y = 0; y < ch; y++) {
        int src_row = bottom_up ? (bmp_h - 1 - y) : y;
        uint8_t* row = pix + src_row * padded_row;
        for (int x = 0; x < cw; x++) {
            uint8_t b = row[x * 3 + 0];
            uint8_t g = row[x * 3 + 1];
            uint8_t r = row[x * 3 + 2];
            canvas[y * CANVAS_W + x] = XGUI_RGB(r, g, b);
        }
    }

    kfree(buf);
    strncpy(pt_filename, resolved, PT_FNAME_MAX - 1);
    pt_filename[PT_FNAME_MAX - 1] = '\0';
    snprintf(pt_status_msg, sizeof(pt_status_msg), "Loaded %dx%d",
             (int)bmp_w, bmp_h);
    pt_status_ticks = 100;
    return 0;
}

/* ---- Toolbar callbacks ---- */

static void pt_on_new(xgui_widget_t* w) {
    (void)w;
    clear_canvas();
    pt_filename[0] = '\0';
    if (paint_window) paint_window->dirty = true;
}

static void pt_on_open(xgui_widget_t* w) {
    (void)w;
    pt_fname_mode = 1;
    pt_fname_buf[0] = '\0';
    pt_fname_pos = 0;
    if (paint_window) paint_window->dirty = true;
}

static void pt_on_save(xgui_widget_t* w) {
    (void)w;
    if (pt_filename[0])
        pt_save_bmp(pt_filename);
    else {
        pt_fname_mode = 2;
        pt_fname_buf[0] = '\0';
        pt_fname_pos = 0;
    }
    if (paint_window) paint_window->dirty = true;
}

static void pt_on_save_as(xgui_widget_t* w) {
    (void)w;
    pt_fname_mode = 2;
    pt_fname_buf[0] = '\0';
    pt_fname_pos = 0;
    if (paint_window) paint_window->dirty = true;
}

/* ---- Small circle helpers for tool icons ---- */

static void icon_circle(xgui_window_t* win, int cx, int cy, int r,
                         uint32_t color) {
    int x = 0, y = r, d = 3 - 2 * r;
    while (x <= y) {
        xgui_win_pixel(win, cx + x, cy + y, color);
        xgui_win_pixel(win, cx - x, cy + y, color);
        xgui_win_pixel(win, cx + x, cy - y, color);
        xgui_win_pixel(win, cx - x, cy - y, color);
        xgui_win_pixel(win, cx + y, cy + x, color);
        xgui_win_pixel(win, cx - y, cy + x, color);
        xgui_win_pixel(win, cx + y, cy - x, color);
        xgui_win_pixel(win, cx - y, cy - x, color);
        if (d < 0) d += 4 * x + 6;
        else { d += 4 * (x - y) + 10; y--; }
        x++;
    }
}

static void icon_filled_circle(xgui_window_t* win, int cx, int cy, int r,
                                uint32_t color) {
    for (int dy = -r; dy <= r; dy++)
        for (int dx = -r; dx <= r; dx++)
            if (dx * dx + dy * dy <= r * r)
                xgui_win_pixel(win, cx + dx, cy + dy, color);
}

/* ---- Draw tool icon inside a 24x24 button ---- */

static void draw_tool_icon(xgui_window_t* win, int x, int y, int tool) {
    switch (tool) {
    case TOOL_PENCIL:
        /* Diagonal thin line with colored tip */
        xgui_win_line(win, x + 17, y + 5, x + 5, y + 17, XGUI_BLACK);
        xgui_win_pixel(win, x + 4, y + 18, XGUI_RGB(180, 0, 0));
        break;
    case TOOL_BRUSH:
        /* Thick brush stroke with handle */
        for (int i = -1; i <= 1; i++)
            xgui_win_line(win, x + 5, y + 18 + i, x + 13, y + 10 + i,
                          XGUI_BLACK);
        xgui_win_rect_filled(win, x + 13, y + 6, 5, 5,
                             XGUI_RGB(160, 120, 60));
        break;
    case TOOL_ERASER:
        /* Pink eraser block */
        xgui_win_rect_filled(win, x + 5, y + 7, 14, 10,
                             XGUI_RGB(255, 200, 200));
        xgui_win_rect(win, x + 5, y + 7, 14, 10, XGUI_BLACK);
        break;
    case TOOL_PICKER:
        /* Eyedropper */
        xgui_win_line(win, x + 8, y + 18, x + 15, y + 11, XGUI_BLACK);
        xgui_win_line(win, x + 9, y + 18, x + 16, y + 11, XGUI_BLACK);
        xgui_win_rect_filled(win, x + 15, y + 5, 4, 7, XGUI_DARK_GRAY);
        break;
    case TOOL_FILL:
        /* Paint bucket */
        xgui_win_rect(win, x + 8, y + 6, 9, 10, XGUI_BLACK);
        xgui_win_line(win, x + 8, y + 8, x + 5, y + 11, XGUI_BLACK);
        xgui_win_pixel(win, x + 6, y + 14, XGUI_BLUE);
        xgui_win_pixel(win, x + 7, y + 16, XGUI_BLUE);
        break;
    case TOOL_LINE:
        /* Diagonal line */
        xgui_win_line(win, x + 4, y + 19, x + 19, y + 4, XGUI_BLACK);
        break;
    case TOOL_RECT:
        /* Rectangle outline */
        xgui_win_rect(win, x + 4, y + 6, 16, 12, XGUI_BLUE);
        break;
    case TOOL_FILLED_RECT:
        /* Filled rectangle */
        xgui_win_rect_filled(win, x + 4, y + 6, 16, 12, XGUI_BLUE);
        xgui_win_rect(win, x + 4, y + 6, 16, 12, XGUI_BLACK);
        break;
    case TOOL_ELLIPSE:
        /* Circle outline */
        icon_circle(win, x + 12, y + 12, 7, XGUI_BLUE);
        break;
    case TOOL_FILLED_ELLIPSE:
        /* Filled circle */
        icon_filled_circle(win, x + 12, y + 12, 7, XGUI_BLUE);
        icon_circle(win, x + 12, y + 12, 7, XGUI_BLACK);
        break;
    }
}

/* ---- Shape preview (drawn on window buffer, not canvas) ---- */

static void draw_shape_preview(xgui_window_t* win, paint_tool_t tool,
                                int x0, int y0, int x1, int y1,
                                uint32_t color) {
    int ox = CANVAS_X, oy = CANVAS_Y;

    switch (tool) {
    case TOOL_LINE:
        xgui_win_line(win, ox + x0, oy + y0, ox + x1, oy + y1, color);
        break;
    case TOOL_RECT: {
        int rx = pt_min(x0, x1), ry = pt_min(y0, y1);
        int rw = pt_abs(x1 - x0) + 1, rh = pt_abs(y1 - y0) + 1;
        xgui_win_rect(win, ox + rx, oy + ry, rw, rh, color);
        break;
    }
    case TOOL_FILLED_RECT: {
        int rx = pt_min(x0, x1), ry = pt_min(y0, y1);
        int rw = pt_abs(x1 - x0) + 1, rh = pt_abs(y1 - y0) + 1;
        xgui_win_rect_filled(win, ox + rx, oy + ry, rw, rh, color);
        break;
    }
    case TOOL_ELLIPSE:
    case TOOL_FILLED_ELLIPSE: {
        int cx = (x0 + x1) / 2, cy = (y0 + y1) / 2;
        int erx = pt_abs(x1 - x0) / 2, ery = pt_abs(y1 - y0) / 2;
        if (erx <= 0 || ery <= 0) break;
        uint32_t rx2 = (uint32_t)(erx * erx);
        uint32_t ry2 = (uint32_t)(ery * ery);
        if (tool == TOOL_FILLED_ELLIPSE) {
            /* Filled preview */
            for (int y = -ery; y <= ery; y++) {
                uint32_t xv = rx2 * (ry2 - (uint32_t)(y * y)) / ry2;
                int xm = isqrt32(xv);
                for (int x = -xm; x <= xm; x++)
                    xgui_win_pixel(win, ox + cx + x, oy + cy + y, color);
            }
        } else {
            /* Outline preview: scan both axes */
            for (int y = -ery; y <= ery; y++) {
                uint32_t xv = rx2 * (ry2 - (uint32_t)(y * y)) / ry2;
                int xm = isqrt32(xv);
                xgui_win_pixel(win, ox + cx + xm, oy + cy + y, color);
                xgui_win_pixel(win, ox + cx - xm, oy + cy + y, color);
            }
            for (int x = -erx; x <= erx; x++) {
                uint32_t yv = ry2 * (rx2 - (uint32_t)(x * x)) / rx2;
                int ym = isqrt32(yv);
                xgui_win_pixel(win, ox + cx + x, oy + cy + ym, color);
                xgui_win_pixel(win, ox + cx + x, oy + cy - ym, color);
            }
        }
        break;
    }
    default:
        break;
    }
}

/* ---- Paint callback ---- */

static void paint_paint(xgui_window_t* win) {
    int cw = win->client_width;
    int ch = win->client_height;

    /* --- Toolbar --- */
    xgui_win_rect_filled(win, 0, 0, cw, TOOLBAR_H, XGUI_RGB(235, 235, 235));
    xgui_win_hline(win, 0, TOOLBAR_H - 1, cw, XGUI_DARK_GRAY);
    xgui_widgets_draw(win);

    /* --- Toolbox sidebar --- */
    xgui_win_rect_filled(win, 0, TOOLBAR_H, TOOLBOX_W,
                         ch - TOOLBAR_H, XGUI_RGB(215, 215, 215));
    xgui_win_vline(win, TOOLBOX_W - 1, TOOLBAR_H,
                   ch - TOOLBAR_H, XGUI_DARK_GRAY);

    /* Tool buttons */
    for (int t = 0; t < TOOL_COUNT; t++) {
        int col = t % TOOL_COLS, row = t / TOOL_COLS;
        int bx = TOOL_PAD + col * (TOOL_BTN + TOOL_GAP);
        int by = TOOLBAR_H + TOOL_PAD + row * (TOOL_BTN + TOOL_GAP);
        if (t == (int)current_tool) {
            xgui_win_rect_filled(win, bx, by, TOOL_BTN, TOOL_BTN,
                                 XGUI_RGB(180, 200, 220));
            xgui_win_rect_3d_sunken(win, bx, by, TOOL_BTN, TOOL_BTN);
        } else {
            xgui_win_rect_filled(win, bx, by, TOOL_BTN, TOOL_BTN,
                                 XGUI_BUTTON_BG);
            xgui_win_rect_3d_raised(win, bx, by, TOOL_BTN, TOOL_BTN);
        }
        draw_tool_icon(win, bx, by, t);
    }

    /* Brush size controls (below tool buttons) */
    int sz_y = TOOLBAR_H + TOOL_PAD + TOOL_ROWS * (TOOL_BTN + TOOL_GAP) + 8;
    xgui_win_text_transparent(win, 6, sz_y, "Size", XGUI_BLACK);
    char sz_buf[8];
    snprintf(sz_buf, sizeof(sz_buf), " %d", brush_size);
    xgui_win_text_transparent(win, 6, sz_y + 16, sz_buf, XGUI_BLACK);
    /* + button */
    xgui_win_rect_filled(win, 4, sz_y + 34, 22, 16, XGUI_BUTTON_BG);
    xgui_win_rect_3d_raised(win, 4, sz_y + 34, 22, 16);
    xgui_win_text_transparent(win, 11, sz_y + 34, "+", XGUI_BLACK);
    /* - button */
    xgui_win_rect_filled(win, 30, sz_y + 34, 22, 16, XGUI_BUTTON_BG);
    xgui_win_rect_3d_raised(win, 30, sz_y + 34, 22, 16);
    xgui_win_text_transparent(win, 37, sz_y + 34, "-", XGUI_BLACK);

    /* --- Canvas --- */
    xgui_win_rect_3d_sunken(win, CANVAS_X - 2, CANVAS_Y - 2,
                            CANVAS_W + 4, CANVAS_H + 4);
    for (int y = 0; y < CANVAS_H; y++)
        for (int x = 0; x < CANVAS_W; x++)
            xgui_win_pixel(win, CANVAS_X + x, CANVAS_Y + y,
                           canvas[y * CANVAS_W + x]);

    /* Shape preview overlay */
    if (shape_dragging)
        draw_shape_preview(win, current_tool, shape_x0, shape_y0,
                           shape_x1, shape_y1, draw_color);

    /* --- Color bar --- */
    xgui_win_rect_filled(win, 0, COLORBAR_Y, cw, COLORBAR_H,
                         XGUI_RGB(215, 215, 215));
    xgui_win_hline(win, 0, COLORBAR_Y, cw, XGUI_DARK_GRAY);

    /* FG/BG color indicator (overlapping squares, MSPaint style) */
    int ind_x = 6, ind_y = COLORBAR_Y + 5;
    /* BG square (behind, offset right/down) */
    xgui_win_rect_filled(win, ind_x + 9, ind_y + 9, 16, 16, bg_color);
    xgui_win_rect(win, ind_x + 9, ind_y + 9, 16, 16, XGUI_BLACK);
    /* FG square (front, top-left) */
    xgui_win_rect_filled(win, ind_x, ind_y, 16, 16, fg_color);
    xgui_win_rect(win, ind_x, ind_y, 16, 16, XGUI_BLACK);

    /* Palette grid (2 rows of 14) */
    for (int r = 0; r < PAL_ROWS; r++) {
        for (int c = 0; c < PAL_COLS; c++) {
            int idx = r * PAL_COLS + c;
            int px = PAL_X_START + c * (PAL_SZ + PAL_GAP);
            int py = COLORBAR_Y + 4 + r * (PAL_SZ + PAL_GAP);
            xgui_win_rect_filled(win, px, py, PAL_SZ, PAL_SZ, palette[idx]);
            if (palette[idx] == fg_color)
                xgui_win_rect(win, px - 1, py - 1, PAL_SZ + 2, PAL_SZ + 2,
                              XGUI_RED);
            else
                xgui_win_rect(win, px, py, PAL_SZ, PAL_SZ, XGUI_DARK_GRAY);
        }
    }

    /* --- Status bar --- */
    xgui_win_rect_filled(win, 0, STATUS_Y, cw, STATUSBAR_H,
                         XGUI_RGB(215, 215, 215));
    xgui_win_hline(win, 0, STATUS_Y, cw, XGUI_DARK_GRAY);

    if (pt_fname_mode) {
        const char* prompt = pt_fname_mode == 1 ? "Open: " : "Save As: ";
        char line[128];
        snprintf(line, sizeof(line), "%s%s_", prompt, pt_fname_buf);
        xgui_win_text(win, 4, STATUS_Y + 2, line, XGUI_BLACK,
                      XGUI_RGB(215, 215, 215));
    } else if (pt_status_ticks > 0) {
        xgui_win_text(win, 4, STATUS_Y + 2, pt_status_msg, XGUI_BLACK,
                      XGUI_RGB(215, 215, 215));
        pt_status_ticks--;
    } else {
        char info[80];
        if (pt_filename[0])
            snprintf(info, sizeof(info), "%s | %s",
                     tool_names[current_tool], pt_filename);
        else
            snprintf(info, sizeof(info), "%s | Untitled",
                     tool_names[current_tool]);
        xgui_win_text(win, 4, STATUS_Y + 2, info, XGUI_BLACK,
                      XGUI_RGB(215, 215, 215));
    }
}

/* ---- Event handler ---- */

static void paint_handler(xgui_window_t* win, xgui_event_t* event) {
    /* Widgets first */
    if (xgui_widgets_handle_event(win, event)) {
        win->dirty = true;
        return;
    }

    if (event->type == XGUI_EVENT_WINDOW_CLOSE) {
        xgui_window_destroy(win);
        paint_window = NULL;
        pt_btn_new = pt_btn_open = pt_btn_save = pt_btn_saveas = NULL;
        return;
    }

    /* Filename input mode (intercepts all keyboard) */
    if (pt_fname_mode) {
        if (event->type == XGUI_EVENT_KEY_CHAR) {
            char c = event->key.character;
            if (c >= 32 && c < 127 && pt_fname_pos < PT_FNAME_MAX - 1) {
                pt_fname_buf[pt_fname_pos++] = c;
                pt_fname_buf[pt_fname_pos] = '\0';
            }
            win->dirty = true;
            return;
        }
        if (event->type == XGUI_EVENT_KEY_DOWN) {
            uint8_t k = event->key.keycode;
            if (k == '\n' || k == '\r') {
                if (pt_fname_pos > 0) {
                    if (pt_fname_mode == 1) pt_load_bmp(pt_fname_buf);
                    else pt_save_bmp(pt_fname_buf);
                }
                pt_fname_mode = 0;
            } else if (k == KEY_ESCAPE) {
                pt_fname_mode = 0;
            } else if (k == '\b') {
                if (pt_fname_pos > 0) pt_fname_buf[--pt_fname_pos] = '\0';
            }
            win->dirty = true;
            return;
        }
        return;
    }

    /* Brush size button Y position (must match paint callback) */
    int sz_y = TOOLBAR_H + TOOL_PAD + TOOL_ROWS * (TOOL_BTN + TOOL_GAP) + 8;

    if (event->type == XGUI_EVENT_MOUSE_DOWN) {
        int mx = event->mouse.x, my = event->mouse.y;
        bool right_btn = (event->mouse.button & XGUI_MOUSE_RIGHT) != 0;

        /* --- Tool selection --- */
        if (mx >= TOOL_PAD && mx < TOOLBOX_W - TOOL_PAD &&
            my >= TOOLBAR_H + TOOL_PAD) {
            int col = (mx - TOOL_PAD) / (TOOL_BTN + TOOL_GAP);
            int row = (my - TOOLBAR_H - TOOL_PAD) / (TOOL_BTN + TOOL_GAP);
            int t = row * TOOL_COLS + col;
            if (col < TOOL_COLS && row < TOOL_ROWS && t < TOOL_COUNT) {
                current_tool = (paint_tool_t)t;
                win->dirty = true;
                return;
            }
        }

        /* --- Brush size +/- --- */
        if (my >= sz_y + 34 && my < sz_y + 50) {
            if (mx >= 4 && mx < 26) {
                if (brush_size < 20) brush_size++;
                win->dirty = true;
                return;
            }
            if (mx >= 30 && mx < 52) {
                if (brush_size > 1) brush_size--;
                win->dirty = true;
                return;
            }
        }

        /* --- FG/BG swap (click on indicator area) --- */
        int ind_x = 6, ind_y = COLORBAR_Y + 5;
        if (mx >= ind_x && mx < ind_x + 25 &&
            my >= ind_y && my < ind_y + 25) {
            uint32_t tmp = fg_color;
            fg_color = bg_color;
            bg_color = tmp;
            win->dirty = true;
            return;
        }

        /* --- Palette click (left=FG, right=BG) --- */
        if (my >= COLORBAR_Y + 4 &&
            my < COLORBAR_Y + 4 + PAL_ROWS * (PAL_SZ + PAL_GAP)) {
            int pr = (my - COLORBAR_Y - 4) / (PAL_SZ + PAL_GAP);
            int pc = (mx - PAL_X_START) / (PAL_SZ + PAL_GAP);
            if (pc >= 0 && pc < PAL_COLS && pr >= 0 && pr < PAL_ROWS) {
                int idx = pr * PAL_COLS + pc;
                if (idx < PALETTE_SIZE) {
                    if (right_btn) bg_color = palette[idx];
                    else fg_color = palette[idx];
                    win->dirty = true;
                    return;
                }
            }
        }

        /* --- Canvas interaction --- */
        if (mx >= CANVAS_X && mx < CANVAS_X + CANVAS_W &&
            my >= CANVAS_Y && my < CANVAS_Y + CANVAS_H) {
            int cx = mx - CANVAS_X, cy = my - CANVAS_Y;

            switch (current_tool) {
            case TOOL_PENCIL:
                draw_color = right_btn ? bg_color : fg_color;
                canvas_set(cx, cy, draw_color);
                drawing = true;
                last_x = cx;
                last_y = cy;
                win->dirty = true;
                break;
            case TOOL_BRUSH:
                draw_color = right_btn ? bg_color : fg_color;
                canvas_brush(cx, cy, brush_size, draw_color);
                drawing = true;
                last_x = cx;
                last_y = cy;
                win->dirty = true;
                break;
            case TOOL_ERASER:
                draw_color = bg_color;
                canvas_brush(cx, cy, brush_size, draw_color);
                drawing = true;
                last_x = cx;
                last_y = cy;
                win->dirty = true;
                break;
            case TOOL_PICKER:
                if (right_btn) bg_color = canvas_get(cx, cy);
                else fg_color = canvas_get(cx, cy);
                win->dirty = true;
                break;
            case TOOL_FILL:
                canvas_flood_fill(cx, cy, right_btn ? bg_color : fg_color);
                win->dirty = true;
                break;
            case TOOL_LINE:
            case TOOL_RECT:
            case TOOL_FILLED_RECT:
            case TOOL_ELLIPSE:
            case TOOL_FILLED_ELLIPSE:
                draw_color = right_btn ? bg_color : fg_color;
                shape_dragging = true;
                shape_x0 = cx;
                shape_y0 = cy;
                shape_x1 = cx;
                shape_y1 = cy;
                win->dirty = true;
                break;
            default:
                break;
            }
        }
    }

    if (event->type == XGUI_EVENT_MOUSE_MOVE) {
        int mx = event->mouse.x, my = event->mouse.y;

        if (drawing) {
            int cx = pt_max(0, pt_min(mx - CANVAS_X, CANVAS_W - 1));
            int cy = pt_max(0, pt_min(my - CANVAS_Y, CANVAS_H - 1));

            switch (current_tool) {
            case TOOL_PENCIL:
                if (last_x >= 0)
                    canvas_line(last_x, last_y, cx, cy, draw_color);
                else
                    canvas_set(cx, cy, draw_color);
                last_x = cx;
                last_y = cy;
                win->dirty = true;
                break;
            case TOOL_BRUSH:
                if (last_x >= 0)
                    canvas_brush_line(last_x, last_y, cx, cy,
                                     brush_size, draw_color);
                else
                    canvas_brush(cx, cy, brush_size, draw_color);
                last_x = cx;
                last_y = cy;
                win->dirty = true;
                break;
            case TOOL_ERASER:
                if (last_x >= 0)
                    canvas_brush_line(last_x, last_y, cx, cy,
                                     brush_size, draw_color);
                else
                    canvas_brush(cx, cy, brush_size, draw_color);
                last_x = cx;
                last_y = cy;
                win->dirty = true;
                break;
            default:
                break;
            }
        }

        if (shape_dragging) {
            shape_x1 = pt_max(0, pt_min(mx - CANVAS_X, CANVAS_W - 1));
            shape_y1 = pt_max(0, pt_min(my - CANVAS_Y, CANVAS_H - 1));
            win->dirty = true;
        }
    }

    if (event->type == XGUI_EVENT_MOUSE_UP) {
        if (drawing) {
            drawing = false;
            last_x = last_y = -1;
        }
        if (shape_dragging) {
            shape_dragging = false;
            commit_shape(current_tool, shape_x0, shape_y0,
                         shape_x1, shape_y1, draw_color);
            win->dirty = true;
        }
    }
}

/* ---- Create / open functions ---- */

void xgui_paint_create(void) {
    if (paint_window) {
        xgui_window_focus(paint_window);
        return;
    }

    /* Reset state */
    fg_color = XGUI_BLACK;
    bg_color = XGUI_WHITE;
    current_tool = TOOL_PENCIL;
    brush_size = 3;
    drawing = false;
    shape_dragging = false;
    last_x = last_y = -1;
    draw_color = XGUI_BLACK;
    clear_canvas();
    pt_filename[0] = '\0';
    pt_fname_mode = 0;
    pt_status_msg[0] = '\0';
    pt_status_ticks = 0;

    paint_window = xgui_window_create("Paint", 30, 15, WIN_CW, WIN_CH,
                                      XGUI_WINDOW_DEFAULT);
    if (!paint_window) return;

    xgui_window_set_paint(paint_window, paint_paint);
    xgui_window_set_handler(paint_window, paint_handler);

    /* Toolbar buttons */
    pt_btn_new    = xgui_button_create(paint_window, 4,   2, 40, 20, "New");
    pt_btn_open   = xgui_button_create(paint_window, 48,  2, 46, 20, "Open");
    pt_btn_save   = xgui_button_create(paint_window, 98,  2, 46, 20, "Save");
    pt_btn_saveas = xgui_button_create(paint_window, 148, 2, 56, 20, "SaveAs");

    if (pt_btn_new)    xgui_widget_set_onclick(pt_btn_new, pt_on_new);
    if (pt_btn_open)   xgui_widget_set_onclick(pt_btn_open, pt_on_open);
    if (pt_btn_save)   xgui_widget_set_onclick(pt_btn_save, pt_on_save);
    if (pt_btn_saveas) xgui_widget_set_onclick(pt_btn_saveas, pt_on_save_as);
}

void xgui_paint_open_file(const char* path) {
    xgui_paint_create();
    if (paint_window && path && path[0]) {
        pt_load_bmp(path);
        paint_window->dirty = true;
    }
}
