/*
 * Hello World - First MiniOS user program
 */

#include "../libc/include/stdio.h"
#include "../libc/include/stdlib.h"
#include "../libc/include/unistd.h"

int main(void) {
    printf("Hello from userspace!\n");
    printf("My PID is: %d\n", getpid());
    
    printf("Testing malloc...\n");
    char* buf = malloc(64);
    if (buf) {
        printf("  malloc(64) = %p\n", buf);
        free(buf);
        printf("  free() OK\n");
    } else {
        printf("  malloc failed!\n");
    }
    
    printf("Goodbye!\n");
    return 0;
}

