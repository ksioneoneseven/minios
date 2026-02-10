/*
 * MiniOS Shell Implementation
 * 
 * Simple command-line shell with built-in commands.
 */

#include "../include/shell.h"
#include "../include/vga.h"
#include "../include/keyboard.h"
#include "../include/string.h"
#include "../include/stdio.h"
#include "../include/vfs.h"
#include "../include/ramfs.h"
#include "../include/process.h"
#include "../include/heap.h"
#include "../include/rtc.h"
#include "../include/loader.h"
#include "../include/user.h"
#include "../include/daemon.h"
#include "../include/spreadsheet.h"
#include "../include/nano.h"
#include "../include/basic.h"
#include "../include/xgui/xgui.h"
#include "../include/ata.h"
#include "../include/blockdev.h"
#include "../include/ext2.h"
#include "../include/timer.h"

/* Maximum number of registered commands */
#define MAX_COMMANDS 90

/* Command history */
#define MAX_HISTORY 16
static char history[MAX_HISTORY][SHELL_MAX_CMD_LEN];
static int history_count = 0;
static int history_index = 0;

/* Command table */
static shell_command_t commands[MAX_COMMANDS];
static int num_commands = 0;

/* Input buffer */
static char input_buffer[SHELL_MAX_CMD_LEN];
static int input_len = 0;    /* Length of input string */
static int cursor_pos = 0;   /* Cursor position within input */

/* Current working directory (not static - shared with syscalls) */
char current_dir[VFS_MAX_PATH] = "/";
vfs_node_t* current_dir_node = NULL;

/* Pipeline support */
#define PIPE_BUFFER_SIZE 8192
static char pipe_output_buffer[PIPE_BUFFER_SIZE];
static int pipe_output_pos = 0;
static bool pipe_capture_mode = false;

static char pipe_input_buffer[PIPE_BUFFER_SIZE];
static int pipe_input_pos = 0;
static int pipe_input_len = 0;
static bool pipe_input_mode = false;

/*
 * Write to pipe output buffer (used when capturing command output)
 */
static void pipe_write_output(const char* str) {
    while (*str && pipe_output_pos < PIPE_BUFFER_SIZE - 1) {
        pipe_output_buffer[pipe_output_pos++] = *str++;
    }
    pipe_output_buffer[pipe_output_pos] = '\0';
}

/*
 * Check if shell is in pipe capture mode
 */
bool shell_is_pipe_mode(void) {
    return pipe_capture_mode;
}

/*
 * Shell output function - writes to VGA or pipe buffer
 */
void shell_output(const char* str) {
    if (pipe_capture_mode) {
        pipe_write_output(str);
    } else {
        vga_puts(str);
    }
}

/*
 * Helper function to resolve a path (relative or absolute) to a full path
 */
static void resolve_path(const char* input, char* output, int output_size) {
    if (input[0] == '/') {
        /* Absolute path */
        strncpy(output, input, output_size - 1);
        output[output_size - 1] = '\0';
    } else {
        /* Relative path */
        if (strcmp(current_dir, "/") == 0) {
            snprintf(output, output_size, "/%s", input);
        } else {
            snprintf(output, output_size, "%s/%s", current_dir, input);
        }
    }
}

/*
 * Helper function to find a file in current directory or by path
 */
static vfs_node_t* find_file(const char* name) {
    char path[VFS_MAX_PATH];
    resolve_path(name, path, VFS_MAX_PATH);
    return vfs_lookup(path);
}

/* Forward declaration for shell_prompt */
static void shell_prompt(void);

/*
 * Tab completion helper - find common prefix among matches
 */
static int find_common_prefix(const char* matches[], int count, char* prefix, int max_len) {
    if (count == 0) return 0;
    strncpy(prefix, matches[0], max_len - 1);
    prefix[max_len - 1] = '\0';
    
    for (int i = 1; i < count; i++) {
        int j = 0;
        while (prefix[j] && matches[i][j] && prefix[j] == matches[i][j]) j++;
        prefix[j] = '\0';
    }
    return strlen(prefix);
}

/*
 * Tab completion - complete commands or file paths
 */
static void shell_tab_complete(void) {
    input_buffer[input_len] = '\0';
    
    /* Find the start of the current word */
    int word_start = cursor_pos;
    while (word_start > 0 && input_buffer[word_start - 1] != ' ') {
        word_start--;
    }
    
    char* partial = &input_buffer[word_start];
    int partial_len = cursor_pos - word_start;
    
    /* Check if this is the first word (command) or a path argument */
    int is_command = (word_start == 0);
    
    const char* matches[32];
    int match_count = 0;
    
    if (is_command) {
        /* Complete command names */
        for (int i = 0; i < num_commands && match_count < 32; i++) {
            if (strncmp(commands[i].name, partial, partial_len) == 0) {
                matches[match_count++] = commands[i].name;
            }
        }
    } else {
        /* Complete file/directory paths */
        /* Determine directory to search and prefix to match */
        char dir_path[VFS_MAX_PATH];
        const char* file_prefix;
        int prefix_len;
        
        /* Find last slash in partial */
        const char* last_slash = NULL;
        for (const char* p = partial; *p; p++) {
            if (*p == '/') last_slash = p;
        }
        
        if (last_slash) {
            /* Path contains directory component */
            int dir_len = last_slash - partial + 1;
            strncpy(dir_path, partial, dir_len);
            dir_path[dir_len] = '\0';
            file_prefix = last_slash + 1;
            prefix_len = partial_len - dir_len;
        } else {
            /* Just a filename in current directory */
            strcpy(dir_path, current_dir);
            file_prefix = partial;
            prefix_len = partial_len;
        }
        
        /* Resolve the directory path */
        char resolved_dir[VFS_MAX_PATH];
        resolve_path(dir_path, resolved_dir, VFS_MAX_PATH);
        vfs_node_t* dir_node = vfs_lookup(resolved_dir);
        
        if (dir_node && (dir_node->flags & VFS_DIRECTORY)) {
            static char match_names[32][VFS_MAX_NAME];
            uint32_t idx = 0;
            dirent_t* entry;
            
            while ((entry = vfs_readdir(dir_node, idx++)) != NULL && match_count < 32) {
                if (strncmp(entry->name, file_prefix, prefix_len) == 0) {
                    strncpy(match_names[match_count], entry->name, VFS_MAX_NAME - 1);
                    match_names[match_count][VFS_MAX_NAME - 1] = '\0';
                    matches[match_count] = match_names[match_count];
                    match_count++;
                }
            }
        }
    }
    
    if (match_count == 0) {
        /* No matches - beep or do nothing */
        return;
    } else if (match_count == 1) {
        /* Single match - complete it */
        const char* completion = matches[0] + partial_len;
        while (*completion && input_len < SHELL_MAX_CMD_LEN - 2) {
            input_buffer[input_len++] = *completion;
            cursor_pos++;
            vga_putchar(*completion);
            completion++;
        }
        /* Add trailing slash for directories, space for commands/files */
        if (is_command) {
            if (input_len < SHELL_MAX_CMD_LEN - 1) {
                input_buffer[input_len++] = ' ';
                cursor_pos++;
                vga_putchar(' ');
            }
        } else {
            /* Check if it's a directory */
            char full_path[VFS_MAX_PATH];
            input_buffer[input_len] = '\0';
            resolve_path(&input_buffer[word_start], full_path, VFS_MAX_PATH);
            vfs_node_t* node = vfs_lookup(full_path);
            if (node && (node->flags & VFS_DIRECTORY)) {
                if (input_len < SHELL_MAX_CMD_LEN - 1) {
                    input_buffer[input_len++] = '/';
                    cursor_pos++;
                    vga_putchar('/');
                }
            } else {
                if (input_len < SHELL_MAX_CMD_LEN - 1) {
                    input_buffer[input_len++] = ' ';
                    cursor_pos++;
                    vga_putchar(' ');
                }
            }
        }
    } else {
        /* Multiple matches - complete common prefix and show options */
        char common[VFS_MAX_NAME];
        int common_len = find_common_prefix(matches, match_count, common, VFS_MAX_NAME);
        
        if (common_len > partial_len) {
            /* Complete the common prefix */
            const char* completion = common + partial_len;
            while (*completion && input_len < SHELL_MAX_CMD_LEN - 1) {
                input_buffer[input_len++] = *completion;
                cursor_pos++;
                vga_putchar(*completion);
                completion++;
            }
        } else {
            /* Show all matches */
            vga_putchar('\n');
            for (int i = 0; i < match_count; i++) {
                vga_puts(matches[i]);
                vga_puts("  ");
            }
            vga_putchar('\n');
            shell_prompt();
            /* Redisplay current input */
            input_buffer[input_len] = '\0';
            vga_puts(input_buffer);
        }
    }
}

/* Forward declarations for built-in commands */
/* Original commands */
static int cmd_help(int argc, char* argv[]);
static int cmd_clear(int argc, char* argv[]);
static int cmd_echo(int argc, char* argv[]);
static int cmd_ls(int argc, char* argv[]);
static int cmd_cat(int argc, char* argv[]);
static int cmd_ps(int argc, char* argv[]);
static int cmd_touch(int argc, char* argv[]);
static int cmd_write(int argc, char* argv[]);
static int cmd_run(int argc, char* argv[]);
static int cmd_progs(int argc, char* argv[]);
static int cmd_mem(int argc, char* argv[]);
static int cmd_uptime(int argc, char* argv[]);
static int cmd_date(int argc, char* argv[]);
static int cmd_uname(int argc, char* argv[]);
static int cmd_kill(int argc, char* argv[]);
static int cmd_reboot(int argc, char* argv[]);
/* 10.1: File System Commands */
static int cmd_pwd(int argc, char* argv[]);
static int cmd_cd(int argc, char* argv[]);
static int cmd_mkdir(int argc, char* argv[]);
static int cmd_rm(int argc, char* argv[]);
static int cmd_rmdir(int argc, char* argv[]);
static int cmd_cp(int argc, char* argv[]);
static int cmd_mv(int argc, char* argv[]);
static int cmd_stat(int argc, char* argv[]);
static int cmd_head(int argc, char* argv[]);
static int cmd_tail(int argc, char* argv[]);
static int cmd_wc(int argc, char* argv[]);
static int cmd_mount(int argc, char* argv[]);
/* 10.2: Process Commands */
static int cmd_sleep(int argc, char* argv[]);
static int cmd_top(int argc, char* argv[]);
static int cmd_nice(int argc, char* argv[]);
/* 10.3: System Information Commands */
static int cmd_free(int argc, char* argv[]);
static int cmd_whoami(int argc, char* argv[]);
static int cmd_hostname(int argc, char* argv[]);
static int cmd_dmesg(int argc, char* argv[]);
static int cmd_interrupts(int argc, char* argv[]);
static int cmd_lscpu(int argc, char* argv[]);
static int cmd_diskmgmt(int argc, char* argv[]);
/* 10.4: Text/Data Commands */
static int cmd_hexdump(int argc, char* argv[]);
static int cmd_xxd(int argc, char* argv[]);
static int cmd_strings(int argc, char* argv[]);
static int cmd_grep(int argc, char* argv[]);
static int cmd_diff(int argc, char* argv[]);
/* 10.5: Shell Built-ins */
static int cmd_history(int argc, char* argv[]);
static int cmd_env(int argc, char* argv[]);
static int cmd_alias(int argc, char* argv[]);
static int cmd_export(int argc, char* argv[]);
static int cmd_set(int argc, char* argv[]);
/* 10.6: Debugging/Development Commands */
static int cmd_peek(int argc, char* argv[]);
static int cmd_poke(int argc, char* argv[]);
static int cmd_dump(int argc, char* argv[]);
static int cmd_heap(int argc, char* argv[]);
static int cmd_regs(int argc, char* argv[]);
static int cmd_gdt(int argc, char* argv[]);
static int cmd_idt(int argc, char* argv[]);
static int cmd_pages(int argc, char* argv[]);
static int cmd_stack(int argc, char* argv[]);
/* 10.7: Fun/Demo Commands */
static int cmd_color(int argc, char* argv[]);
static int cmd_version(int argc, char* argv[]);
static int cmd_about(int argc, char* argv[]);
static int cmd_credits(int argc, char* argv[]);
static int cmd_beep(int argc, char* argv[]);
static int cmd_banner(int argc, char* argv[]);
static int cmd_fortune(int argc, char* argv[]);
/* User/Permissions Commands */
static int cmd_login(int argc, char* argv[]);
static int cmd_logout(int argc, char* argv[]);
static int cmd_su(int argc, char* argv[]);
static int cmd_passwd(int argc, char* argv[]);
static int cmd_useradd(int argc, char* argv[]);
static int cmd_userdel(int argc, char* argv[]);
static int cmd_groupadd(int argc, char* argv[]);
static int cmd_groups(int argc, char* argv[]);
static int cmd_chmod(int argc, char* argv[]);
static int cmd_chown(int argc, char* argv[]);
static int cmd_id(int argc, char* argv[]);
/* Documentation */
static int cmd_man(int argc, char* argv[]);
/* Daemon Commands */
static int cmd_daemons(int argc, char* argv[]);
static int cmd_service(int argc, char* argv[]);
/* Applications */
static int cmd_spreadsheet(int argc, char* argv[]);
static int cmd_nano(int argc, char* argv[]);
static int cmd_basic(int argc, char* argv[]);
static int cmd_xgui(int argc, char* argv[]);
/* New Commands */
static int cmd_which(int argc, char* argv[]);
static int cmd_df(int argc, char* argv[]);
static int cmd_find(int argc, char* argv[]);
static int cmd_sort(int argc, char* argv[]);
static int cmd_time(int argc, char* argv[]);
static int cmd_type(int argc, char* argv[]);
static int cmd_seq(int argc, char* argv[]);
static int cmd_rev(int argc, char* argv[]);
static int cmd_sandbox(int argc, char* argv[]);

/*
 * Print the shell prompt
 */
static void shell_prompt(void) {
    vga_write(SHELL_PROMPT, vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
}

/*
 * Parse input into argc/argv
 */
static int parse_command(char* input, char* argv[], int max_args) {
    int argc = 0;
    char* p = input;
    
    while (*p && argc < max_args) {
        /* Skip whitespace */
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0') break;
        
        /* Start of argument */
        argv[argc++] = p;
        
        /* Find end of argument */
        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p) *p++ = '\0';
    }
    
    return argc;
}

/*
 * Find and execute a command
 */
