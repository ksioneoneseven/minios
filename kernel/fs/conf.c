/*
 * MiniOS Configuration Database
 *
 * Persistent key=value storage on /mnt/conf/.
 */

#include "../include/conf.h"
#include "../include/vfs.h"
#include "../include/ext2.h"
#include "../include/ramfs.h"
#include "../include/heap.h"
#include "../include/string.h"
#include "../include/stdio.h"
#include "../include/serial.h"

/*
 * Initialize the config system — ensure /mnt/conf/ exists.
 */
int conf_init(void) {
    vfs_node_t* mnt = vfs_lookup("/mnt");
    if (!mnt || !(mnt->flags & VFS_DIRECTORY)) {
        serial_write_string("CONF: /mnt not available\n");
        return -1;
    }

    /* Check if /mnt has ext2 (i.e. is a real mounted filesystem) */
    if (mnt->readdir != ext2_vfs_readdir) {
        serial_write_string("CONF: /mnt is not ext2, skipping\n");
        return -1;
    }

    /* Check if conf directory exists */
    vfs_node_t* conf_dir = vfs_lookup(CONF_BASE_DIR);
    if (!conf_dir) {
        /* Create it on the ext2 filesystem */
        conf_dir = ext2_create_dir(mnt, "conf");
        if (!conf_dir) {
            serial_write_string("CONF: failed to create /mnt/conf\n");
            return -1;
        }
        serial_write_string("CONF: created /mnt/conf/\n");
    }

    serial_write_string("CONF: initialized\n");
    return 0;
}

/*
 * Parse a line "key=value\n" into entry. Returns true on success.
 */
static bool parse_line(const char* line, conf_entry_t* entry) {
    const char* eq = NULL;
    for (const char* p = line; *p && *p != '\n' && *p != '\r'; p++) {
        if (*p == '=') { eq = p; break; }
    }
    if (!eq) return false;

    int klen = (int)(eq - line);
    if (klen <= 0 || klen >= CONF_MAX_KEY) return false;

    /* Skip comment lines */
    if (line[0] == '#') return false;

    memcpy(entry->key, line, klen);
    entry->key[klen] = '\0';

    /* Trim leading spaces from key */
    /* (not needed for simple configs) */

    const char* val = eq + 1;
    int vlen = 0;
    while (val[vlen] && val[vlen] != '\n' && val[vlen] != '\r' && vlen < CONF_MAX_VALUE - 1) {
        vlen++;
    }
    memcpy(entry->value, val, vlen);
    entry->value[vlen] = '\0';

    return true;
}

/*
 * Load a config section from /mnt/conf/<name>.conf
 */
int conf_load(conf_section_t* section, const char* name) {
    if (!section || !name) return -1;

    memset(section, 0, sizeof(*section));
    strncpy(section->name, name, sizeof(section->name) - 1);

    char path[CONF_MAX_PATH];
    snprintf(path, sizeof(path), "%s/%s.conf", CONF_BASE_DIR, name);

    vfs_node_t* node = vfs_lookup(path);
    if (!node) {
        /* File doesn't exist yet — not an error, just empty config */
        return 0;
    }

    uint32_t fsize = node->length;
    if (fsize == 0) return 0;
    if (fsize > 4096) fsize = 4096; /* sanity limit */

    uint8_t* buf = (uint8_t*)kmalloc(fsize + 1);
    if (!buf) return -1;

    int32_t rd = vfs_read(node, 0, fsize, buf);
    if (rd <= 0) {
        kfree(buf);
        return -1;
    }
    buf[rd] = '\0';

    /* Parse line by line */
    const char* p = (const char*)buf;
    while (*p && section->count < CONF_MAX_ENTRIES) {
        /* Skip blank lines */
        if (*p == '\n' || *p == '\r') { p++; continue; }

        conf_entry_t entry;
        if (parse_line(p, &entry)) {
            section->entries[section->count++] = entry;
        }

        /* Advance to next line */
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }

    kfree(buf);
    section->dirty = false;

    serial_write_string("CONF: loaded ");
    serial_write_string(path);
    serial_write_string(" (");
    char num[8];
    snprintf(num, sizeof(num), "%d", section->count);
    serial_write_string(num);
    serial_write_string(" entries)\n");

    return 0;
}

/*
 * Save a config section to /mnt/conf/<name>.conf
 */
