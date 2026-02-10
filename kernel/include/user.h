/*
 * MiniOS User and Group Management
 * 
 * Provides user authentication, groups, and permissions.
 */

#ifndef _USER_H
#define _USER_H

#include "types.h"

/* Constants */
#define MAX_USERS           32
#define MAX_GROUPS          16
#define MAX_USERNAME        32
#define MAX_PASSWORD        64
#define MAX_HOME_PATH       64
#define MAX_GROUP_MEMBERS   16

/* Special user/group IDs */
#define ROOT_UID            0
#define ROOT_GID            0
#define GUEST_UID           1000
#define GUEST_GID           1000

/* Permission bits (Unix-style) */
#define PERM_READ           0x04
#define PERM_WRITE          0x02
#define PERM_EXEC           0x01

/* Permission masks for owner/group/others */
#define PERM_OWNER_SHIFT    6
#define PERM_GROUP_SHIFT    3
#define PERM_OTHER_SHIFT    0

#define PERM_OWNER_READ     (PERM_READ << PERM_OWNER_SHIFT)   /* 0400 */
#define PERM_OWNER_WRITE    (PERM_WRITE << PERM_OWNER_SHIFT)  /* 0200 */
#define PERM_OWNER_EXEC     (PERM_EXEC << PERM_OWNER_SHIFT)   /* 0100 */
#define PERM_GROUP_READ     (PERM_READ << PERM_GROUP_SHIFT)   /* 0040 */
#define PERM_GROUP_WRITE    (PERM_WRITE << PERM_GROUP_SHIFT)  /* 0020 */
#define PERM_GROUP_EXEC     (PERM_EXEC << PERM_GROUP_SHIFT)   /* 0010 */
#define PERM_OTHER_READ     (PERM_READ << PERM_OTHER_SHIFT)   /* 0004 */
#define PERM_OTHER_WRITE    (PERM_WRITE << PERM_OTHER_SHIFT)  /* 0002 */
#define PERM_OTHER_EXEC     (PERM_EXEC << PERM_OTHER_SHIFT)   /* 0001 */

/* Common permission combinations */
#define PERM_RW_R_R         (PERM_OWNER_READ | PERM_OWNER_WRITE | PERM_GROUP_READ | PERM_OTHER_READ)  /* 0644 */
#define PERM_RWXR_XR_X      (0755)  /* rwxr-xr-x */
#define PERM_RW_______      (PERM_OWNER_READ | PERM_OWNER_WRITE)  /* 0600 */

/* User structure */
typedef struct user {
    uint32_t uid;                       /* User ID */
    uint32_t gid;                       /* Primary group ID */
    char username[MAX_USERNAME];        /* Username */
    char password[MAX_PASSWORD];        /* Password (plain text for simplicity) */
    char home[MAX_HOME_PATH];           /* Home directory */
    uint8_t is_active;                  /* 1 if slot is in use */
} user_t;

/* Group structure */
typedef struct group {
    uint32_t gid;                       /* Group ID */
    char groupname[MAX_USERNAME];       /* Group name */
    uint32_t members[MAX_GROUP_MEMBERS]; /* Member UIDs */
    uint8_t member_count;               /* Number of members */
    uint8_t is_active;                  /* 1 if slot is in use */
} group_t;

/* Current logged-in user */
extern uint32_t current_uid;
extern uint32_t current_gid;

/*
 * Initialize user subsystem
 */
void user_init(void);

/*
 * User management functions
 */
int user_add(const char* username, const char* password, uint32_t uid, uint32_t gid, const char* home);
int user_del(uint32_t uid);
user_t* user_get(uint32_t uid);
user_t* user_get_by_name(const char* username);
int user_set_password(uint32_t uid, const char* password);

/*
 * Authentication
 */
int user_auth(const char* username, const char* password);
int user_login(const char* username, const char* password);
void user_logout(void);
int user_switch(uint32_t uid, const char* password);

/*
 * Group management functions
 */
int group_add(const char* groupname, uint32_t gid);
int group_del(uint32_t gid);
group_t* group_get(uint32_t gid);
group_t* group_get_by_name(const char* groupname);
int group_add_member(uint32_t gid, uint32_t uid);
int group_remove_member(uint32_t gid, uint32_t uid);
int user_in_group(uint32_t uid, uint32_t gid);

/*
 * Permission checking
 */
int check_permission(uint32_t file_uid, uint32_t file_gid, uint16_t mode, 
                     uint32_t user_uid, uint32_t user_gid, uint8_t access);

/*
 * Utility functions
 */
const char* user_get_name(uint32_t uid);
const char* group_get_name(uint32_t gid);
int user_is_root(void);

/*
 * Home directory management
 */
void user_create_home(const char* username);

#endif /* _USER_H */

