/*
 * MiniOS Nano Text Editor Header
 * 
 * A full-featured nano clone for text editing.
 */

#ifndef _NANO_H
#define _NANO_H

#include "types.h"

/*
 * Initialize the nano editor
 */
void nano_init(void);

/*
 * Run nano without a file (new buffer)
 */
void nano_run(void);

/*
 * Run nano with a file
 */
void nano_run_file(const char* filename);

#endif /* _NANO_H */

