/*
 * MiniOS Pipe Implementation
 */

#include "../include/pipe.h"
#include "../include/heap.h"
#include "../include/string.h"
#include "../include/process.h"

/* Global file descriptor table (per-process in real implementation) */
static file_descriptor_t* fd_table[MAX_FD];

/*
 * Initialize file descriptor table
 */
void fd_init(void) {
    for (int i = 0; i < MAX_FD; i++) {
        fd_table[i] = NULL;
    }

    /* Reserve standard file descriptors */
    /* stdin (0), stdout (1), stderr (2) would be initialized here */
}

/*
 * Allocate a new file descriptor
 */
int32_t fd_alloc(void) {
    /* Start from 3 to reserve 0, 1, 2 for stdin, stdout, stderr */
    for (int32_t i = 3; i < MAX_FD; i++) {
        if (fd_table[i] == NULL) {
            fd_table[i] = (file_descriptor_t*)kmalloc(sizeof(file_descriptor_t));
            if (fd_table[i]) {
                memset(fd_table[i], 0, sizeof(file_descriptor_t));
                return i;
            }
            return -1;
        }
    }
    return -1;  /* No free descriptors */
}

/*
 * Free a file descriptor
 */
void fd_free(int32_t fd) {
    if (fd < 0 || fd >= MAX_FD) return;
    if (fd_table[fd] == NULL) return;

    file_descriptor_t* descriptor = fd_table[fd];

    /* Close pipe if this is a pipe descriptor */
    if (descriptor->is_pipe && descriptor->pipe) {
        if (descriptor->is_read_end) {
            descriptor->pipe->readers--;
            if (descriptor->pipe->readers == 0) {
                descriptor->pipe->read_open = false;
            }
        } else {
            descriptor->pipe->writers--;
            if (descriptor->pipe->writers == 0) {
                descriptor->pipe->write_open = false;
            }
        }

        /* Destroy pipe if both ends are closed */
        if (!descriptor->pipe->read_open && !descriptor->pipe->write_open) {
            pipe_destroy(descriptor->pipe);
        }
    }

    kfree(fd_table[fd]);
    fd_table[fd] = NULL;
}

/*
 * Get file descriptor structure
 */
file_descriptor_t* fd_get(int32_t fd) {
    if (fd < 0 || fd >= MAX_FD) return NULL;
    return fd_table[fd];
}

/*
 * Duplicate file descriptor (dup2)
 */
int32_t fd_dup2(int32_t oldfd, int32_t newfd) {
    if (oldfd < 0 || oldfd >= MAX_FD) return -1;
    if (newfd < 0 || newfd >= MAX_FD) return -1;
    if (fd_table[oldfd] == NULL) return -1;

    /* Close newfd if it's already open */
    if (fd_table[newfd] != NULL) {
        fd_free(newfd);
    }

    /* Allocate new descriptor */
    fd_table[newfd] = (file_descriptor_t*)kmalloc(sizeof(file_descriptor_t));
    if (!fd_table[newfd]) return -1;

    /* Copy descriptor */
    memcpy(fd_table[newfd], fd_table[oldfd], sizeof(file_descriptor_t));

    /* Increment reference counts for pipes */
    if (fd_table[newfd]->is_pipe && fd_table[newfd]->pipe) {
        if (fd_table[newfd]->is_read_end) {
            fd_table[newfd]->pipe->readers++;
        } else {
            fd_table[newfd]->pipe->writers++;
        }
    }

    return newfd;
}

/*
 * Create a new pipe
 */
pipe_t* pipe_create(void) {
    pipe_t* pipe = (pipe_t*)kmalloc(sizeof(pipe_t));
    if (!pipe) return NULL;

    memset(pipe, 0, sizeof(pipe_t));
    pipe->read_pos = 0;
    pipe->write_pos = 0;
    pipe->data_size = 0;
    pipe->read_open = true;
    pipe->write_open = true;
    pipe->readers = 0;
    pipe->writers = 0;

    return pipe;
}

/*
 * Destroy a pipe
 */
void pipe_destroy(pipe_t* pipe) {
    if (pipe) {
        kfree(pipe);
    }
}

/*
 * Check if pipe is empty
 */
bool pipe_is_empty(pipe_t* pipe) {
    return pipe->data_size == 0;
}

/*
 * Check if pipe is full
 */
bool pipe_is_full(pipe_t* pipe) {
    return pipe->data_size >= PIPE_BUF_SIZE;
}

/*
 * Read from pipe
 * Returns: Number of bytes read, 0 on EOF, -1 on error
 */
int32_t pipe_read(pipe_t* pipe, uint8_t* buffer, uint32_t size) {
    if (!pipe || !buffer) return -1;

    /* If pipe is empty and write end is closed, return EOF */
    if (pipe_is_empty(pipe) && !pipe->write_open) {
        return 0;  /* EOF */
    }

    /* If pipe is empty and write end is open, block would happen here */
    /* For simplicity, we return 0 (would need scheduler integration for real blocking) */
    if (pipe_is_empty(pipe)) {
        return 0;  /* Would block - TODO: implement blocking */
    }

    uint32_t bytes_read = 0;
    while (bytes_read < size && !pipe_is_empty(pipe)) {
        buffer[bytes_read] = pipe->buffer[pipe->read_pos];
        pipe->read_pos = (pipe->read_pos + 1) % PIPE_BUF_SIZE;
        pipe->data_size--;
        bytes_read++;
    }

    return bytes_read;
}

/*
 * Write to pipe
 * Returns: Number of bytes written, -1 on error
 */
int32_t pipe_write(pipe_t* pipe, const uint8_t* buffer, uint32_t size) {
    if (!pipe || !buffer) return -1;

    /* If read end is closed, return error (SIGPIPE in real Unix) */
    if (!pipe->read_open) {
        return -1;  /* Broken pipe */
    }

    /* If pipe is full, block would happen here */
    /* For simplicity, we write what we can (would need scheduler for real blocking) */
    uint32_t bytes_written = 0;
    while (bytes_written < size && !pipe_is_full(pipe)) {
        pipe->buffer[pipe->write_pos] = buffer[bytes_written];
        pipe->write_pos = (pipe->write_pos + 1) % PIPE_BUF_SIZE;
        pipe->data_size++;
        bytes_written++;
    }

    return bytes_written;
}
