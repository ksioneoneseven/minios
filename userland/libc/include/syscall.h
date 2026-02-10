/*
 * MiniOS User-space System Call Interface
 * 
 * Provides syscall wrappers for user programs.
 */

#ifndef _SYSCALL_H
#define _SYSCALL_H

/* System call numbers (must match kernel) */
#define SYS_EXIT        0
#define SYS_WRITE       1
#define SYS_READ        2
#define SYS_GETPID      3
#define SYS_FORK        4
#define SYS_EXEC        5
#define SYS_WAIT        6
#define SYS_YIELD       7
#define SYS_SLEEP       8
#define SYS_OPEN        9
#define SYS_CLOSE       10
#define SYS_SBRK        11
#define SYS_READDIR     12
#define SYS_STAT        13
#define SYS_CHDIR       14
#define SYS_GETCWD      15
#define SYS_GETUID      16
#define SYS_PS          17
#define SYS_UPTIME      18
#define SYS_UNAME       19
#define SYS_SCROLL      20
#define SYS_MKDIR       21
#define SYS_UNLINK      22
#define SYS_FREAD       23
#define SYS_FWRITE      24
#define SYS_MEMINFO     25
#define SYS_DATE        26

/* Standard file descriptors */
#define STDIN_FILENO    0
#define STDOUT_FILENO   1
#define STDERR_FILENO   2

/* Type definitions */
typedef unsigned int size_t;
typedef int ssize_t;
typedef int pid_t;

/*
 * Raw syscall interface
 * Arguments passed in EBX, ECX, EDX, ESI, EDI
 * Return value in EAX
 */
static inline int syscall0(int num) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num)
        : "memory"
    );
    return ret;
}

static inline int syscall1(int num, int arg1) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(arg1)
        : "memory"
    );
    return ret;
}

static inline int syscall2(int num, int arg1, int arg2) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(arg1), "c"(arg2)
        : "memory"
    );
    return ret;
}

static inline int syscall3(int num, int arg1, int arg2, int arg3) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(arg1), "c"(arg2), "d"(arg3)
        : "memory"
    );
    return ret;
}

#endif /* _SYSCALL_H */