static int execute_command(int argc, char* argv[]) {
    if (argc == 0) return 0;
    
    for (int i = 0; i < num_commands; i++) {
        if (strcmp(argv[0], commands[i].name) == 0) {
            return commands[i].handler(argc, argv);
        }
    }
    
    vga_write("Unknown command: ", vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
    vga_puts(argv[0]);
    vga_puts("\nType 'help' for available commands.\n");
    return -1;
}

/*
 * Execute a shell command string (public API for GUI terminal)
 */
int shell_execute_command(const char* cmdline) {
    char cmd_copy[SHELL_MAX_CMD_LEN];
    strncpy(cmd_copy, cmdline, SHELL_MAX_CMD_LEN - 1);
    cmd_copy[SHELL_MAX_CMD_LEN - 1] = '\0';

    char* argv[SHELL_MAX_ARGS];
    int argc = parse_command(cmd_copy, argv, SHELL_MAX_ARGS);
    return execute_command(argc, argv);
}

/*
 * Check if a string contains a pipe character (not in quotes)
 */
static char* find_pipe_char(char* str) {
    bool in_single_quote = false;
    bool in_double_quote = false;

    while (*str) {
        if (*str == '\'' && !in_double_quote) {
            in_single_quote = !in_single_quote;
        } else if (*str == '"' && !in_single_quote) {
            in_double_quote = !in_double_quote;
        } else if (*str == '|' && !in_single_quote && !in_double_quote) {
            return str;
        }
        str++;
    }
    return NULL;
}

/*
 * Execute a pipeline of commands separated by |
 */
static void execute_pipeline(char* cmdline) {
    char* commands_str[8];  /* Max 8 commands in pipeline */
    int num_cmds = 0;

    /* Split by pipe character */
    char* current = cmdline;
    char* pipe_pos;

    while ((pipe_pos = find_pipe_char(current)) != NULL && num_cmds < 7) {
        *pipe_pos = '\0';
        commands_str[num_cmds++] = current;
        current = pipe_pos + 1;
        /* Skip whitespace */
        while (*current == ' ' || *current == '\t') current++;
    }
    commands_str[num_cmds++] = current;

    if (num_cmds == 1) {
        /* No pipes - execute normally */
        char* argv[SHELL_MAX_ARGS];
        int argc = parse_command(commands_str[0], argv, SHELL_MAX_ARGS);
        execute_command(argc, argv);
        return;
    }

    /* Execute pipeline */
    pipe_output_pos = 0;
    pipe_output_buffer[0] = '\0';
    pipe_input_pos = 0;
    pipe_input_len = 0;

    for (int i = 0; i < num_cmds; i++) {
        char cmd_copy[SHELL_MAX_CMD_LEN];
        strncpy(cmd_copy, commands_str[i], SHELL_MAX_CMD_LEN - 1);
        cmd_copy[SHELL_MAX_CMD_LEN - 1] = '\0';

        /* Trim leading/trailing whitespace */
        char* cmd = cmd_copy;
        while (*cmd == ' ' || *cmd == '\t') cmd++;
        int len = strlen(cmd);
        while (len > 0 && (cmd[len-1] == ' ' || cmd[len-1] == '\t')) {
            cmd[--len] = '\0';
        }

        if (strlen(cmd) == 0) continue;

        /* Set up input from previous command's output */
        if (i > 0) {
            memcpy(pipe_input_buffer, pipe_output_buffer, pipe_output_pos + 1);
            pipe_input_len = pipe_output_pos;
            pipe_input_pos = 0;
            pipe_input_mode = true;
        }

        /* Capture output (except for last command) */
        if (i < num_cmds - 1) {
            pipe_capture_mode = true;
            pipe_output_pos = 0;
            pipe_output_buffer[0] = '\0';
        } else {
            pipe_capture_mode = false;
        }

        /* Parse and execute command */
        char* argv[SHELL_MAX_ARGS];
        int argc = parse_command(cmd, argv, SHELL_MAX_ARGS);
        execute_command(argc, argv);
    }

    /* Reset pipe state */
    pipe_capture_mode = false;
    pipe_input_mode = false;
}

/*
 * Process a complete command line
 */
static void process_input(void) {
    input_buffer[input_len] = '\0';

    if (input_len > 0) {
        /* Save to history */
        if (history_count < MAX_HISTORY) {
            strncpy(history[history_count], input_buffer, SHELL_MAX_CMD_LEN - 1);
            history[history_count][SHELL_MAX_CMD_LEN - 1] = '\0';
            history_count++;
        } else {
            /* Shift history up */
            for (int i = 0; i < MAX_HISTORY - 1; i++) {
                strncpy(history[i], history[i + 1], SHELL_MAX_CMD_LEN);
            }
            strncpy(history[MAX_HISTORY - 1], input_buffer, SHELL_MAX_CMD_LEN - 1);
        }
        history_index = history_count;

        /* Check for pipeline and execute */
        execute_pipeline(input_buffer);
    }

    input_len = 0;
    cursor_pos = 0;
    shell_prompt();
}

/*
 * Register a shell command
 */
int shell_register_command(const char* name, const char* desc, shell_cmd_handler_t handler) {
    if (num_commands >= MAX_COMMANDS) return -1;
    
    commands[num_commands].name = name;
    commands[num_commands].description = desc;
    commands[num_commands].handler = handler;
    num_commands++;
    
    return 0;
}

static int cmd_mount(int argc, char* argv[]) {
    if (argc < 3) {
        vga_puts("Usage: mount <device> <mountpoint> [ext2]\n");
        return -1;
    }

    const char* dev_name = argv[1];
    const char* mount_path = argv[2];
    const char* fstype = (argc >= 4) ? argv[3] : "ext2";

    if (strcmp(fstype, "ext2") != 0) {
        vga_puts("mount: only ext2 supported right now\n");
        return -1;
    }

    blockdev_t* bdev = blockdev_get_by_name(dev_name);
    if (bdev == NULL) {
        vga_puts("mount: unknown block device\n");
        return -1;
    }

    vfs_node_t* mp = vfs_lookup(mount_path);
    if (mp == NULL) {
        vga_puts("mount: mountpoint not found\n");
        return -1;
    }

    if (!(mp->flags & VFS_DIRECTORY)) {
        vga_puts("mount: mountpoint is not a directory\n");
        return -1;
    }

    vfs_node_t* root = ext2_mount(bdev);
    if (root == NULL) {
        vga_puts("mount: ext2 mount failed\n");
        return -1;
    }

    if (vfs_mount(mount_path, root) < 0) {
        vga_puts("mount: vfs_mount failed\n");
        return -1;
    }

    vga_puts("Mounted ");
    vga_puts(dev_name);
    vga_puts(" on ");
    vga_puts(mount_path);
    vga_puts(" (ext2)\n");
    return 0;
}

/*
 * Initialize the shell
 */
void shell_init(void) {
    num_commands = 0;
    input_len = 0;
    cursor_pos = 0;
    memset(input_buffer, 0, sizeof(input_buffer));

    /* Initialize current working directory */
    strcpy(current_dir, "/");
    current_dir_node = vfs_root;

    /* Register built-in commands */
    shell_register_command("help", "Show available commands", cmd_help);
    shell_register_command("clear", "Clear the screen", cmd_clear);
    shell_register_command("echo", "Print arguments", cmd_echo);
    shell_register_command("ls", "List directory contents", cmd_ls);
    shell_register_command("cat", "Display file contents", cmd_cat);
    shell_register_command("ps", "List processes", cmd_ps);
    shell_register_command("touch", "Create a file", cmd_touch);
    shell_register_command("write", "Write to a file", cmd_write);
    shell_register_command("run", "Run a program", cmd_run);
    shell_register_command("progs", "List available programs", cmd_progs);
    shell_register_command("mem", "Show memory info", cmd_mem);
    shell_register_command("uptime", "Show system uptime", cmd_uptime);
    shell_register_command("date", "Show current date and time", cmd_date);
    shell_register_command("uname", "Show system info", cmd_uname);
    shell_register_command("kill", "Kill a process", cmd_kill);
    shell_register_command("reboot", "Reboot the system", cmd_reboot);

    /* 10.1: File System Commands */
    shell_register_command("pwd", "Print working directory", cmd_pwd);
    shell_register_command("cd", "Change directory", cmd_cd);
    shell_register_command("mkdir", "Create directory", cmd_mkdir);
    shell_register_command("rm", "Remove file", cmd_rm);
    shell_register_command("rmdir", "Remove directory", cmd_rmdir);
    shell_register_command("cp", "Copy file", cmd_cp);
    shell_register_command("mv", "Move/rename file", cmd_mv);
    shell_register_command("stat", "Show file info", cmd_stat);
    shell_register_command("head", "Show first lines", cmd_head);
    shell_register_command("tail", "Show last lines", cmd_tail);
    shell_register_command("wc", "Count lines/words/bytes", cmd_wc);
    shell_register_command("mount", "Mount a filesystem", cmd_mount);

    /* 10.2: Process Commands */
    shell_register_command("sleep", "Sleep for seconds", cmd_sleep);
    shell_register_command("top", "Show process stats", cmd_top);
    shell_register_command("nice", "Show/set priority", cmd_nice);

    /* 10.3: System Information Commands */
    shell_register_command("free", "Show memory details", cmd_free);
    shell_register_command("whoami", "Show current user", cmd_whoami);
    shell_register_command("hostname", "Show hostname", cmd_hostname);
    shell_register_command("dmesg", "Show boot messages", cmd_dmesg);
    shell_register_command("interrupts", "Show IRQ stats", cmd_interrupts);
    shell_register_command("lscpu", "Show CPU info", cmd_lscpu);
    shell_register_command("diskmgmt", "Disk management", cmd_diskmgmt);

    /* 10.4: Text/Data Commands */
    shell_register_command("hexdump", "Hex dump file", cmd_hexdump);
    shell_register_command("xxd", "Hex dump with ASCII", cmd_xxd);
    shell_register_command("strings", "Show printable strings", cmd_strings);
    shell_register_command("grep", "Search for pattern", cmd_grep);
    shell_register_command("diff", "Compare two files", cmd_diff);

    /* 10.5: Shell Built-ins */
    shell_register_command("history", "Show command history", cmd_history);
    shell_register_command("env", "Show environment", cmd_env);
    shell_register_command("alias", "Create alias", cmd_alias);
    shell_register_command("export", "Set env variable", cmd_export);
    shell_register_command("set", "Show/set options", cmd_set);

    /* 10.6: Debugging Commands */
    shell_register_command("peek", "Read memory address", cmd_peek);
    shell_register_command("poke", "Write memory address", cmd_poke);
    shell_register_command("dump", "Dump memory range", cmd_dump);
    shell_register_command("heap", "Show heap stats", cmd_heap);
    shell_register_command("regs", "Show CPU registers", cmd_regs);
    shell_register_command("gdt", "Show GDT entries", cmd_gdt);
    shell_register_command("idt", "Show IDT entries", cmd_idt);
    shell_register_command("pages", "Show page tables", cmd_pages);
    shell_register_command("stack", "Show stack trace", cmd_stack);

    /* 10.7: Fun/Demo Commands */
    shell_register_command("color", "Set text color", cmd_color);
    shell_register_command("version", "Show version", cmd_version);
    shell_register_command("about", "About MiniOS", cmd_about);
    shell_register_command("credits", "Show credits", cmd_credits);
    shell_register_command("beep", "PC speaker beep", cmd_beep);
    shell_register_command("banner", "ASCII art text", cmd_banner);
    shell_register_command("fortune", "Random quote", cmd_fortune);

    /* User/Permissions Commands */
    shell_register_command("login", "Login as user", cmd_login);
    shell_register_command("logout", "Logout to root", cmd_logout);
    shell_register_command("su", "Switch user", cmd_su);
    shell_register_command("passwd", "Change password", cmd_passwd);
    shell_register_command("useradd", "Add new user", cmd_useradd);
    shell_register_command("userdel", "Delete user", cmd_userdel);
    shell_register_command("groupadd", "Add new group", cmd_groupadd);
    shell_register_command("groups", "Show user groups", cmd_groups);
    shell_register_command("chmod", "Change permissions", cmd_chmod);
    shell_register_command("chown", "Change owner", cmd_chown);
    shell_register_command("id", "Show user/group IDs", cmd_id);

    /* Documentation */
    shell_register_command("man", "Show command manual", cmd_man);

    /* Daemon Commands */
    shell_register_command("daemons", "List system daemons", cmd_daemons);
    shell_register_command("service", "Manage services", cmd_service);

    /* Applications */
    shell_register_command("spreadsheet", "Spreadsheet application", cmd_spreadsheet);
    shell_register_command("nano", "Text editor", cmd_nano);
    shell_register_command("basic", "BASIC interpreter", cmd_basic);
    shell_register_command("xgui", "Start graphical interface", cmd_xgui);

    /* New Commands */
    shell_register_command("which", "Locate a command", cmd_which);
    shell_register_command("type", "Show command type", cmd_type);
    shell_register_command("df", "Show filesystem space", cmd_df);
    shell_register_command("find", "Search for files", cmd_find);
    shell_register_command("sort", "Sort lines of text", cmd_sort);
    shell_register_command("time", "Time a command", cmd_time);
    shell_register_command("seq", "Print number sequence", cmd_seq);
    shell_register_command("rev", "Reverse lines", cmd_rev);
    shell_register_command("sandbox", "Run program in Ring 3", cmd_sandbox);

    printk("Shell: Initialized (%d commands)\n", num_commands);
}

/* ============================================
 * Built-in Command Implementations
 * ============================================ */

/*
 * help - Show available commands
 */
static int cmd_help(int argc, char* argv[]) {
    (void)argc; (void)argv;

    vga_puts("Available commands:\n");
    for (int i = 0; i < num_commands; i++) {
        vga_write("  ", vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        vga_write(commands[i].name, vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
        vga_puts(" - ");
        vga_puts(commands[i].description);
        vga_puts("\n");
    }
    return 0;
}

/*
 * clear - Clear the screen
 */
static int cmd_clear(int argc, char* argv[]) {
    (void)argc; (void)argv;
    vga_clear();
    return 0;
}

/*
 * echo - Print arguments
 */
/*
 * echo - Print arguments
 * Flags: -n (no newline), -e (interpret escapes)
 */
static int cmd_echo(int argc, char* argv[]) {
    bool no_newline = false;
    bool interpret_escapes = false;
    int start_arg = 1;

    /* Parse flags */
    while (start_arg < argc && argv[start_arg][0] == '-') {
        bool valid_flag = false;
        for (int j = 1; argv[start_arg][j]; j++) {
            switch (argv[start_arg][j]) {
                case 'n': no_newline = true; valid_flag = true; break;
                case 'e': interpret_escapes = true; valid_flag = true; break;
                case 'E': interpret_escapes = false; valid_flag = true; break;
                default: valid_flag = false; break;
            }
        }
        if (!valid_flag) break;  /* Not a flag, treat as text */
        start_arg++;
    }

    for (int i = start_arg; i < argc; i++) {
        if (i > start_arg) {
            if (pipe_capture_mode) shell_output(" ");
            else vga_putchar(' ');
        }
        if (interpret_escapes) {
            for (const char* p = argv[i]; *p; p++) {
                char c = *p;
                if (*p == '\\' && *(p + 1)) {
                    p++;
                    switch (*p) {
                        case 'n': c = '\n'; break;
                        case 't': c = '\t'; break;
                        case 'r': c = '\r'; break;
                        case '\\': c = '\\'; break;
                        default: c = *p; break;
                    }
                }
                if (pipe_capture_mode) {
                    char s[2] = {c, '\0'};
                    shell_output(s);
                } else {
                    vga_putchar(c);
                }
            }
        } else {
            if (pipe_capture_mode) shell_output(argv[i]);
            else vga_puts(argv[i]);
        }
    }
    if (!no_newline) {
        if (pipe_capture_mode) shell_output("\n");
        else vga_putchar('\n');
    }
    return 0;
}

/*
 * ls - List directory contents
 * Flags: -l (long), -a (all/hidden), -h (human-readable), -1 (one per line), -R (recursive)
 */
static void format_size_human(uint32_t size, char* buf, int buf_size) {
    if (size < 1024) {
        snprintf(buf, buf_size, "%u", size);
    } else if (size < 1024 * 1024) {
        snprintf(buf, buf_size, "%uK", size / 1024);
    } else if (size < 1024 * 1024 * 1024) {
        snprintf(buf, buf_size, "%uM", size / (1024 * 1024));
    } else {
        snprintf(buf, buf_size, "%uG", size / (1024 * 1024 * 1024));
    }
}

static void format_permissions(uint32_t mode, uint32_t flags, char* buf) {
    buf[0] = (flags & VFS_DIRECTORY) ? 'd' : '-';
    buf[1] = (mode & 0400) ? 'r' : '-';
    buf[2] = (mode & 0200) ? 'w' : '-';
    buf[3] = (mode & 0100) ? 'x' : '-';
    buf[4] = (mode & 0040) ? 'r' : '-';
    buf[5] = (mode & 0020) ? 'w' : '-';
    buf[6] = (mode & 0010) ? 'x' : '-';
    buf[7] = (mode & 0004) ? 'r' : '-';
    buf[8] = (mode & 0002) ? 'w' : '-';
    buf[9] = (mode & 0001) ? 'x' : '-';
    buf[10] = '\0';
}

static void ls_print_entry(vfs_node_t* node, const char* name, bool long_format, bool human_readable) {
    char line[256];

    if (long_format) {
        char perms[12];
        char size_str[16];
        format_permissions(node->mode, node->flags, perms);

        if (human_readable) {
            format_size_human(node->length, size_str, sizeof(size_str));
        } else {
            snprintf(size_str, sizeof(size_str), "%u", node->length);
        }

        if (pipe_capture_mode) {
            snprintf(line, sizeof(line), "%s %5u %5u %8s %s\n",
                     perms, node->uid, node->gid, size_str, name);
            shell_output(line);
        } else {
            snprintf(line, sizeof(line), "%s %5u %5u %8s ",
                     perms, node->uid, node->gid, size_str);
            vga_puts(line);
            if (node->flags & VFS_DIRECTORY) {
                vga_write(name, vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
            } else if (node->mode & 0111) {
                vga_write(name, vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
            } else {
                vga_puts(name);
            }
            vga_putchar('\n');
        }
    } else {
        if (pipe_capture_mode) {
            shell_output(name);
            shell_output("\n");
        } else {
            if (node->flags & VFS_DIRECTORY) {
                vga_write(name, vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
            } else {
                vga_puts(name);
            }
        }
    }
}

static void ls_directory(vfs_node_t* dir_node, const char* path, bool long_format,
                         bool show_all, bool human_readable, bool one_per_line, bool recursive);

static void ls_directory(vfs_node_t* dir_node, const char* path, bool long_format,
                         bool show_all, bool human_readable, bool one_per_line, bool recursive) {
    uint32_t idx = 0;
    dirent_t* entry;
    int count = 0;

    while ((entry = vfs_readdir(dir_node, idx++)) != NULL) {
        /* Skip hidden files unless -a */
        if (!show_all && entry->name[0] == '.') {
            continue;
        }

        vfs_node_t* child = vfs_finddir(dir_node, entry->name);
        if (child) {
            ls_print_entry(child, entry->name, long_format, human_readable);
            if (!long_format && !one_per_line && !pipe_capture_mode) {
                vga_puts("  ");
            }
            count++;
        }
    }

    if (!pipe_capture_mode && !long_format && count > 0 && !one_per_line) {
        vga_putchar('\n');
    }
    if (!pipe_capture_mode && count == 0) {
        vga_puts("(empty)\n");
    }

    /* Recursive listing */
    if (recursive) {
        idx = 0;
        while ((entry = vfs_readdir(dir_node, idx++)) != NULL) {
            if (!show_all && entry->name[0] == '.') continue;
            if (strcmp(entry->name, ".") == 0 || strcmp(entry->name, "..") == 0) continue;

            vfs_node_t* child = vfs_finddir(dir_node, entry->name);
            if (child && (child->flags & VFS_DIRECTORY)) {
                char subpath[VFS_MAX_PATH];
                if (strcmp(path, "/") == 0) {
                    snprintf(subpath, sizeof(subpath), "/%s", entry->name);
                } else {
                    snprintf(subpath, sizeof(subpath), "%s/%s", path, entry->name);
                }
                vga_putchar('\n');
                vga_puts(subpath);
                vga_puts(":\n");
                ls_directory(child, subpath, long_format, show_all, human_readable, one_per_line, recursive);
            }
        }
    }
}

static int cmd_ls(int argc, char* argv[]) {
    vfs_node_t* dir_node = current_dir_node;
    bool long_format = false;
    bool show_all = false;
    bool human_readable = false;
    bool one_per_line = pipe_capture_mode;
    bool recursive = false;
    const char* target_path_str = NULL;

    /* Parse arguments */
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            /* Parse flags */
            for (int j = 1; argv[i][j]; j++) {
                switch (argv[i][j]) {
                    case 'l': long_format = true; break;
                    case 'a': show_all = true; break;
                    case 'h': human_readable = true; break;
                    case '1': one_per_line = true; break;
                    case 'R': recursive = true; break;
                    default:
                        printk("ls: invalid option -- '%c'\n", argv[i][j]);
                        return -1;
                }
            }
        } else {
            target_path_str = argv[i];
        }
    }

    /* Resolve target path */
    char target_path[VFS_MAX_PATH];
    if (target_path_str) {
        if (target_path_str[0] == '/') {
            strncpy(target_path, target_path_str, VFS_MAX_PATH - 1);
        } else if (strcmp(current_dir, "/") == 0) {
            snprintf(target_path, VFS_MAX_PATH, "/%s", target_path_str);
        } else {
            snprintf(target_path, VFS_MAX_PATH, "%s/%s", current_dir, target_path_str);
        }
        target_path[VFS_MAX_PATH - 1] = '\0';

        dir_node = vfs_lookup(target_path);
        if (dir_node == NULL) {
            printk("ls: cannot access '%s': No such file or directory\n", target_path_str);
            return -1;
        }
    } else {
        strncpy(target_path, current_dir, VFS_MAX_PATH - 1);
        target_path[VFS_MAX_PATH - 1] = '\0';
    }

    if (dir_node == NULL) {
        vga_puts("No filesystem mounted\n");
        return -1;
    }

    /* If target is a file, just display it */
    if (!(dir_node->flags & VFS_DIRECTORY)) {
        ls_print_entry(dir_node, dir_node->name, long_format, human_readable);
        if (!long_format && !pipe_capture_mode) vga_putchar('\n');
        return 0;
    }

    ls_directory(dir_node, target_path, long_format, show_all, human_readable, one_per_line, recursive);
    return 0;
}

/*
 * cat - Display file contents
 * Flags: -n (line numbers), -b (number non-empty lines only)
 */
static void cat_output_with_lines(const char* content, bool number_lines, bool number_nonempty) {
    int line_num = 1;
    bool at_line_start = true;
    char num_buf[16];

    for (const char* p = content; *p; p++) {
        if (at_line_start && (number_lines || (number_nonempty && *p != '\n'))) {
            snprintf(num_buf, sizeof(num_buf), "%6d  ", line_num++);
            if (pipe_capture_mode) {
                shell_output(num_buf);
            } else {
                vga_puts(num_buf);
            }
        }
        at_line_start = false;

        if (pipe_capture_mode) {
            char c[2] = {*p, '\0'};
            shell_output(c);
        } else {
            vga_putchar(*p);
        }

        if (*p == '\n') {
            at_line_start = true;
        }
    }
}

static int cmd_cat(int argc, char* argv[]) {
    bool number_lines = false;
    bool number_nonempty = false;
    const char* filename = NULL;

    /* Parse arguments */
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            for (int j = 1; argv[i][j]; j++) {
                switch (argv[i][j]) {
                    case 'n': number_lines = true; break;
                    case 'b': number_nonempty = true; number_lines = false; break;
                    default:
                        printk("cat: invalid option -- '%c'\n", argv[i][j]);
                        return -1;
                }
            }
        } else {
            filename = argv[i];
        }
    }

    /* Check if reading from pipe */
    if (!filename) {
        if (pipe_input_mode && pipe_input_len > 0) {
            if (number_lines || number_nonempty) {
                cat_output_with_lines(pipe_input_buffer, number_lines, number_nonempty);
            } else if (pipe_capture_mode) {
                shell_output(pipe_input_buffer);
            } else {
                vga_puts(pipe_input_buffer);
            }
            return 0;
        }
        vga_puts("Usage: cat [-n] [-b] <filename>\n");
        return -1;
    }

    /* Resolve path (relative or absolute) */
    char path[VFS_MAX_PATH];
    resolve_path(filename, path, VFS_MAX_PATH);

    vfs_node_t* node = vfs_lookup(path);
    if (node == NULL) {
        vga_puts("File not found: ");
        vga_puts(filename);
        vga_putchar('\n');
        return -1;
    }

    /* Read file contents */
    if (number_lines || number_nonempty) {
        /* Need to process line by line for numbering */
        uint8_t* content = (uint8_t*)kmalloc(node->length + 1);
        if (!content) {
            vga_puts("cat: out of memory\n");
            return -1;
        }
        vfs_read(node, 0, node->length, content);
        content[node->length] = '\0';
        cat_output_with_lines((char*)content, number_lines, number_nonempty);
        kfree(content);
    } else {
        /* Simple output */
        uint8_t buffer[256];
        uint32_t offset = 0;
        int32_t bytes_read;

        while ((bytes_read = vfs_read(node, offset, sizeof(buffer) - 1, buffer)) > 0) {
            buffer[bytes_read] = '\0';
            if (pipe_capture_mode) {
                shell_output((char*)buffer);
            } else {
                vga_puts((char*)buffer);
            }
            offset += bytes_read;
        }
    }

    return 0;
}

/*
 * ps - List processes
 */
/*
 * ps - Process status
 * Flags: -a (all), -l (long format), -e (show environment/extended)
 */
static int cmd_ps(int argc, char* argv[]) {
    bool long_format = false;
    bool extended = false;

    /* Parse arguments */
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            for (int j = 1; argv[i][j]; j++) {
                switch (argv[i][j]) {
                    case 'a': /* already shows all */ break;
                    case 'l': long_format = true; break;
                    case 'e': extended = true; break;
                    default:
                        printk("ps: invalid option -- '%c'\n", argv[i][j]);
                        return -1;
                }
            }
        }
    }

    if (long_format || extended) {
        vga_puts("  PID  PPID  UID  STATE     PRIO   TICKS  NAME\n");
        vga_puts("---------------------------------------------\n");
        for (int i = 0; i < MAX_PROCESSES; i++) {
            process_t* p = &process_table[i];
            if (p->state != PROCESS_STATE_UNUSED) {
                const char* state_str;
                switch (p->state) {
                    case PROCESS_STATE_CREATED: state_str = "CREATED"; break;
                    case PROCESS_STATE_READY: state_str = "READY  "; break;
                    case PROCESS_STATE_RUNNING: state_str = "RUNNING"; break;
                    case PROCESS_STATE_BLOCKED: state_str = "BLOCKED"; break;
                    case PROCESS_STATE_ZOMBIE: state_str = "ZOMBIE "; break;
                    default: state_str = "UNKNOWN"; break;
                }
                printk("%5u %5u %4u  %s %4u %7u  %s\n",
                       p->pid, p->ppid, p->uid, state_str,
                       p->priority, (uint32_t)p->total_ticks, p->name);
            }
        }
    } else {
        process_print_all();
    }
    return 0;
}

/*
 * touch - Create a file
 */
static int cmd_touch(int argc, char* argv[]) {
    if (argc < 2) {
        vga_puts("Usage: touch <filename>\n");
        return -1;
    }

    /* Determine parent directory and filename */
    vfs_node_t* parent = current_dir_node;
    const char* filename = argv[1];

    /* Handle absolute paths */
    if (argv[1][0] == '/') {
        parent = vfs_root;
        filename = argv[1] + 1;  /* Skip leading / */
    }

    /* Check filesystem type and use appropriate create function */
    vfs_node_t* node = NULL;
    if (parent->readdir == ext2_vfs_readdir) {
        node = ext2_create_file(parent, filename);
    } else if (parent->readdir == ramfs_readdir || parent->readdir == NULL) {
        node = ramfs_create_file_in(parent, filename, 0);
    } else {
        vga_puts("touch: unsupported filesystem\n");
        return -1;
    }
    if (node == NULL) {
        vga_puts("Failed to create file\n");
        return -1;
    }

    vga_puts("Created: ");
    vga_puts(argv[1]);
    vga_putchar('\n');
    return 0;
}

/*
 * write - Write text to a file
 */
static int cmd_write(int argc, char* argv[]) {
    if (argc < 3) {
        vga_puts("Usage: write <filename> <text...>\n");
        return -1;
    }

    /* Resolve path (relative or absolute) */
    char path[VFS_MAX_PATH];
    resolve_path(argv[1], path, VFS_MAX_PATH);

    vfs_node_t* node = vfs_lookup(path);
    if (node == NULL) {
        vga_puts("File not found: ");
        vga_puts(argv[1]);
        vga_putchar('\n');
        return -1;
    }

    /* Concatenate remaining arguments */
    char text[256];
    int pos = 0;
    for (int i = 2; i < argc && pos < 250; i++) {
        if (i > 2) text[pos++] = ' ';
        int len = strlen(argv[i]);
        if (pos + len > 250) len = 250 - pos;
        memcpy(text + pos, argv[i], len);
        pos += len;
    }
    text[pos++] = '\n';
    text[pos] = '\0';

    /* Write to file */
    int32_t written = vfs_write(node, node->length, pos, (uint8_t*)text);
    if (written < 0) {
        vga_puts("Write failed\n");
        return -1;
    }

    printk("Wrote %d bytes to %s\n", written, argv[1]);
    return 0;
}

/*
 * run - Execute a program
 */
static int cmd_run(int argc, char* argv[]) {
    if (argc < 2) {
        vga_puts("Usage: run <program>\n");
        return -1;
    }

    int pid = loader_exec(argv[1]);
    if (pid < 0) {
        vga_puts("Failed to run: ");
        vga_puts(argv[1]);
        vga_putchar('\n');
        return -1;
    }

    vga_puts("Started process PID ");
    char buf[12];
    int i = 0;
    int n = pid;
    do {
        buf[i++] = '0' + (n % 10);
        n /= 10;
    } while (n > 0);
    while (i > 0) vga_putchar(buf[--i]);
    vga_putchar('\n');

    return 0;
}

/*
 * progs - List available programs
 */
static int cmd_progs(int argc, char* argv[]) {
    (void)argc; (void)argv;
    loader_list();
    return 0;
}

/*
 * mem - Show memory information
 */
static int cmd_mem(int argc, char* argv[]) {
    (void)argc; (void)argv;

    extern uint32_t pmm_get_total_pages(void);
    extern uint32_t pmm_get_used_pages(void);

    uint32_t total = pmm_get_total_pages();
    uint32_t used = pmm_get_used_pages();
    uint32_t free_pages = total - used;

    vga_puts("Memory Info:\n");
    printk("  Total: %u pages (%u KB)\n", total, (total * 4096) / 1024);
    printk("  Used:  %u pages (%u KB)\n", used, (used * 4096) / 1024);
    printk("  Free:  %u pages (%u KB)\n", free_pages, (free_pages * 4096) / 1024);

    return 0;
}

/*
 * uptime - Show system uptime
 */
static int cmd_uptime(int argc, char* argv[]) {
    (void)argc; (void)argv;

    extern uint32_t timer_get_ticks(void);

    uint32_t ticks = timer_get_ticks();
    uint32_t seconds = ticks / 100;  /* 100 Hz timer */
    uint32_t minutes = seconds / 60;
    uint32_t hours = minutes / 60;

    seconds %= 60;
    minutes %= 60;

    vga_puts("System uptime: ");
    printk("%u:%02u:%02u (%u ticks)\n", hours, minutes, seconds, ticks);

    return 0;
}

/*
 * date - Show current date and time from RTC
 */
static int cmd_date(int argc, char* argv[]) {
    (void)argc; (void)argv;

    static const char* months[] = {
        "", "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    static const char* days[] = {
        "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
    };

    rtc_time_t t;
    rtc_get_adjusted_time(&t);

    /* Zeller's formula for day of week (simplified for 2000s) */
    int y = t.year;
    int m = t.month;
    int d = t.day;
    if (m < 3) { m += 12; y--; }
    int dow = (d + (13 * (m + 1)) / 5 + y + y / 4 - y / 100 + y / 400) % 7;
    /* Zeller: 0=Sat, 1=Sun, ... 6=Fri. Convert to 0=Sun. */
    dow = (dow + 6) % 7;

    int mon = t.month;
    if (mon < 1) mon = 1;
    if (mon > 12) mon = 12;

    /* 12-hour format */
    uint8_t h = t.hours;
    const char* ampm = (h < 12) ? "AM" : "PM";
    if (h == 0) h = 12;
    else if (h > 12) h -= 12;

    printk("%s %s %2d %2d:%02d:%02d %s %d\n",
           days[dow], months[mon], t.day,
           h, t.minutes, t.seconds, ampm, t.year);

    return 0;
}

/*
 * uname - Show system information
 */
static int cmd_uname(int argc, char* argv[]) {
    (void)argc; (void)argv;

    vga_puts("MiniOS v1.0 (i686)\n");
    vga_puts("A minimal Unix-like operating system\n");

    return 0;
}

/*
 * kill - Send signal to a process
 * Flags: -s <signal>, -l (list signals), -<signal>
 */
