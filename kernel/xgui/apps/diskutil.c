/*
 * MiniOS XGUI Disk Utility
 *
 * Shows disk info, volumes, partitions, free space, etc.
 */

#include "xgui/xgui.h"
#include "xgui/wm.h"
#include "xgui/widget.h"
#include "xgui/theme.h"
#include "blockdev.h"
#include "ata.h"
#include "ext2.h"
#include "vfs.h"
#include "heap.h"
#include "string.h"
#include "stdio.h"

/* Window dimensions */
#define DU_WIDTH        440
#define DU_HEIGHT       420

/* Layout constants */
#define DU_LIST_X       6
#define DU_LIST_Y       6
#define DU_LIST_W       140
#define DU_ITEM_H       20
#define DU_DETAIL_X     154
#define DU_DETAIL_Y     6
#define DU_LABEL_H      16
#define DU_SECTION_GAP  8

/* Singleton window */
static xgui_window_t* du_window = NULL;
static xgui_widget_t* btn_refresh = NULL;

/* Device list */
#define DU_MAX_DEVS     16

typedef struct {
    char name[16];
    blockdev_type_t type;
    uint32_t sector_size;
    uint32_t sector_count;
    uint32_t size_mb;
    uint32_t start_lba;
    uint8_t  partition_count;
    /* ATA info (for disks) */
    bool has_ata;
    char model[41];
    char serial[21];
    char firmware[9];
    bool lba48;
    /* Partition info */
    uint8_t part_type;
    bool    part_active;
    /* Parent name for partitions */
    char parent_name[16];
    /* Partition table for whole disks */
    partition_info_t parts[BLOCKDEV_MAX_PARTITIONS];
    /* Filesystem info (from ext2 superblock if mounted) */
    bool has_fs_stats;
    ext2_fs_stats_t fs_stats;
    char mount_path[32];
} du_dev_t;

static du_dev_t du_devs[DU_MAX_DEVS];
static int du_dev_count = 0;
static int du_selected = 0;
static int du_scroll = 0;

/*
 * Format size in MB to human-readable string
 */
static void du_format_mb(uint32_t mb, char* buf, int buf_size) {
    if (mb >= 1024) {
        uint32_t gb = mb / 1024;
        uint32_t gb_frac = (mb % 1024) * 10 / 1024;
        snprintf(buf, buf_size, "%u.%u GB", gb, gb_frac);
    } else if (mb > 0) {
        snprintf(buf, buf_size, "%u MB", mb);
    } else {
        snprintf(buf, buf_size, "< 1 MB");
    }
}

/*
 * Refresh the device list from blockdev layer
 */
