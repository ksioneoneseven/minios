/*
 * MiniOS User and Group Management Implementation
 */

#include "../include/user.h"
#include "../include/string.h"
#include "../include/stdio.h"
#include "../include/ramfs.h"
#include "../include/vfs.h"

/* User and group databases */
static user_t users[MAX_USERS];
static group_t groups[MAX_GROUPS];

/* Current logged-in user */
uint32_t current_uid = ROOT_UID;
uint32_t current_gid = ROOT_GID;

/* Forward declaration for creating home directories */
static void create_user_home(const char* username, const char* home_path);

/*
 * Initialize user subsystem
 */
void user_init(void) {
    /* Clear databases */
    memset(users, 0, sizeof(users));
    memset(groups, 0, sizeof(groups));

    /* Create directory structure */

    /* Create /home directory */
    vfs_node_t* home_dir = ramfs_create_dir("home");
    if (home_dir) {
        home_dir->uid = ROOT_UID;
        home_dir->gid = ROOT_GID;
        home_dir->mode = 0755;
    }

    /* Create /etc directory */
    vfs_node_t* etc_dir = ramfs_create_dir("etc");
    if (etc_dir) {
        etc_dir->uid = ROOT_UID;
        etc_dir->gid = ROOT_GID;
        etc_dir->mode = 0755;
    }

    /* Create /etc/skel directory with default files */
    vfs_node_t* skel_dir = ramfs_create_dir_in(etc_dir, "skel");
    if (skel_dir) {
        skel_dir->uid = ROOT_UID;
        skel_dir->gid = ROOT_GID;
        skel_dir->mode = 0755;

        /* Create default .profile in skel */
        vfs_node_t* profile = ramfs_create_file_in(skel_dir, ".profile", VFS_FILE);
        if (profile) {
            const char* profile_content = "# User profile\n# Add your startup commands here\n";
            vfs_write(profile, 0, strlen(profile_content), (uint8_t*)profile_content);
            profile->uid = ROOT_UID;
            profile->gid = ROOT_GID;
            profile->mode = 0644;
        }

        /* Create default .bashrc equivalent */
        vfs_node_t* rc = ramfs_create_file_in(skel_dir, ".shellrc", VFS_FILE);
        if (rc) {
            const char* rc_content = "# Shell configuration\n# Customize your shell here\n";
            vfs_write(rc, 0, strlen(rc_content), (uint8_t*)rc_content);
            rc->uid = ROOT_UID;
            rc->gid = ROOT_GID;
            rc->mode = 0644;
        }
    }

    /* Create /root directory (root's home) */
    vfs_node_t* root_home = ramfs_create_dir("root");
    if (root_home) {
        root_home->uid = ROOT_UID;
        root_home->gid = ROOT_GID;
        root_home->mode = 0700;  /* Only root can access */
    }

    /* Create root user (uid=0, gid=0) */
    user_add("root", "root", ROOT_UID, ROOT_GID, "/root");

    /* Create root group */
    group_add("root", ROOT_GID);
    group_add_member(ROOT_GID, ROOT_UID);

    /* Create guest user (uid=1000, gid=1000) */
    user_add("guest", "guest", GUEST_UID, GUEST_GID, "/home/guest");

    /* Create guest's home directory */
    create_user_home("guest", "/home/guest");

    /* Create guest group */
    group_add("users", GUEST_GID);
    group_add_member(GUEST_GID, GUEST_UID);

    /* Start as root */
    current_uid = ROOT_UID;
    current_gid = ROOT_GID;

    printk("User: Initialized (root, guest) with home directories\n");
}

/*
 * Add a new user
 */
int user_add(const char* username, const char* password, uint32_t uid, uint32_t gid, const char* home) {
    if (username == NULL || password == NULL) return -1;
    
    /* Check if username or uid already exists */
    for (int i = 0; i < MAX_USERS; i++) {
        if (users[i].is_active) {
            if (users[i].uid == uid) return -1;  /* UID exists */
            if (strcmp(users[i].username, username) == 0) return -1;  /* Username exists */
        }
    }
    
    /* Find empty slot */
    for (int i = 0; i < MAX_USERS; i++) {
        if (!users[i].is_active) {
            users[i].uid = uid;
            users[i].gid = gid;
            strncpy(users[i].username, username, MAX_USERNAME - 1);
            users[i].username[MAX_USERNAME - 1] = '\0';
            strncpy(users[i].password, password, MAX_PASSWORD - 1);
            users[i].password[MAX_PASSWORD - 1] = '\0';
            strncpy(users[i].home, home ? home : "/", MAX_HOME_PATH - 1);
            users[i].home[MAX_HOME_PATH - 1] = '\0';
            users[i].is_active = 1;
            return 0;
        }
    }
    
    return -1;  /* No free slots */
}

