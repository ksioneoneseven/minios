/*
 * MiniOS Shell Header
 * 
 * Simple command-line shell for user interaction.
 */

#ifndef _SHELL_H
#define _SHELL_H

#include "types.h"

/* Shell configuration */
#define SHELL_MAX_CMD_LEN   256
#define SHELL_MAX_ARGS      16
#define SHELL_PROMPT        "minios> "

/* Command handler function type */
typedef int (*shell_cmd_handler_t)(int argc, char* argv[]);

/* Command structure */
typedef struct {
    const char* name;
    const char* description;
    shell_cmd_handler_t handler;
} shell_command_t;

/*
 * Initialize the shell
 */
void shell_init(void);

/*
 * Run the shell (main loop)
 */
void shell_run(void);

/*
 * Register a shell command
 */
int shell_register_command(const char* name, const char* desc, shell_cmd_handler_t handler);

/*
 * Get current working directory path
 */
const char* shell_get_cwd(void);

/*
 * Get current working directory node
 */
struct vfs_node* shell_get_cwd_node(void);

/*
 * Resolve a relative path to absolute path using current directory
 */
void shell_resolve_path(const char* input, char* output, int output_size);

/*
 * Execute a shell command string (for GUI terminal use)
 * Returns: command exit code, or -1 if command not found
 */
int shell_execute_command(const char* cmdline);

/*
 * Get the registered command table and count (for tab completion)
 */
const shell_command_t* shell_get_commands(int* out_count);

#endif /* _SHELL_H */

