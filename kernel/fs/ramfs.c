/*
 * MiniOS RAM File System Implementation
 * 
 * Simple in-memory file system.
 */

#include "../include/ramfs.h"
#include "../include/vfs.h"
#include "../include/string.h"
#include "../include/stdio.h"
#include "../include/heap.h"
#include "../include/user.h"

/* RAM file entry */
typedef struct {
    vfs_node_t node;
    uint8_t* data;              /* File contents */
    uint32_t capacity;          /* Allocated size */
    struct ramfs_entry* children[RAMFS_MAX_FILES];  /* For directories */
    uint32_t child_count;
} ramfs_entry_t;

/* File storage */
static ramfs_entry_t ramfs_files[RAMFS_MAX_FILES];
static uint32_t ramfs_file_count = 0;
static vfs_node_t* ramfs_root_node = NULL;

/* Forward declarations */
static int32_t ramfs_read(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer);
static int32_t ramfs_write(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer);
dirent_t* ramfs_readdir(vfs_node_t* node, uint32_t index);
static vfs_node_t* ramfs_finddir(vfs_node_t* node, const char* name);

/* Static dirent for readdir */
static dirent_t ramfs_dirent;

/*
 * Read from a ramfs file
 */
static int32_t ramfs_read(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
    ramfs_entry_t* entry = (ramfs_entry_t*)node;
    
    if (offset >= node->length) {
        return 0;  /* EOF */
    }
    
    uint32_t to_read = size;
    if (offset + size > node->length) {
        to_read = node->length - offset;
    }
    
    if (entry->data != NULL) {
        memcpy(buffer, entry->data + offset, to_read);
    }
    
    return (int32_t)to_read;
}

/*
 * Write to a ramfs file
 */
static int32_t ramfs_write(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
    ramfs_entry_t* entry = (ramfs_entry_t*)node;

    /* Check if write would exceed max file size */
    uint32_t new_size = offset + size;
    if (new_size > RAMFS_MAX_FILE_SIZE) {
        /* Truncate to max */
        if (offset >= RAMFS_MAX_FILE_SIZE) {
            return 0;  /* Can't write past max */
        }
        size = RAMFS_MAX_FILE_SIZE - offset;
        new_size = RAMFS_MAX_FILE_SIZE;
    }

    /* Expand file if needed */
    if (new_size > entry->capacity) {
        uint32_t new_capacity = new_size + 256;  /* Extra space */
        if (new_capacity > RAMFS_MAX_FILE_SIZE) {
            new_capacity = RAMFS_MAX_FILE_SIZE;
        }

        uint8_t* new_data = (uint8_t*)kmalloc(new_capacity);
        if (new_data == NULL) {
            return -1;
        }

        if (entry->data != NULL) {
            memcpy(new_data, entry->data, entry->capacity);
            kfree(entry->data);
        }

        entry->data = new_data;
        entry->capacity = new_capacity;
    }

    /* Write data */
    memcpy(entry->data + offset, buffer, size);

    /* Update length */
    if (new_size > node->length) {
        node->length = new_size;
    }

    return (int32_t)size;
}

/*
 * Read directory entry
 */
dirent_t* ramfs_readdir(vfs_node_t* node, uint32_t index) {
    ramfs_entry_t* entry = (ramfs_entry_t*)node;
    
    if (index >= entry->child_count) {
        return NULL;
    }
    
    ramfs_entry_t* child = (ramfs_entry_t*)entry->children[index];
    strncpy(ramfs_dirent.name, child->node.name, VFS_MAX_NAME - 1);
    ramfs_dirent.name[VFS_MAX_NAME - 1] = '\0';
    ramfs_dirent.inode = child->node.inode;
    
    return &ramfs_dirent;
}

/*
 * Find entry in directory
 */
static vfs_node_t* ramfs_finddir(vfs_node_t* node, const char* name) {
    ramfs_entry_t* entry = (ramfs_entry_t*)node;
    
    for (uint32_t i = 0; i < entry->child_count; i++) {
        ramfs_entry_t* child = (ramfs_entry_t*)entry->children[i];
        if (strcmp(child->node.name, name) == 0) {
            return &child->node;
        }
    }
    
    return NULL;
}

/*
 * Allocate a new ramfs entry
 */
static ramfs_entry_t* ramfs_alloc_entry(void) {
    if (ramfs_file_count >= RAMFS_MAX_FILES) {
        return NULL;
    }
    return &ramfs_files[ramfs_file_count++];
}

/*
 * Initialize the RAM filesystem
 */
vfs_node_t* ramfs_init(void) {
    memset(ramfs_files, 0, sizeof(ramfs_files));
    ramfs_file_count = 0;

    /* Create root directory */
    ramfs_entry_t* root = ramfs_alloc_entry();
    if (root == NULL) {
        return NULL;
    }

    strncpy(root->node.name, "/", VFS_MAX_NAME);
    root->node.flags = VFS_DIRECTORY;
    root->node.inode = 0;
    root->node.length = 0;
    root->node.readdir = ramfs_readdir;
    root->node.finddir = ramfs_finddir;
    root->child_count = 0;

    /* Root directory is owned by root with rwxr-xr-x (0755) permissions */
    root->node.uid = ROOT_UID;
    root->node.gid = ROOT_GID;
    root->node.mode = 0755;

    ramfs_root_node = &root->node;

    printk("RAMFS: Initialized\n");
    return ramfs_root_node;
}

