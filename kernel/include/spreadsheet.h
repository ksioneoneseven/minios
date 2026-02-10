/*
 * MiniOS Spreadsheet Application Header
 */

#ifndef _SPREADSHEET_H
#define _SPREADSHEET_H

#include "types.h"

/*
 * Initialize spreadsheet data structures
 */
void spreadsheet_init(void);

/*
 * Run the spreadsheet application
 * Returns when user exits (ESC or Ctrl+Q)
 */
void spreadsheet_run(void);

/*
 * Run spreadsheet with optional filename
 */
void spreadsheet_run_file(const char* filename);

#endif /* _SPREADSHEET_H */