/*
 * Delete a user
 */
int user_del(uint32_t uid) {
    if (uid == ROOT_UID) return -1;  /* Cannot delete root */
    
    for (int i = 0; i < MAX_USERS; i++) {
        if (users[i].is_active && users[i].uid == uid) {
            users[i].is_active = 0;
            return 0;
        }
    }
    return -1;
}

/*
 * Get user by UID
 */
user_t* user_get(uint32_t uid) {
    for (int i = 0; i < MAX_USERS; i++) {
        if (users[i].is_active && users[i].uid == uid) {
            return &users[i];
        }
    }
    return NULL;
}

/*
 * Get user by username
 */
user_t* user_get_by_name(const char* username) {
    if (username == NULL) return NULL;
    
    for (int i = 0; i < MAX_USERS; i++) {
        if (users[i].is_active && strcmp(users[i].username, username) == 0) {
            return &users[i];
        }
    }
    return NULL;
}

/*
 * Set user password
 */
int user_set_password(uint32_t uid, const char* password) {
    user_t* user = user_get(uid);
    if (user == NULL || password == NULL) return -1;
    
    strncpy(user->password, password, MAX_PASSWORD - 1);
    user->password[MAX_PASSWORD - 1] = '\0';
    return 0;
}

/*
 * Authenticate user (check password)
 */
int user_auth(const char* username, const char* password) {
    user_t* user = user_get_by_name(username);
    if (user == NULL) return -1;
    
    if (strcmp(user->password, password) == 0) {
        return user->uid;
    }
    return -1;
}

/*
 * Login as user
 */
int user_login(const char* username, const char* password) {
    int uid = user_auth(username, password);
    if (uid < 0) return -1;
    
    user_t* user = user_get(uid);
    if (user == NULL) return -1;
    
    current_uid = user->uid;
    current_gid = user->gid;
    return 0;
}

/*
 * Logout (become root)
 */
void user_logout(void) {
    current_uid = ROOT_UID;
    current_gid = ROOT_GID;
}

/*
 * Switch to another user (su)
 */
int user_switch(uint32_t uid, const char* password) {
    user_t* user = user_get(uid);
    if (user == NULL) return -1;

    /* Root can switch to anyone without password */
    if (current_uid == ROOT_UID) {
        current_uid = user->uid;
        current_gid = user->gid;
        return 0;
    }

    /* Others need password */
    if (password == NULL) return -1;
    if (strcmp(user->password, password) != 0) return -1;

    current_uid = user->uid;
    current_gid = user->gid;
    return 0;
}

/*
 * Add a new group
 */
int group_add(const char* groupname, uint32_t gid) {
    if (groupname == NULL) return -1;

    /* Check if groupname or gid already exists */
    for (int i = 0; i < MAX_GROUPS; i++) {
        if (groups[i].is_active) {
            if (groups[i].gid == gid) return -1;
            if (strcmp(groups[i].groupname, groupname) == 0) return -1;
        }
    }

    /* Find empty slot */
    for (int i = 0; i < MAX_GROUPS; i++) {
        if (!groups[i].is_active) {
            groups[i].gid = gid;
            strncpy(groups[i].groupname, groupname, MAX_USERNAME - 1);
            groups[i].groupname[MAX_USERNAME - 1] = '\0';
            groups[i].member_count = 0;
            groups[i].is_active = 1;
            return 0;
        }
    }

    return -1;  /* No free slots */
}

/*
 * Delete a group
 */
int group_del(uint32_t gid) {
    if (gid == ROOT_GID) return -1;  /* Cannot delete root group */

    for (int i = 0; i < MAX_GROUPS; i++) {
        if (groups[i].is_active && groups[i].gid == gid) {
            groups[i].is_active = 0;
            return 0;
        }
    }
    return -1;
}

/*
 * Get group by GID
 */
group_t* group_get(uint32_t gid) {
    for (int i = 0; i < MAX_GROUPS; i++) {
        if (groups[i].is_active && groups[i].gid == gid) {
            return &groups[i];
        }
    }
    return NULL;
}

/*
 * Get group by name
 */
group_t* group_get_by_name(const char* groupname) {
    if (groupname == NULL) return NULL;

    for (int i = 0; i < MAX_GROUPS; i++) {
        if (groups[i].is_active && strcmp(groups[i].groupname, groupname) == 0) {
            return &groups[i];
        }
    }
    return NULL;
}

/*
 * Add member to group
 */
