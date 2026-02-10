/*
 * MiniOS Configuration Database
 *
 * Persistent key=value storage on /mnt/conf/.
 * Each config "section" is a separate text file (e.g. /mnt/conf/gui.conf).
 */

#ifndef _CONF_H
#define _CONF_H

#include "types.h"

/* Max sizes */
#define CONF_MAX_KEY        64
#define CONF_MAX_VALUE      128
#define CONF_MAX_PATH       128
#define CONF_MAX_ENTRIES    32
#define CONF_BASE_DIR       "/mnt/conf"

/* A single key=value entry */
typedef struct {
    char key[CONF_MAX_KEY];
    char value[CONF_MAX_VALUE];
} conf_entry_t;

/* A loaded config section */
typedef struct {
    char name[32];
    conf_entry_t entries[CONF_MAX_ENTRIES];
    int count;
    bool dirty;
} conf_section_t;

/*
 * Initialize the config system.
 * Creates /mnt/conf/ directory if it doesn't exist.
 * Returns 0 on success, -1 if /mnt is not mounted.
 */
int conf_init(void);

/*
 * Load a config section from disk (e.g. "gui" loads /mnt/conf/gui.conf).
 * Returns 0 on success, -1 on error (file may not exist yet, which is OK).
 */
int conf_load(conf_section_t* section, const char* name);

/*
 * Save a config section to disk.
 * Returns 0 on success, -1 on error.
 */
int conf_save(conf_section_t* section);

/*
 * Get a string value from a section. Returns default_val if not found.
 */
const char* conf_get(conf_section_t* section, const char* key, const char* default_val);

/*
 * Get an integer value from a section. Returns default_val if not found.
 */
int conf_get_int(conf_section_t* section, const char* key, int default_val);

/*
 * Get an unsigned 32-bit value (hex-friendly). Returns default_val if not found.
 */
uint32_t conf_get_uint32(conf_section_t* section, const char* key, uint32_t default_val);

/*
 * Set a string value in a section (marks dirty).
 */
void conf_set(conf_section_t* section, const char* key, const char* value);

/*
 * Set an integer value in a section (marks dirty).
 */
void conf_set_int(conf_section_t* section, const char* key, int value);

/*
 * Set an unsigned 32-bit value as hex (marks dirty).
 */
void conf_set_uint32(conf_section_t* section, const char* key, uint32_t value);

#endif /* _CONF_H */
