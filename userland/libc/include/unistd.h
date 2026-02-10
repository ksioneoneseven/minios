/*
 * MiniOS User-space POSIX-like Declarations
 */

#ifndef _UNISTD_H
#define _UNISTD_H

#include "syscall.h"

/* Write to file descriptor */
static inline ssize_t write(int fd, const void* buf, size_t count) {
    return syscall3(SYS_WRITE, fd, (int)buf, (int)count);
}

/* Read from file descriptor */
static inline ssize_t read(int fd, void* buf, size_t count) {
    return syscall3(SYS_READ, fd, (int)buf, (int)count);
}

/* Open a file */
static inline int open(const char* path, int flags) {
    return syscall2(SYS_OPEN, (int)path, flags);
}

/* Close a file descriptor */
static inline int close(int fd) {
    return syscall1(SYS_CLOSE, fd);
}

/* Get process ID */
static inline pid_t getpid(void) {
    return syscall0(SYS_GETPID);
}

/* Fork process */
static inline pid_t fork(void) {
    return syscall0(SYS_FORK);
}

/* Execute program */
static inline int execv(const char* path, char* const argv[]) {
    return syscall2(SYS_EXEC, (int)path, (int)argv);
}

/* Wait for child */
static inline pid_t wait(int* status) {
    return syscall1(SYS_WAIT, (int)status);
}

/* Yield CPU */
static inline void yield(void) {
    syscall0(SYS_YIELD);
}

/* Sleep for milliseconds */
static inline void sleep_ms(unsigned int ms) {
    syscall1(SYS_SLEEP, (int)ms);
}

/* Grow heap */
static inline void* sbrk(int increment) {
    return (void*)syscall1(SYS_SBRK, increment);
}

/* Directory entry structure (must match kernel) */
typedef struct {
    /* Must match kernel's dirent_t layout exactly (see kernel/include/vfs.h) */
    char name[64];   /* VFS_MAX_NAME */
    unsigned int inode;
} dirent_t;

/* Simple stat buffer (flags, size, uid, gid, mode) */
typedef struct {
    unsigned int flags;
    unsigned int size;
    unsigned int uid;
    unsigned int gid;
    unsigned int mode;
} stat_t;

/* Process info structure (for ps) */
typedef struct {
    unsigned int pid;
    unsigned int state;
    char name[24];
} procinfo_t;

/* System info structure (for uname) */
typedef struct {
    char sysname[32];
    char nodename[32];
    char release[32];
    char version[32];
    char machine[32];
} utsname_t;

/* Read directory entries */
static inline int readdir(const char* path, dirent_t* entries, int count) {
    return syscall3(SYS_READDIR, (int)path, (int)entries, count);
}

/* Get file status */
static inline int stat(const char* path, stat_t* buf) {
    return syscall2(SYS_STAT, (int)path, (int)buf);
}

/* Change directory */
static inline int chdir(const char* path) {
    return syscall1(SYS_CHDIR, (int)path);
}

/* Get current working directory */
static inline int getcwd(char* buf, size_t size) {
    return syscall2(SYS_GETCWD, (int)buf, (int)size);
}

/* Get user ID */
static inline unsigned int getuid(void) {
    return (unsigned int)syscall0(SYS_GETUID);
}

/* Get process list */
static inline int ps(procinfo_t* buf, int count) {
    return syscall2(SYS_PS, (int)buf, count);
}

/* Get uptime in ticks */
static inline unsigned int uptime(void) {
    return (unsigned int)syscall0(SYS_UPTIME);
}

/* Get system info */
static inline int uname(utsname_t* buf) {
    return syscall1(SYS_UNAME, (int)buf);
}

/* Scroll display: 0=up, 1=down, 2=to bottom */
static inline int scroll(int direction, int lines) {
    return syscall2(SYS_SCROLL, direction, lines);
}

/* Create directory */
static inline int mkdir(const char* path) {
    return syscall1(SYS_MKDIR, (int)path);
}

/* Remove file */
static inline int unlink(const char* path) {
    return syscall1(SYS_UNLINK, (int)path);
}

/* Read from file fd */
static inline int fread_fd(int fd, void* buf, size_t count) {
    return syscall3(SYS_FREAD, fd, (int)buf, (int)count);
}

/* Write to file fd */
static inline int fwrite_fd(int fd, const void* buf, size_t count) {
    return syscall3(SYS_FWRITE, fd, (int)buf, (int)count);
}

/* Memory info structure */
typedef struct {
    unsigned int total;
    unsigned int used;
    unsigned int free;
    unsigned int heap_used;
} meminfo_t;

/* Get memory info */
static inline int meminfo(meminfo_t* buf) {
    return syscall1(SYS_MEMINFO, (int)buf);
}

/* Date/time structure */
typedef struct {
    unsigned int year;
    unsigned int month;
    unsigned int day;
    unsigned int hour;
    unsigned int minute;
    unsigned int second;
} datetime_t;

/* Get date/time */
static inline int getdate(datetime_t* buf) {
    return syscall1(SYS_DATE, (int)buf);
}

/* Special key codes (must match kernel keyboard.h) */
#define KEY_UP      0x90
#define KEY_DOWN    0x91
#define KEY_LEFT    0x92
#define KEY_RIGHT   0x93
#define KEY_HOME    0x94
#define KEY_END     0x95
#define KEY_PAGEUP  0x96
#define KEY_PAGEDOWN 0x97
#define KEY_INSERT  0x98
#define KEY_DELETE  0x99

#endif /* _UNISTD_H */

