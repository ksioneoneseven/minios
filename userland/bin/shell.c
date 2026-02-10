/*
 * MiniOS Userland Shell
 * 
 * This is the first REAL user mode shell - running in ring 3.
 * This marks the transition to Stage 3: Architectural Legitimacy.
 */

#include "../libc/include/stdio.h"
#include "../libc/include/stdlib.h"
#include "../libc/include/unistd.h"
#include "../libc/include/string.h"

#define MAX_INPUT 256
#define MAX_ARGS  16

/* Built-in command handlers */
static int cmd_help(int argc, char* argv[]);
static int cmd_echo(int argc, char* argv[]);
static int cmd_exit(int argc, char* argv[]);
static int cmd_pid(int argc, char* argv[]);
static int cmd_clear(int argc, char* argv[]);
static int cmd_ls(int argc, char* argv[]);
static int cmd_cat(int argc, char* argv[]);
static int cmd_ps(int argc, char* argv[]);
static int cmd_pwd(int argc, char* argv[]);
static int cmd_cd(int argc, char* argv[]);
static int cmd_whoami(int argc, char* argv[]);
static int cmd_uptime(int argc, char* argv[]);
static int cmd_uname(int argc, char* argv[]);
static int cmd_mkdir(int argc, char* argv[]);
static int cmd_rm(int argc, char* argv[]);
static int cmd_touch(int argc, char* argv[]);
static int cmd_sleep(int argc, char* argv[]);
static int cmd_date(int argc, char* argv[]);
static int cmd_free(int argc, char* argv[]);
static int cmd_version(int argc, char* argv[]);

/* Command table */
typedef struct {
    const char* name;
    int (*handler)(int argc, char* argv[]);
    const char* help;
} command_t;

static command_t commands[] = {
    {"help",    cmd_help,    "Display available commands"},
    {"echo",    cmd_echo,    "Echo arguments to screen"},
    {"exit",    cmd_exit,    "Exit the shell"},
    {"pid",     cmd_pid,     "Display current process ID"},
    {"clear",   cmd_clear,   "Clear the screen"},
    {"ls",      cmd_ls,      "List directory contents"},
    {"cat",     cmd_cat,     "Display file contents"},
    {"ps",      cmd_ps,      "List running processes"},
    {"pwd",     cmd_pwd,     "Print working directory"},
    {"cd",      cmd_cd,      "Change directory"},
    {"whoami",  cmd_whoami,  "Display current user"},
    {"uptime",  cmd_uptime,  "Show system uptime"},
    {"uname",   cmd_uname,   "Display system info"},
    {"mkdir",   cmd_mkdir,   "Create directory"},
    {"rm",      cmd_rm,      "Remove file"},
    {"touch",   cmd_touch,   "Create empty file"},
    {"sleep",   cmd_sleep,   "Sleep for seconds"},
    {"date",    cmd_date,    "Show date/time"},
    {"free",    cmd_free,    "Show memory info"},
    {"version", cmd_version, "Show version"},
    {NULL,      NULL,        NULL}
};

/* Clear screen by printing newlines */
static int cmd_clear(int argc, char* argv[]) {
    (void)argc; (void)argv;
    for (int i = 0; i < 25; i++) printf("\n");
    return 0;
}

static int cmd_help(int argc, char* argv[]) {
    (void)argc; (void)argv;
    printf("MiniOS Userland Shell - Built-in Commands:\n");
    printf("-------------------------------------------\n");
    for (int i = 0; commands[i].name != NULL; i++) {
        printf("  %-10s - %s\n", commands[i].name, commands[i].help);
    }
    return 0;
}

static int cmd_echo(int argc, char* argv[]) {
    for (int i = 1; i < argc; i++) {
        printf("%s", argv[i]);
        if (i < argc - 1) printf(" ");
    }
    printf("\n");
    return 0;
}

static int cmd_exit(int argc, char* argv[]) {
    (void)argc; (void)argv;
    printf("Goodbye!\n");
    exit(0);
    return 0;
}

static int cmd_pid(int argc, char* argv[]) {
    (void)argc; (void)argv;
    printf("PID: %d\n", getpid());
    return 0;
}

