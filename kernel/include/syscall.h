/*
 * MiniOS System Call Header
 * 
 * Defines system call numbers and interface.
 * System calls use INT 0x80 with:
 *   - EAX: syscall number
 *   - EBX, ECX, EDX, ESI, EDI: arguments
 *   - EAX: return value
 */

#ifndef _SYSCALL_H
#define _SYSCALL_H

#include "types.h"

/* System call numbers */
#define SYS_EXIT        0   /* void exit(int status) */
#define SYS_WRITE       1   /* ssize_t write(int fd, const void* buf, size_t count) */
#define SYS_READ        2   /* ssize_t read(int fd, void* buf, size_t count) */
#define SYS_GETPID      3   /* pid_t getpid(void) */
#define SYS_FORK        4   /* pid_t fork(void) */
#define SYS_EXEC        5   /* int exec(const char* path) */
#define SYS_WAIT        6   /* pid_t wait(int* status) */
#define SYS_YIELD       7   /* void yield(void) */
#define SYS_SLEEP       8   /* void sleep(uint32_t ms) */
#define SYS_OPEN        9   /* int open(const char* path, int flags) */
#define SYS_CLOSE       10  /* int close(int fd) */
#define SYS_SBRK        11  /* void* sbrk(intptr_t increment) */
#define SYS_READDIR     12  /* int readdir(const char* path, void* buf, size_t count) */
#define SYS_STAT        13  /* int stat(const char* path, void* buf) */
#define SYS_CHDIR       14  /* int chdir(const char* path) */
#define SYS_GETCWD      15  /* int getcwd(char* buf, size_t size) */
#define SYS_GETUID      16  /* uid_t getuid(void) */
#define SYS_PS          17  /* int ps(void* buf, size_t count) */
#define SYS_UPTIME      18  /* uint32_t uptime(void) */
#define SYS_UNAME       19  /* int uname(void* buf) */
#define SYS_SCROLL      20  /* int scroll(int direction, int lines) */
#define SYS_MKDIR       21  /* int mkdir(const char* path) */
#define SYS_UNLINK      22  /* int unlink(const char* path) */
#define SYS_FREAD       23  /* int fread(int fd, void* buf, size_t count) - read from file fd */
#define SYS_FWRITE      24  /* int fwrite(int fd, const void* buf, size_t count) - write to file fd */
#define SYS_MEMINFO     25  /* int meminfo(void* buf) - get memory info */
#define SYS_DATE        26  /* int date(void* buf) - get date/time */
#define SYS_PIPE        27  /* int pipe(int pipefd[2]) - create pipe */
#define SYS_DUP2        28  /* int dup2(int oldfd, int newfd) - duplicate fd */
#define SYS_KILL        29  /* int kill(pid_t pid, int sig) - send signal to process */
#define SYS_SIGNAL      30  /* sighandler_t signal(int signum, sighandler_t handler) - set signal handler */

#define NUM_SYSCALLS    31

/* Standard file descriptors */
#define STDIN_FILENO    0
#define STDOUT_FILENO   1
#define STDERR_FILENO   2

/* System call handler type */
typedef int32_t (*syscall_handler_t)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);

/*
 * Initialize system call interface
 * Sets up INT 0x80 handler
 */
void syscall_init(void);

/*
 * Register a system call handler
 */
void syscall_register(uint32_t num, syscall_handler_t handler);

/*
 * System call implementations (in syscalls.c)
 */
int32_t sys_exit(uint32_t status, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sys_write(uint32_t fd, uint32_t buf, uint32_t count, uint32_t, uint32_t);
int32_t sys_read(uint32_t fd, uint32_t buf, uint32_t count, uint32_t, uint32_t);
int32_t sys_getpid(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sys_fork(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sys_exec(uint32_t path, uint32_t argv, uint32_t, uint32_t, uint32_t);
int32_t sys_wait(uint32_t status_ptr, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sys_yield(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sys_sleep(uint32_t ms, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sys_open(uint32_t path, uint32_t flags, uint32_t mode, uint32_t, uint32_t);
int32_t sys_close(uint32_t fd, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sys_sbrk(uint32_t increment, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sys_pipe(uint32_t pipefd_ptr, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sys_dup2(uint32_t oldfd, uint32_t newfd, uint32_t, uint32_t, uint32_t);
int32_t sys_kill(uint32_t pid, uint32_t sig, uint32_t, uint32_t, uint32_t);
int32_t sys_signal(uint32_t signum, uint32_t handler, uint32_t, uint32_t, uint32_t);

#endif /* _SYSCALL_H */

