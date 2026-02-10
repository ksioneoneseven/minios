/*
 * MiniOS XGUI - Graphical User Interface
 *
 * Main entry point and initialization for the GUI system.
 */

#ifndef _XGUI_H
#define _XGUI_H

#include "types.h"

/* Include all XGUI components */
#include "xgui/display.h"
#include "xgui/event.h"
#include "xgui/wm.h"
#include "xgui/desktop.h"
#include "xgui/widget.h"
#include "xgui/font.h"
#include "xgui/theme.h"
#include "xgui/clipboard.h"
#include "xgui/contextmenu.h"

/*
 * Initialize the XGUI system
 * Returns 0 on success, -1 on failure
 */
int xgui_init(void);

/*
 * Run the XGUI main loop
 * Returns when user exits the GUI
 */
void xgui_run(void);

/*
 * Request XGUI to exit
 */
void xgui_quit(void);

/*
 * Perform a real shutdown sequence (flush disks, power off)
 */
void xgui_shutdown(void);

/*
 * Check if XGUI is running
 */
bool xgui_is_running(void);

/*
 * Draw the mouse cursor
 */
void xgui_draw_cursor(int x, int y);

/* Built-in applications */
void xgui_calculator_create(void);
void xgui_about_create(void);
void xgui_notepad_create(void);
void xgui_paint_create(void);
void xgui_explorer_create(void);
void xgui_terminal_create(void);
void xgui_controlpanel_create(void);
void xgui_gui_editor_create(void);
void xgui_gui_editor_open_file(const char* path);
void xgui_gui_spreadsheet_create(void);
void xgui_gui_spreadsheet_open_file(const char* path);
void xgui_paint_open_file(const char* path);
void xgui_taskmgr_create(void);
void xgui_diskutil_create(void);
void xgui_clock_settings_create(void);
void xgui_analogclock_create(void);
void xgui_analogclock_update(void);
void xgui_calendar_create(void);
void xgui_calendar_toggle(void);
void xgui_stickynotes_restore(void);

#endif /* _XGUI_H */