static void du_refresh(void) {
    du_dev_count = 0;

    uint8_t count = blockdev_count();
    for (uint8_t i = 0; i < count && du_dev_count < DU_MAX_DEVS; i++) {
        blockdev_t* bdev = blockdev_get(i);
        if (!bdev) continue;

        du_dev_t* d = &du_devs[du_dev_count];
        memset(d, 0, sizeof(*d));
        strncpy(d->name, bdev->name, sizeof(d->name) - 1);
        d->type = bdev->type;
        d->sector_size = bdev->sector_size;
        d->sector_count = bdev->sector_count;
        d->size_mb = bdev->size_mb;
        d->start_lba = bdev->start_lba;
        d->partition_count = bdev->partition_count;

        /* Copy partition table for whole disks */
        if (bdev->type == BLOCKDEV_TYPE_DISK) {
            for (int p = 0; p < bdev->partition_count && p < BLOCKDEV_MAX_PARTITIONS; p++) {
                d->parts[p] = bdev->partitions[p];
            }
        }

        /* For partitions, get parent name and type */
        if (bdev->type == BLOCKDEV_TYPE_PARTITION) {
            if (bdev->parent) {
                strncpy(d->parent_name, bdev->parent->name, sizeof(d->parent_name) - 1);
            }
            /* Find partition type from parent */
            if (bdev->parent) {
                for (int p = 0; p < bdev->parent->partition_count; p++) {
                    if (bdev->parent->partitions[p].start_lba == bdev->start_lba) {
                        d->part_type = bdev->parent->partitions[p].type;
                        d->part_active = bdev->parent->partitions[p].active;
                        break;
                    }
                }
            }
        }

        /* Try to get ATA drive info */
        if (bdev->type == BLOCKDEV_TYPE_DISK) {
            ata_drive_t* drive = (ata_drive_t*)bdev->driver_data;
            if (drive && drive->present) {
                d->has_ata = true;
                strncpy(d->model, drive->model, sizeof(d->model) - 1);
                strncpy(d->serial, drive->serial, sizeof(d->serial) - 1);
                strncpy(d->firmware, drive->firmware, sizeof(d->firmware) - 1);
                d->lba48 = drive->lba48_supported;
            }
        }

        /* Try to find ext2 filesystem stats for partitions */
        if (bdev->type == BLOCKDEV_TYPE_PARTITION) {
            /* Check common mount points */
            static const char* mount_paths[] = { "/mnt", "/mnt2", "/mnt3", NULL };
            for (int m = 0; mount_paths[m]; m++) {
                vfs_node_t* mp = vfs_lookup(mount_paths[m]);
                if (mp && mp->readdir == ext2_vfs_readdir && mp->impl) {
                    /* Check if this ext2 fs is backed by our block device */
                    if (ext2_get_fs_stats(mp, &d->fs_stats)) {
                        d->has_fs_stats = true;
                        strncpy(d->mount_path, mount_paths[m], sizeof(d->mount_path) - 1);
                        break;
                    }
                }
            }
        }

        du_dev_count++;
    }

    if (du_selected >= du_dev_count) {
        du_selected = du_dev_count > 0 ? 0 : -1;
    }

    if (du_window) du_window->dirty = true;
}

/*
 * Draw a label: value pair
 */
static int du_draw_field(xgui_window_t* win, int x, int y, int label_w,
                         const char* label, const char* value) {
    xgui_win_text_transparent(win, x, y, label, XGUI_DARK_GRAY);
    xgui_win_text_transparent(win, x + label_w, y, value, XGUI_BLACK);
    return y + DU_LABEL_H + 2;
}

/*
 * Paint callback
 */