int conf_save(conf_section_t* section) {
    if (!section) return -1;

    char path[CONF_MAX_PATH];
    snprintf(path, sizeof(path), "%s/%s.conf", CONF_BASE_DIR, section->name);

    /* Build file content */
    /* Estimate size: each line is key=value\n */
    int total = 0;
    for (int i = 0; i < section->count; i++) {
        total += strlen(section->entries[i].key) + 1 + strlen(section->entries[i].value) + 1;
    }
    total += 64; /* header comment */

    uint8_t* buf = (uint8_t*)kmalloc(total);
    if (!buf) return -1;

    int pos = 0;
    /* Header comment */
    const char* hdr = "# MiniOS config\n";
    int hlen = strlen(hdr);
    memcpy(buf + pos, hdr, hlen);
    pos += hlen;

    for (int i = 0; i < section->count; i++) {
        int klen = strlen(section->entries[i].key);
        int vlen = strlen(section->entries[i].value);
        memcpy(buf + pos, section->entries[i].key, klen);
        pos += klen;
        buf[pos++] = '=';
        memcpy(buf + pos, section->entries[i].value, vlen);
        pos += vlen;
        buf[pos++] = '\n';
    }

    /* Create or overwrite file */
    vfs_node_t* node = vfs_lookup(path);
    if (!node) {
        vfs_node_t* conf_dir = vfs_lookup(CONF_BASE_DIR);
        if (!conf_dir) {
            kfree(buf);
            return -1;
        }

        char fname[64];
        snprintf(fname, sizeof(fname), "%s.conf", section->name);

        if (conf_dir->readdir == ext2_vfs_readdir) {
            node = ext2_create_file(conf_dir, fname);
        } else {
            node = ramfs_create_file_in(conf_dir, fname, 0);
        }
        if (!node) {
            kfree(buf);
            return -1;
        }
    }

    int32_t written = vfs_write(node, 0, pos, buf);
    kfree(buf);

    if (written < 0) return -1;

    section->dirty = false;

    serial_write_string("CONF: saved ");
    serial_write_string(path);
    serial_write_string("\n");

    return 0;
}

/*
 * Get a string value. Returns default_val if not found.
 */
const char* conf_get(conf_section_t* section, const char* key, const char* default_val) {
    if (!section || !key) return default_val;
    for (int i = 0; i < section->count; i++) {
        if (strcmp(section->entries[i].key, key) == 0) {
            return section->entries[i].value;
        }
    }
    return default_val;
}

/*
 * Get an integer value.
 */
int conf_get_int(conf_section_t* section, const char* key, int default_val) {
    const char* val = conf_get(section, key, NULL);
    if (!val) return default_val;

    int result = 0;
    int sign = 1;
    const char* p = val;
    if (*p == '-') { sign = -1; p++; }
    while (*p >= '0' && *p <= '9') {
        result = result * 10 + (*p - '0');
        p++;
    }
    return (p == val || (sign == -1 && p == val + 1)) ? default_val : result * sign;
}

/*
 * Get a uint32 value (stored as hex "0xNNNNNNNN").
 */
uint32_t conf_get_uint32(conf_section_t* section, const char* key, uint32_t default_val) {
    const char* val = conf_get(section, key, NULL);
    if (!val) return default_val;

    /* Parse hex: 0xNNNN or just NNNN */
    const char* p = val;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;

    uint32_t result = 0;
    bool found = false;
    while (*p) {
        char c = *p;
        if (c >= '0' && c <= '9') {
            result = (result << 4) | (c - '0');
            found = true;
        } else if (c >= 'a' && c <= 'f') {
            result = (result << 4) | (c - 'a' + 10);
            found = true;
        } else if (c >= 'A' && c <= 'F') {
            result = (result << 4) | (c - 'A' + 10);
            found = true;
        } else {
            break;
        }
        p++;
    }
    return found ? result : default_val;
}

/*
 * Set a string value.
 */
void conf_set(conf_section_t* section, const char* key, const char* value) {
    if (!section || !key || !value) return;

    /* Update existing */
    for (int i = 0; i < section->count; i++) {
        if (strcmp(section->entries[i].key, key) == 0) {
            strncpy(section->entries[i].value, value, CONF_MAX_VALUE - 1);
            section->entries[i].value[CONF_MAX_VALUE - 1] = '\0';
            section->dirty = true;
            return;
        }
    }

    /* Add new */
    if (section->count < CONF_MAX_ENTRIES) {
        strncpy(section->entries[section->count].key, key, CONF_MAX_KEY - 1);
        section->entries[section->count].key[CONF_MAX_KEY - 1] = '\0';
        strncpy(section->entries[section->count].value, value, CONF_MAX_VALUE - 1);
        section->entries[section->count].value[CONF_MAX_VALUE - 1] = '\0';
        section->count++;
        section->dirty = true;
    }
}

/*
 * Set an integer value.
 */
void conf_set_int(conf_section_t* section, const char* key, int value) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", value);
    conf_set(section, key, buf);
}

/*
 * Set a uint32 value as hex.
 */
void conf_set_uint32(conf_section_t* section, const char* key, uint32_t value) {
    char buf[16];
    /* Manual hex formatting */
    buf[0] = '0'; buf[1] = 'x';
    static const char hex[] = "0123456789ABCDEF";
    for (int i = 7; i >= 0; i--) {
        buf[2 + (7 - i)] = hex[(value >> (i * 4)) & 0xF];
    }
    buf[10] = '\0';
    conf_set(section, key, buf);
}
