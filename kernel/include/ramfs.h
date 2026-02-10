/*
 * MiniOS RAM File System Header
 * 
 * Simple in-memory file system for testing and /tmp.
 */

#ifndef _RAMFS_H
#define _RAMFS_H

#include "vfs.h"

/* Maximum files in ramfs */
#define RAMFS_MAX_FILES     64
#define RAMFS_MAX_FILE_SIZE 524288  /* 512KB - enough for BMP files and binaries */

/*
 * Initialize the RAM filesystem
 * Returns the root node
 */
vfs_node_t* ramfs_init(void);

/*
 * Create a file in ramfs (in root directory)
 */
vfs_node_t* ramfs_create_file(const char* name, uint32_t flags);

/*
 * Create a file in a specific directory
 */
vfs_node_t* ramfs_create_file_in(vfs_node_t* parent, const char* name, uint32_t flags);

/*
 * Create a directory in ramfs (in root directory)
 */
vfs_node_t* ramfs_create_dir(const char* name);

/*
 * Create a directory in a specific parent directory
 */
vfs_node_t* ramfs_create_dir_in(vfs_node_t* parent, const char* name);

/*
 * Delete a file or empty directory from a parent directory
 */
int ramfs_delete(vfs_node_t* parent, const char* name);

/*
 * RAMFS readdir function (for filesystem type detection)
 */
dirent_t* ramfs_readdir(vfs_node_t* node, uint32_t index);

#endif /* _RAMFS_H */