static const char* signal_names[] = {
    NULL, "HUP", "INT", "QUIT", "ILL", "TRAP", "ABRT", "BUS", "FPE",
    "KILL", "USR1", "SEGV", "USR2", "PIPE", "ALRM", "TERM", NULL,
    "CHLD", "CONT", "STOP", "TSTP", "TTIN", "TTOU"
};

static int cmd_kill(int argc, char* argv[]) {
    int signal = 15;  /* SIGTERM default */
    bool list_signals = false;
    int pid = -1;

    /* Parse arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-l") == 0) {
            list_signals = true;
        } else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            i++;
            /* Parse signal name or number */
            if (argv[i][0] >= '0' && argv[i][0] <= '9') {
                signal = atoi(argv[i]);
            } else {
                signal = -1;
                for (int j = 1; j <= 22; j++) {
                    if (signal_names[j] && strcmp(argv[i], signal_names[j]) == 0) {
                        signal = j;
                        break;
                    }
                }
                if (signal < 0) {
                    printk("kill: invalid signal: %s\n", argv[i]);
                    return -1;
                }
            }
        } else if (argv[i][0] == '-' && argv[i][1] >= '0' && argv[i][1] <= '9') {
            signal = atoi(&argv[i][1]);
        } else if (argv[i][0] == '-') {
            /* Signal name like -KILL, -TERM */
            signal = -1;
            for (int j = 1; j <= 22; j++) {
                if (signal_names[j] && strcmp(&argv[i][1], signal_names[j]) == 0) {
                    signal = j;
                    break;
                }
            }
            if (signal < 0) {
                printk("kill: invalid signal: %s\n", &argv[i][1]);
                return -1;
            }
        } else {
            pid = atoi(argv[i]);
        }
    }

    if (list_signals) {
        vga_puts("Signals:\n");
        for (int i = 1; i <= 22; i++) {
            if (signal_names[i]) {
                printk(" %2d) SIG%s\n", i, signal_names[i]);
            }
        }
        return 0;
    }

    if (pid < 0) {
        vga_puts("Usage: kill [-s signal | -signal] <pid>\n");
        vga_puts("       kill -l (list signals)\n");
        return -1;
    }

    if (pid == 0) {
        vga_puts("Cannot signal idle process\n");
        return -1;
    }

    /* Find and signal process */
    process_t* proc = process_get((uint32_t)pid);
    if (proc == NULL) {
        vga_puts("Process not found\n");
        return -1;
    }

    /* Use signal_send for proper signal handling */
    extern int signal_send(uint32_t pid, int signum);
    if (signal_send(pid, signal) < 0) {
        vga_puts("Failed to send signal\n");
        return -1;
    }

    printk("Sent signal %d to process %d\n", signal, pid);
    return 0;
}

/* Helper for port I/O */
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

/*
 * reboot - Reboot the system
 */
static int cmd_reboot(int argc, char* argv[]) {
    (void)argc; (void)argv;

    vga_puts("Rebooting...\n");

    /* Method 1: Use keyboard controller to pulse CPU reset line */
    /* Wait for keyboard controller input buffer to be empty */
    while (inb(0x64) & 0x02);

    /* Send reset command (0xFE) to keyboard controller */
    outb(0x64, 0xFE);

    /* If that didn't work, halt */
    __asm__ volatile("cli; hlt");

    return 0;  /* Never reached */
}

/* ============================================
 * 10.1: File System Commands
 * ============================================ */

static int cmd_pwd(int argc, char* argv[]) {
    (void)argc; (void)argv;
    printk("%s\n", current_dir);
    return 0;
}

static int cmd_cd(int argc, char* argv[]) {
    /* cd with no args or cd / goes to root */
    if (argc < 2 || strcmp(argv[1], "/") == 0) {
        strcpy(current_dir, "/");
        current_dir_node = vfs_root;
        return 0;
    }

    /* cd .. - go to parent directory */
    if (strcmp(argv[1], "..") == 0) {
        if (strcmp(current_dir, "/") == 0) {
            /* Already at root */
            return 0;
        }
        /* Find last slash and truncate */
        int len = strlen(current_dir);
        for (int i = len - 1; i >= 0; i--) {
            if (current_dir[i] == '/' && i > 0) {
                current_dir[i] = '\0';
                break;
            } else if (i == 0) {
                strcpy(current_dir, "/");
                break;
            }
        }
        /* Re-lookup the directory */
        if (strcmp(current_dir, "/") == 0) {
            current_dir_node = vfs_root;
        } else {
            current_dir_node = vfs_lookup(current_dir);
        }
        return 0;
    }

    /* Build the target path */
    char target_path[VFS_MAX_PATH];
    if (argv[1][0] == '/') {
        /* Absolute path */
        strncpy(target_path, argv[1], VFS_MAX_PATH - 1);
        target_path[VFS_MAX_PATH - 1] = '\0';
    } else {
        /* Relative path */
        if (strcmp(current_dir, "/") == 0) {
            snprintf(target_path, VFS_MAX_PATH, "/%s", argv[1]);
        } else {
            snprintf(target_path, VFS_MAX_PATH, "%s/%s", current_dir, argv[1]);
        }
    }

    /* Look up the directory */
    vfs_node_t* node = vfs_lookup(target_path);
    if (node == NULL) {
        printk("cd: %s: No such file or directory\n", argv[1]);
        return -1;
    }

    /* Check if it's a directory */
    if ((node->flags & 0x7) != VFS_DIRECTORY) {
        printk("cd: %s: Not a directory\n", argv[1]);
        return -1;
    }

    /* Update current directory */
    strncpy(current_dir, target_path, VFS_MAX_PATH - 1);
    current_dir[VFS_MAX_PATH - 1] = '\0';
    current_dir_node = node;

    return 0;
}

/*
 * mkdir - Create directories
 * Flags: -p (create parents), -v (verbose)
 */
static int cmd_mkdir(int argc, char* argv[]) {
    bool create_parents = false;
    bool verbose = false;
    const char* dirname = NULL;

    /* Parse arguments */
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            for (int j = 1; argv[i][j]; j++) {
                switch (argv[i][j]) {
                    case 'p': create_parents = true; break;
                    case 'v': verbose = true; break;
                    default:
                        printk("mkdir: invalid option -- '%c'\n", argv[i][j]);
                        return -1;
                }
            }
        } else {
            dirname = argv[i];
        }
    }

    if (!dirname) {
        vga_puts("Usage: mkdir [-pv] <dirname>\n");
        return -1;
    }

    /* Determine parent directory and name */
    vfs_node_t* parent = current_dir_node;
    const char* name = dirname;

    /* Handle absolute paths */
    if (dirname[0] == '/') {
        parent = vfs_root;
        name = dirname + 1;
    }

    /* If -p, create intermediate directories */
    if (create_parents && strchr(name, '/')) {
        char path_copy[VFS_MAX_PATH];
        strncpy(path_copy, dirname, sizeof(path_copy) - 1);
        path_copy[sizeof(path_copy) - 1] = '\0';

        char* p = path_copy;
        if (*p == '/') p++;

        vfs_node_t* cur_parent = (dirname[0] == '/') ? vfs_root : current_dir_node;
        char* next;
        while ((next = strchr(p, '/')) != NULL) {
            *next = '\0';
            vfs_node_t* existing = vfs_finddir(cur_parent, p);
            if (!existing) {
                vfs_node_t* new_dir = NULL;
                if (cur_parent->readdir == ext2_vfs_readdir) {
                    new_dir = ext2_create_dir(cur_parent, p);
                } else {
                    new_dir = ramfs_create_dir_in(cur_parent, p);
                }
                if (!new_dir) {
                    printk("mkdir: cannot create directory '%s'\n", p);
                    return -1;
                }
                if (verbose) printk("mkdir: created directory '%s'\n", p);
                cur_parent = new_dir;
            } else {
                cur_parent = existing;
            }
            p = next + 1;
        }
        /* Create final directory */
        parent = cur_parent;
        name = p;
    }

    /* Check filesystem type and use appropriate create function */
    vfs_node_t* result = NULL;
    if (parent->readdir == ext2_vfs_readdir) {
        result = ext2_create_dir(parent, name);
    } else if (parent->readdir == ramfs_readdir || parent->readdir == NULL) {
        result = ramfs_create_dir_in(parent, name);
    } else {
        vga_puts("mkdir: unsupported filesystem\n");
        return -1;
    }

    if (result == NULL) {
        vga_puts("mkdir: failed to create directory\n");
        return -1;
    }
    if (verbose) printk("mkdir: created directory '%s'\n", dirname);
    return 0;
}

/*
 * rm - Remove files
 * Flags: -r (recursive), -f (force), -v (verbose)
 */
static int rm_recursive(vfs_node_t* node, const char* name, vfs_node_t* parent, bool verbose);

static int rm_recursive(vfs_node_t* node, const char* name, vfs_node_t* parent, bool verbose) {
    if (node->flags & VFS_DIRECTORY) {
        /* Remove all children first */
        uint32_t idx = 0;
        dirent_t* entry;
        while ((entry = vfs_readdir(node, idx++)) != NULL) {
            if (strcmp(entry->name, ".") == 0 || strcmp(entry->name, "..") == 0) {
                continue;
            }
            vfs_node_t* child = vfs_finddir(node, entry->name);
            if (child) {
                rm_recursive(child, entry->name, node, verbose);
            }
        }
    }

    /* Now remove this node */
    if (parent->readdir == ext2_vfs_readdir) {
        if (!ext2_unlink(parent, name)) {
            return -1;
        }
    } else {
        node->name[0] = '\0';
        node->flags = 0;
    }
    if (verbose) printk("removed '%s'\n", name);
    return 0;
}

static int cmd_rm(int argc, char* argv[]) {
    bool recursive = false;
    bool force = false;
    bool verbose = false;
    const char* filename = NULL;

    /* Parse arguments */
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            for (int j = 1; argv[i][j]; j++) {
                switch (argv[i][j]) {
                    case 'r':
                    case 'R': recursive = true; break;
                    case 'f': force = true; break;
                    case 'v': verbose = true; break;
                    default:
                        printk("rm: invalid option -- '%c'\n", argv[i][j]);
                        return -1;
                }
            }
        } else {
            filename = argv[i];
        }
    }

    if (!filename) {
        vga_puts("Usage: rm [-rfv] <filename>\n");
        return -1;
    }

    vfs_node_t* node = find_file(filename);
    if (node == NULL) {
        if (!force) {
            printk("rm: cannot remove '%s': No such file or directory\n", filename);
            return -1;
        }
        return 0;  /* -f suppresses error */
    }

    /* Check if it's a directory */
    if (node->flags & VFS_DIRECTORY) {
        if (!recursive) {
            printk("rm: cannot remove '%s': Is a directory\n", filename);
            return -1;
        }
    }

    /* Get parent directory */
    vfs_node_t* parent = node->parent ? node->parent : current_dir_node;

    if (recursive && (node->flags & VFS_DIRECTORY)) {
        return rm_recursive(node, filename, parent, verbose);
    }

    /* Check filesystem type and use appropriate delete function */
    if (parent->readdir == ext2_vfs_readdir) {
        if (!ext2_unlink(parent, filename)) {
            if (!force) {
                vga_puts("rm: failed to remove file\n");
                return -1;
            }
        }
    } else {
        /* ramfs - just mark as deleted */
        node->name[0] = '\0';
        node->flags = 0;
    }

    if (verbose) printk("removed '%s'\n", filename);
    return 0;
}

static int cmd_rmdir(int argc, char* argv[]) {
    if (argc < 2) {
        vga_puts("Usage: rmdir <dirname>\n");
        return -1;
    }

    vfs_node_t* node = find_file(argv[1]);
    if (node == NULL) {
        vga_puts("rmdir: directory not found\n");
        return -1;
    }

    if ((node->flags & VFS_DIRECTORY) == 0) {
        vga_puts("rmdir: not a directory\n");
        return -1;
    }

    node->name[0] = '\0';
    node->flags = 0;
    printk("Removed directory: %s\n", argv[1]);
    return 0;
}

/*
 * cp - Copy files
 * Flags: -r (recursive), -v (verbose)
 */
static int cp_file(vfs_node_t* src, vfs_node_t* dest_parent, const char* dest_name, bool verbose) {
    vfs_node_t* dest;
    if (dest_parent->readdir == ext2_vfs_readdir) {
        dest = ext2_create_file(dest_parent, dest_name);
    } else {
        dest = ramfs_create_file_in(dest_parent, dest_name, VFS_FILE);
    }
    if (!dest) return -1;

    uint8_t buf[4096];
    uint32_t offset = 0;
    int32_t bytes;
    while ((bytes = vfs_read(src, offset, 4096, buf)) > 0) {
        int32_t written = vfs_write(dest, offset, bytes, buf);
        if (written < 0) {
            printk("cp: write error at offset %u\n", offset);
            return -1;
        }
        offset += bytes;
    }
    if (verbose) printk("'%s' -> '%s'\n", src->name, dest_name);
    return 0;
}

static int cp_recursive(vfs_node_t* src, vfs_node_t* dest_parent, const char* dest_name, bool verbose);

static int cp_recursive(vfs_node_t* src, vfs_node_t* dest_parent, const char* dest_name, bool verbose) {
    if (!(src->flags & VFS_DIRECTORY)) {
        return cp_file(src, dest_parent, dest_name, verbose);
    }

    /* Create destination directory */
    vfs_node_t* new_dir;
    if (dest_parent->readdir == ext2_vfs_readdir) {
        new_dir = ext2_create_dir(dest_parent, dest_name);
    } else {
        new_dir = ramfs_create_dir_in(dest_parent, dest_name);
    }
    if (!new_dir) {
        printk("cp: cannot create directory '%s'\n", dest_name);
        return -1;
    }
    if (verbose) printk("'%s' -> '%s'\n", src->name, dest_name);

    /* Copy children */
    uint32_t idx = 0;
    dirent_t* entry;
    while ((entry = vfs_readdir(src, idx++)) != NULL) {
        if (strcmp(entry->name, ".") == 0 || strcmp(entry->name, "..") == 0) {
            continue;
        }
        vfs_node_t* child = vfs_finddir(src, entry->name);
        if (child) {
            cp_recursive(child, new_dir, entry->name, verbose);
        }
    }
    return 0;
}

static int cmd_cp(int argc, char* argv[]) {
    bool recursive = false;
    bool verbose = false;
    const char* src_name = NULL;
    const char* dest_name = NULL;

    /* Parse arguments */
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            for (int j = 1; argv[i][j]; j++) {
                switch (argv[i][j]) {
                    case 'r':
                    case 'R': recursive = true; break;
                    case 'v': verbose = true; break;
                    default:
                        printk("cp: invalid option -- '%c'\n", argv[i][j]);
                        return -1;
                }
            }
        } else if (!src_name) {
            src_name = argv[i];
        } else {
            dest_name = argv[i];
        }
    }

    if (!src_name || !dest_name) {
        vga_puts("Usage: cp [-rv] <src> <dest>\n");
        return -1;
    }

    vfs_node_t* src = find_file(src_name);
    if (src == NULL) {
        printk("cp: cannot stat '%s': No such file or directory\n", src_name);
        return -1;
    }

    if ((src->flags & VFS_DIRECTORY) && !recursive) {
        printk("cp: -r not specified; omitting directory '%s'\n", src_name);
        return -1;
    }

    if (recursive && (src->flags & VFS_DIRECTORY)) {
        return cp_recursive(src, current_dir_node, dest_name, verbose);
    }

    if (cp_file(src, current_dir_node, dest_name, verbose) < 0) {
        vga_puts("cp: failed to create destination\n");
        return -1;
    }
    return 0;
}

static int cmd_mv(int argc, char* argv[]) {
    if (argc < 3) {
        vga_puts("Usage: mv <src> <dest>\n");
        return -1;
    }

    vfs_node_t* src = find_file(argv[1]);
    if (src == NULL) {
        vga_puts("mv: source not found\n");
        return -1;
    }

    /* Just rename (VFS_MAX_NAME is 64) */
    strncpy(src->name, argv[2], 63);
    src->name[63] = '\0';
    printk("Renamed %s to %s\n", argv[1], argv[2]);
    return 0;
}

static int cmd_stat(int argc, char* argv[]) {
    if (argc < 2) {
        vga_puts("Usage: stat <filename>\n");
        return -1;
    }

    vfs_node_t* node = find_file(argv[1]);
    if (node == NULL) {
        vga_puts("stat: file not found\n");
        return -1;
    }

    printk("  File: %s\n", node->name);
    printk("  Size: %u bytes\n", node->length);
    printk("  Type: %s\n", (node->flags & VFS_DIRECTORY) ? "directory" : "file");
    printk("  Inode: %u\n", node->inode);
    return 0;
}

static int cmd_head(int argc, char* argv[]) {
    int lines = 10;
    const char* filename = NULL;

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] == 'n' && i + 1 < argc) {
            lines = 0;
            const char* s = argv[++i];
            while (*s >= '0' && *s <= '9') lines = lines * 10 + (*s++ - '0');
        } else {
            filename = argv[i];
        }
    }

    if (filename == NULL) {
        vga_puts("Usage: head [-n lines] <file>\n");
        return -1;
    }

    vfs_node_t* node = find_file(filename);
    if (node == NULL) {
        vga_puts("head: file not found\n");
        return -1;
    }

    uint8_t buf[512];
    int32_t bytes = vfs_read(node, 0, 512, buf);
    int line_count = 0;
    for (int i = 0; i < bytes && line_count < lines; i++) {
        vga_putchar(buf[i]);
        if (buf[i] == '\n') line_count++;
    }
    return 0;
}

static int cmd_tail(int argc, char* argv[]) {
    int lines = 10;
    const char* filename = NULL;

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] == 'n' && i + 1 < argc) {
            lines = 0;
            const char* s = argv[++i];
            while (*s >= '0' && *s <= '9') lines = lines * 10 + (*s++ - '0');
        } else {
            filename = argv[i];
        }
    }

    if (filename == NULL) {
        vga_puts("Usage: tail [-n lines] <file>\n");
        return -1;
    }

    vfs_node_t* node = find_file(filename);
    if (node == NULL) {
        vga_puts("tail: file not found\n");
        return -1;
    }

    uint8_t buf[512];
    int32_t bytes = vfs_read(node, 0, node->length < 512 ? node->length : 512, buf);

    /* Find start of last N lines */
    int newlines = 0;
    int start = bytes - 1;

    /* Skip trailing newline if present */
    if (start >= 0 && buf[start] == '\n') start--;

    /* Count backwards to find Nth newline from end */
    while (start >= 0) {
        if (buf[start] == '\n') {
            newlines++;
            if (newlines >= lines) {
                start++;  /* Move past this newline */
                break;
            }
        }
        start--;
    }

    /* If we didn't find enough lines, start from beginning */
    if (start < 0) start = 0;

    for (int i = start; i < bytes; i++) {
        vga_putchar(buf[i]);
    }
    return 0;
}

/*
 * wc - Word, line, character count
 * Flags: -l (lines), -w (words), -c (bytes)
 */
static int cmd_wc(int argc, char* argv[]) {
    bool show_lines = false;
    bool show_words = false;
    bool show_chars = false;
    bool any_flag = false;
    const char* filename = NULL;

    /* Parse arguments */
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            for (int j = 1; argv[i][j]; j++) {
                switch (argv[i][j]) {
                    case 'l': show_lines = true; any_flag = true; break;
                    case 'w': show_words = true; any_flag = true; break;
                    case 'c': show_chars = true; any_flag = true; break;
                    default:
                        printk("wc: invalid option -- '%c'\n", argv[i][j]);
                        return -1;
                }
            }
        } else {
            filename = argv[i];
        }
    }

    /* If no flags specified, show all */
    if (!any_flag) {
        show_lines = show_words = show_chars = true;
    }

    uint8_t* buf;
    int32_t bytes;
    uint8_t file_buf[4096];
    const char* display_name = filename ? filename : "(stdin)";

    /* Check if we should read from pipe input or file */
    if (!filename && pipe_input_mode && pipe_input_len > 0) {
        buf = (uint8_t*)pipe_input_buffer;
        bytes = pipe_input_len;
    } else if (filename) {
        vfs_node_t* node = find_file(filename);
        if (node == NULL) {
            vga_puts("wc: file not found\n");
            return -1;
        }
        bytes = vfs_read(node, 0, node->length < 4096 ? node->length : 4096, file_buf);
        buf = file_buf;
    } else {
        vga_puts("Usage: wc [-lwc] <file>\n");
        vga_puts("       or pipe input: cmd | wc\n");
        return -1;
    }

    int lines = 0, words = 0, chars = bytes;
    int in_word = 0;
    for (int i = 0; i < bytes; i++) {
        if (buf[i] == '\n') lines++;
        if (buf[i] == ' ' || buf[i] == '\n' || buf[i] == '\t') {
            in_word = 0;
        } else if (!in_word) {
            in_word = 1;
            words++;
        }
    }

    char output[128];
    int pos = 0;
    if (show_lines) pos += snprintf(output + pos, sizeof(output) - pos, "%7d ", lines);
    if (show_words) pos += snprintf(output + pos, sizeof(output) - pos, "%7d ", words);
    if (show_chars) pos += snprintf(output + pos, sizeof(output) - pos, "%7d ", chars);
    snprintf(output + pos, sizeof(output) - pos, "%s\n", display_name);

    if (pipe_capture_mode) {
        shell_output(output);
    } else {
        vga_puts(output);
    }
    return 0;
}

/* ============================================
 * 10.2: Process Commands
 * ============================================ */

static int cmd_sleep(int argc, char* argv[]) {
    if (argc < 2) {
        vga_puts("Usage: sleep <seconds>\n");
        return -1;
    }

    int seconds = 0;
    const char* s = argv[1];
    while (*s >= '0' && *s <= '9') seconds = seconds * 10 + (*s++ - '0');

    extern void timer_sleep_ms(uint32_t ms);
    printk("Sleeping for %d seconds...\n", seconds);
    timer_sleep_ms(seconds * 1000);
    vga_puts("Done.\n");
    return 0;
}

/* ============================================
 * 10.3: System Information Commands
 * ============================================ */

static int cmd_free(int argc, char* argv[]) {
    (void)argc; (void)argv;

    extern uint32_t pmm_get_total_pages(void);
    extern uint32_t pmm_get_used_pages(void);

    uint32_t total = pmm_get_total_pages();
    uint32_t used = pmm_get_used_pages();
    uint32_t free_p = total - used;

    vga_puts("             total       used       free\n");
    printk("Mem:    %10u %10u %10u  (pages)\n", total, used, free_p);
    printk("        %10u %10u %10u  (KB)\n",
           (total * 4), (used * 4), (free_p * 4));
    return 0;
}

static int cmd_whoami(int argc, char* argv[]) {
    (void)argc; (void)argv;
    printk("%s\n", user_get_name(current_uid));
    return 0;
}

static int cmd_hostname(int argc, char* argv[]) {
    static char hostname[32] = "minios";

    if (argc > 1) {
        strncpy(hostname, argv[1], 31);
        hostname[31] = '\0';
        printk("Hostname set to: %s\n", hostname);
    } else {
        printk("%s\n", hostname);
    }
    return 0;
}

/* Boot message buffer */
static char dmesg_buffer[2048] = "MiniOS Boot Log\n"
    "===============\n"
    "[ OK ] VGA driver initialized\n"
    "[ OK ] GDT loaded\n"
    "[ OK ] IDT loaded\n"
    "[ OK ] PIC configured\n"
    "[ OK ] Timer initialized (100 Hz)\n"
    "[ OK ] Keyboard driver initialized\n"
    "[ OK ] Physical memory manager initialized\n"
    "[ OK ] Paging enabled\n"
    "[ OK ] Kernel heap initialized\n"
    "[ OK ] Process manager initialized\n"
    "[ OK ] Scheduler initialized\n"
    "[ OK ] System calls initialized\n"
    "[ OK ] VFS initialized\n"
    "[ OK ] RAMFS mounted\n"
    "[ OK ] Program loader initialized\n"
    "[ OK ] Shell initialized\n";

