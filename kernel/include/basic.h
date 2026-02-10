/*
 * MiniOS BASIC Interpreter
 *
 * A simple BASIC interpreter supporting classic commands.
 */

#ifndef _BASIC_H
#define _BASIC_H

#include "types.h"

/*
 * Start the BASIC interpreter
 * filename: optional file to load on startup (NULL for interactive)
 */
void basic_run(const char* filename);

#endif /* _BASIC_H */
