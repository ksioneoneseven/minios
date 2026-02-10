#ifndef _EXT2_H
#define _EXT2_H

#include "types.h"
#include "vfs.h"
#include "blockdev.h"

vfs_node_t* ext2_mount(blockdev_t* bdev);
vfs_node_t* ext2_create_file(vfs_node_t* parent, const char* name);
vfs_node_t* ext2_create_dir(vfs_node_t* parent, const char* name);
bool ext2_unlink(vfs_node_t* parent, const char* name);

/* For filesystem type detection */
dirent_t* ext2_vfs_readdir(vfs_node_t* node, uint32_t index);

/* Filesystem statistics (read from superblock) */
typedef struct {
    uint32_t total_blocks;
    uint32_t free_blocks;
    uint32_t total_inodes;
    uint32_t free_inodes;
    uint32_t block_size;
    uint32_t blocks_per_group;
    uint32_t groups_count;
    uint32_t total_size_kb;     /* total size in KB */
    uint32_t free_size_kb;      /* free size in KB */
    uint32_t used_size_kb;      /* used size in KB */
    char     volume_name[16];
    char     last_mounted[64];
    uint16_t state;             /* 1=clean, 2=errors */
    uint32_t rev_level;         /* revision level */
} ext2_fs_stats_t;

/*
 * Get filesystem stats from a mounted ext2 VFS node.
 * The node must be an ext2 directory (e.g. the mount root).
 * Returns true on success.
 */
bool ext2_get_fs_stats(vfs_node_t* node, ext2_fs_stats_t* stats);

#endif /* _EXT2_H */