static int cmd_dmesg(int argc, char* argv[]) {
    (void)argc; (void)argv;
    vga_puts(dmesg_buffer);
    return 0;
}

static int cmd_interrupts(int argc, char* argv[]) {
    (void)argc; (void)argv;

    extern uint32_t timer_get_ticks(void);

    vga_puts("IRQ Statistics:\n");
    printk("  IRQ0 (Timer):    %u\n", timer_get_ticks());
    vga_puts("  IRQ1 (Keyboard): (count not tracked)\n");
    return 0;
}

/* ============================================
 * 10.4: Text/Data Commands
 * ============================================ */

static int cmd_hexdump(int argc, char* argv[]) {
    if (argc < 2) {
        vga_puts("Usage: hexdump <file>\n");
        return -1;
    }

    vfs_node_t* node = find_file(argv[1]);
    if (node == NULL) {
        vga_puts("hexdump: file not found\n");
        return -1;
    }

    uint8_t buf[256];
    int32_t bytes = vfs_read(node, 0, node->length < 256 ? node->length : 256, buf);

    for (int i = 0; i < bytes; i += 16) {
        printk("%08x  ", i);
        for (int j = 0; j < 16 && i + j < bytes; j++) {
            printk("%02x ", buf[i + j]);
        }
        vga_putchar('\n');
    }
    return 0;
}

static int cmd_xxd(int argc, char* argv[]) {
    if (argc < 2) {
        vga_puts("Usage: xxd <file>\n");
        return -1;
    }

    vfs_node_t* node = find_file(argv[1]);
    if (node == NULL) {
        vga_puts("xxd: file not found\n");
        return -1;
    }

    uint8_t buf[256];
    int32_t bytes = vfs_read(node, 0, node->length < 256 ? node->length : 256, buf);

    for (int i = 0; i < bytes; i += 16) {
        printk("%08x: ", i);
        for (int j = 0; j < 16; j++) {
            if (i + j < bytes) printk("%02x", buf[i + j]);
            else vga_puts("  ");
            if (j % 2 == 1) vga_putchar(' ');
        }
        vga_puts(" ");
        for (int j = 0; j < 16 && i + j < bytes; j++) {
            char c = buf[i + j];
            vga_putchar((c >= 32 && c < 127) ? c : '.');
        }
        vga_putchar('\n');
    }
    return 0;
}

static int cmd_strings(int argc, char* argv[]) {
    if (argc < 2) {
        vga_puts("Usage: strings <file>\n");
        return -1;
    }

    vfs_node_t* node = find_file(argv[1]);
    if (node == NULL) {
        vga_puts("strings: file not found\n");
        return -1;
    }

    uint8_t buf[512];
    int32_t bytes = vfs_read(node, 0, node->length < 512 ? node->length : 512, buf);

    char str[64];
    int len = 0;
    for (int i = 0; i < bytes; i++) {
        if (buf[i] >= 32 && buf[i] < 127) {
            if (len < 63) str[len++] = buf[i];
        } else {
            if (len >= 4) {
                str[len] = '\0';
                printk("%s\n", str);
            }
            len = 0;
        }
    }
    if (len >= 4) {
        str[len] = '\0';
        printk("%s\n", str);
    }
    return 0;
}

/* ============================================
 * 10.5: Shell Built-ins
 * ============================================ */

static int cmd_history(int argc, char* argv[]) {
    (void)argc; (void)argv;

    if (history_count == 0) {
        vga_puts("No commands in history.\n");
        return 0;
    }

    for (int i = 0; i < history_count; i++) {
        printk("  %d  %s\n", i + 1, history[i]);
    }
    return 0;
}

static int cmd_env(int argc, char* argv[]) {
    (void)argc; (void)argv;

    vga_puts("PATH=/bin\n");
    vga_puts("HOME=/\n");
    vga_puts("USER=root\n");
    vga_puts("SHELL=/bin/sh\n");
    vga_puts("TERM=vga\n");
    return 0;
}

/* ============================================
 * 10.6: Debugging/Development Commands
 * ============================================ */

static int cmd_peek(int argc, char* argv[]) {
    if (argc < 2) {
        vga_puts("Usage: peek <address>\n");
        return -1;
    }

    /* Parse hex address */
    uint32_t addr = 0;
    const char* s = argv[1];
    if (s[0] == '0' && s[1] == 'x') s += 2;
    while (*s) {
        addr <<= 4;
        if (*s >= '0' && *s <= '9') addr |= (*s - '0');
        else if (*s >= 'a' && *s <= 'f') addr |= (*s - 'a' + 10);
        else if (*s >= 'A' && *s <= 'F') addr |= (*s - 'A' + 10);
        s++;
    }

    uint32_t value = *(volatile uint32_t*)addr;
    printk("0x%08x: 0x%08x (%u)\n", addr, value, value);
    return 0;
}

static int cmd_poke(int argc, char* argv[]) {
    if (argc < 3) {
        vga_puts("Usage: poke <address> <value>\n");
        return -1;
    }

    /* Parse hex address */
    uint32_t addr = 0;
    const char* s = argv[1];
    if (s[0] == '0' && s[1] == 'x') s += 2;
    while (*s) {
        addr <<= 4;
        if (*s >= '0' && *s <= '9') addr |= (*s - '0');
        else if (*s >= 'a' && *s <= 'f') addr |= (*s - 'a' + 10);
        else if (*s >= 'A' && *s <= 'F') addr |= (*s - 'A' + 10);
        s++;
    }

    /* Parse value */
    uint32_t value = 0;
    s = argv[2];
    if (s[0] == '0' && s[1] == 'x') {
        s += 2;
        while (*s) {
            value <<= 4;
            if (*s >= '0' && *s <= '9') value |= (*s - '0');
            else if (*s >= 'a' && *s <= 'f') value |= (*s - 'a' + 10);
            else if (*s >= 'A' && *s <= 'F') value |= (*s - 'A' + 10);
            s++;
        }
    } else {
        while (*s >= '0' && *s <= '9') value = value * 10 + (*s++ - '0');
    }

    *(volatile uint32_t*)addr = value;
    printk("Wrote 0x%08x to 0x%08x\n", value, addr);
    return 0;
}

static int cmd_dump(int argc, char* argv[]) {
    if (argc < 2) {
        vga_puts("Usage: dump <address> [length]\n");
        return -1;
    }

    /* Parse hex address */
    uint32_t addr = 0;
    const char* s = argv[1];
    if (s[0] == '0' && s[1] == 'x') s += 2;
    while (*s) {
        addr <<= 4;
        if (*s >= '0' && *s <= '9') addr |= (*s - '0');
        else if (*s >= 'a' && *s <= 'f') addr |= (*s - 'a' + 10);
        else if (*s >= 'A' && *s <= 'F') addr |= (*s - 'A' + 10);
        s++;
    }

    int len = 64;
    if (argc > 2) {
        len = 0;
        s = argv[2];
        while (*s >= '0' && *s <= '9') len = len * 10 + (*s++ - '0');
    }
    if (len > 256) len = 256;

    uint8_t* ptr = (uint8_t*)addr;
    for (int i = 0; i < len; i += 16) {
        printk("%08x: ", addr + i);
        for (int j = 0; j < 16 && i + j < len; j++) {
            printk("%02x ", ptr[i + j]);
        }
        vga_putchar('\n');
    }
    return 0;
}

static int cmd_heap(int argc, char* argv[]) {
    (void)argc; (void)argv;

    extern uint32_t heap_get_start(void);
    extern uint32_t heap_get_end(void);
    extern uint32_t heap_get_used(void);

    uint32_t start = heap_get_start();
    uint32_t end = heap_get_end();
    uint32_t used = heap_get_used();
    uint32_t total = end - start;
    uint32_t free_mem = total - used;

    vga_puts("Kernel Heap Info:\n");
    printk("  Start:   0x%08x\n", start);
    printk("  End:     0x%08x\n", end);
    printk("  Total:   %u bytes\n", total);
    printk("  Used:    %u bytes\n", used);
    printk("  Free:    %u bytes\n", free_mem);
    return 0;
}

/* ============================================
 * 10.7: Fun/Demo Commands
 * ============================================ */

static int cmd_color(int argc, char* argv[]) {
    if (argc < 2) {
        vga_puts("Usage: color <fg> [bg]\n");
        vga_puts("Colors: 0=black,1=blue,2=green,3=cyan,4=red,5=magenta,\n");
        vga_puts("        6=brown,7=lgrey,8=dgrey,9=lblue,10=lgreen,\n");
        vga_puts("        11=lcyan,12=lred,13=lmagenta,14=yellow,15=white\n");
        return -1;
    }

    int fg = 0, bg = 0;
    const char* s = argv[1];
    while (*s >= '0' && *s <= '9') fg = fg * 10 + (*s++ - '0');

    if (argc > 2) {
        s = argv[2];
        while (*s >= '0' && *s <= '9') bg = bg * 10 + (*s++ - '0');
    }

    if (fg > 15) fg = 15;
    if (bg > 15) bg = 15;

    extern void vga_set_color(uint8_t color);
    vga_set_color(vga_entry_color(fg, bg));
    printk("Color set to fg=%d bg=%d\n", fg, bg);
    return 0;
}

static int cmd_version(int argc, char* argv[]) {
    (void)argc; (void)argv;

    vga_puts("MiniOS Version 1.0\n");
    vga_puts("Build: Phase 10 Complete\n");
    vga_puts("Architecture: i686 (x86 32-bit)\n");
    vga_puts("Compiler: i686-elf-gcc\n");
    return 0;
}

static int cmd_about(int argc, char* argv[]) {
    (void)argc; (void)argv;

    vga_write("\n", vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
    vga_write("  __  __ _       _  ___  ____  \n", vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    vga_write(" |  \\/  (_)_ __ (_)/ _ \\/ ___| \n", vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    vga_write(" | |\\/| | | '_ \\| | | | \\___ \\ \n", vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    vga_write(" | |  | | | | | | | |_| |___) |\n", vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    vga_write(" |_|  |_|_|_| |_|_|\\___/|____/ \n", vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    vga_puts("\n");
    vga_puts("A minimal Unix-like operating system\n");
    vga_puts("written from scratch in C and x86 assembly.\n\n");
    vga_puts("Features:\n");
    vga_puts("  - Multiboot-compliant bootloader\n");
    vga_puts("  - Protected mode with GDT/IDT\n");
    vga_puts("  - Physical & virtual memory management\n");
    vga_puts("  - Preemptive multitasking\n");
    vga_puts("  - System call interface (INT 0x80)\n");
    vga_puts("  - Virtual file system with RAMFS\n");
    vga_puts("  - Interactive shell with 50+ commands\n");
    vga_puts("\n");
    return 0;
}

static int cmd_credits(int argc, char* argv[]) {
    (void)argc; (void)argv;

    vga_write("\n", vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
    vga_write("=====================================\n", vga_entry_color(VGA_COLOR_LIGHT_BROWN, VGA_COLOR_BLACK));
    vga_write("           MiniOS Credits            \n", vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
    vga_write("=====================================\n", vga_entry_color(VGA_COLOR_LIGHT_BROWN, VGA_COLOR_BLACK));
    vga_puts("\n");
    vga_write("  Lead Developer:\n", vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    vga_write("    Robert Stacy II\n\n", vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
    vga_write("  Project:\n", vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    vga_puts("    MiniOS - A minimal Unix-like OS\n\n");
    vga_write("  Special Thanks:\n", vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    vga_puts("    OSDev Wiki community\n");
    vga_puts("    GNU toolchain developers\n");
    vga_puts("    QEMU developers\n");
    vga_puts("\n");
    vga_write("=====================================\n", vga_entry_color(VGA_COLOR_LIGHT_BROWN, VGA_COLOR_BLACK));
    vga_puts("\n");
    return 0;
}

/* ============================================
 * Additional 10.2: Process Commands
 * ============================================ */

static int cmd_top(int argc, char* argv[]) {
    (void)argc; (void)argv;
    vga_puts("PID  NAME             STATE      TICKS\n");
    vga_puts("---  ---------------  ---------  -----\n");
    for (int i = 0; i < MAX_PROCESSES; i++) {
        process_t* p = process_get(i);
        if (p != NULL && p->state != PROCESS_STATE_UNUSED) {
            const char* state_str = "UNKNOWN";
            switch (p->state) {
                case PROCESS_STATE_CREATED: state_str = "CREATED"; break;
                case PROCESS_STATE_READY:   state_str = "READY";   break;
                case PROCESS_STATE_RUNNING: state_str = "RUNNING"; break;
                case PROCESS_STATE_BLOCKED: state_str = "BLOCKED"; break;
                case PROCESS_STATE_ZOMBIE:  state_str = "ZOMBIE";  break;
                default: break;
            }
            printk("%-4d %-16s %-9s  %d\n", p->pid, p->name, state_str, p->total_ticks);
        }
    }
    return 0;
}

static int cmd_nice(int argc, char* argv[]) {
    (void)argc; (void)argv;
    vga_puts("nice: Priority scheduling not implemented\n");
    vga_puts("      All processes share equal time slices\n");
    return 0;
}

/* ============================================
 * Additional 10.3: System Information Commands
 * ============================================ */

static int cmd_lscpu(int argc, char* argv[]) {
    (void)argc; (void)argv;
    vga_puts("Architecture:      i686 (x86 32-bit)\n");
    vga_puts("CPU op-modes:      32-bit\n");
    vga_puts("Byte Order:        Little Endian\n");

    /* Use CPUID to get vendor string */
    uint32_t eax, ebx, ecx, edx;
    char vendor[13];

    __asm__ volatile("cpuid"
                     : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                     : "a"(0));

    *(uint32_t*)&vendor[0] = ebx;
    *(uint32_t*)&vendor[4] = edx;
    *(uint32_t*)&vendor[8] = ecx;
    vendor[12] = '\0';

    printk("Vendor ID:         %s\n", vendor);
    vga_puts("Model name:        Generic x86\n");
    return 0;
}

static int cmd_diskmgmt(int argc, char* argv[]) {
    (void)argc; (void)argv;
    
    vga_write("=== Disk Management ===\n", vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    vga_puts("\n");
    
    /* Show block devices */
    uint8_t bdev_count = blockdev_count();
    if (bdev_count == 0) {
        vga_puts("No block devices registered.\n\n");
    } else {
        printk("Block Devices (%u):\n\n", bdev_count);
        vga_puts("NAME       TYPE        SIZE       PARTITIONS\n");
        vga_puts("---------- ----------- ---------- ----------\n");
        
        for (int i = 0; i < bdev_count; i++) {
            blockdev_t* bdev = blockdev_get(i);
            if (bdev == NULL) continue;
            
            const char* type_str;
            switch (bdev->type) {
                case BLOCKDEV_TYPE_DISK:      type_str = "disk"; break;
                case BLOCKDEV_TYPE_PARTITION: type_str = "partition"; break;
                case BLOCKDEV_TYPE_RAMDISK:   type_str = "ramdisk"; break;
                default:                      type_str = "unknown"; break;
            }
            
            printk("%-10s %-11s %4u MB    ", bdev->name, type_str, bdev->size_mb);
            
            if (bdev->type == BLOCKDEV_TYPE_DISK && bdev->partition_count > 0) {
                printk("%u", bdev->partition_count);
            } else if (bdev->type == BLOCKDEV_TYPE_PARTITION) {
                printk("(LBA %u)", bdev->start_lba);
            } else {
                vga_puts("-");
            }
            vga_puts("\n");
        }
        vga_puts("\n");
    }
    
    /* Show partition details for disks */
    vga_write("=== Partition Tables ===\n", vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    vga_puts("\n");
    
    bool found_partitions = false;
    for (int i = 0; i < bdev_count; i++) {
        blockdev_t* bdev = blockdev_get(i);
        if (bdev == NULL || bdev->type != BLOCKDEV_TYPE_DISK) continue;
        if (bdev->partition_count == 0) continue;
        
        found_partitions = true;
        printk("Disk %s:\n", bdev->name);
        vga_puts("  #  TYPE         START LBA    SIZE\n");
        
        for (int p = 0; p < bdev->partition_count; p++) {
            partition_info_t* part = &bdev->partitions[p];
            printk("  %d  %-12s %-12u %u MB%s\n",
                   p + 1,
                   blockdev_partition_type_name(part->type),
                   part->start_lba,
                   part->size_mb,
                   part->active ? " *" : "");
        }
        vga_puts("\n");
    }
    
    if (!found_partitions) {
        vga_puts("No partitions found on any disk.\n");
        vga_puts("(Disk may be unformatted or use GPT)\n\n");
    }
    
    /* Show mounted filesystems */
    vga_write("=== Mounted Filesystems ===\n", vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    vga_puts("\n");
    vga_puts("DEVICE     FSTYPE   SIZE       MOUNT\n");
    vga_puts("---------- -------- ---------- -----\n");
    vga_puts("ramfs      ramfs    (memory)   /\n");
    vga_puts("\n");
    
    return 0;
}

/* ============================================
 * Additional 10.4: Text/Data Commands
 * ============================================ */

/*
 * grep - Search for pattern in file
 * Flags: -i (case insensitive), -v (invert), -c (count), -n (line numbers)
 */
static char grep_tolower(char c) {
    if (c >= 'A' && c <= 'Z') return c + 32;
    return c;
}

static int cmd_grep(int argc, char* argv[]) {
    bool case_insensitive = false;
    bool invert_match = false;
    bool count_only = false;
    bool show_line_numbers = false;
    const char* pattern = NULL;
    const char* filename = NULL;

    /* Parse arguments */
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] != '\0') {
            for (int j = 1; argv[i][j]; j++) {
                switch (argv[i][j]) {
                    case 'i': case_insensitive = true; break;
                    case 'v': invert_match = true; break;
                    case 'c': count_only = true; break;
                    case 'n': show_line_numbers = true; break;
                    default:
                        printk("grep: invalid option -- '%c'\n", argv[i][j]);
                        return -1;
                }
            }
        } else if (!pattern) {
            pattern = argv[i];
        } else {
            filename = argv[i];
        }
    }

    if (!pattern) {
        vga_puts("Usage: grep [-ivnc] <pattern> [file]\n");
        return -1;
    }

    uint8_t* buf;
    int32_t bytes;
    uint8_t file_buf[4096];

    /* Check if we should read from pipe input or file */
    if (!filename && pipe_input_mode && pipe_input_len > 0) {
        buf = (uint8_t*)pipe_input_buffer;
        bytes = pipe_input_len;
    } else if (filename) {
        vfs_node_t* node = find_file(filename);
        if (node == NULL) {
            vga_puts("grep: file not found\n");
            return -1;
        }
        bytes = vfs_read(node, 0, node->length < 4096 ? node->length : 4096, file_buf);
        buf = file_buf;
    } else {
        vga_puts("Usage: grep [-ivnc] <pattern> <file>\n");
        vga_puts("       or pipe input: cmd | grep <pattern>\n");
        return -1;
    }

    /* Search line by line */
    int line_start = 0;
    int line_num = 1;
    int match_count = 0;
    int pattern_len = strlen(pattern);
    char num_buf[16];

    for (int i = 0; i <= bytes; i++) {
        if (i == bytes || buf[i] == '\n') {
            /* Check if line contains pattern */
            int found = 0;
            for (int j = line_start; j <= i - pattern_len; j++) {
                int match = 1;
                for (int k = 0; pattern[k]; k++) {
                    char bc = buf[j + k];
                    char pc = pattern[k];
                    if (case_insensitive) {
                        bc = grep_tolower(bc);
                        pc = grep_tolower(pc);
                    }
                    if (bc != pc) {
                        match = 0;
                        break;
                    }
                }
                if (match) {
                    found = 1;
                    break;
                }
            }

            /* Apply invert flag */
            if (invert_match) found = !found;

            if (found) {
                match_count++;
                if (!count_only) {
                    /* Print line number if requested */
                    if (show_line_numbers) {
                        snprintf(num_buf, sizeof(num_buf), "%d:", line_num);
                        if (pipe_capture_mode) {
                            shell_output(num_buf);
                        } else {
                            vga_write(num_buf, vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
                        }
                    }
                    /* Print the line */
                    for (int j = line_start; j < i; j++) {
                        if (pipe_capture_mode) {
                            char c[2] = {buf[j], '\0'};
                            shell_output(c);
                        } else {
                            vga_putchar(buf[j]);
                        }
                    }
                    if (pipe_capture_mode) {
                        shell_output("\n");
                    } else {
                        vga_putchar('\n');
                    }
                }
            }
            line_start = i + 1;
            line_num++;
        }
    }

    /* Print count if requested */
    if (count_only) {
        snprintf(num_buf, sizeof(num_buf), "%d\n", match_count);
        if (pipe_capture_mode) {
            shell_output(num_buf);
        } else {
            vga_puts(num_buf);
        }
    }

    return 0;
}

static int cmd_diff(int argc, char* argv[]) {
    if (argc < 3) {
        vga_puts("Usage: diff <file1> <file2>\n");
        return -1;
    }

    vfs_node_t* node1 = find_file(argv[1]);
    vfs_node_t* node2 = find_file(argv[2]);

    if (node1 == NULL) {
        printk("diff: %s not found\n", argv[1]);
        return -1;
    }
    if (node2 == NULL) {
        printk("diff: %s not found\n", argv[2]);
        return -1;
    }

    uint8_t buf1[256], buf2[256];
    int32_t len1 = vfs_read(node1, 0, 256, buf1);
    int32_t len2 = vfs_read(node2, 0, 256, buf2);

    if (len1 != len2) {
        printk("Files differ in size: %d vs %d bytes\n", len1, len2);
        return 1;
    }

    int differs = 0;
    for (int i = 0; i < len1; i++) {
        if (buf1[i] != buf2[i]) {
            printk("Byte %d: 0x%02x vs 0x%02x\n", i, buf1[i], buf2[i]);
            differs = 1;
        }
    }

    if (!differs) {
        vga_puts("Files are identical\n");
    }
    return differs;
}

/* ============================================
 * Additional 10.5: Shell Built-ins
 * ============================================ */

static int cmd_alias(int argc, char* argv[]) {
    (void)argc; (void)argv;
    vga_puts("alias: Command aliases not implemented\n");
    vga_puts("       This is a placeholder command\n");
    return 0;
}

static int cmd_export(int argc, char* argv[]) {
    if (argc < 2) {
        vga_puts("Usage: export NAME=value\n");
        return -1;
    }
    printk("export: Would set %s\n", argv[1]);
    vga_puts("        Environment variables not fully implemented\n");
    return 0;
}

static int cmd_set(int argc, char* argv[]) {
    (void)argc; (void)argv;
    vga_puts("Shell options:\n");
    vga_puts("  echo     = off\n");
    vga_puts("  verbose  = off\n");
    vga_puts("  history  = on (16 entries)\n");
    return 0;
}

/* ============================================
 * Additional 10.6: Debugging Commands
 * ============================================ */

static int cmd_regs(int argc, char* argv[]) {
    (void)argc; (void)argv;
    uint32_t eax, ebx, ecx, edx, esi, edi, ebp, esp;
    uint32_t cs, ds, es, ss, eflags;

    __asm__ volatile(
        "movl %%eax, %0\n"
        "movl %%ebx, %1\n"
        "movl %%ecx, %2\n"
        "movl %%edx, %3\n"
        "movl %%esi, %4\n"
        "movl %%edi, %5\n"
        "movl %%ebp, %6\n"
        "movl %%esp, %7\n"
        : "=m"(eax), "=m"(ebx), "=m"(ecx), "=m"(edx),
          "=m"(esi), "=m"(edi), "=m"(ebp), "=m"(esp)
    );

    __asm__ volatile("movl %%cs, %0" : "=r"(cs));
    __asm__ volatile("movl %%ds, %0" : "=r"(ds));
    __asm__ volatile("movl %%es, %0" : "=r"(es));
    __asm__ volatile("movl %%ss, %0" : "=r"(ss));
    __asm__ volatile("pushfl; popl %0" : "=r"(eflags));

    vga_puts("CPU Registers:\n");
    printk("  EAX=0x%08x  EBX=0x%08x\n", eax, ebx);
    printk("  ECX=0x%08x  EDX=0x%08x\n", ecx, edx);
    printk("  ESI=0x%08x  EDI=0x%08x\n", esi, edi);
    printk("  EBP=0x%08x  ESP=0x%08x\n", ebp, esp);
    printk("  CS=0x%04x  DS=0x%04x  ES=0x%04x  SS=0x%04x\n", cs, ds, es, ss);
    printk("  EFLAGS=0x%08x\n", eflags);
    return 0;
}

static int cmd_gdt(int argc, char* argv[]) {
    (void)argc; (void)argv;
    vga_puts("GDT Entries (6 total):\n");
    vga_puts("  #  Selector  Description\n");
    vga_puts("  0  0x00      Null descriptor\n");
    vga_puts("  1  0x08      Kernel code (ring 0)\n");
    vga_puts("  2  0x10      Kernel data (ring 0)\n");
    vga_puts("  3  0x18      User code (ring 3)\n");
    vga_puts("  4  0x20      User data (ring 3)\n");
    vga_puts("  5  0x28      TSS\n");
    return 0;
}

static int cmd_idt(int argc, char* argv[]) {
    (void)argc; (void)argv;
    vga_puts("IDT Summary (256 entries):\n");
    vga_puts("  0-31    CPU Exceptions\n");
    vga_puts("  32-47   Hardware IRQs (remapped)\n");
    vga_puts("    32    Timer (IRQ0)\n");
    vga_puts("    33    Keyboard (IRQ1)\n");
    vga_puts("  128     System call (INT 0x80)\n");
    return 0;
}

static int cmd_pages(int argc, char* argv[]) {
    (void)argc; (void)argv;
    vga_puts("Page Table Info:\n");
    vga_puts("  Page size:       4 KB\n");
    vga_puts("  Identity mapped: 0-16 MB\n");
    vga_puts("  Kernel:          0x100000 (1 MB)\n");
    vga_puts("  Kernel heap:     0x400000 (4 MB)\n");
    vga_puts("  User code:       0x40000000 (1 GB)\n");
    vga_puts("  User heap:       0x40100000 (1 GB + 1 MB)\n");
    return 0;
}

static int cmd_stack(int argc, char* argv[]) {
    (void)argc; (void)argv;
    uint32_t ebp, esp;
    __asm__ volatile("movl %%ebp, %0" : "=r"(ebp));
    __asm__ volatile("movl %%esp, %0" : "=r"(esp));

    vga_puts("Stack Trace:\n");
    printk("  ESP=0x%08x  EBP=0x%08x\n", esp, ebp);

    /* Walk stack frames */
    uint32_t* frame = (uint32_t*)ebp;
    int depth = 0;
    while (frame != NULL && depth < 8 && (uint32_t)frame > 0x100000) {
        uint32_t ret_addr = frame[1];
        printk("  Frame %d: EBP=0x%08x RET=0x%08x\n", depth, (uint32_t)frame, ret_addr);
        frame = (uint32_t*)frame[0];
        depth++;
    }
    return 0;
}

/* ============================================
 * Additional 10.7: Fun/Demo Commands
 * ============================================ */

static int cmd_beep(int argc, char* argv[]) {
    (void)argc; (void)argv;
    /* PC speaker control via PIT channel 2 */
    uint32_t freq = 1000;  /* 1kHz beep */
    uint32_t divisor = 1193180 / freq;

    /* Set up PIT channel 2 */
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)0xB6), "Nd"((uint16_t)0x43));
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)(divisor & 0xFF)), "Nd"((uint16_t)0x42));
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)(divisor >> 8)), "Nd"((uint16_t)0x42));

    /* Enable speaker */
    uint8_t tmp;
    __asm__ volatile("inb %1, %0" : "=a"(tmp) : "Nd"((uint16_t)0x61));
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)(tmp | 3)), "Nd"((uint16_t)0x61));

    /* Wait ~200ms using simple busy loop */
    for (volatile int i = 0; i < 10000000; i++) {}

    /* Disable speaker */
    __asm__ volatile("inb %1, %0" : "=a"(tmp) : "Nd"((uint16_t)0x61));
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)(tmp & ~3)), "Nd"((uint16_t)0x61));

    vga_puts("Beep!\n");
    return 0;
}