static void du_paint(xgui_window_t* win) {
    int cw = win->client_width;
    int ch = win->client_height;

    /* === Left panel: device list === */
    int list_h = ch - DU_LIST_Y - 32;
    xgui_win_rect_filled(win, DU_LIST_X, DU_LIST_Y, DU_LIST_W, list_h, XGUI_WHITE);
    xgui_win_rect_3d_sunken(win, DU_LIST_X, DU_LIST_Y, DU_LIST_W, list_h);

    /* Header */
    xgui_win_rect_filled(win, DU_LIST_X + 2, DU_LIST_Y + 2, DU_LIST_W - 4, DU_ITEM_H,
                         XGUI_LIGHT_GRAY);
    xgui_win_text(win, DU_LIST_X + 6, DU_LIST_Y + 4, "Devices", XGUI_BLACK, XGUI_LIGHT_GRAY);
    xgui_win_hline(win, DU_LIST_X + 2, DU_LIST_Y + 2 + DU_ITEM_H, DU_LIST_W - 4, XGUI_DARK_GRAY);

    /* Device entries */
    int item_y = DU_LIST_Y + 2 + DU_ITEM_H + 2;
    int visible = (list_h - DU_ITEM_H - 8) / DU_ITEM_H;

    for (int i = 0; i < du_dev_count && i < visible; i++) {
        int idx = i + du_scroll;
        if (idx >= du_dev_count) break;

        int y = item_y + i * DU_ITEM_H;
        uint32_t bg = XGUI_WHITE;
        uint32_t fg = XGUI_BLACK;

        if (idx == du_selected) {
            bg = XGUI_SELECTION;
            fg = XGUI_WHITE;
            xgui_win_rect_filled(win, DU_LIST_X + 2, y, DU_LIST_W - 4, DU_ITEM_H, bg);
        }

        /* Icon hint: disk or partition */
        char prefix[4] = "";
        if (du_devs[idx].type == BLOCKDEV_TYPE_DISK) {
            strncpy(prefix, "[D]", 3);
        } else if (du_devs[idx].type == BLOCKDEV_TYPE_PARTITION) {
            strncpy(prefix, " P ", 3);
        } else {
            strncpy(prefix, "[R]", 3);
        }
        prefix[3] = '\0';

        char entry[24];
        snprintf(entry, sizeof(entry), "%s %s", prefix, du_devs[idx].name);
        xgui_win_text(win, DU_LIST_X + 4, y + 3, entry, fg, bg);
    }

    /* === Right panel: detail view === */
    int detail_w = cw - DU_DETAIL_X - 6;
    int detail_h = ch - DU_DETAIL_Y - 32;
    xgui_win_rect_filled(win, DU_DETAIL_X, DU_DETAIL_Y, detail_w, detail_h,
                         XGUI_RGB(250, 250, 250));
    xgui_win_rect_3d_sunken(win, DU_DETAIL_X, DU_DETAIL_Y, detail_w, detail_h);

    if (du_selected < 0 || du_selected >= du_dev_count) {
        xgui_win_text_transparent(win, DU_DETAIL_X + 20, DU_DETAIL_Y + 30,
                                  "No device selected", XGUI_DARK_GRAY);
        xgui_widgets_draw(win);
        return;
    }

    du_dev_t* dev = &du_devs[du_selected];
    int dx = DU_DETAIL_X + 8;
    int dy = DU_DETAIL_Y + 8;
    int lw = 96;  /* label width */
    char buf[80];

    /* Device name header */
    xgui_win_rect_filled(win, DU_DETAIL_X + 2, dy, detail_w - 4, 20,
                         xgui_theme_current()->title_active);
    xgui_win_text_transparent(win, dx, dy + 3, dev->name, XGUI_WHITE);

    /* Type badge */
    const char* type_str = "Disk";
    if (dev->type == BLOCKDEV_TYPE_PARTITION) type_str = "Partition";
    else if (dev->type == BLOCKDEV_TYPE_RAMDISK) type_str = "RAM Disk";
    xgui_win_text_transparent(win, dx + detail_w - 80, dy + 3, type_str, XGUI_WHITE);

    dy += 28;

    /* --- General Info section --- */
    xgui_win_text_transparent(win, dx, dy, "General Information", XGUI_BLACK);
    dy += DU_LABEL_H + 4;
    xgui_win_hline(win, dx, dy, detail_w - 16, XGUI_LIGHT_GRAY);
    dy += 4;

    /* Size */
    du_format_mb(dev->size_mb, buf, sizeof(buf));
    dy = du_draw_field(win, dx, dy, lw, "Size:", buf);

    /* Total sectors */
    snprintf(buf, sizeof(buf), "%u", dev->sector_count);
    dy = du_draw_field(win, dx, dy, lw, "Sectors:", buf);

    /* Sector size */
    snprintf(buf, sizeof(buf), "%u bytes", dev->sector_size);
    dy = du_draw_field(win, dx, dy, lw, "Sector Size:", buf);

    if (dev->type == BLOCKDEV_TYPE_PARTITION) {
        /* Partition-specific info */
        snprintf(buf, sizeof(buf), "%u", dev->start_lba);
        dy = du_draw_field(win, dx, dy, lw, "Start LBA:", buf);

        const char* ptype = blockdev_partition_type_name(dev->part_type);
        snprintf(buf, sizeof(buf), "%s (0x%02X)", ptype, dev->part_type);
        dy = du_draw_field(win, dx, dy, lw, "FS Type:", buf);

        dy = du_draw_field(win, dx, dy, lw, "Bootable:",
                           dev->part_active ? "Yes" : "No");

        snprintf(buf, sizeof(buf), "%s", dev->parent_name);
        dy = du_draw_field(win, dx, dy, lw, "Parent Disk:", buf);
    }

    if (dev->type == BLOCKDEV_TYPE_DISK) {
        /* Partitions */
        snprintf(buf, sizeof(buf), "%u", dev->partition_count);
        dy = du_draw_field(win, dx, dy, lw, "Partitions:", buf);
    }

    dy += DU_SECTION_GAP;

    /* --- ATA Info section (for physical disks) --- */
    if (dev->has_ata) {
        xgui_win_text_transparent(win, dx, dy, "Hardware Information", XGUI_BLACK);
        dy += DU_LABEL_H + 4;
        xgui_win_hline(win, dx, dy, detail_w - 16, XGUI_LIGHT_GRAY);
        dy += 4;

        dy = du_draw_field(win, dx, dy, lw, "Model:", dev->model);
        dy = du_draw_field(win, dx, dy, lw, "Serial:", dev->serial);
        dy = du_draw_field(win, dx, dy, lw, "Firmware:", dev->firmware);
        dy = du_draw_field(win, dx, dy, lw, "LBA48:",
                           dev->lba48 ? "Supported" : "28-bit only");
        dy = du_draw_field(win, dx, dy, lw, "Interface:", "ATA/IDE PIO");
        dy += DU_SECTION_GAP;
    }

    /* --- Partition table section (for whole disks with partitions) --- */
    if (dev->type == BLOCKDEV_TYPE_DISK && dev->partition_count > 0) {
        xgui_win_text_transparent(win, dx, dy, "Partition Table", XGUI_BLACK);
        dy += DU_LABEL_H + 4;
        xgui_win_hline(win, dx, dy, detail_w - 16, XGUI_LIGHT_GRAY);
        dy += 4;

        /* Table header */
        xgui_win_rect_filled(win, dx, dy, detail_w - 16, DU_LABEL_H,
                             XGUI_LIGHT_GRAY);
        xgui_win_text(win, dx + 2, dy + 1, "#", XGUI_BLACK, XGUI_LIGHT_GRAY);
        xgui_win_text(win, dx + 20, dy + 1, "Type", XGUI_BLACK, XGUI_LIGHT_GRAY);
        xgui_win_text(win, dx + 112, dy + 1, "Start", XGUI_BLACK, XGUI_LIGHT_GRAY);
        xgui_win_text(win, dx + 176, dy + 1, "Size", XGUI_BLACK, XGUI_LIGHT_GRAY);
        dy += DU_LABEL_H + 2;

        for (int p = 0; p < dev->partition_count && p < BLOCKDEV_MAX_PARTITIONS; p++) {
            partition_info_t* pi = &dev->parts[p];
            char row[80];

            /* Partition number */
            snprintf(buf, sizeof(buf), "%d", p + 1);
            xgui_win_text_transparent(win, dx + 2, dy, buf,
                                      pi->active ? XGUI_RGB(0, 128, 0) : XGUI_BLACK);

            /* Type */
            const char* ptn = blockdev_partition_type_name(pi->type);
            char type_buf[16];
            strncpy(type_buf, ptn, 12);
            type_buf[12] = '\0';
            xgui_win_text_transparent(win, dx + 20, dy, type_buf, XGUI_BLACK);

            /* Start LBA */
            snprintf(row, sizeof(row), "%u", pi->start_lba);
            xgui_win_text_transparent(win, dx + 112, dy, row, XGUI_DARK_GRAY);

            /* Size */
            snprintf(row, sizeof(row), "%u MB", pi->size_mb);
            xgui_win_text_transparent(win, dx + 176, dy, row, XGUI_DARK_GRAY);

            dy += DU_LABEL_H + 1;
        }

        dy += DU_SECTION_GAP;
    }

    /* --- Filesystem Information section (for partitions with ext2 stats) --- */
    if (dev->has_fs_stats && dy + 20 < DU_DETAIL_Y + detail_h) {
        ext2_fs_stats_t* fs = &dev->fs_stats;

        xgui_win_text_transparent(win, dx, dy, "Filesystem Information", XGUI_BLACK);
        dy += DU_LABEL_H + 4;
        xgui_win_hline(win, dx, dy, detail_w - 16, XGUI_LIGHT_GRAY);
        dy += 4;

        dy = du_draw_field(win, dx, dy, lw, "Format:", "ext2");

        if (dev->mount_path[0]) {
            dy = du_draw_field(win, dx, dy, lw, "Mounted At:", dev->mount_path);
        }

        if (fs->volume_name[0]) {
            dy = du_draw_field(win, dx, dy, lw, "Volume Name:", fs->volume_name);
        }

        snprintf(buf, sizeof(buf), "%u", fs->block_size);
        dy = du_draw_field(win, dx, dy, lw, "Block Size:", buf);

        const char* state_str = "Unknown";
        if (fs->state == 1) state_str = "Clean";
        else if (fs->state == 2) state_str = "Errors";
        dy = du_draw_field(win, dx, dy, lw, "State:", state_str);

        snprintf(buf, sizeof(buf), "%u", fs->total_inodes - fs->free_inodes);
        dy = du_draw_field(win, dx, dy, lw, "Inodes Used:", buf);

        /* Total / Used / Free sizes */
        char total_str[16], used_str[16], free_str[16];
        du_format_mb(fs->total_size_kb / 1024, total_str, sizeof(total_str));
        du_format_mb(fs->used_size_kb / 1024, used_str, sizeof(used_str));
        du_format_mb(fs->free_size_kb / 1024, free_str, sizeof(free_str));

        dy = du_draw_field(win, dx, dy, lw, "Total:", total_str);
        dy = du_draw_field(win, dx, dy, lw, "Used:", used_str);
        dy = du_draw_field(win, dx, dy, lw, "Free:", free_str);

        /* Usage bar */
        if (dy + 24 < DU_DETAIL_Y + detail_h) {
            int bar_x = dx;
            int bar_w = detail_w - 16;
            int bar_h = 16;

            xgui_win_rect_filled(win, bar_x, dy, bar_w, bar_h, XGUI_WHITE);
            xgui_win_rect_3d_sunken(win, bar_x, dy, bar_w, bar_h);

            int inner_w = bar_w - 4;
            /* Calculate used portion — use KB to avoid overflow */
            int used_px = 0;
            if (fs->total_size_kb > 0) {
                used_px = (int)(fs->used_size_kb / (fs->total_size_kb / (uint32_t)inner_w));
                if (used_px > inner_w) used_px = inner_w;
                if (used_px < 1 && fs->used_size_kb > 0) used_px = 1;
            }

            /* Used portion (blue) */
            if (used_px > 0) {
                xgui_win_rect_filled(win, bar_x + 2, dy + 2,
                                     used_px, bar_h - 4, XGUI_RGB(70, 130, 180));
            }
            /* Free portion (light green) */
            if (used_px < inner_w) {
                xgui_win_rect_filled(win, bar_x + 2 + used_px, dy + 2,
                                     inner_w - used_px, bar_h - 4, XGUI_RGB(180, 220, 180));
            }

            dy += bar_h + 4;

            /* Legend */
            xgui_win_rect_filled(win, dx, dy, 8, 8, XGUI_RGB(70, 130, 180));
            xgui_win_text_transparent(win, dx + 12, dy - 1, "Used", XGUI_DARK_GRAY);
            xgui_win_rect_filled(win, dx + 60, dy, 8, 8, XGUI_RGB(180, 220, 180));
            xgui_win_text_transparent(win, dx + 72, dy - 1, "Free", XGUI_DARK_GRAY);

            /* Percentage */
            if (fs->total_size_kb > 0) {
                uint32_t pct = (fs->used_size_kb * 100) / fs->total_size_kb;
                snprintf(buf, sizeof(buf), "%u%% used", pct);
                xgui_win_text_transparent(win, dx + 120, dy - 1, buf, XGUI_BLACK);
            }
        }

        dy += DU_SECTION_GAP;
    }

    /* --- Capacity bar (for disks and partitions without fs stats) --- */
    if (!dev->has_fs_stats && dev->size_mb > 0 && dy + 30 < DU_DETAIL_Y + detail_h) {
        int bar_x = dx;
        int bar_w = detail_w - 16;
        int bar_h = 16;

        xgui_win_rect_filled(win, bar_x, dy, bar_w, bar_h, XGUI_WHITE);
        xgui_win_rect_3d_sunken(win, bar_x, dy, bar_w, bar_h);

        if (dev->type == BLOCKDEV_TYPE_DISK && dev->partition_count > 0) {
            /* Show partitions as colored segments */
            uint32_t colors[] = {
                XGUI_RGB(70, 130, 180),
                XGUI_RGB(180, 100, 70),
                XGUI_RGB(70, 180, 100),
                XGUI_RGB(180, 70, 160),
                XGUI_RGB(180, 180, 70),
                XGUI_RGB(100, 70, 180),
                XGUI_RGB(70, 180, 180),
                XGUI_RGB(180, 130, 70)
            };
            int inner_w = bar_w - 4;
            for (int p = 0; p < dev->partition_count && p < BLOCKDEV_MAX_PARTITIONS; p++) {
                partition_info_t* pi = &dev->parts[p];
                if (dev->size_mb == 0) continue;
                /* Scale down to avoid 64-bit division */
                uint32_t scale = dev->sector_count / (uint32_t)inner_w;
                if (scale == 0) scale = 1;
                int px = (int)(pi->start_lba / scale);
                int pw = (int)(pi->sector_count / scale);
                if (pw < 2) pw = 2;
                xgui_win_rect_filled(win, bar_x + 2 + px, dy + 2,
                                     pw, bar_h - 4, colors[p % 8]);
            }
        } else {
            /* Solid fill */
            int fill_w = bar_w - 4;
            xgui_win_rect_filled(win, bar_x + 2, dy + 2,
                                 fill_w, bar_h - 4, XGUI_RGB(70, 130, 180));
        }
    }

    xgui_widgets_draw(win);
}