static int cmd_ls(int argc, char* argv[]) {
    const char* path = (argc > 1) ? argv[1] : ".";
    dirent_t entries[8];  /* Reduced to avoid stack overflow */

    int count = readdir(path, entries, 8);
    if (count < 0) {
        printf("ls: cannot access '%s'\n", path);
        return -1;
    }

    for (int i = 0; i < count; i++) {
        printf("%s\n", entries[i].name);
    }
    return 0;
}

static int cmd_cat(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: cat <file>\n");
        return -1;
    }

    int fd = open(argv[1], 0);
    if (fd < 0) {
        printf("cat: %s: No such file\n", argv[1]);
        return -1;
    }

    /* Read and print file contents */
    char buf[256];
    int n;
    while ((n = read(fd, buf, sizeof(buf) - 1)) > 0) {
        buf[n] = '\0';
        printf("%s", buf);
    }

    close(fd);
    return 0;
}

static int cmd_ps(int argc, char* argv[]) {
    (void)argc; (void)argv;
    procinfo_t procs[8];  /* Reduced to avoid stack issues */

    int count = ps(procs, 8);
    if (count < 0) {
        printf("ps: error\n");
        return -1;
    }

    printf("PID   STATE  NAME\n");
    printf("---   -----  ----\n");
    for (int i = 0; i < count; i++) {
        const char* state;
        switch (procs[i].state) {
            case 0: state = "UNUSED"; break;
            case 1: state = "READY "; break;
            case 2: state = "RUN   "; break;
            case 3: state = "BLOCK "; break;
            case 4: state = "ZOMBIE"; break;
            default: state = "?     "; break;
        }
        printf("%-5d %s %s\n", procs[i].pid, state, procs[i].name);
    }
    return 0;
}

static int cmd_pwd(int argc, char* argv[]) {
    (void)argc; (void)argv;
    char buf[128];

    if (getcwd(buf, sizeof(buf)) == 0) {
        printf("%s\n", buf);
    } else {
        printf("pwd: error\n");
    }
    return 0;
}

static int cmd_cd(int argc, char* argv[]) {
    const char* path = (argc > 1) ? argv[1] : "/";

    if (chdir(path) < 0) {
        printf("cd: %s: No such directory\n", path);
        return -1;
    }
    return 0;
}

static int cmd_whoami(int argc, char* argv[]) {
    (void)argc; (void)argv;
    unsigned int uid = getuid();

    if (uid == 0) {
        printf("root\n");
    } else {
        printf("user%d\n", uid);
    }
    return 0;
}

static int cmd_uptime(int argc, char* argv[]) {
    (void)argc; (void)argv;
    unsigned int ticks = uptime();
    unsigned int seconds = ticks / 100;  /* Assuming 100Hz timer */
    unsigned int minutes = seconds / 60;
    seconds = seconds % 60;

    printf("up %d min, %d sec (%d ticks)\n", minutes, seconds, ticks);
    return 0;
}

static int cmd_uname(int argc, char* argv[]) {
    (void)argc; (void)argv;
    utsname_t info;

    if (uname(&info) == 0) {
        printf("%s %s %s %s %s\n",
               info.sysname, info.nodename, info.release,
               info.version, info.machine);
    } else {
        printf("uname: error\n");
    }
    return 0;
}

static int cmd_mkdir(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: mkdir <directory>\n");
        return -1;
    }
    if (mkdir(argv[1]) < 0) {
        printf("mkdir: failed to create '%s'\n", argv[1]);
        return -1;
    }
    return 0;
}

static int cmd_rm(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: rm <file>\n");
        return -1;
    }
    if (unlink(argv[1]) < 0) {
        printf("rm: cannot remove '%s'\n", argv[1]);
        return -1;
    }
    return 0;
}

static int cmd_touch(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: touch <file>\n");
        return -1;
    }
    int fd = open(argv[1], 1);  /* O_CREAT */
    if (fd < 0) {
        printf("touch: cannot create '%s'\n", argv[1]);
        return -1;
    }
    close(fd);
    return 0;
}