static int cmd_banner(int argc, char* argv[]) {
    const char* text = "MiniOS";
    if (argc > 1) text = argv[1];

    /* Simple banner - just print text in large style */
    vga_write("=====================================\n", vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    vga_write("     ", vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
    vga_write(text, vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    vga_puts("\n");
    vga_write("=====================================\n", vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    return 0;
}

static int cmd_fortune(int argc, char* argv[]) {
    (void)argc; (void)argv;

    static const char* fortunes[] = {
        "The only way to do great work is to love what you do.",
        "First, solve the problem. Then, write the code.",
        "The best error message is the one that never shows up.",
        "Debugging is twice as hard as writing code.",
        "Simplicity is the soul of efficiency.",
        "Talk is cheap. Show me the code. - Linus Torvalds",
        "Programs must be written for people to read.",
        "Code is like humor. When you have to explain it, it's bad."
    };

    extern uint32_t timer_get_ticks(void);
    int index = timer_get_ticks() % 8;

    vga_write("\n  \"", vga_entry_color(VGA_COLOR_LIGHT_BROWN, VGA_COLOR_BLACK));
    vga_write(fortunes[index], vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
    vga_write("\"\n\n", vga_entry_color(VGA_COLOR_LIGHT_BROWN, VGA_COLOR_BLACK));
    return 0;
}

/* ============================================
 * User/Permissions Command Implementations
 * ============================================ */

/*
 * login - Login as a user
 */
static int cmd_login(int argc, char* argv[]) {
    if (argc < 2) {
        vga_puts("Usage: login <username>\n");
        return 1;
    }

    char password[64];
    vga_puts("Password: ");

    /* Read password (hidden) */
    int pos = 0;
    while (pos < 63) {
        char c = keyboard_getchar();
        if (c == '\n') break;
        if (c == '\b' && pos > 0) {
            pos--;
            continue;
        }
        if (c >= 32 && c < 127) {
            password[pos++] = c;
        }
    }
    password[pos] = '\0';
    vga_putchar('\n');

    if (user_login(argv[1], password) == 0) {
        printk("Logged in as %s\n", argv[1]);
        return 0;
    } else {
        vga_puts("Login failed: invalid username or password\n");
        return 1;
    }
}

/*
 * logout - Logout current user
 */
static int cmd_logout(int argc, char* argv[]) {
    (void)argc; (void)argv;
    user_logout();
    vga_puts("Logged out. Now running as root.\n");
    return 0;
}

/*
 * su - Switch user
 */
static int cmd_su(int argc, char* argv[]) {
    if (argc < 2) {
        vga_puts("Usage: su <username>\n");
        return 1;
    }

    user_t* user = user_get_by_name(argv[1]);
    if (user == NULL) {
        printk("User '%s' not found\n", argv[1]);
        return 1;
    }

    /* Root doesn't need password */
    if (current_uid == ROOT_UID) {
        if (user_switch(user->uid, NULL) == 0) {
            printk("Switched to %s\n", argv[1]);
            return 0;
        }
    }

    /* Others need password */
    char password[64];
    vga_puts("Password: ");
    int pos = 0;
    while (pos < 63) {
        char c = keyboard_getchar();
        if (c == '\n') break;
        if (c == '\b' && pos > 0) { pos--; continue; }
        if (c >= 32 && c < 127) password[pos++] = c;
    }
    password[pos] = '\0';
    vga_putchar('\n');

    if (user_switch(user->uid, password) == 0) {
        printk("Switched to %s\n", argv[1]);
        return 0;
    } else {
        vga_puts("Authentication failed\n");
        return 1;
    }
}

/*
 * passwd - Change password
 */
static int cmd_passwd(int argc, char* argv[]) {
    uint32_t target_uid = current_uid;

    if (argc >= 2) {
        /* Only root can change other users' passwords */
        if (current_uid != ROOT_UID) {
            vga_puts("Only root can change other users' passwords\n");
            return 1;
        }
        user_t* user = user_get_by_name(argv[1]);
        if (user == NULL) {
            printk("User '%s' not found\n", argv[1]);
            return 1;
        }
        target_uid = user->uid;
    }

    char password[64];
    vga_puts("New password: ");
    int pos = 0;
    while (pos < 63) {
        char c = keyboard_getchar();
        if (c == '\n') break;
        if (c == '\b' && pos > 0) { pos--; continue; }
        if (c >= 32 && c < 127) password[pos++] = c;
    }
    password[pos] = '\0';
    vga_putchar('\n');

    if (user_set_password(target_uid, password) == 0) {
        vga_puts("Password changed successfully\n");
        return 0;
    } else {
        vga_puts("Failed to change password\n");
        return 1;
    }
}

/*
 * useradd - Add a new user
 */
static int cmd_useradd(int argc, char* argv[]) {
    if (current_uid != ROOT_UID) {
        vga_puts("Permission denied: only root can add users\n");
        return 1;
    }

    if (argc < 2) {
        vga_puts("Usage: useradd <username> [uid] [gid]\n");
        return 1;
    }

    /* Find next available UID */
    uint32_t uid = 1001;
    uint32_t gid = 1000;

    if (argc >= 3) {
        uid = 0;
        for (int i = 0; argv[2][i]; i++) {
            uid = uid * 10 + (argv[2][i] - '0');
        }
    }
    if (argc >= 4) {
        gid = 0;
        for (int i = 0; argv[3][i]; i++) {
            gid = gid * 10 + (argv[3][i] - '0');
        }
    }

    /* Default password is username */
    char home[64];
    snprintf(home, sizeof(home), "/home/%s", argv[1]);

    if (user_add(argv[1], argv[1], uid, gid, home) == 0) {
        /* Create home directory with skel contents */
        user_create_home(argv[1]);
        printk("User '%s' created (uid=%u, gid=%u)\n", argv[1], uid, gid);
        printk("Home directory: %s\n", home);
        printk("Default password is '%s'\n", argv[1]);
        return 0;
    } else {
        vga_puts("Failed to create user\n");
        return 1;
    }
}

/*
 * userdel - Delete a user
 */
static int cmd_userdel(int argc, char* argv[]) {
    if (current_uid != ROOT_UID) {
        vga_puts("Permission denied: only root can delete users\n");
        return 1;
    }

    if (argc < 2) {
        vga_puts("Usage: userdel <username>\n");
        return 1;
    }

    user_t* user = user_get_by_name(argv[1]);
    if (user == NULL) {
        printk("User '%s' not found\n", argv[1]);
        return 1;
    }

    if (user->uid == ROOT_UID) {
        vga_puts("Cannot delete root user\n");
        return 1;
    }

    if (user_del(user->uid) == 0) {
        printk("User '%s' deleted\n", argv[1]);
        return 0;
    } else {
        vga_puts("Failed to delete user\n");
        return 1;
    }
}

/*
 * groupadd - Add a new group
 */
static int cmd_groupadd(int argc, char* argv[]) {
    if (current_uid != ROOT_UID) {
        vga_puts("Permission denied: only root can add groups\n");
        return 1;
    }

    if (argc < 2) {
        vga_puts("Usage: groupadd <groupname> [gid]\n");
        return 1;
    }

    uint32_t gid = 1001;
    if (argc >= 3) {
        gid = 0;
        for (int i = 0; argv[2][i]; i++) {
            gid = gid * 10 + (argv[2][i] - '0');
        }
    }

    if (group_add(argv[1], gid) == 0) {
        printk("Group '%s' created (gid=%u)\n", argv[1], gid);
        return 0;
    } else {
        vga_puts("Failed to create group\n");
        return 1;
    }
}

/*
 * groups - Show groups for a user
 */
static int cmd_groups(int argc, char* argv[]) {
    uint32_t uid = current_uid;

    if (argc >= 2) {
        user_t* user = user_get_by_name(argv[1]);
        if (user == NULL) {
            printk("User '%s' not found\n", argv[1]);
            return 1;
        }
        uid = user->uid;
    }

    user_t* user = user_get(uid);
    if (user == NULL) {
        vga_puts("User not found\n");
        return 1;
    }

    printk("%s : %s", user->username, group_get_name(user->gid));
    vga_putchar('\n');
    return 0;
}

/*
 * chmod - Change file permissions
 */
static int cmd_chmod(int argc, char* argv[]) {
    if (argc < 3) {
        vga_puts("Usage: chmod <mode> <file>\n");
        vga_puts("  mode: octal (e.g., 755, 644)\n");
        return 1;
    }

    /* Parse octal mode */
    uint16_t mode = 0;
    for (int i = 0; argv[1][i]; i++) {
        if (argv[1][i] >= '0' && argv[1][i] <= '7') {
            mode = (mode << 3) | (argv[1][i] - '0');
        } else {
            vga_puts("Invalid mode: use octal (e.g., 755)\n");
            return 1;
        }
    }

    vfs_node_t* node = vfs_lookup(argv[2]);
    if (node == NULL) {
        printk("File not found: %s\n", argv[2]);
        return 1;
    }

    if (vfs_chmod(node, mode) == 0) {
        printk("Changed mode of '%s' to %03o\n", argv[2], mode);
        return 0;
    } else {
        vga_puts("Permission denied\n");
        return 1;
    }
}

/*
 * chown - Change file owner
 */
static int cmd_chown(int argc, char* argv[]) {
    if (argc < 3) {
        vga_puts("Usage: chown <owner>[:<group>] <file>\n");
        return 1;
    }

    if (current_uid != ROOT_UID) {
        vga_puts("Permission denied: only root can chown\n");
        return 1;
    }

    /* Parse owner:group */
    char owner[32];
    char group[32];
    int i = 0, j = 0;

    while (argv[1][i] && argv[1][i] != ':' && i < 31) {
        owner[j++] = argv[1][i++];
    }
    owner[j] = '\0';

    j = 0;
    if (argv[1][i] == ':') {
        i++;
        while (argv[1][i] && j < 31) {
            group[j++] = argv[1][i++];
        }
    }
    group[j] = '\0';

    user_t* user = user_get_by_name(owner);
    if (user == NULL) {
        printk("User '%s' not found\n", owner);
        return 1;
    }

    uint32_t gid = user->gid;
    if (group[0]) {
        group_t* grp = group_get_by_name(group);
        if (grp == NULL) {
            printk("Group '%s' not found\n", group);
            return 1;
        }
        gid = grp->gid;
    }

    vfs_node_t* node = vfs_lookup(argv[2]);
    if (node == NULL) {
        printk("File not found: %s\n", argv[2]);
        return 1;
    }

    if (vfs_chown(node, user->uid, gid) == 0) {
        printk("Changed owner of '%s' to %s:%s\n", argv[2], owner, group_get_name(gid));
        return 0;
    } else {
        vga_puts("Failed to change owner\n");
        return 1;
    }
}

/*
 * id - Show user and group IDs
 */
static int cmd_id(int argc, char* argv[]) {
    uint32_t uid = current_uid;

    if (argc >= 2) {
        user_t* user = user_get_by_name(argv[1]);
        if (user == NULL) {
            printk("User '%s' not found\n", argv[1]);
            return 1;
        }
        uid = user->uid;
    }

    user_t* user = user_get(uid);
    if (user == NULL) {
        vga_puts("User not found\n");
        return 1;
    }

    printk("uid=%u(%s) gid=%u(%s)\n",
           user->uid, user->username,
           user->gid, group_get_name(user->gid));
    return 0;
}

/*
 * man - Display manual page for a command
 */
static int cmd_man(int argc, char* argv[]) {
    if (argc < 2) {
        vga_puts("Usage: man <command>\n");
        vga_puts("Display manual page for a command.\n");
        vga_puts("\nAvailable sections:\n");
        vga_puts("  File System: ls, cat, touch, write, mkdir, rmdir, rm, cp, mv\n");
        vga_puts("               pwd, cd, stat, head, tail, wc, hexdump, xxd, strings\n");
        vga_puts("               mount, find, sort, rev\n");
        vga_puts("  Process:     ps, kill, sleep, top, nice, run, progs, time\n");
        vga_puts("  System:      mem, free, uptime, uname, date, hostname, dmesg, df\n");
        vga_puts("               interrupts, lscpu, reboot\n");
        vga_puts("  User:        login, logout, su, passwd, useradd, userdel\n");
        vga_puts("               groupadd, groups, chmod, chown, id, whoami\n");
        vga_puts("  Shell:       help, clear, echo, history, env, alias, export, set\n");
        vga_puts("               which, type, seq\n");
        vga_puts("  Debug:       peek, poke, dump, heap, regs, gdt, idt, pages, stack\n");
        vga_puts("  Text:        grep, diff\n");
        vga_puts("  Apps:        nano, spreadsheet, basic\n");
        vga_puts("  Misc:        color, version, about, credits, beep, banner, fortune\n");
        return 0;
    }

    const char* cmd = argv[1];

    /* File System Commands */
    if (strcmp(cmd, "ls") == 0) {
        vga_puts("LS(1)                    MiniOS Manual                    LS(1)\n\n");
        vga_puts("NAME\n    ls - list directory contents\n\n");
        vga_puts("SYNOPSIS\n    ls [-lahR1] [directory]\n\n");
        vga_puts("DESCRIPTION\n    List files and directories.\n\n");
        vga_puts("OPTIONS\n");
        vga_puts("    -l    Long format (permissions, size, owner)\n");
        vga_puts("    -a    Show all files including hidden (dot files)\n");
        vga_puts("    -h    Human-readable sizes (K, M, G)\n");
        vga_puts("    -R    Recursive listing of subdirectories\n");
        vga_puts("    -1    One file per line\n\n");
        vga_puts("EXAMPLES\n    ls           List current directory\n");
        vga_puts("    ls -la       Long format with hidden files\n");
        vga_puts("    ls -lh /home Human-readable listing of /home\n");
    } else if (strcmp(cmd, "cat") == 0) {
        vga_puts("CAT(1)                   MiniOS Manual                   CAT(1)\n\n");
        vga_puts("NAME\n    cat - concatenate and display files\n\n");
        vga_puts("SYNOPSIS\n    cat [-nb] <filename>\n\n");
        vga_puts("DESCRIPTION\n    Display the contents of a file to the terminal.\n\n");
        vga_puts("OPTIONS\n");
        vga_puts("    -n    Number all output lines\n");
        vga_puts("    -b    Number non-empty lines only\n\n");
        vga_puts("EXAMPLES\n    cat file.txt      Display contents\n");
        vga_puts("    cat -n file.txt   Display with line numbers\n");
    } else if (strcmp(cmd, "touch") == 0) {
        vga_puts("TOUCH(1)                 MiniOS Manual                 TOUCH(1)\n\n");
        vga_puts("NAME\n    touch - create an empty file\n\n");
        vga_puts("SYNOPSIS\n    touch <filename>\n\n");
        vga_puts("DESCRIPTION\n    Create a new empty file in the current directory.\n\n");
        vga_puts("EXAMPLES\n    touch newfile.txt    Create newfile.txt\n");
    } else if (strcmp(cmd, "write") == 0) {
        vga_puts("WRITE(1)                 MiniOS Manual                 WRITE(1)\n\n");
        vga_puts("NAME\n    write - write text to a file\n\n");
        vga_puts("SYNOPSIS\n    write <filename> <text...>\n\n");
        vga_puts("DESCRIPTION\n    Append text to a file. Creates the file if needed.\n\n");
        vga_puts("EXAMPLES\n    write file.txt Hello World\n");
    } else if (strcmp(cmd, "mkdir") == 0) {
        vga_puts("MKDIR(1)                 MiniOS Manual                 MKDIR(1)\n\n");
        vga_puts("NAME\n    mkdir - create directories\n\n");
        vga_puts("SYNOPSIS\n    mkdir [-pv] <dirname>\n\n");
        vga_puts("DESCRIPTION\n    Create a new directory.\n\n");
        vga_puts("OPTIONS\n");
        vga_puts("    -p    Create parent directories as needed\n");
        vga_puts("    -v    Verbose output\n\n");
        vga_puts("EXAMPLES\n    mkdir docs           Create docs directory\n");
        vga_puts("    mkdir -p a/b/c       Create nested directories\n");
    } else if (strcmp(cmd, "rmdir") == 0) {
        vga_puts("RMDIR(1)                 MiniOS Manual                 RMDIR(1)\n\n");
        vga_puts("NAME\n    rmdir - remove an empty directory\n\n");
        vga_puts("SYNOPSIS\n    rmdir <dirname>\n\n");
        vga_puts("DESCRIPTION\n    Remove an empty directory.\n\n");
        vga_puts("EXAMPLES\n    rmdir olddir    Remove olddir directory\n");
    } else if (strcmp(cmd, "rm") == 0) {
        vga_puts("RM(1)                    MiniOS Manual                    RM(1)\n\n");
        vga_puts("NAME\n    rm - remove files and directories\n\n");
        vga_puts("SYNOPSIS\n    rm [-rfv] <filename>\n\n");
        vga_puts("DESCRIPTION\n    Delete files and directories.\n\n");
        vga_puts("OPTIONS\n");
        vga_puts("    -r    Recursive (remove directories and contents)\n");
        vga_puts("    -f    Force (ignore nonexistent files, no prompts)\n");
        vga_puts("    -v    Verbose (print each file removed)\n\n");
        vga_puts("EXAMPLES\n    rm file.txt        Delete file.txt\n");
        vga_puts("    rm -rf dir/        Remove directory and contents\n");
    } else if (strcmp(cmd, "cp") == 0) {
        vga_puts("CP(1)                    MiniOS Manual                    CP(1)\n\n");
        vga_puts("NAME\n    cp - copy files and directories\n\n");
        vga_puts("SYNOPSIS\n    cp [-rv] <source> <destination>\n\n");
        vga_puts("DESCRIPTION\n    Copy files and directories.\n\n");
        vga_puts("OPTIONS\n");
        vga_puts("    -r    Recursive (copy directories)\n");
        vga_puts("    -v    Verbose (show files being copied)\n\n");
        vga_puts("EXAMPLES\n    cp file.txt backup.txt  Copy a file\n");
        vga_puts("    cp -r src/ dest/       Copy directory recursively\n");
    } else if (strcmp(cmd, "mv") == 0) {
        vga_puts("MV(1)                    MiniOS Manual                    MV(1)\n\n");
        vga_puts("NAME\n    mv - move or rename a file\n\n");
        vga_puts("SYNOPSIS\n    mv <source> <destination>\n\n");
        vga_puts("DESCRIPTION\n    Rename a file or move it to a new location.\n\n");
        vga_puts("EXAMPLES\n    mv old.txt new.txt    Rename old.txt to new.txt\n");
    } else if (strcmp(cmd, "pwd") == 0) {
        vga_puts("PWD(1)                   MiniOS Manual                   PWD(1)\n\n");
        vga_puts("NAME\n    pwd - print working directory\n\n");
        vga_puts("SYNOPSIS\n    pwd\n\n");
        vga_puts("DESCRIPTION\n    Display the current working directory path.\n\n");
        vga_puts("EXAMPLES\n    pwd    Shows: /home/user\n");
    } else if (strcmp(cmd, "cd") == 0) {
        vga_puts("CD(1)                    MiniOS Manual                    CD(1)\n\n");
        vga_puts("NAME\n    cd - change directory\n\n");
        vga_puts("SYNOPSIS\n    cd [directory]\n\n");
        vga_puts("DESCRIPTION\n    Change the current working directory.\n");
        vga_puts("    Without arguments, changes to root (/).\n\n");
        vga_puts("EXAMPLES\n    cd /home      Go to /home\n");
        vga_puts("    cd ..        Go to parent directory\n");
        vga_puts("    cd           Go to root\n");
    } else if (strcmp(cmd, "stat") == 0) {
        vga_puts("STAT(1)                  MiniOS Manual                  STAT(1)\n\n");
        vga_puts("NAME\n    stat - display file information\n\n");
        vga_puts("SYNOPSIS\n    stat <filename>\n\n");
        vga_puts("DESCRIPTION\n    Show detailed information about a file including\n");
        vga_puts("    size, type, and inode number.\n\n");
        vga_puts("EXAMPLES\n    stat file.txt    Show info about file.txt\n");
    } else if (strcmp(cmd, "head") == 0) {
        vga_puts("HEAD(1)                  MiniOS Manual                  HEAD(1)\n\n");
        vga_puts("NAME\n    head - display first lines of a file\n\n");
        vga_puts("SYNOPSIS\n    head [-n lines] <file>\n\n");
        vga_puts("DESCRIPTION\n    Display the first N lines of a file (default 10).\n\n");
        vga_puts("EXAMPLES\n    head file.txt       First 10 lines\n");
        vga_puts("    head -n 5 file.txt  First 5 lines\n");
    } else if (strcmp(cmd, "tail") == 0) {
        vga_puts("TAIL(1)                  MiniOS Manual                  TAIL(1)\n\n");
        vga_puts("NAME\n    tail - display last lines of a file\n\n");
        vga_puts("SYNOPSIS\n    tail [-n lines] <file>\n\n");
        vga_puts("DESCRIPTION\n    Display the last N lines of a file (default 10).\n\n");
        vga_puts("EXAMPLES\n    tail file.txt       Last 10 lines\n");
        vga_puts("    tail -n 5 file.txt  Last 5 lines\n");
    } else if (strcmp(cmd, "wc") == 0) {
        vga_puts("WC(1)                    MiniOS Manual                    WC(1)\n\n");
        vga_puts("NAME\n    wc - word, line, and byte count\n\n");
        vga_puts("SYNOPSIS\n    wc [-lwc] <file>\n\n");
        vga_puts("DESCRIPTION\n    Count lines, words, and bytes in a file.\n\n");
        vga_puts("OPTIONS\n");
        vga_puts("    -l    Print line count only\n");
        vga_puts("    -w    Print word count only\n");
        vga_puts("    -c    Print byte count only\n\n");
        vga_puts("EXAMPLES\n    wc file.txt     Show all counts\n");
        vga_puts("    wc -l file.txt  Show line count only\n");
        vga_puts("    ls | wc -l      Count files in directory\n");
    } else if (strcmp(cmd, "hexdump") == 0) {
        vga_puts("HEXDUMP(1)               MiniOS Manual               HEXDUMP(1)\n\n");
        vga_puts("NAME\n    hexdump - display file in hexadecimal\n\n");
        vga_puts("SYNOPSIS\n    hexdump <file>\n\n");
        vga_puts("DESCRIPTION\n    Display file contents in hexadecimal format.\n\n");
        vga_puts("EXAMPLES\n    hexdump binary.bin    Hex dump of binary.bin\n");
    } else if (strcmp(cmd, "xxd") == 0) {
        vga_puts("XXD(1)                   MiniOS Manual                   XXD(1)\n\n");
        vga_puts("NAME\n    xxd - hex dump with ASCII display\n\n");
        vga_puts("SYNOPSIS\n    xxd <file>\n\n");
        vga_puts("DESCRIPTION\n    Display file in hex with ASCII representation.\n\n");
        vga_puts("EXAMPLES\n    xxd file.bin    Hex+ASCII dump\n");
    } else if (strcmp(cmd, "strings") == 0) {
        vga_puts("STRINGS(1)               MiniOS Manual               STRINGS(1)\n\n");
        vga_puts("NAME\n    strings - print printable strings from file\n\n");
        vga_puts("SYNOPSIS\n    strings <file>\n\n");
        vga_puts("DESCRIPTION\n    Extract and display printable strings from a file.\n\n");
        vga_puts("EXAMPLES\n    strings binary    Find text in binary file\n");
    /* Process Commands */
    } else if (strcmp(cmd, "ps") == 0) {
        vga_puts("PS(1)                    MiniOS Manual                    PS(1)\n\n");
        vga_puts("NAME\n    ps - list running processes\n\n");
        vga_puts("SYNOPSIS\n    ps [-ale]\n\n");
        vga_puts("DESCRIPTION\n    Display a list of running processes.\n\n");
        vga_puts("OPTIONS\n");
        vga_puts("    -a    Show all processes\n");
        vga_puts("    -l    Long format (PID, PPID, UID, state, etc.)\n");
        vga_puts("    -e    Extended information\n\n");
        vga_puts("EXAMPLES\n    ps       Show all processes\n");
        vga_puts("    ps -l    Long format listing\n");
    } else if (strcmp(cmd, "kill") == 0) {
        vga_puts("KILL(1)                  MiniOS Manual                  KILL(1)\n\n");
        vga_puts("NAME\n    kill - send signal to a process\n\n");
        vga_puts("SYNOPSIS\n    kill [-s signal | -signal] <pid>\n");
        vga_puts("           kill -l\n\n");
        vga_puts("DESCRIPTION\n    Send a signal to a process.\n\n");
        vga_puts("OPTIONS\n");
        vga_puts("    -s SIG   Specify signal to send\n");
        vga_puts("    -TERM    Send SIGTERM (same as -s TERM)\n");
        vga_puts("    -9       Send SIGKILL\n");
        vga_puts("    -l       List available signals\n\n");
        vga_puts("EXAMPLES\n    kill 5          Send SIGTERM to PID 5\n");
        vga_puts("    kill -9 5       Force kill PID 5\n");
        vga_puts("    kill -s HUP 5   Send SIGHUP to PID 5\n");
    } else if (strcmp(cmd, "sleep") == 0) {
        vga_puts("SLEEP(1)                 MiniOS Manual                 SLEEP(1)\n\n");
        vga_puts("NAME\n    sleep - pause for specified seconds\n\n");
        vga_puts("SYNOPSIS\n    sleep <seconds>\n\n");
        vga_puts("DESCRIPTION\n    Pause execution for the specified number of seconds.\n\n");
        vga_puts("EXAMPLES\n    sleep 5    Pause for 5 seconds\n");
    } else if (strcmp(cmd, "top") == 0) {
        vga_puts("TOP(1)                   MiniOS Manual                   TOP(1)\n\n");
        vga_puts("NAME\n    top - display process statistics\n\n");
        vga_puts("SYNOPSIS\n    top\n\n");
        vga_puts("DESCRIPTION\n    Show process statistics including PID, name, state,\n");
        vga_puts("    and CPU ticks used.\n\n");
        vga_puts("EXAMPLES\n    top    Show process stats\n");
    } else if (strcmp(cmd, "nice") == 0) {
        vga_puts("NICE(1)                  MiniOS Manual                  NICE(1)\n\n");
        vga_puts("NAME\n    nice - show or set process priority\n\n");
        vga_puts("SYNOPSIS\n    nice [pid] [priority]\n\n");
        vga_puts("DESCRIPTION\n    Display or modify process scheduling priority.\n\n");
        vga_puts("EXAMPLES\n    nice         Show current priority\n");
        vga_puts("    nice 5 10   Set PID 5 priority to 10\n");
    } else if (strcmp(cmd, "run") == 0) {
        vga_puts("RUN(1)                   MiniOS Manual                   RUN(1)\n\n");
        vga_puts("NAME\n    run - execute a program\n\n");
        vga_puts("SYNOPSIS\n    run <program>\n\n");
        vga_puts("DESCRIPTION\n    Execute a built-in program by name.\n\n");
        vga_puts("EXAMPLES\n    run hello    Run the hello program\n");
    } else if (strcmp(cmd, "progs") == 0) {
        vga_puts("PROGS(1)                 MiniOS Manual                 PROGS(1)\n\n");
        vga_puts("NAME\n    progs - list available programs\n\n");
        vga_puts("SYNOPSIS\n    progs\n\n");
        vga_puts("DESCRIPTION\n    Display a list of all available programs.\n\n");
        vga_puts("EXAMPLES\n    progs    List programs\n");
    /* System Commands */
    } else if (strcmp(cmd, "mem") == 0) {
        vga_puts("MEM(1)                   MiniOS Manual                   MEM(1)\n\n");
        vga_puts("NAME\n    mem - display memory information\n\n");
        vga_puts("SYNOPSIS\n    mem\n\n");
        vga_puts("DESCRIPTION\n    Show physical memory usage statistics.\n\n");
        vga_puts("EXAMPLES\n    mem    Show memory info\n");
    } else if (strcmp(cmd, "free") == 0) {
        vga_puts("FREE(1)                  MiniOS Manual                  FREE(1)\n\n");
        vga_puts("NAME\n    free - display memory usage\n\n");
        vga_puts("SYNOPSIS\n    free\n\n");
        vga_puts("DESCRIPTION\n    Show detailed memory usage including heap stats.\n\n");
        vga_puts("EXAMPLES\n    free    Show memory usage\n");
    } else if (strcmp(cmd, "uptime") == 0) {
        vga_puts("UPTIME(1)                MiniOS Manual                UPTIME(1)\n\n");
        vga_puts("NAME\n    uptime - show system uptime\n\n");
        vga_puts("SYNOPSIS\n    uptime\n\n");
        vga_puts("DESCRIPTION\n    Display how long the system has been running.\n\n");
        vga_puts("EXAMPLES\n    uptime    Show uptime\n");
    } else if (strcmp(cmd, "uname") == 0) {
        vga_puts("UNAME(1)                 MiniOS Manual                 UNAME(1)\n\n");
        vga_puts("NAME\n    uname - print system information\n\n");
        vga_puts("SYNOPSIS\n    uname\n\n");
        vga_puts("DESCRIPTION\n    Display system name and version information.\n\n");
        vga_puts("EXAMPLES\n    uname    Show system info\n");
    } else if (strcmp(cmd, "date") == 0) {
        vga_puts("DATE(1)                  MiniOS Manual                  DATE(1)\n\n");
        vga_puts("NAME\n    date - display date and time\n\n");
        vga_puts("SYNOPSIS\n    date\n\n");
        vga_puts("DESCRIPTION\n    Show current system uptime as date/time.\n\n");
        vga_puts("EXAMPLES\n    date    Show date/time\n");
    } else if (strcmp(cmd, "hostname") == 0) {
        vga_puts("HOSTNAME(1)              MiniOS Manual              HOSTNAME(1)\n\n");
        vga_puts("NAME\n    hostname - show system hostname\n\n");
        vga_puts("SYNOPSIS\n    hostname\n\n");
        vga_puts("DESCRIPTION\n    Display the system's hostname.\n\n");
        vga_puts("EXAMPLES\n    hostname    Show hostname\n");
    } else if (strcmp(cmd, "dmesg") == 0) {
        vga_puts("DMESG(1)                 MiniOS Manual                 DMESG(1)\n\n");
        vga_puts("NAME\n    dmesg - print kernel boot messages\n\n");
        vga_puts("SYNOPSIS\n    dmesg\n\n");
        vga_puts("DESCRIPTION\n    Display kernel boot and initialization messages.\n\n");
        vga_puts("EXAMPLES\n    dmesg    Show boot messages\n");
    } else if (strcmp(cmd, "interrupts") == 0) {
        vga_puts("INTERRUPTS(1)            MiniOS Manual            INTERRUPTS(1)\n\n");
        vga_puts("NAME\n    interrupts - show IRQ statistics\n\n");
        vga_puts("SYNOPSIS\n    interrupts\n\n");
        vga_puts("DESCRIPTION\n    Display interrupt request statistics.\n\n");
        vga_puts("EXAMPLES\n    interrupts    Show IRQ stats\n");
    } else if (strcmp(cmd, "lscpu") == 0) {
        vga_puts("LSCPU(1)                 MiniOS Manual                 LSCPU(1)\n\n");
        vga_puts("NAME\n    lscpu - display CPU information\n\n");
        vga_puts("SYNOPSIS\n    lscpu\n\n");
        vga_puts("DESCRIPTION\n    Show CPU vendor and feature information via CPUID.\n\n");
        vga_puts("EXAMPLES\n    lscpu    Show CPU info\n");
    } else if (strcmp(cmd, "reboot") == 0) {
        vga_puts("REBOOT(1)                MiniOS Manual                REBOOT(1)\n\n");
        vga_puts("NAME\n    reboot - restart the system\n\n");
        vga_puts("SYNOPSIS\n    reboot\n\n");
        vga_puts("DESCRIPTION\n    Restart the computer. Use with caution.\n\n");
        vga_puts("EXAMPLES\n    reboot    Restart system\n");
    /* User Commands */
    } else if (strcmp(cmd, "login") == 0) {
        vga_puts("LOGIN(1)                 MiniOS Manual                 LOGIN(1)\n\n");
        vga_puts("NAME\n    login - log in as a user\n\n");
        vga_puts("SYNOPSIS\n    login <username>\n\n");
        vga_puts("DESCRIPTION\n    Log in as the specified user. Prompts for password.\n\n");
        vga_puts("EXAMPLES\n    login guest    Log in as guest\n");
    } else if (strcmp(cmd, "logout") == 0) {
        vga_puts("LOGOUT(1)                MiniOS Manual                LOGOUT(1)\n\n");
        vga_puts("NAME\n    logout - log out current user\n\n");
        vga_puts("SYNOPSIS\n    logout\n\n");
        vga_puts("DESCRIPTION\n    Log out and return to root user.\n\n");
        vga_puts("EXAMPLES\n    logout    Return to root\n");
    } else if (strcmp(cmd, "su") == 0) {
        vga_puts("SU(1)                    MiniOS Manual                    SU(1)\n\n");
        vga_puts("NAME\n    su - switch user\n\n");
        vga_puts("SYNOPSIS\n    su <username>\n\n");
        vga_puts("DESCRIPTION\n    Switch to another user account.\n\n");
        vga_puts("EXAMPLES\n    su guest    Switch to guest user\n");
    } else if (strcmp(cmd, "passwd") == 0) {
        vga_puts("PASSWD(1)                MiniOS Manual                PASSWD(1)\n\n");
        vga_puts("NAME\n    passwd - change user password\n\n");
        vga_puts("SYNOPSIS\n    passwd [username]\n\n");
        vga_puts("DESCRIPTION\n    Change password for current user or specified user\n");
        vga_puts("    (root only for other users).\n\n");
        vga_puts("EXAMPLES\n    passwd         Change own password\n");
        vga_puts("    passwd guest  Change guest's password (root)\n");
    } else if (strcmp(cmd, "useradd") == 0) {
        vga_puts("USERADD(1)               MiniOS Manual               USERADD(1)\n\n");
        vga_puts("NAME\n    useradd - create a new user\n\n");
        vga_puts("SYNOPSIS\n    useradd <username> [uid] [gid]\n\n");
        vga_puts("DESCRIPTION\n    Create a new user account with home directory.\n");
        vga_puts("    Default password is the username. Root only.\n\n");
        vga_puts("EXAMPLES\n    useradd bob         Create user bob\n");
        vga_puts("    useradd bob 1002   Create bob with UID 1002\n");
    } else if (strcmp(cmd, "userdel") == 0) {
        vga_puts("USERDEL(1)               MiniOS Manual               USERDEL(1)\n\n");
        vga_puts("NAME\n    userdel - delete a user\n\n");
        vga_puts("SYNOPSIS\n    userdel <username>\n\n");
        vga_puts("DESCRIPTION\n    Remove a user account. Root only.\n\n");
        vga_puts("EXAMPLES\n    userdel bob    Delete user bob\n");
    } else if (strcmp(cmd, "groupadd") == 0) {
        vga_puts("GROUPADD(1)              MiniOS Manual              GROUPADD(1)\n\n");
        vga_puts("NAME\n    groupadd - create a new group\n\n");
        vga_puts("SYNOPSIS\n    groupadd <groupname> [gid]\n\n");
        vga_puts("DESCRIPTION\n    Create a new group. Root only.\n\n");
        vga_puts("EXAMPLES\n    groupadd developers    Create developers group\n");
    } else if (strcmp(cmd, "groups") == 0) {
        vga_puts("GROUPS(1)                MiniOS Manual                GROUPS(1)\n\n");
        vga_puts("NAME\n    groups - show user's groups\n\n");
        vga_puts("SYNOPSIS\n    groups [username]\n\n");
        vga_puts("DESCRIPTION\n    Display groups the user belongs to.\n\n");
        vga_puts("EXAMPLES\n    groups        Show own groups\n");
        vga_puts("    groups bob   Show bob's groups\n");
    } else if (strcmp(cmd, "chmod") == 0) {
        vga_puts("CHMOD(1)                 MiniOS Manual                 CHMOD(1)\n\n");
        vga_puts("NAME\n    chmod - change file permissions\n\n");
        vga_puts("SYNOPSIS\n    chmod <mode> <file>\n\n");
        vga_puts("DESCRIPTION\n    Change file permissions using octal mode.\n");
        vga_puts("    Mode: owner|group|other (rwx = 4+2+1)\n\n");
        vga_puts("EXAMPLES\n    chmod 755 script    rwxr-xr-x\n");
        vga_puts("    chmod 644 file.txt  rw-r--r--\n");
    } else if (strcmp(cmd, "chown") == 0) {
        vga_puts("CHOWN(1)                 MiniOS Manual                 CHOWN(1)\n\n");
        vga_puts("NAME\n    chown - change file owner\n\n");
        vga_puts("SYNOPSIS\n    chown <user> <file>\n\n");
        vga_puts("DESCRIPTION\n    Change the owner of a file. Root only.\n\n");
        vga_puts("EXAMPLES\n    chown bob file.txt    Make bob owner\n");
    } else if (strcmp(cmd, "id") == 0) {
        vga_puts("ID(1)                    MiniOS Manual                    ID(1)\n\n");
        vga_puts("NAME\n    id - display user and group IDs\n\n");
        vga_puts("SYNOPSIS\n    id [username]\n\n");
        vga_puts("DESCRIPTION\n    Show UID and GID for current or specified user.\n\n");
        vga_puts("EXAMPLES\n    id         Show own IDs\n");
        vga_puts("    id guest   Show guest's IDs\n");
    } else if (strcmp(cmd, "whoami") == 0) {
        vga_puts("WHOAMI(1)                MiniOS Manual                WHOAMI(1)\n\n");
        vga_puts("NAME\n    whoami - print current username\n\n");
        vga_puts("SYNOPSIS\n    whoami\n\n");
        vga_puts("DESCRIPTION\n    Display the username of the current user.\n\n");
        vga_puts("EXAMPLES\n    whoami    Shows: root\n");
    /* Shell Commands */
    } else if (strcmp(cmd, "help") == 0) {
        vga_puts("HELP(1)                  MiniOS Manual                  HELP(1)\n\n");
        vga_puts("NAME\n    help - display available commands\n\n");
        vga_puts("SYNOPSIS\n    help\n\n");
        vga_puts("DESCRIPTION\n    List all available shell commands with descriptions.\n\n");
        vga_puts("SEE ALSO\n    man - for detailed command documentation\n");
    } else if (strcmp(cmd, "clear") == 0) {
        vga_puts("CLEAR(1)                 MiniOS Manual                 CLEAR(1)\n\n");
        vga_puts("NAME\n    clear - clear the terminal screen\n\n");
        vga_puts("SYNOPSIS\n    clear\n\n");
        vga_puts("DESCRIPTION\n    Clear all text from the terminal screen.\n\n");
        vga_puts("EXAMPLES\n    clear    Clear screen\n");
    } else if (strcmp(cmd, "echo") == 0) {
        vga_puts("ECHO(1)                  MiniOS Manual                  ECHO(1)\n\n");
        vga_puts("NAME\n    echo - display text\n\n");
        vga_puts("SYNOPSIS\n    echo [-neE] [text...]\n\n");
        vga_puts("DESCRIPTION\n    Print arguments to the terminal.\n\n");
        vga_puts("OPTIONS\n");
        vga_puts("    -n    Do not output trailing newline\n");
        vga_puts("    -e    Enable interpretation of backslash escapes\n");
        vga_puts("    -E    Disable interpretation of escapes (default)\n\n");
        vga_puts("ESCAPE SEQUENCES (with -e)\n");
        vga_puts("    \\\\n   newline    \\\\t   tab    \\\\\\\\   backslash\n\n");
        vga_puts("EXAMPLES\n    echo Hello World      Prints: Hello World\n");
        vga_puts("    echo -n no newline   No trailing newline\n");
        vga_puts("    echo -e 'a\\\\tb'       Tab between a and b\n");
    } else if (strcmp(cmd, "history") == 0) {
        vga_puts("HISTORY(1)               MiniOS Manual               HISTORY(1)\n\n");
        vga_puts("NAME\n    history - show command history\n\n");
        vga_puts("SYNOPSIS\n    history\n\n");
        vga_puts("DESCRIPTION\n    Display previously executed commands.\n\n");
        vga_puts("EXAMPLES\n    history    Show command history\n");
    } else if (strcmp(cmd, "env") == 0) {
        vga_puts("ENV(1)                   MiniOS Manual                   ENV(1)\n\n");
        vga_puts("NAME\n    env - show environment variables\n\n");
        vga_puts("SYNOPSIS\n    env\n\n");
        vga_puts("DESCRIPTION\n    Display all environment variables.\n\n");
        vga_puts("EXAMPLES\n    env    Show environment\n");
    } else if (strcmp(cmd, "alias") == 0) {
        vga_puts("ALIAS(1)                 MiniOS Manual                 ALIAS(1)\n\n");
        vga_puts("NAME\n    alias - create command alias\n\n");
        vga_puts("SYNOPSIS\n    alias [name=value]\n\n");
        vga_puts("DESCRIPTION\n    Create or display command aliases.\n\n");
        vga_puts("EXAMPLES\n    alias    Show aliases\n");
    } else if (strcmp(cmd, "export") == 0) {
        vga_puts("EXPORT(1)                MiniOS Manual                EXPORT(1)\n\n");
        vga_puts("NAME\n    export - set environment variable\n\n");
        vga_puts("SYNOPSIS\n    export [name=value]\n\n");
        vga_puts("DESCRIPTION\n    Set or display environment variables.\n\n");
        vga_puts("EXAMPLES\n    export PATH=/bin    Set PATH\n");
    } else if (strcmp(cmd, "set") == 0) {
        vga_puts("SET(1)                   MiniOS Manual                   SET(1)\n\n");
        vga_puts("NAME\n    set - show or set shell options\n\n");
        vga_puts("SYNOPSIS\n    set [option]\n\n");
        vga_puts("DESCRIPTION\n    Display or modify shell options.\n\n");
        vga_puts("EXAMPLES\n    set    Show options\n");
    /* Debug Commands */
    } else if (strcmp(cmd, "peek") == 0) {
        vga_puts("PEEK(1)                  MiniOS Manual                  PEEK(1)\n\n");
        vga_puts("NAME\n    peek - read memory address\n\n");
        vga_puts("SYNOPSIS\n    peek <address>\n\n");
        vga_puts("DESCRIPTION\n    Read and display value at memory address (hex).\n\n");
        vga_puts("EXAMPLES\n    peek 0xB8000    Read VGA memory\n");
    } else if (strcmp(cmd, "poke") == 0) {
        vga_puts("POKE(1)                  MiniOS Manual                  POKE(1)\n\n");
        vga_puts("NAME\n    poke - write to memory address\n\n");
        vga_puts("SYNOPSIS\n    poke <address> <value>\n\n");
        vga_puts("DESCRIPTION\n    Write value to memory address. Use with caution!\n\n");
        vga_puts("EXAMPLES\n    poke 0xB8000 0x0741    Write 'A' to VGA\n");
    } else if (strcmp(cmd, "dump") == 0) {
        vga_puts("DUMP(1)                  MiniOS Manual                  DUMP(1)\n\n");
        vga_puts("NAME\n    dump - dump memory range\n\n");
        vga_puts("SYNOPSIS\n    dump <address> [length]\n\n");
        vga_puts("DESCRIPTION\n    Display memory contents in hex format.\n\n");
        vga_puts("EXAMPLES\n    dump 0x100000 64    Dump 64 bytes at 1MB\n");
    } else if (strcmp(cmd, "heap") == 0) {
        vga_puts("HEAP(1)                  MiniOS Manual                  HEAP(1)\n\n");
        vga_puts("NAME\n    heap - show heap statistics\n\n");
        vga_puts("SYNOPSIS\n    heap\n\n");
        vga_puts("DESCRIPTION\n    Display kernel heap usage and block information.\n\n");
        vga_puts("EXAMPLES\n    heap    Show heap stats\n");
    } else if (strcmp(cmd, "regs") == 0) {
        vga_puts("REGS(1)                  MiniOS Manual                  REGS(1)\n\n");
        vga_puts("NAME\n    regs - display CPU registers\n\n");
        vga_puts("SYNOPSIS\n    regs\n\n");
        vga_puts("DESCRIPTION\n    Show current CPU register values.\n\n");
        vga_puts("EXAMPLES\n    regs    Show registers\n");
    } else if (strcmp(cmd, "gdt") == 0) {
        vga_puts("GDT(1)                   MiniOS Manual                   GDT(1)\n\n");
        vga_puts("NAME\n    gdt - show Global Descriptor Table\n\n");
        vga_puts("SYNOPSIS\n    gdt\n\n");
        vga_puts("DESCRIPTION\n    Display GDT entries and segment information.\n\n");
        vga_puts("EXAMPLES\n    gdt    Show GDT\n");
    } else if (strcmp(cmd, "idt") == 0) {
        vga_puts("IDT(1)                   MiniOS Manual                   IDT(1)\n\n");
        vga_puts("NAME\n    idt - show Interrupt Descriptor Table\n\n");
        vga_puts("SYNOPSIS\n    idt\n\n");
        vga_puts("DESCRIPTION\n    Display IDT summary and interrupt handlers.\n\n");
        vga_puts("EXAMPLES\n    idt    Show IDT\n");
    } else if (strcmp(cmd, "pages") == 0) {
        vga_puts("PAGES(1)                 MiniOS Manual                 PAGES(1)\n\n");
        vga_puts("NAME\n    pages - show page table information\n\n");
        vga_puts("SYNOPSIS\n    pages\n\n");
        vga_puts("DESCRIPTION\n    Display paging and virtual memory information.\n\n");
        vga_puts("EXAMPLES\n    pages    Show page tables\n");
    } else if (strcmp(cmd, "stack") == 0) {
        vga_puts("STACK(1)                 MiniOS Manual                 STACK(1)\n\n");
        vga_puts("NAME\n    stack - show stack trace\n\n");
        vga_puts("SYNOPSIS\n    stack\n\n");
        vga_puts("DESCRIPTION\n    Display current stack with return addresses.\n\n");
        vga_puts("EXAMPLES\n    stack    Show stack trace\n");
    /* Text Commands */
    } else if (strcmp(cmd, "grep") == 0) {
        vga_puts("GREP(1)                  MiniOS Manual                  GREP(1)\n\n");
        vga_puts("NAME\n    grep - search for pattern in files\n\n");
        vga_puts("SYNOPSIS\n    grep [-ivnc] <pattern> [file]\n\n");
        vga_puts("DESCRIPTION\n    Search for lines matching pattern.\n\n");
        vga_puts("OPTIONS\n");
        vga_puts("    -i    Case-insensitive matching\n");
        vga_puts("    -v    Invert match (show non-matching lines)\n");
        vga_puts("    -n    Show line numbers\n");
        vga_puts("    -c    Count matching lines only\n\n");
        vga_puts("EXAMPLES\n    grep error log.txt       Find 'error' in log.txt\n");
        vga_puts("    grep -i ERROR log.txt   Case-insensitive search\n");
        vga_puts("    grep -n TODO *.c        Show line numbers\n");
        vga_puts("    ls | grep txt           Filter pipe output\n");
    } else if (strcmp(cmd, "diff") == 0) {
        vga_puts("DIFF(1)                  MiniOS Manual                  DIFF(1)\n\n");
        vga_puts("NAME\n    diff - compare two files\n\n");
        vga_puts("SYNOPSIS\n    diff <file1> <file2>\n\n");
        vga_puts("DESCRIPTION\n    Compare two files and show differences.\n\n");
        vga_puts("EXAMPLES\n    diff old.txt new.txt    Compare files\n");
    /* Misc Commands */
    } else if (strcmp(cmd, "color") == 0) {
        vga_puts("COLOR(1)                 MiniOS Manual                 COLOR(1)\n\n");
        vga_puts("NAME\n    color - set terminal text color\n\n");
        vga_puts("SYNOPSIS\n    color <fg> [bg]\n\n");
        vga_puts("DESCRIPTION\n    Set foreground and background colors (0-15).\n\n");
        vga_puts("EXAMPLES\n    color 10      Light green text\n");
        vga_puts("    color 15 1   White on blue\n");
    } else if (strcmp(cmd, "version") == 0) {
        vga_puts("VERSION(1)               MiniOS Manual               VERSION(1)\n\n");
        vga_puts("NAME\n    version - show MiniOS version\n\n");
        vga_puts("SYNOPSIS\n    version\n\n");
        vga_puts("DESCRIPTION\n    Display MiniOS version information.\n\n");
        vga_puts("EXAMPLES\n    version    Show version\n");
    } else if (strcmp(cmd, "about") == 0) {
        vga_puts("ABOUT(1)                 MiniOS Manual                 ABOUT(1)\n\n");
        vga_puts("NAME\n    about - about MiniOS\n\n");
        vga_puts("SYNOPSIS\n    about\n\n");
        vga_puts("DESCRIPTION\n    Display information about MiniOS.\n\n");
        vga_puts("EXAMPLES\n    about    Show about info\n");
    } else if (strcmp(cmd, "credits") == 0) {
        vga_puts("CREDITS(1)               MiniOS Manual               CREDITS(1)\n\n");
        vga_puts("NAME\n    credits - show credits\n\n");
        vga_puts("SYNOPSIS\n    credits\n\n");
        vga_puts("DESCRIPTION\n    Display MiniOS credits and acknowledgments.\n\n");
        vga_puts("EXAMPLES\n    credits    Show credits\n");
    } else if (strcmp(cmd, "beep") == 0) {
        vga_puts("BEEP(1)                  MiniOS Manual                  BEEP(1)\n\n");
        vga_puts("NAME\n    beep - make a beep sound\n\n");
        vga_puts("SYNOPSIS\n    beep\n\n");
        vga_puts("DESCRIPTION\n    Play a beep using the PC speaker.\n\n");
        vga_puts("EXAMPLES\n    beep    Make beep sound\n");
    } else if (strcmp(cmd, "banner") == 0) {
        vga_puts("BANNER(1)                MiniOS Manual                BANNER(1)\n\n");
        vga_puts("NAME\n    banner - display ASCII art text\n\n");
        vga_puts("SYNOPSIS\n    banner <text>\n\n");
        vga_puts("DESCRIPTION\n    Display text as large ASCII art letters.\n\n");
        vga_puts("EXAMPLES\n    banner HELLO    Display HELLO in ASCII art\n");
    } else if (strcmp(cmd, "fortune") == 0) {
        vga_puts("FORTUNE(1)               MiniOS Manual               FORTUNE(1)\n\n");
        vga_puts("NAME\n    fortune - display random quote\n\n");
        vga_puts("SYNOPSIS\n    fortune\n\n");
        vga_puts("DESCRIPTION\n    Display a random programming quote or fortune.\n\n");
        vga_puts("EXAMPLES\n    fortune    Show random quote\n");
    } else if (strcmp(cmd, "man") == 0) {
        vga_puts("MAN(1)                   MiniOS Manual                   MAN(1)\n\n");
        vga_puts("NAME\n    man - display command manual\n\n");
        vga_puts("SYNOPSIS\n    man <command>\n\n");
        vga_puts("DESCRIPTION\n    Display the manual page for a command, including\n");
        vga_puts("    usage, description, and examples.\n\n");
        vga_puts("EXAMPLES\n    man ls      Show manual for ls\n");
        vga_puts("    man chmod   Show manual for chmod\n");
    /* Daemon Commands */
    } else if (strcmp(cmd, "daemons") == 0) {
        vga_puts("DAEMONS(1)               MiniOS Manual               DAEMONS(1)\n\n");
        vga_puts("NAME\n    daemons - list system daemons\n\n");
        vga_puts("SYNOPSIS\n    daemons\n\n");
        vga_puts("DESCRIPTION\n    Display status of all system daemons including\n");
        vga_puts("    their PID, state, and description.\n\n");
        vga_puts("EXAMPLES\n    daemons    Show all daemon status\n");
    } else if (strcmp(cmd, "service") == 0) {
        vga_puts("SERVICE(1)               MiniOS Manual               SERVICE(1)\n\n");
        vga_puts("NAME\n    service - manage system services\n\n");
        vga_puts("SYNOPSIS\n    service <name> [action]\n\n");
        vga_puts("DESCRIPTION\n    Start, stop, restart, or check status of daemons.\n");
        vga_puts("    Actions: start, stop, restart, status (default)\n\n");
        vga_puts("EXAMPLES\n    service klogd status   Check klogd status\n");
        vga_puts("    service crond restart  Restart crond\n");
    /* Applications */
    } else if (strcmp(cmd, "spreadsheet") == 0) {
        vga_puts("SPREADSHEET(1)           MiniOS Manual           SPREADSHEET(1)\n\n");
        vga_puts("NAME\n    spreadsheet - spreadsheet application\n\n");
        vga_puts("SYNOPSIS\n    spreadsheet [file.csv]\n\n");
        vga_puts("DESCRIPTION\n    Full-featured spreadsheet with formulas.\n");
        vga_puts("    Arrows: Navigate  Enter: Edit cell  ESC: Exit\n");
        vga_puts("    F2: Save  F3: Load  F5: Clear all\n\n");
        vga_puts("FORMULAS\n    =A1+B1  =A1*2  =SUM(A1:A10)\n\n");
        vga_puts("EXAMPLES\n    spreadsheet           New spreadsheet\n");
        vga_puts("    spreadsheet data.csv  Open file\n");
    /* New Commands */
    } else if (strcmp(cmd, "which") == 0) {
        vga_puts("WHICH(1)                 MiniOS Manual                 WHICH(1)\n\n");
        vga_puts("NAME\n    which - locate a command\n\n");
        vga_puts("SYNOPSIS\n    which <command>\n\n");
        vga_puts("DESCRIPTION\n    Show whether a command is a shell built-in or an\n");
        vga_puts("    external program in /bin. Displays the path if found.\n\n");
        vga_puts("EXAMPLES\n    which ls       Shows: ls: shell built-in\n");
        vga_puts("    which shell    Shows: /bin/shell\n");
    } else if (strcmp(cmd, "type") == 0) {
        vga_puts("TYPE(1)                  MiniOS Manual                  TYPE(1)\n\n");
        vga_puts("NAME\n    type - display command type\n\n");
        vga_puts("SYNOPSIS\n    type <command>\n\n");
        vga_puts("DESCRIPTION\n    Show the type of a command: shell builtin, external\n");
        vga_puts("    program, or alias.\n\n");
        vga_puts("EXAMPLES\n    type echo     Shows: echo is a shell builtin\n");
        vga_puts("    type shell    Shows: shell is /bin/shell\n");
    } else if (strcmp(cmd, "df") == 0) {
        vga_puts("DF(1)                    MiniOS Manual                    DF(1)\n\n");
        vga_puts("NAME\n    df - report filesystem disk space usage\n\n");
        vga_puts("SYNOPSIS\n    df\n\n");
        vga_puts("DESCRIPTION\n    Display information about mounted filesystems including\n");
        vga_puts("    total blocks, used space, available space, and mount point.\n\n");
        vga_puts("EXAMPLES\n    df    Show all mounted filesystems\n");
    } else if (strcmp(cmd, "find") == 0) {
        vga_puts("FIND(1)                  MiniOS Manual                  FIND(1)\n\n");
        vga_puts("NAME\n    find - search for files in a directory hierarchy\n\n");
        vga_puts("SYNOPSIS\n    find [path] [-name pattern]\n\n");
        vga_puts("DESCRIPTION\n    Recursively search for files matching a pattern.\n");
        vga_puts("    Without -name, lists all files. Pattern uses substring match.\n\n");
        vga_puts("OPTIONS\n    -name pattern    Match files containing pattern\n\n");
        vga_puts("EXAMPLES\n    find /home           List all files under /home\n");
        vga_puts("    find . -name txt    Find files containing 'txt'\n");
        vga_puts("    find /mnt -name log Find log files on mounted drive\n");
    } else if (strcmp(cmd, "sort") == 0) {
        vga_puts("SORT(1)                  MiniOS Manual                  SORT(1)\n\n");
        vga_puts("NAME\n    sort - sort lines of text files\n\n");
        vga_puts("SYNOPSIS\n    sort [-rnuf] [file]\n\n");
        vga_puts("DESCRIPTION\n    Read input and output lines in sorted order.\n\n");
        vga_puts("OPTIONS\n");
        vga_puts("    -r    Reverse sort order\n");
        vga_puts("    -n    Numeric sort (compare as numbers)\n");
        vga_puts("    -u    Unique (remove duplicate lines)\n");
        vga_puts("    -f    Case-insensitive sorting\n\n");
        vga_puts("EXAMPLES\n    sort names.txt       Sort alphabetically\n");
        vga_puts("    sort -r names.txt   Reverse order\n");
        vga_puts("    sort -n numbers.txt Sort numerically\n");
        vga_puts("    ls | sort -u        Unique sorted listing\n");
    } else if (strcmp(cmd, "time") == 0) {
        vga_puts("TIME(1)                  MiniOS Manual                  TIME(1)\n\n");
        vga_puts("NAME\n    time - time command execution\n\n");
        vga_puts("SYNOPSIS\n    time <command> [args...]\n\n");
        vga_puts("DESCRIPTION\n    Execute a command and report how long it took.\n");
        vga_puts("    Shows elapsed time in seconds and milliseconds.\n\n");
        vga_puts("EXAMPLES\n    time sleep 2       Time a 2-second sleep\n");
        vga_puts("    time find /        Time a recursive find\n");
        vga_puts("    time cat bigfile   Time reading a large file\n");
    } else if (strcmp(cmd, "seq") == 0) {
        vga_puts("SEQ(1)                   MiniOS Manual                   SEQ(1)\n\n");
        vga_puts("NAME\n    seq - print a sequence of numbers\n\n");
        vga_puts("SYNOPSIS\n    seq [start] [step] end\n\n");
        vga_puts("DESCRIPTION\n    Print numbers from start to end. Default start is 1,\n");
        vga_puts("    default step is 1. Negative step counts down.\n\n");
        vga_puts("EXAMPLES\n    seq 5           Print 1 2 3 4 5\n");
        vga_puts("    seq 2 5         Print 2 3 4 5\n");
        vga_puts("    seq 1 2 10      Print 1 3 5 7 9 (step 2)\n");
        vga_puts("    seq 10 -1 1     Print 10 9 8 ... 1\n");
    } else if (strcmp(cmd, "rev") == 0) {
        vga_puts("REV(1)                   MiniOS Manual                   REV(1)\n\n");
        vga_puts("NAME\n    rev - reverse lines of a file\n\n");
        vga_puts("SYNOPSIS\n    rev <filename>\n\n");
        vga_puts("DESCRIPTION\n    Read a file and output each line with characters\n");
        vga_puts("    reversed. Useful for text manipulation.\n\n");
        vga_puts("EXAMPLES\n    rev file.txt    Reverse each line\n");
        vga_puts("    (hello -> olleh)\n");
    } else if (strcmp(cmd, "nano") == 0) {
        vga_puts("NANO(1)                  MiniOS Manual                  NANO(1)\n\n");
        vga_puts("NAME\n    nano - text editor\n\n");
        vga_puts("SYNOPSIS\n    nano [filename]\n\n");
        vga_puts("DESCRIPTION\n    Full-featured text editor with cut/copy/paste,\n");
        vga_puts("    search, and file save/load capabilities.\n\n");
        vga_puts("KEYS\n    Ctrl+O: Save   Ctrl+X: Exit   Ctrl+K: Cut line\n");
        vga_puts("    Ctrl+U: Paste  Ctrl+W: Search Ctrl+G: Go to line\n\n");
        vga_puts("EXAMPLES\n    nano              New file\n");
        vga_puts("    nano file.txt     Edit file.txt\n");
    } else if (strcmp(cmd, "basic") == 0) {
        vga_puts("BASIC(1)                 MiniOS Manual                 BASIC(1)\n\n");
        vga_puts("NAME\n    basic - BASIC programming language interpreter\n\n");
        vga_puts("SYNOPSIS\n    basic [file.bas]\n\n");
        vga_puts("DESCRIPTION\n    Interactive BASIC interpreter supporting classic\n");
        vga_puts("    commands and statements. Programs can be saved/loaded.\n\n");
        vga_puts("COMMANDS\n    RUN       Execute the program\n");
        vga_puts("    LIST      Display program listing\n");
        vga_puts("    NEW       Clear program from memory\n");
        vga_puts("    SAVE \"f\"  Save program to file\n");
        vga_puts("    LOAD \"f\"  Load program from file\n");
        vga_puts("    BYE       Exit BASIC interpreter\n\n");
        vga_puts("STATEMENTS\n    PRINT expr    Output to screen\n");
        vga_puts("    INPUT var     Read user input\n");
        vga_puts("    LET var=expr  Assign variable\n");
        vga_puts("    IF cond THEN  Conditional execution\n");
        vga_puts("    GOTO line     Jump to line number\n");
        vga_puts("    GOSUB/RETURN  Subroutine call\n");
        vga_puts("    FOR/NEXT      Loop construct\n");
        vga_puts("    REM comment   Comment line\n");
        vga_puts("    END           End program\n");
        vga_puts("    CLS           Clear screen\n\n");
        vga_puts("FUNCTIONS\n    RND(x)  Random number 0-1\n");
        vga_puts("    ABS(x)  Absolute value\n");
        vga_puts("    INT(x)  Integer part\n\n");
        vga_puts("EXAMPLES\n    10 PRINT \"HELLO WORLD\"\n");
        vga_puts("    20 FOR I=1 TO 10\n");
        vga_puts("    30 PRINT I\n");
        vga_puts("    40 NEXT I\n");
    } else if (strcmp(cmd, "mount") == 0) {
        vga_puts("MOUNT(1)                 MiniOS Manual                 MOUNT(1)\n\n");
        vga_puts("NAME\n    mount - mount a filesystem\n\n");
        vga_puts("SYNOPSIS\n    mount <device> <mountpoint> <fstype>\n\n");
        vga_puts("DESCRIPTION\n    Mount a block device at a directory. Currently\n");
        vga_puts("    supports ext2 filesystem type.\n\n");
        vga_puts("EXAMPLES\n    mount hd0p1 /mnt ext2    Mount partition 1 at /mnt\n");
    } else {
        printk("No manual entry for '%s'\n", cmd);
        vga_puts("Use 'man' without arguments to see available topics.\n");
        return 1;
    }

    return 0;
}

/*
 * daemons - List system daemons
 */
static int cmd_daemons(int argc, char* argv[]) {
    (void)argc; (void)argv;
    daemon_print_status();
    return 0;
}

/*
 * service - Manage system services/daemons
 */
static int cmd_service(int argc, char* argv[]) {
    if (argc < 2) {
        vga_puts("Usage: service <name> <action>\n");
        vga_puts("Actions: start, stop, restart, status\n");
        vga_puts("\nAvailable services:\n");
        for (int i = 0; i < num_daemons; i++) {
            printk("  %s - %s\n", daemons[i].name, daemons[i].description);
        }
        return 0;
    }

    const char* name = argv[1];
    const char* action = (argc >= 3) ? argv[2] : "status";

    daemon_t* d = daemon_get(name);
    if (d == NULL) {
        printk("Unknown service: %s\n", name);
        return 1;
    }

    if (strcmp(action, "start") == 0) {
        if (current_uid != ROOT_UID) {
            vga_puts("Permission denied: only root can start services\n");
            return 1;
        }
        if (daemon_start(name) == 0) {
            printk("Service '%s' started (PID %d)\n", name, d->pid);
        } else {
            printk("Failed to start service '%s'\n", name);
            return 1;
        }
    } else if (strcmp(action, "stop") == 0) {
        if (current_uid != ROOT_UID) {
            vga_puts("Permission denied: only root can stop services\n");
            return 1;
        }
        if (daemon_stop(name) == 0) {
            printk("Service '%s' stopped\n", name);
        } else {
            printk("Failed to stop service '%s'\n", name);
            return 1;
        }
    } else if (strcmp(action, "restart") == 0) {
        if (current_uid != ROOT_UID) {
            vga_puts("Permission denied: only root can restart services\n");
            return 1;
        }
        if (daemon_restart(name) == 0) {
            printk("Service '%s' restarted (PID %d)\n", name, d->pid);
        } else {
            printk("Failed to restart service '%s'\n", name);
            return 1;
        }
    } else if (strcmp(action, "status") == 0) {
        const char* state_str;
        switch (d->state) {
            case DAEMON_STOPPED:  state_str = "stopped"; break;
            case DAEMON_STARTING: state_str = "starting"; break;
            case DAEMON_RUNNING:  state_str = "running"; break;
            case DAEMON_STOPPING: state_str = "stopping"; break;
            case DAEMON_FAILED:   state_str = "failed"; break;
            default:              state_str = "unknown"; break;
        }
        printk("Service: %s\n", d->name);
        printk("  Description: %s\n", d->description);
        printk("  State: %s\n", state_str);
        if (d->pid >= 0) {
            printk("  PID: %d\n", d->pid);
        }
        printk("  Restarts: %u\n", d->restart_count);
    } else {
        printk("Unknown action: %s\n", action);
        vga_puts("Valid actions: start, stop, restart, status\n");
        return 1;
    }

    return 0;
}

/*
 * spreadsheet - Launch spreadsheet application
 */
static int cmd_spreadsheet(int argc, char* argv[]) {
    if (argc > 1) {
        spreadsheet_run_file(argv[1]);
    } else {
        spreadsheet_run();
    }

    /* Restore shell display */
    vga_clear();
    vga_puts("Returned from spreadsheet.\n");
    return 0;
}

/*
 * nano - Text editor
 */
static int cmd_nano(int argc, char* argv[]) {
    /* Check for help */
    if (argc > 1 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        vga_puts("Usage: nano [FILE]\n");
        vga_puts("\n");
        vga_puts("Open the nano text editor.\n");
        vga_puts("\n");
        vga_puts("Arguments:\n");
        vga_puts("  FILE    File to open (optional)\n");
        vga_puts("\n");
        vga_puts("Keyboard Shortcuts:\n");
        vga_puts("  Ctrl+X    Exit\n");
        vga_puts("  Ctrl+O    Save file\n");
        vga_puts("  Ctrl+R    Read/insert file\n");
        vga_puts("  Ctrl+W    Search\n");
        vga_puts("  Ctrl+\\    Search and replace\n");
        vga_puts("  Ctrl+G    Help screen\n");
        vga_puts("  Ctrl+K    Cut line\n");
        vga_puts("  Ctrl+U    Paste\n");
        vga_puts("  Ctrl+6    Copy line\n");
        vga_puts("  Ctrl+Z    Undo\n");
        vga_puts("  Ctrl+_    Go to line\n");
        vga_puts("  Ctrl+C    Show cursor position\n");
        vga_puts("  Ctrl+L    Refresh screen\n");
        vga_puts("  Ctrl+A    Go to start of line\n");
        vga_puts("  Ctrl+E    Go to end of line\n");
        vga_puts("\n");
        vga_puts("Examples:\n");
        vga_puts("  nano           Open new file\n");
        vga_puts("  nano file.txt  Open file.txt for editing\n");
        return 0;
    }

    if (argc > 1) {
        nano_run_file(argv[1]);
    } else {
        nano_run();
    }

    /* Restore shell display */
    vga_clear();
    vga_puts("Returned from nano.\n");
    return 0;
}

/*
 * basic - BASIC interpreter
 */
static int cmd_basic(int argc, char* argv[]) {
    if (argc > 1 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        vga_puts("Usage: basic [FILE.BAS]\n\n");
        vga_puts("Start the BASIC interpreter.\n\n");
        vga_puts("Commands:\n");
        vga_puts("  RUN       Execute program\n");
        vga_puts("  LIST      Show program listing\n");
        vga_puts("  NEW       Clear program\n");
        vga_puts("  SAVE      Save to file\n");
        vga_puts("  LOAD      Load from file\n");
        vga_puts("  BYE       Exit BASIC\n\n");
        vga_puts("Statements:\n");
        vga_puts("  PRINT, INPUT, LET, IF/THEN\n");
        vga_puts("  GOTO, GOSUB, RETURN, FOR/NEXT\n");
        vga_puts("  END, REM, CLS\n\n");
        vga_puts("Examples:\n");
        vga_puts("  basic              Start interactive\n");
        vga_puts("  basic program.bas  Load and run file\n");
        return 0;
    }

    if (argc > 1) {
        basic_run(argv[1]);
    } else {
        basic_run(NULL);
    }

    return 0;
}

/*
 * xgui - Start graphical interface
 */
static int cmd_xgui(int argc, char* argv[]) {
    if (argc > 1 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        vga_puts("Usage: xgui\n\n");
        vga_puts("Start the MiniOS graphical user interface.\n\n");
        vga_puts("Requirements:\n");
        vga_puts("  - VESA graphics mode must be available\n");
        vga_puts("  - PS/2 mouse recommended\n\n");
        vga_puts("Controls:\n");
        vga_puts("  ESC       Exit GUI and return to shell\n");
        vga_puts("  Mouse     Click and drag windows\n");
        return 0;
    }

    vga_puts("Starting XGUI...\n");
    xgui_run();
    vga_puts("XGUI exited.\n");
    
    return 0;
}

/*
 * Run the shell main loop
 */
void shell_run(void) {
    vga_puts("\n");
    vga_write("=====================================\n",
              vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    vga_write("  Welcome to MiniOS Shell!          \n",
              vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
    vga_write("  Type 'help' for commands          \n",
              vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    vga_write("=====================================\n",
              vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    vga_puts("\n");

    shell_prompt();

    while (1) {
        char c = keyboard_getchar();
        uint8_t uc = (uint8_t)c;

        /* Get modifier state for this keypress (captured at time key was pressed) */
        uint16_t mods = keyboard_get_modifiers();

        /* Handle arrow keys for history navigation */
        if (uc == KEY_UP) {
            /* Navigate to previous command in history */
            if (history_index > 0) {
                history_index--;
                /* Clear current input */
                while (cursor_pos > 0) {
                    cursor_pos--;
                    vga_move_cursor_left();
                }
                /* Clear displayed text */
                for (int i = 0; i < input_len; i++) {
                    vga_putchar(' ');
                }
                for (int i = 0; i < input_len; i++) {
                    vga_move_cursor_left();
                }
                /* Copy history entry to input buffer */
                strncpy(input_buffer, history[history_index], SHELL_MAX_CMD_LEN - 1);
                input_buffer[SHELL_MAX_CMD_LEN - 1] = '\0';
                input_len = strlen(input_buffer);
                cursor_pos = input_len;
                vga_puts(input_buffer);
            }
            continue;
        } else if (uc == KEY_DOWN) {
            /* Navigate to next command in history */
            if (history_index < history_count - 1) {
                history_index++;
                /* Clear current input */
                while (cursor_pos > 0) {
                    cursor_pos--;
                    vga_move_cursor_left();
                }
                /* Clear displayed text */
                for (int i = 0; i < input_len; i++) {
                    vga_putchar(' ');
                }
                for (int i = 0; i < input_len; i++) {
                    vga_move_cursor_left();
                }
                /* Copy history entry to input buffer */
                strncpy(input_buffer, history[history_index], SHELL_MAX_CMD_LEN - 1);
                input_buffer[SHELL_MAX_CMD_LEN - 1] = '\0';
                input_len = strlen(input_buffer);
                cursor_pos = input_len;
                vga_puts(input_buffer);
            } else if (history_index == history_count - 1) {
                /* At end of history, clear to empty line */
                history_index = history_count;
                while (cursor_pos > 0) {
                    cursor_pos--;
                    vga_move_cursor_left();
                }
                /* Clear displayed text */
                for (int i = 0; i < input_len; i++) {
                    vga_putchar(' ');
                }
                for (int i = 0; i < input_len; i++) {
                    vga_move_cursor_left();
                }
                input_buffer[0] = '\0';
                input_len = 0;
                cursor_pos = 0;
            }
            continue;
        } else if (uc == KEY_PAGEUP) {
            /* Shift+PageUp = single line, PageUp = half page */
            int scroll_amount = (mods & KEY_SHIFT) ? 1 : (VGA_HEIGHT / 2);
            vga_scroll_up(scroll_amount);
            continue;
        } else if (uc == KEY_PAGEDOWN) {
            /* Shift+PageDown = single line, PageDown = half page */
            int scroll_amount = (mods & KEY_SHIFT) ? 1 : (VGA_HEIGHT / 2);
            vga_scroll_down(scroll_amount);
            continue;
        } else if (uc == KEY_HOME) {
            /* Scroll to top of buffer */
            vga_scroll_up(VGA_SCROLLBACK_LINES);
            continue;
        } else if (uc == KEY_END) {
            vga_scroll_to_bottom();
            continue;
        } else if (uc == KEY_LEFT) {
            /* Move cursor left */
            if (cursor_pos > 0) {
                cursor_pos--;
                vga_move_cursor_left();
            }
            continue;
        } else if (uc == KEY_RIGHT) {
            /* Move cursor right */
            if (cursor_pos < input_len) {
                cursor_pos++;
                vga_move_cursor_right();
            }
            continue;
        }

        if (c == '\n') {
            vga_putchar('\n');
            process_input();
        } else if (c == '\t') {
            /* Tab completion */
            shell_tab_complete();
        } else if (c == '\b') {
            /* Backspace - delete character before cursor */
            if (cursor_pos > 0) {
                /* Shift characters left from cursor position */
                for (int i = cursor_pos - 1; i < input_len - 1; i++) {
                    input_buffer[i] = input_buffer[i + 1];
                }
                input_len--;
                cursor_pos--;
                input_buffer[input_len] = '\0';
                
                /* Redraw: move cursor back, print rest of line, clear last char, restore cursor */
                vga_move_cursor_left();
                for (int i = cursor_pos; i < input_len; i++) {
                    vga_putchar(input_buffer[i]);
                }
                vga_putchar(' ');  /* Clear the last character position */
                /* Move cursor back to correct position */
                for (int i = 0; i < input_len - cursor_pos + 1; i++) {
                    vga_move_cursor_left();
                }
            }
        } else if (uc == KEY_DELETE) {
            /* Delete - delete character at cursor */
            if (cursor_pos < input_len) {
                /* Shift characters left from cursor position */
                for (int i = cursor_pos; i < input_len - 1; i++) {
                    input_buffer[i] = input_buffer[i + 1];
                }
                input_len--;
                input_buffer[input_len] = '\0';
                
                /* Redraw: print rest of line, clear last char, restore cursor */
                for (int i = cursor_pos; i < input_len; i++) {
                    vga_putchar(input_buffer[i]);
                }
                vga_putchar(' ');  /* Clear the last character position */
                /* Move cursor back to correct position */
                for (int i = 0; i < input_len - cursor_pos + 1; i++) {
                    vga_move_cursor_left();
                }
            }
        } else if (input_len < SHELL_MAX_CMD_LEN - 1 && c >= 32 && c < 127) {
            /* Insert character at cursor position */
            if (cursor_pos < input_len) {
                /* Shift characters right to make room */
                for (int i = input_len; i > cursor_pos; i--) {
                    input_buffer[i] = input_buffer[i - 1];
                }
            }
            input_buffer[cursor_pos] = c;
            input_len++;
            cursor_pos++;
            input_buffer[input_len] = '\0';
            
            /* Print from cursor position to end, then restore cursor */
            for (int i = cursor_pos - 1; i < input_len; i++) {
                vga_putchar(input_buffer[i]);
            }
            /* Move cursor back to correct position */
            for (int i = 0; i < input_len - cursor_pos; i++) {
                vga_move_cursor_left();
            }
        }
    }
}

/*
 * Get current working directory path
 */
const char* shell_get_cwd(void) {
    return current_dir;
}

/*
 * Get current working directory node
 */
vfs_node_t* shell_get_cwd_node(void) {
    return current_dir_node;
}

/*
 * Resolve a relative path to absolute path using current directory
 * (Public version of the static resolve_path function)
 */
void shell_resolve_path(const char* input, char* output, int output_size) {
    resolve_path(input, output, output_size);
}

const shell_command_t* shell_get_commands(int* out_count) {
    if (out_count) *out_count = num_commands;
    return commands;
}

/* ============================================
 * New Command Implementations
 * ============================================ */

/*
 * which - Locate a command
 */
static int cmd_which(int argc, char* argv[]) {
    if (argc < 2) {
        shell_output("Usage: which <command>\n");
        return -1;
    }

    for (int i = 0; i < num_commands; i++) {
        if (strcmp(commands[i].name, argv[1]) == 0) {
            shell_output(argv[1]);
            shell_output(": shell built-in\n");
            return 0;
        }
    }

    /* Check /bin for external commands */
    char path[VFS_MAX_PATH];
    snprintf(path, sizeof(path), "/bin/%s", argv[1]);
    vfs_node_t* node = vfs_lookup(path);
    if (node) {
        shell_output(path);
        shell_output("\n");
        return 0;
    }

    shell_output(argv[1]);
    shell_output(": not found\n");
    return -1;
}

/*
 * type - Show command type (builtin vs external)
 */
static int cmd_type(int argc, char* argv[]) {
    if (argc < 2) {
        shell_output("Usage: type <command>\n");
        return -1;
    }

    for (int i = 0; i < num_commands; i++) {
        if (strcmp(commands[i].name, argv[1]) == 0) {
            shell_output(argv[1]);
            shell_output(" is a shell builtin\n");
            return 0;
        }
    }

    /* Check /bin */
    char path[VFS_MAX_PATH];
    snprintf(path, sizeof(path), "/bin/%s", argv[1]);
    vfs_node_t* node = vfs_lookup(path);
    if (node) {
        shell_output(argv[1]);
        shell_output(" is ");
        shell_output(path);
        shell_output("\n");
        return 0;
    }

    shell_output(argv[1]);
    shell_output(": not found\n");
    return -1;
}

/*
 * df - Show filesystem space usage
 */
static int cmd_df(int argc, char* argv[]) {
    (void)argc; (void)argv;

    shell_output("Filesystem      Blocks      Used     Avail  Use%  Mounted on\n");

    /* Show ramfs (root) - estimate based on heap */
    shell_output("ramfs            N/A        N/A       N/A    -    /\n");

    /* Check for mounted ext2 at /mnt */
    vfs_node_t* mnt = vfs_lookup("/mnt");
    if (mnt && (mnt->flags & VFS_MOUNTPOINT)) {
        /* Get ext2 superblock info - we need to read from the mounted fs */
        vfs_node_t* mounted_root = mnt->ptr;
        if (mounted_root && mounted_root->impl) {
            /* Access ext2 filesystem structure */
            /* For now, show placeholder - would need ext2 API to get stats */
            shell_output("hd0p1            ext2       ext2      ext2   -    /mnt\n");
        }
    }

    return 0;
}

/*
 * find - Search for files recursively
 */
static void find_recursive(vfs_node_t* dir, const char* pattern, const char* current_path) {
    if (!dir || !(dir->flags & VFS_DIRECTORY)) return;

    uint32_t idx = 0;
    dirent_t* entry;

    while ((entry = vfs_readdir(dir, idx++)) != NULL) {
        if (strcmp(entry->name, ".") == 0 || strcmp(entry->name, "..") == 0) {
            continue;
        }

        char full_path[VFS_MAX_PATH];
        if (strcmp(current_path, "/") == 0) {
            snprintf(full_path, sizeof(full_path), "/%s", entry->name);
        } else {
            snprintf(full_path, sizeof(full_path), "%s/%s", current_path, entry->name);
        }

        /* Check if name matches pattern (simple substring match) */
        if (pattern == NULL || strstr(entry->name, pattern) != NULL) {
            shell_output(full_path);
            shell_output("\n");
        }

        /* Recurse into subdirectories */
        vfs_node_t* child = vfs_finddir(dir, entry->name);
        if (child && (child->flags & VFS_DIRECTORY)) {
            find_recursive(child, pattern, full_path);
        }
    }
}

static int cmd_find(int argc, char* argv[]) {
    const char* start_path = ".";
    const char* pattern = NULL;

    /* Parse arguments: find [path] [-name pattern] */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-name") == 0 && i + 1 < argc) {
            pattern = argv[++i];
        } else if (argv[i][0] != '-') {
            start_path = argv[i];
        }
    }

    char resolved[VFS_MAX_PATH];
    resolve_path(start_path, resolved, sizeof(resolved));

    vfs_node_t* start = vfs_lookup(resolved);
    if (!start) {
        shell_output("find: ");
        shell_output(start_path);
        shell_output(": No such file or directory\n");
        return -1;
    }

    if (start->flags & VFS_DIRECTORY) {
        find_recursive(start, pattern, resolved);
    } else {
        /* Single file */
        if (pattern == NULL || strstr(start->name, pattern) != NULL) {
            shell_output(resolved);
            shell_output("\n");
        }
    }

    return 0;
}

/*
 * sort - Sort lines of text (from file or pipe)
 */
/*
 * sort - Sort lines of text
 * Flags: -r (reverse), -n (numeric), -u (unique), -f (case-insensitive)
 */
static int sort_atoi(const char* s) {
    int n = 0, sign = 1;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') n = n * 10 + (*s++ - '0');
    return n * sign;
}

static int cmd_sort(int argc, char* argv[]) {
    bool reverse = false;
    bool numeric = false;
    bool unique = false;
    bool case_fold = false;
    const char* filename = NULL;

    /* Parse arguments */
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] != '\0') {
            for (int j = 1; argv[i][j]; j++) {
                switch (argv[i][j]) {
                    case 'r': reverse = true; break;
                    case 'n': numeric = true; break;
                    case 'u': unique = true; break;
                    case 'f': case_fold = true; break;
                    default:
                        printk("sort: invalid option -- '%c'\n", argv[i][j]);
                        return -1;
                }
            }
        } else {
            filename = argv[i];
        }
    }

    uint8_t* content = NULL;
    uint32_t content_len = 0;
    bool free_content = true;

    /* Check if reading from pipe or file */
    if (!filename && pipe_input_mode && pipe_input_len > 0) {
        content = (uint8_t*)pipe_input_buffer;
        content_len = pipe_input_len;
        free_content = false;
    } else if (filename) {
        vfs_node_t* file = find_file(filename);
        if (!file) {
            shell_output("sort: ");
            shell_output(filename);
            shell_output(": No such file\n");
            return -1;
        }
        if (file->length == 0) return 0;

        content = (uint8_t*)kmalloc(file->length + 1);
        if (!content) {
            shell_output("sort: out of memory\n");
            return -1;
        }
        vfs_read(file, 0, file->length, content);
        content[file->length] = '\0';
        content_len = file->length;
    } else {
        shell_output("Usage: sort [-rnuf] [file]\n");
        return -1;
    }

    if (content_len == 0) {
        if (free_content && content) kfree(content);
        return 0;
    }

    /* Make a working copy if reading from pipe */
    uint8_t* work_content;
    if (!free_content) {
        work_content = (uint8_t*)kmalloc(content_len + 1);
        if (!work_content) {
            shell_output("sort: out of memory\n");
            return -1;
        }
        memcpy(work_content, content, content_len);
        work_content[content_len] = '\0';
    } else {
        work_content = content;
    }

    /* Count lines */
    int num_lines = 1;
    for (uint32_t i = 0; i < content_len; i++) {
        if (work_content[i] == '\n') num_lines++;
    }

    /* Build line pointer array */
    char** lines = (char**)kmalloc(num_lines * sizeof(char*));
    if (!lines) {
        kfree(work_content);
        shell_output("sort: out of memory\n");
        return -1;
    }

    int line_idx = 0;
    lines[line_idx++] = (char*)work_content;
    for (uint32_t i = 0; i < content_len && line_idx < num_lines; i++) {
        if (work_content[i] == '\n') {
            work_content[i] = '\0';
            if (i + 1 < content_len) {
                lines[line_idx++] = (char*)&work_content[i + 1];
            }
        }
    }
    num_lines = line_idx;

    /* Bubble sort with options */
    for (int i = 0; i < num_lines - 1; i++) {
        for (int j = 0; j < num_lines - i - 1; j++) {
            int cmp;
            if (numeric) {
                cmp = sort_atoi(lines[j]) - sort_atoi(lines[j + 1]);
            } else if (case_fold) {
                /* Case-insensitive comparison */
                const char *a = lines[j], *b = lines[j + 1];
                while (*a && *b) {
                    char ca = (*a >= 'A' && *a <= 'Z') ? *a + 32 : *a;
                    char cb = (*b >= 'A' && *b <= 'Z') ? *b + 32 : *b;
                    if (ca != cb) break;
                    a++; b++;
                }
                char ca = (*a >= 'A' && *a <= 'Z') ? *a + 32 : *a;
                char cb = (*b >= 'A' && *b <= 'Z') ? *b + 32 : *b;
                cmp = ca - cb;
            } else {
                cmp = strcmp(lines[j], lines[j + 1]);
            }
            if (reverse) cmp = -cmp;
            if (cmp > 0) {
                char* tmp = lines[j];
                lines[j] = lines[j + 1];
                lines[j + 1] = tmp;
            }
        }
    }

    /* Print sorted lines (with optional unique filter) */
    const char* prev = NULL;
    for (int i = 0; i < num_lines; i++) {
        if (lines[i][0] != '\0') {
            if (unique && prev && strcmp(prev, lines[i]) == 0) {
                continue;  /* Skip duplicate */
            }
            shell_output(lines[i]);
            shell_output("\n");
            prev = lines[i];
        }
    }

    kfree(lines);
    kfree(work_content);
    return 0;
}

/*
 * time - Time a command execution
 */
static int cmd_time(int argc, char* argv[]) {
    if (argc < 2) {
        shell_output("Usage: time <command> [args...]\n");
        return -1;
    }

    uint32_t start_ticks = timer_get_ticks();

    /* Execute the command */
    int result = execute_command(argc - 1, &argv[1]);

    uint32_t end_ticks = timer_get_ticks();
    uint32_t elapsed = end_ticks - start_ticks;

    /* Convert ticks to time (100 Hz timer) */
    uint32_t ms = elapsed * 10;
    uint32_t secs = ms / 1000;
    ms = ms % 1000;

    shell_output("\nreal\t");
    char buf[32];
    snprintf(buf, sizeof(buf), "%u.%03us\n", secs, ms);
    shell_output(buf);

    return result;
}

/*
 * seq - Print a sequence of numbers
 */
static int cmd_seq(int argc, char* argv[]) {
    int start = 1, end = 1, step = 1;

    if (argc == 2) {
        end = atoi(argv[1]);
    } else if (argc == 3) {
        start = atoi(argv[1]);
        end = atoi(argv[2]);
    } else if (argc >= 4) {
        start = atoi(argv[1]);
        step = atoi(argv[2]);
        end = atoi(argv[3]);
    } else {
        shell_output("Usage: seq [start] [step] end\n");
        return -1;
    }

    if (step == 0) step = 1;

    char buf[16];
    if (step > 0) {
        for (int i = start; i <= end; i += step) {
            snprintf(buf, sizeof(buf), "%d\n", i);
            shell_output(buf);
        }
    } else {
        for (int i = start; i >= end; i += step) {
            snprintf(buf, sizeof(buf), "%d\n", i);
            shell_output(buf);
        }
    }

    return 0;
}

/*
 * rev - Reverse each line of input/file
 */
static int cmd_rev(int argc, char* argv[]) {
    uint8_t* content = NULL;
    uint32_t content_len = 0;
    bool free_content = true;

    /* Check if reading from pipe or file */
    if (argc < 2 && pipe_input_mode && pipe_input_len > 0) {
        /* Read from pipe input */
        content = (uint8_t*)pipe_input_buffer;
        content_len = pipe_input_len;
        free_content = false;
    } else if (argc >= 2) {
        /* Read from file */
        vfs_node_t* file = find_file(argv[1]);
        if (!file) {
            shell_output("rev: ");
            shell_output(argv[1]);
            shell_output(": No such file\n");
            return -1;
        }

        if (file->length == 0) {
            return 0;
        }

        content = (uint8_t*)kmalloc(file->length + 1);
        if (!content) {
            shell_output("rev: out of memory\n");
            return -1;
        }

        vfs_read(file, 0, file->length, content);
        content[file->length] = '\0';
        content_len = file->length;
    } else {
        shell_output("Usage: rev <filename>\n");
        return -1;
    }

    if (content_len == 0) {
        if (free_content && content) kfree(content);
        return 0;
    }

    /* Process line by line */
    char line_buf[256];
    char* line_start = (char*)content;
    for (uint32_t i = 0; i <= content_len; i++) {
        if (content[i] == '\n' || content[i] == '\0') {
            char saved = content[i];
            content[i] = '\0';

            /* Reverse the line into buffer */
            int len = strlen(line_start);
            if (len > 0 && len < 256) {
                for (int j = 0; j < len; j++) {
                    line_buf[j] = line_start[len - 1 - j];
                }
                line_buf[len] = '\0';
                shell_output(line_buf);
            }
            shell_output("\n");

            content[i] = saved;
            line_start = (char*)&content[i + 1];
        }
    }

    if (free_content) kfree(content);
    return 0;
}

/*
 * sandbox - Run a program in Ring 3 (user mode)
 */
static int cmd_sandbox(int argc, char* argv[]) {
    if (argc < 2) {
        vga_puts("Usage: sandbox <program> [args...]\n");
        vga_puts("Runs a program in Ring 3 (user mode) with memory protection.\n");
        return -1;
    }

    /* Build full path if not absolute */
    char path[VFS_MAX_PATH];
    if (argv[1][0] == '/') {
        strncpy(path, argv[1], sizeof(path) - 1);
        path[sizeof(path) - 1] = '\0';
    } else {
        /* Check in /bin first */
        snprintf(path, sizeof(path), "/bin/%s", argv[1]);
        vfs_node_t* node = vfs_lookup(path);
        if (!node) {
            /* Try current directory */
            resolve_path(argv[1], path, sizeof(path));
        }
    }

    /* Check if file exists */
    vfs_node_t* prog_file = vfs_lookup(path);
    if (!prog_file) {
        vga_puts("sandbox: ");
        vga_puts(argv[1]);
        vga_puts(": program not found\n");
        return -1;
    }

    vga_puts("Starting '");
    vga_puts(argv[1]);
    vga_puts("' in Ring 3 sandbox...\n");

    /* Create user process from ELF file */
    extern int process_exec_elf(const char* path);
    int pid = process_exec_elf(path);
    if (pid < 0) {
        vga_puts("sandbox: failed to start process\n");
        return -1;
    }

    vga_puts("Process started (PID ");
    char buf[12];
    int i = 0;
    int n = pid;
    do {
        buf[i++] = '0' + (n % 10);
        n /= 10;
    } while (n > 0);
    while (i > 0) vga_putchar(buf[--i]);
    vga_puts(")\n");

    /* Start the scheduler to run the user process */
    vga_puts("Entering user mode... (Ctrl+C to interrupt)\n");
    extern void scheduler_start(void);
    scheduler_start();

    /* If we return here, the process exited */
    vga_puts("sandbox: process terminated\n");
    return 0;
}
