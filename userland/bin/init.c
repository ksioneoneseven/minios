/*
 * Init Process - First user process (PID 1)
 * 
 * This is the first process started by the kernel.
 * It sets up the user environment and starts the shell.
 */

#include "../libc/include/stdio.h"
#include "../libc/include/stdlib.h"
#include "../libc/include/unistd.h"

int main(void) {
    printf("\n");
    printf("======================================\n");
    printf("  MiniOS Init Process (PID %d)\n", getpid());
    printf("======================================\n");
    printf("\n");
    
    printf("[init] Starting system initialization...\n");
    
    /* In a full implementation, init would:
     * 1. Mount filesystems
     * 2. Start system services
     * 3. Fork and exec the shell
     * 4. Reap zombie processes in a loop
     */
    
    printf("[init] System ready.\n");

    printf("[init] Starting user shell...\n");
    pid_t pid = fork();
    if (pid == 0) {
        char* const argv[] = {"shell", NULL};
        execv("/bin/shell", argv);
        printf("[init] Failed to exec shell.\n");
        exit(1);
    } else if (pid < 0) {
        printf("[init] Failed to fork shell.\n");
    }

    printf("[init] Entering wait loop...\n");
    while (1) {
        int status = 0;
        pid_t child = wait(&status);
        if (child > 0) {
            printf("[init] Reaped child %d (status %d)\n", child, status);
        }
        yield();
    }
    
    return 0;
}