static int cmd_sleep(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: sleep <seconds>\n");
        return -1;
    }
    int seconds = atoi(argv[1]);
    sleep_ms(seconds * 1000);
    return 0;
}

static int cmd_date(int argc, char* argv[]) {
    (void)argc; (void)argv;
    datetime_t dt;
    if (getdate(&dt) == 0) {
        printf("%04d-%02d-%02d %02d:%02d:%02d\n",
               dt.year, dt.month, dt.day,
               dt.hour, dt.minute, dt.second);
    }
    return 0;
}

static int cmd_free(int argc, char* argv[]) {
    (void)argc; (void)argv;
    meminfo_t mem;
    if (meminfo(&mem) == 0) {
        printf("Total:  %d KB\n", mem.total / 1024);
        printf("Used:   %d KB\n", mem.used / 1024);
        printf("Free:   %d KB\n", mem.free / 1024);
        printf("Heap:   %d KB\n", mem.heap_used / 1024);
    }
    return 0;
}

static int cmd_version(int argc, char* argv[]) {
    (void)argc; (void)argv;
    printf("MiniOS version 0.3.0 (Stage 3: Architectural Legitimacy)\n");
    printf("Built for i686 architecture\n");
    return 0;
}

/* Parse input into argv array */
static int parse_line(char* line, char* argv[]) {
    int argc = 0;
    char* p = line;
    
    while (*p && argc < MAX_ARGS - 1) {
        /* Skip whitespace */
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0' || *p == '\n') break;
        
        /* Start of argument */
        argv[argc++] = p;
        
        /* Find end of argument */
        while (*p && *p != ' ' && *p != '\t' && *p != '\n') p++;
        
        if (*p) {
            *p++ = '\0';
        }
    }
    argv[argc] = NULL;
    return argc;
}

/* Read a line of input with echo */
static int read_line(char* buf, int max) {
    int i = 0;
    int c;

    while (i < max - 1) {
        c = getchar();
        if (c == EOF) return -1;

        /* Handle special keys for scrollback */
        unsigned char uc = (unsigned char)c;
        if (uc == KEY_PAGEUP) {
            scroll(0, 24);  /* Scroll up one page */
            continue;
        } else if (uc == KEY_PAGEDOWN) {
            scroll(1, 24);  /* Scroll down one page */
            continue;
        } else if (uc == KEY_UP) {
            scroll(0, 1);   /* Scroll up one line */
            continue;
        } else if (uc == KEY_DOWN) {
            scroll(1, 1);   /* Scroll down one line */
            continue;
        } else if (uc == KEY_HOME) {
            scroll(0, 1000); /* Scroll to top */
            continue;
        } else if (uc == KEY_END) {
            scroll(2, 0);    /* Scroll to bottom */
            continue;
        }

        if (c == '\n' || c == '\r') {
            putchar('\n');  /* Echo newline */
            buf[i] = '\0';
            return i;
        }
        if (c == '\b' || c == 127) {
            if (i > 0) {
                i--;
                /* Erase character on screen: backspace, space, backspace */
                write(STDOUT_FILENO, "\b \b", 3);
            }
            continue;
        }
        /* Echo the character */
        putchar(c);
        buf[i++] = c;
    }
    buf[i] = '\0';
    return i;
}

/* Main shell loop */
int main(void) {
    char input[MAX_INPUT];
    char* argv[MAX_ARGS];
    int argc;
    
    printf("\n");
    printf("===========================================\n");
    printf("  MiniOS Userland Shell (Ring 3)\n");
    printf("  Type 'help' for available commands\n");
    printf("===========================================\n\n");
    
    while (1) {
        printf("user$ ");
        
        if (read_line(input, MAX_INPUT) < 0) {
            break;
        }
        
        if (input[0] == '\0') continue;
        
        argc = parse_line(input, argv);
        if (argc == 0) continue;
        
        /* Find and execute command */
        int found = 0;
        for (int i = 0; commands[i].name != NULL; i++) {
            if (strcmp(argv[0], commands[i].name) == 0) {
                commands[i].handler(argc, argv);
                found = 1;
                break;
            }
        }

        if (!found) {
            printf("Unknown command: %s\n", argv[0]);
        }
    }
    
    return 0;
}

