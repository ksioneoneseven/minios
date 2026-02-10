/*
 * MiniOS Pipe Implementation
 *
 * Unix-style pipes for inter-process communication.
 */

#ifndef _PIPE_H
#define _PIPE_H

#include "types.h"
#include "vfs.h"

#define PIPE_BUF_SIZE 4096  /* 4KB pipe buffer */

/* Pipe structure */
typedef struct pipe {
    uint8_t buffer[PIPE_BUF_SIZE];
    uint32_t read_pos;          /* Read position in circular buffer */
    uint32_t write_pos;         /* Write position in circular buffer */
    uint32_t data_size;         /* Amount of data in pipe */
    bool read_open;             /* Read end is open */
    bool write_open;            /* Write end is open */
    uint32_t readers;           /* Number of readers */
    uint32_t writers;           /* Number of writers */
} pipe_t;

/* File descriptor flags */
#define O_RDONLY    0x0000
#define O_WRONLY    0x0001
#define O_RDWR      0x0002
#define O_CREAT     0x0100
#define O_TRUNC     0x0200
#define O_APPEND    0x0400

/* Maximum file descriptors per process */
#define MAX_FD 32

/* Pipe functions */
pipe_t* pipe_create(void);
void pipe_destroy(pipe_t* pipe);
int32_t pipe_read(pipe_t* pipe, uint8_t* buffer, uint32_t size);
int32_t pipe_write(pipe_t* pipe, const uint8_t* buffer, uint32_t size);
bool pipe_is_empty(pipe_t* pipe);
bool pipe_is_full(pipe_t* pipe);

/* File descriptor functions */
void fd_init(void);
int32_t fd_alloc(void);
void fd_free(int32_t fd);
file_descriptor_t* fd_get(int32_t fd);
int32_t fd_dup2(int32_t oldfd, int32_t newfd);

#endif /* _PIPE_H */