/*
 * Refresh button callback
 */
static void on_refresh(xgui_widget_t* widget) {
    (void)widget;
    du_refresh();
}

/*
 * Event handler
 */
static void du_handler(xgui_window_t* win, xgui_event_t* event) {
    if (xgui_widgets_handle_event(win, event)) {
        win->dirty = true;
        return;
    }

    if (event->type == XGUI_EVENT_WINDOW_CLOSE) {
        xgui_window_destroy(win);
        du_window = NULL;
        btn_refresh = NULL;
        return;
    }

    /* Click in device list */
    if (event->type == XGUI_EVENT_MOUSE_DOWN && event->mouse.button == XGUI_MOUSE_LEFT) {
        int mx = event->mouse.x;
        int my = event->mouse.y;

        int list_h = win->client_height - DU_LIST_Y - 32;
        int item_y = DU_LIST_Y + 2 + DU_ITEM_H + 2;
        int visible = (list_h - DU_ITEM_H - 8) / DU_ITEM_H;

        if (mx >= DU_LIST_X && mx < DU_LIST_X + DU_LIST_W &&
            my >= item_y && my < item_y + visible * DU_ITEM_H) {
            int clicked = (my - item_y) / DU_ITEM_H + du_scroll;
            if (clicked >= 0 && clicked < du_dev_count) {
                du_selected = clicked;
                win->dirty = true;
            }
        }
    }

    /* Keyboard navigation */
    if (event->type == XGUI_EVENT_KEY_DOWN) {
        if (event->key.keycode == 0x48) { /* Up arrow */
            if (du_selected > 0) {
                du_selected--;
                win->dirty = true;
            }
        } else if (event->key.keycode == 0x50) { /* Down arrow */
            if (du_selected < du_dev_count - 1) {
                du_selected++;
                win->dirty = true;
            }
        }
    }
}

/*
 * Create the Disk Utility window
 */
void xgui_diskutil_create(void) {
    if (du_window) {
        xgui_window_focus(du_window);
        du_refresh();
        return;
    }

    du_window = xgui_window_create("Disk Utility", 80, 40,
                                    DU_WIDTH, DU_HEIGHT,
                                    XGUI_WINDOW_DEFAULT);
    if (!du_window) return;

    xgui_window_set_paint(du_window, du_paint);
    xgui_window_set_handler(du_window, du_handler);
    xgui_window_set_bgcolor(du_window, XGUI_LIGHT_GRAY);

    /* Refresh button at bottom */
    btn_refresh = xgui_button_create(du_window, 6, DU_HEIGHT - 52, 70, 22, "Refresh");
    if (btn_refresh) xgui_widget_set_onclick(btn_refresh, on_refresh);

    du_refresh();
}
