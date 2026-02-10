/*
 * MiniOS Virtual File System (VFS) Implementation
 * 
 * Provides abstract file operations that delegate to specific filesystems.
 */

#include "../include/vfs.h"
#include "../include/string.h"
#include "../include/stdio.h"
#include "../include/heap.h"
#include "../include/user.h"

/* VFS root node */
vfs_node_t* vfs_root = NULL;

/* Mount table */
static struct {
    char path[VFS_MAX_PATH];
    vfs_node_t* root;
} mount_table[VFS_MAX_MOUNTS];
static int mount_count = 0;

 static vfs_node_t* vfs_resolve_mount(vfs_node_t* node) {
     if (node != NULL && (node->flags & VFS_MOUNTPOINT) && node->ptr != NULL) {
         return node->ptr;
     }
     return node;
 }

/*
 * Initialize the VFS
 */
void vfs_init(void) {
    vfs_root = NULL;
    mount_count = 0;
    memset(mount_table, 0, sizeof(mount_table));
    printk("VFS: Initialized\n");
}

/*
 * Read from a file
 */
int32_t vfs_read(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
     node = vfs_resolve_mount(node);
    if (node == NULL || node->read == NULL) {
        return -1;
    }
    return node->read(node, offset, size, buffer);
}

/*
 * Write to a file
 */
int32_t vfs_write(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
     node = vfs_resolve_mount(node);
    if (node == NULL || node->write == NULL) {
        return -1;
    }
    return node->write(node, offset, size, buffer);
}

/*
 * Open a file
 */
int32_t vfs_open(vfs_node_t* node, uint32_t flags) {
     node = vfs_resolve_mount(node);
    if (node == NULL) {
        return -1;
    }
    if (node->open != NULL) {
        return node->open(node, flags);
    }
    return 0;  /* Success if no open function */
}

/*
 * Close a file
 */
int32_t vfs_close(vfs_node_t* node) {
     node = vfs_resolve_mount(node);
    if (node == NULL) {
        return -1;
    }
    if (node->close != NULL) {
        return node->close(node);
    }
    return 0;  /* Success if no close function */
}

/*
 * Read directory entry
 */
dirent_t* vfs_readdir(vfs_node_t* node, uint32_t index) {
     node = vfs_resolve_mount(node);
    if (node == NULL || !(node->flags & VFS_DIRECTORY) || node->readdir == NULL) {
        return NULL;
    }
    return node->readdir(node, index);
}

/*
 * Find entry in directory
 */
vfs_node_t* vfs_finddir(vfs_node_t* node, const char* name) {
     node = vfs_resolve_mount(node);
    if (node == NULL || !(node->flags & VFS_DIRECTORY) || node->finddir == NULL) {
        return NULL;
    }
    return node->finddir(node, name);
}

/*
 * Lookup a path and return the VFS node
 */
vfs_node_t* vfs_lookup(const char* path) {
    if (path == NULL || path[0] == '\0') {
        return NULL;
    }
    
    /* Must start with / */
    if (path[0] != '/') {
        return NULL;
    }
    
    /* Root path */
    if (path[1] == '\0') {
        return vfs_root;
    }
    
    /* Start from root */
    vfs_node_t* current = vfs_resolve_mount(vfs_root);
    if (current == NULL) {
        return NULL;
    }
    
    /* Parse path components */
    char component[VFS_MAX_NAME];
    const char* p = path + 1;  /* Skip leading / */
    
    while (*p != '\0' && current != NULL) {
        /* Extract next component */
        int i = 0;
        while (*p != '\0' && *p != '/' && i < VFS_MAX_NAME - 1) {
            component[i++] = *p++;
        }
        component[i] = '\0';
        
        /* Skip trailing slash */
        if (*p == '/') {
            p++;
        }
        
        /* Empty component (double slash) */
        if (i == 0) {
            continue;
        }
        
        /* Look up component in current directory */
        current = vfs_finddir(current, component);
        current = vfs_resolve_mount(current);
    }
    
    return current;
}

/*
 * Alias for vfs_lookup
 */
vfs_node_t* vfs_namei(const char* path) {
    return vfs_lookup(path);
}

/*
 * Mount a filesystem at a path
 */
int vfs_mount(const char* path, vfs_node_t* fs_root) {
    if (mount_count >= VFS_MAX_MOUNTS) {
        return -1;
    }
    
    /* Root mount */
    if (strcmp(path, "/") == 0) {
        vfs_root = fs_root;
        printk("VFS: Mounted root filesystem\n");
        return 0;
    }
    
    /* Find mount point */
    vfs_node_t* mount_point = vfs_lookup(path);
    if (mount_point == NULL) {
        printk("VFS: Mount point '%s' not found\n", path);
        return -1;
    }
    
    /* Mark as mount point */
    mount_point->flags |= VFS_MOUNTPOINT;
    mount_point->ptr = fs_root;
    
    /* Add to mount table */
    strncpy(mount_table[mount_count].path, path, VFS_MAX_PATH - 1);
    mount_table[mount_count].root = fs_root;
    mount_count++;
    
    printk("VFS: Mounted filesystem at '%s'\n", path);
    return 0;
}

/*
 * Check read permission for current user
 */
int vfs_check_read(vfs_node_t* node) {
    if (node == NULL) return 0;
    return check_permission(node->uid, node->gid, node->mode,
                           current_uid, current_gid, PERM_READ);
}

/*
 * Check write permission for current user
 */
int vfs_check_write(vfs_node_t* node) {
    if (node == NULL) return 0;
    return check_permission(node->uid, node->gid, node->mode,
                           current_uid, current_gid, PERM_WRITE);
}

/*
 * Check execute permission for current user
 */
int vfs_check_exec(vfs_node_t* node) {
    if (node == NULL) return 0;
    return check_permission(node->uid, node->gid, node->mode,
                           current_uid, current_gid, PERM_EXEC);
}

/*
 * Change file permissions (chmod)
 */
int vfs_chmod(vfs_node_t* node, uint16_t mode) {
    if (node == NULL) return -1;

    /* Only owner or root can chmod */
    if (current_uid != ROOT_UID && current_uid != node->uid) {
        return -1;
    }

    node->mode = mode & 0777;  /* Only permission bits */
    return 0;
}

/*
 * Change file ownership (chown)
 */
int vfs_chown(vfs_node_t* node, uint32_t uid, uint32_t gid) {
    if (node == NULL) return -1;

    /* Only root can chown */
    if (current_uid != ROOT_UID) {
        return -1;
    }

    node->uid = uid;
    node->gid = gid;
    return 0;
}