int group_add_member(uint32_t gid, uint32_t uid) {
    group_t* group = group_get(gid);
    if (group == NULL) return -1;

    /* Check if already member */
    for (int i = 0; i < group->member_count; i++) {
        if (group->members[i] == uid) return 0;  /* Already member */
    }

    if (group->member_count >= MAX_GROUP_MEMBERS) return -1;

    group->members[group->member_count++] = uid;
    return 0;
}

/*
 * Remove member from group
 */
int group_remove_member(uint32_t gid, uint32_t uid) {
    group_t* group = group_get(gid);
    if (group == NULL) return -1;

    for (int i = 0; i < group->member_count; i++) {
        if (group->members[i] == uid) {
            /* Shift remaining members */
            for (int j = i; j < group->member_count - 1; j++) {
                group->members[j] = group->members[j + 1];
            }
            group->member_count--;
            return 0;
        }
    }
    return -1;  /* Not a member */
}

/*
 * Check if user is in group
 */
int user_in_group(uint32_t uid, uint32_t gid) {
    /* Check primary group */
    user_t* user = user_get(uid);
    if (user != NULL && user->gid == gid) return 1;

    /* Check secondary groups */
    group_t* group = group_get(gid);
    if (group == NULL) return 0;

    for (int i = 0; i < group->member_count; i++) {
        if (group->members[i] == uid) return 1;
    }
    return 0;
}

/*
 * Check permission for file access
 * access: PERM_READ, PERM_WRITE, or PERM_EXEC
 */
int check_permission(uint32_t file_uid, uint32_t file_gid, uint16_t mode,
                     uint32_t user_uid, uint32_t user_gid, uint8_t access) {
    /* Root can do anything */
    if (user_uid == ROOT_UID) return 1;

    uint8_t perm;

    if (user_uid == file_uid) {
        /* Owner permissions */
        perm = (mode >> PERM_OWNER_SHIFT) & 0x07;
    } else if (user_gid == file_gid || user_in_group(user_uid, file_gid)) {
        /* Group permissions */
        perm = (mode >> PERM_GROUP_SHIFT) & 0x07;
    } else {
        /* Other permissions */
        perm = (mode >> PERM_OTHER_SHIFT) & 0x07;
    }

    return (perm & access) == access;
}

/*
 * Get username from UID
 */
const char* user_get_name(uint32_t uid) {
    user_t* user = user_get(uid);
    return user ? user->username : "unknown";
}

/*
 * Get group name from GID
 */
const char* group_get_name(uint32_t gid) {
    group_t* group = group_get(gid);
    return group ? group->groupname : "unknown";
}

/*
 * Check if current user is root
 */
int user_is_root(void) {
    return current_uid == ROOT_UID;
}

/*
 * Create a user's home directory with skel contents
 */
static void create_user_home(const char* username, const char* home_path) {
    if (home_path == NULL || home_path[0] == '\0') return;

    /* Find /home directory */
    vfs_node_t* home_parent = vfs_lookup("/home");
    if (home_parent == NULL) return;

    /* Create user's home directory */
    vfs_node_t* user_home = ramfs_create_dir_in(home_parent, username);
    if (user_home == NULL) return;

    /* Set ownership to the new user (we need to find their uid) */
    user_t* user = user_get_by_name(username);
    if (user) {
        user_home->uid = user->uid;
        user_home->gid = user->gid;
        user_home->mode = 0755;
    }

    /* Copy files from /etc/skel */
    vfs_node_t* skel = vfs_lookup("/etc/skel");
    if (skel == NULL) return;

    /* Iterate through skel directory and copy files */
    uint32_t index = 0;
    dirent_t* dent;
    while ((dent = vfs_readdir(skel, index++)) != NULL) {
        if (dent->name[0] == '\0') continue;
        if (strcmp(dent->name, ".") == 0 || strcmp(dent->name, "..") == 0) continue;

        /* Find the actual file node */
        vfs_node_t* src = vfs_finddir(skel, dent->name);
        if (src == NULL) continue;

        /* Create copy in user's home */
        vfs_node_t* copy = ramfs_create_file_in(user_home, dent->name, VFS_FILE);
        if (copy) {
            /* Copy content */
            uint8_t buf[256];
            uint32_t offset = 0;
            int32_t bytes;
            while ((bytes = vfs_read(src, offset, 256, buf)) > 0) {
                vfs_write(copy, offset, bytes, buf);
                offset += bytes;
            }
            /* Set ownership */
            if (user) {
                copy->uid = user->uid;
                copy->gid = user->gid;
            }
            copy->mode = 0644;
        }
    }
}

/*
 * Public function to create home directory for a new user
 */
void user_create_home(const char* username) {
    user_t* user = user_get_by_name(username);
    if (user && user->home[0] != '\0') {
        create_user_home(username, user->home);
    }
}