/*
 * Create a file in a specific directory
 */
vfs_node_t* ramfs_create_file_in(vfs_node_t* parent, const char* name, uint32_t flags) {
    if (parent == NULL) {
        parent = ramfs_root_node;
    }

    if (parent == ramfs_root_node) {
        ramfs_entry_t* p = (ramfs_entry_t*)parent;
        printk("RAMFS: create_file_in(parent='/', before child_count=%u) name='%s'\n", p->child_count, name);
    }

    ramfs_entry_t* entry = ramfs_alloc_entry();
    if (entry == NULL) {
        return NULL;
    }

    strncpy(entry->node.name, name, VFS_MAX_NAME - 1);
    entry->node.name[VFS_MAX_NAME - 1] = '\0';
    entry->node.flags = VFS_FILE | flags;
    entry->node.inode = ramfs_file_count - 1;
    entry->node.length = 0;
    entry->node.read = ramfs_read;
    entry->node.write = ramfs_write;
    entry->data = NULL;
    entry->capacity = 0;

    /* Set ownership to current user with rw-r--r-- (0644) permissions */
    entry->node.uid = current_uid;
    entry->node.gid = current_gid;
    entry->node.mode = 0644;  /* rw-r--r-- */

    /* Add to parent directory */
    ramfs_entry_t* parent_entry = (ramfs_entry_t*)parent;
    if (parent_entry->child_count < RAMFS_MAX_FILES) {
        parent_entry->children[parent_entry->child_count++] = (struct ramfs_entry*)entry;
        entry->node.parent = parent;
    }

    if (parent == ramfs_root_node) {
        ramfs_entry_t* p = (ramfs_entry_t*)parent;
        printk("RAMFS: create_file_in(parent='/', after child_count=%u) new_node=%p\n", p->child_count, entry);
    }

    return &entry->node;
}

/*
 * Create a file in ramfs (in root directory)
 */
vfs_node_t* ramfs_create_file(const char* name, uint32_t flags) {
    return ramfs_create_file_in(ramfs_root_node, name, flags);
}

/*
 * Create a directory in a specific parent directory
 */
vfs_node_t* ramfs_create_dir_in(vfs_node_t* parent, const char* name) {
    if (parent == NULL) {
        parent = ramfs_root_node;
    }

    /* Check for existing child with same name */
    ramfs_entry_t* parent_ent = (ramfs_entry_t*)parent;
    for (uint32_t i = 0; i < parent_ent->child_count; i++) {
        ramfs_entry_t* child = (ramfs_entry_t*)parent_ent->children[i];
        if (strcmp(child->node.name, name) == 0 && (child->node.flags & VFS_DIRECTORY)) {
            return &child->node;
        }
    }

    ramfs_entry_t* entry = ramfs_alloc_entry();
    if (entry == NULL) {
        return NULL;
    }

    strncpy(entry->node.name, name, VFS_MAX_NAME - 1);
    entry->node.name[VFS_MAX_NAME - 1] = '\0';
    entry->node.flags = VFS_DIRECTORY;
    entry->node.inode = ramfs_file_count - 1;
    entry->node.length = 0;
    entry->node.readdir = ramfs_readdir;
    entry->node.finddir = ramfs_finddir;
    entry->child_count = 0;

    /* Set ownership to current user with rwxr-xr-x (0755) permissions */
    entry->node.uid = current_uid;
    entry->node.gid = current_gid;
    entry->node.mode = 0755;  /* rwxr-xr-x */

    /* Add to parent directory */
    ramfs_entry_t* parent_entry2 = (ramfs_entry_t*)parent;
    if (parent_entry2->child_count < RAMFS_MAX_FILES) {
        parent_entry2->children[parent_entry2->child_count++] = (struct ramfs_entry*)entry;
        entry->node.parent = parent;
    }

    return &entry->node;
}

/*
 * Delete a file or empty directory from a parent directory.
 * Returns 0 on success, -1 on failure.
 */
int ramfs_delete(vfs_node_t* parent, const char* name) {
    if (!parent || !name) return -1;
    ramfs_entry_t* pent = (ramfs_entry_t*)parent;

    for (uint32_t i = 0; i < pent->child_count; i++) {
        ramfs_entry_t* child = (ramfs_entry_t*)pent->children[i];
        if (strcmp(child->node.name, name) == 0) {
            /* Don't delete non-empty directories */
            if ((child->node.flags & VFS_DIRECTORY) && child->child_count > 0) {
                return -1;
            }
            /* Free file data if any */
            if (child->data) {
                kfree(child->data);
                child->data = NULL;
            }
            /* Clear the entry */
            child->node.name[0] = '\0';
            child->node.length = 0;
            child->capacity = 0;
            /* Remove from parent's children array by shifting */
            for (uint32_t j = i; j < pent->child_count - 1; j++) {
                pent->children[j] = pent->children[j + 1];
            }
            pent->child_count--;
            return 0;
        }
    }
    return -1;
}

/*
 * Create a directory in ramfs (in root directory)
 */
vfs_node_t* ramfs_create_dir(const char* name) {
    return ramfs_create_dir_in(ramfs_root_node, name);
}

