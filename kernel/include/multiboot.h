/*
 * MiniOS Multiboot Header Structures
 * 
 * Defines structures for parsing multiboot information
 * passed by GRUB bootloader.
 */

#ifndef _MULTIBOOT_H
#define _MULTIBOOT_H

#include "types.h"

/* Multiboot magic number (passed in EAX by GRUB) */
#define MULTIBOOT_BOOTLOADER_MAGIC  0x2BADB002

/* Multiboot info flags */
#define MULTIBOOT_INFO_MEMORY       0x00000001  /* mem_lower/upper valid */
#define MULTIBOOT_INFO_BOOTDEV      0x00000002  /* boot_device valid */
#define MULTIBOOT_INFO_CMDLINE      0x00000004  /* cmdline valid */
#define MULTIBOOT_INFO_MODS         0x00000008  /* mods valid */
#define MULTIBOOT_INFO_MEM_MAP      0x00000040  /* mmap valid */
#define MULTIBOOT_INFO_VBE_INFO     0x00000080  /* vbe_* fields valid */
#define MULTIBOOT_INFO_FRAMEBUFFER  0x00001000  /* framebuffer_* fields valid */

/* Multiboot memory map entry types */
#define MULTIBOOT_MEMORY_AVAILABLE  1
#define MULTIBOOT_MEMORY_RESERVED   2

/* Multiboot info structure - passed by GRUB */
typedef struct {
    uint32_t flags;             /* Feature flags */
    uint32_t mem_lower;         /* KB of lower memory (< 1MB) */
    uint32_t mem_upper;         /* KB of upper memory (> 1MB) */
    uint32_t boot_device;       /* Boot device */
    uint32_t cmdline;           /* Kernel command line */
    uint32_t mods_count;        /* Number of modules loaded */
    uint32_t mods_addr;         /* Address of module info */
    
    /* ELF section header info (we don't use this) */
    uint32_t syms[4];
    
    /* Memory map info */
    uint32_t mmap_length;       /* Length of memory map buffer */
    uint32_t mmap_addr;         /* Address of memory map */
    
    /* Drive info */
    uint32_t drives_length;
    uint32_t drives_addr;
    
    /* ROM configuration table */
    uint32_t config_table;
    
    /* Bootloader name */
    uint32_t boot_loader_name;
    
    /* APM table */
    uint32_t apm_table;
    
    /* VBE info */
    uint32_t vbe_control_info;
    uint32_t vbe_mode_info;
    uint16_t vbe_mode;
    uint16_t vbe_interface_seg;
    uint16_t vbe_interface_off;
    uint16_t vbe_interface_len;

    /* Framebuffer info (Multiboot 1, bit 12 in flags) */
    uint64_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t  framebuffer_bpp;
    uint8_t  framebuffer_type;
    uint8_t  color_info[6];
} __attribute__((packed)) multiboot_info_t;

/* VBE mode info structure (returned by BIOS INT 10h AX=4F01h) */
typedef struct {
    uint16_t attributes;
    uint8_t  window_a;
    uint8_t  window_b;
    uint16_t granularity;
    uint16_t window_size;
    uint16_t segment_a;
    uint16_t segment_b;
    uint32_t win_func_ptr;
    uint16_t pitch;             /* Bytes per scanline */
    uint16_t width;             /* Width in pixels */
    uint16_t height;            /* Height in pixels */
    uint8_t  w_char;
    uint8_t  y_char;
    uint8_t  planes;
    uint8_t  bpp;               /* Bits per pixel */
    uint8_t  banks;
    uint8_t  memory_model;
    uint8_t  bank_size;
    uint8_t  image_pages;
    uint8_t  reserved0;
    /* Direct color fields */
    uint8_t  red_mask;
    uint8_t  red_position;
    uint8_t  green_mask;
    uint8_t  green_position;
    uint8_t  blue_mask;
    uint8_t  blue_position;
    uint8_t  rsv_mask;
    uint8_t  rsv_position;
    uint8_t  direct_color_attributes;
    /* VBE 2.0+ */
    uint32_t framebuffer;       /* Physical address of framebuffer */
    uint32_t off_screen_mem_off;
    uint16_t off_screen_mem_size;
} __attribute__((packed)) vbe_mode_info_t;

/* Memory map entry structure */
typedef struct {
    uint32_t size;              /* Size of this entry (not including size field) */
    uint64_t addr;              /* Start address */
    uint64_t len;               /* Length in bytes */
    uint32_t type;              /* Type (1 = available, 2 = reserved) */
} __attribute__((packed)) multiboot_mmap_entry_t;

/* Module info structure */
typedef struct {
    uint32_t mod_start;         /* Start address of module */
    uint32_t mod_end;           /* End address of module */
    uint32_t cmdline;           /* Module command line */
    uint32_t reserved;          /* Padding */
} __attribute__((packed)) multiboot_module_t;

#endif /* _MULTIBOOT_H */

