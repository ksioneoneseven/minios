/*
 * MiniOS Block Device Layer
 * 
 * Provides a unified interface for block storage devices.
 * Sits between hardware drivers (ATA, AHCI, etc.) and filesystems.
 */

#ifndef _BLOCKDEV_H
#define _BLOCKDEV_H

#include "types.h"

/* Maximum block devices */
#define BLOCKDEV_MAX_DEVICES    16

/* Maximum partitions per device */
#define BLOCKDEV_MAX_PARTITIONS 8

/* Default sector size */
#define BLOCKDEV_SECTOR_SIZE    512

/* Block device types */
typedef enum {
    BLOCKDEV_TYPE_DISK = 0,     /* Whole disk */
    BLOCKDEV_TYPE_PARTITION,    /* Partition on a disk */
    BLOCKDEV_TYPE_RAMDISK       /* RAM-backed disk */
} blockdev_type_t;

/* Partition types (MBR) */
#define PART_TYPE_EMPTY         0x00
#define PART_TYPE_FAT12         0x01
#define PART_TYPE_FAT16_SMALL   0x04
#define PART_TYPE_EXTENDED      0x05
#define PART_TYPE_FAT16         0x06
#define PART_TYPE_NTFS          0x07
#define PART_TYPE_FAT32         0x0B
#define PART_TYPE_FAT32_LBA     0x0C
#define PART_TYPE_FAT16_LBA     0x0E
#define PART_TYPE_EXTENDED_LBA  0x0F
#define PART_TYPE_LINUX         0x83
#define PART_TYPE_LINUX_SWAP    0x82
#define PART_TYPE_LINUX_LVM     0x8E

/* Forward declaration */
struct blockdev;

/* Block device operations */
typedef struct {
    bool (*read)(struct blockdev* dev, uint32_t lba, uint32_t count, void* buffer);
    bool (*write)(struct blockdev* dev, uint32_t lba, uint32_t count, const void* buffer);
    bool (*flush)(struct blockdev* dev);
} blockdev_ops_t;

/* Partition information */
typedef struct {
    bool active;                /* Bootable flag */
    uint8_t type;               /* Partition type */
    uint32_t start_lba;         /* Starting LBA */
    uint32_t sector_count;      /* Number of sectors */
    uint32_t size_mb;           /* Size in MB */
} partition_info_t;

/* Block device structure */
typedef struct blockdev {
    char name[16];              /* Device name (e.g., "hd0", "hd0p1") */
    blockdev_type_t type;       /* Device type */
    
    uint32_t sector_size;       /* Bytes per sector (usually 512) */
    uint32_t sector_count;      /* Total sectors */
    uint32_t size_mb;           /* Size in megabytes */
    
    /* For partitions: offset from parent device */
    uint32_t start_lba;         /* Starting LBA (0 for whole disk) */
    struct blockdev* parent;    /* Parent device (NULL for whole disk) */
    
    /* Hardware driver data */
    void* driver_data;          /* Opaque pointer to driver-specific data */
    uint8_t driver_id;          /* Driver-assigned ID (e.g., ATA drive number) */
    
    /* Operations */
    const blockdev_ops_t* ops;
    
    /* Partition table (for whole disks) */
    uint8_t partition_count;
    partition_info_t partitions[BLOCKDEV_MAX_PARTITIONS];
} blockdev_t;

/* Initialize block device subsystem */
void blockdev_init(void);

/* Register a new block device */
blockdev_t* blockdev_register(const char* name, blockdev_type_t type,
                               uint32_t sector_size, uint32_t sector_count,
                               const blockdev_ops_t* ops, void* driver_data,
                               uint8_t driver_id);

/* Unregister a block device */
void blockdev_unregister(blockdev_t* dev);

/* Get block device by name */
blockdev_t* blockdev_get_by_name(const char* name);

/* Get block device by index */
blockdev_t* blockdev_get(uint8_t index);

/* Get number of registered block devices */
uint8_t blockdev_count(void);

/* Read sectors from block device */
bool blockdev_read(blockdev_t* dev, uint32_t lba, uint32_t count, void* buffer);

/* Write sectors to block device */
bool blockdev_write(blockdev_t* dev, uint32_t lba, uint32_t count, const void* buffer);

/* Flush block device cache */
bool blockdev_flush(blockdev_t* dev);

/* Probe and parse partition table */
int blockdev_probe_partitions(blockdev_t* dev);

/* Get partition type name */
const char* blockdev_partition_type_name(uint8_t type);

#endif /* _BLOCKDEV_H */
