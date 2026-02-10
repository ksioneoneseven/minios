/*
 * MiniOS Virtual File System (VFS) Header
 * 
 * Provides an abstract interface for file operations.
 * All filesystems implement this interface.
 */

#ifndef _VFS_H
#define _VFS_H

#include "types.h"

/* Maximum path length */
#define VFS_MAX_PATH        256
#define VFS_MAX_NAME        64
#define VFS_MAX_OPEN_FILES  64
#define VFS_MAX_MOUNTS      8

/* File types */
#define VFS_FILE        0x01
#define VFS_DIRECTORY   0x02
#define VFS_CHARDEVICE  0x03
#define VFS_BLOCKDEVICE 0x04
#define VFS_PIPE        0x05
#define VFS_SYMLINK     0x06
#define VFS_MOUNTPOINT  0x08

/* Open flags */
#define O_RDONLY    0x0000
#define O_WRONLY    0x0001
#define O_RDWR      0x0002
#define O_CREAT     0x0100
#define O_TRUNC     0x0200
#define O_APPEND    0x0400

/* Seek modes */
#define SEEK_SET    0
#define SEEK_CUR    1
#define SEEK_END    2

/* Forward declarations */
struct vfs_node;
struct dirent;

/* File operations function pointers */
typedef int32_t (*read_fn_t)(struct vfs_node*, uint32_t offset, uint32_t size, uint8_t* buffer);
typedef int32_t (*write_fn_t)(struct vfs_node*, uint32_t offset, uint32_t size, uint8_t* buffer);
typedef int32_t (*open_fn_t)(struct vfs_node*, uint32_t flags);
typedef int32_t (*close_fn_t)(struct vfs_node*);
typedef struct dirent* (*readdir_fn_t)(struct vfs_node*, uint32_t index);
typedef struct vfs_node* (*finddir_fn_t)(struct vfs_node*, const char* name);

/* VFS node (inode) structure */
typedef struct vfs_node {
    char name[VFS_MAX_NAME];    /* File name */
    uint32_t flags;             /* File type and flags */
    uint32_t inode;             /* Inode number */
    uint32_t length;            /* File size in bytes */
    uint32_t impl;              /* Implementation-specific data */

    /* Ownership and permissions */
    uint32_t uid;               /* Owner user ID */
    uint32_t gid;               /* Owner group ID */
    uint16_t mode;              /* Permission bits (rwxrwxrwx) */

    /* File operations */
    read_fn_t read;
    write_fn_t write;
    open_fn_t open;
    close_fn_t close;
    readdir_fn_t readdir;
    finddir_fn_t finddir;

    /* For mountpoints */
    struct vfs_node* ptr;       /* Mounted filesystem root */
    struct vfs_node* parent;    /* Parent directory */
} vfs_node_t;

/* Directory entry */
typedef struct dirent {
    char name[VFS_MAX_NAME];
    uint32_t inode;
} dirent_t;

/* Forward declaration for pipe */
struct pipe;

/* File descriptor */
typedef struct {
    vfs_node_t* node;           /* VFS node */
    struct pipe* pipe;          /* Associated pipe (for pipes) */
    uint32_t offset;            /* Current position */
    uint32_t flags;             /* Open flags */
    uint32_t refcount;          /* Reference count */
    bool is_pipe;               /* True if this is a pipe */
    bool is_read_end;           /* True if this is the read end of a pipe */
} file_descriptor_t;

/* VFS root node */
extern vfs_node_t* vfs_root;

/*
 * Initialize the VFS
 */
void vfs_init(void);

/*
 * Standard file operations
 */
int32_t vfs_read(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer);
int32_t vfs_write(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer);
int32_t vfs_open(vfs_node_t* node, uint32_t flags);
int32_t vfs_close(vfs_node_t* node);
dirent_t* vfs_readdir(vfs_node_t* node, uint32_t index);
vfs_node_t* vfs_finddir(vfs_node_t* node, const char* name);

/*
 * Path operations
 */
vfs_node_t* vfs_lookup(const char* path);
vfs_node_t* vfs_namei(const char* path);  /* Resolve path to node */

/*
 * Mount operations
 */
int vfs_mount(const char* path, vfs_node_t* fs_root);

/*
 * Permission operations
 */
int vfs_check_read(vfs_node_t* node);
int vfs_check_write(vfs_node_t* node);
int vfs_check_exec(vfs_node_t* node);
int vfs_chmod(vfs_node_t* node, uint16_t mode);
int vfs_chown(vfs_node_t* node, uint32_t uid, uint32_t gid);

#endif /* _VFS_H */

