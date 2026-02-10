/*
 * MiniOS Block Device Layer
 * 
 * Provides a unified interface for block storage devices.
 */

#include "../include/blockdev.h"
#include "../include/string.h"
#include "../include/stdio.h"
#include "../include/heap.h"

/* Registered block devices */
static blockdev_t* devices[BLOCKDEV_MAX_DEVICES];
static uint8_t device_count = 0;

/* MBR partition entry structure (16 bytes) */
typedef struct {
    uint8_t  status;            /* 0x80 = bootable, 0x00 = not */
    uint8_t  chs_first[3];      /* CHS of first sector */
    uint8_t  type;              /* Partition type */
    uint8_t  chs_last[3];       /* CHS of last sector */
    uint32_t lba_first;         /* LBA of first sector */
    uint32_t sector_count;      /* Number of sectors */
} __attribute__((packed)) mbr_partition_entry_t;

/* MBR structure (512 bytes) */
typedef struct {
    uint8_t  bootstrap[446];    /* Bootstrap code */
    mbr_partition_entry_t partitions[4];  /* Partition table */
    uint16_t signature;         /* 0xAA55 */
} __attribute__((packed)) mbr_t;

/*
 * Initialize block device subsystem
 */
void blockdev_init(void) {
    device_count = 0;
    for (int i = 0; i < BLOCKDEV_MAX_DEVICES; i++) {
        devices[i] = NULL;
    }
    printk("BlockDev: Initialized\n");
}

/*
 * Register a new block device
 */
blockdev_t* blockdev_register(const char* name, blockdev_type_t type,
                               uint32_t sector_size, uint32_t sector_count,
                               const blockdev_ops_t* ops, void* driver_data,
                               uint8_t driver_id) {
    if (device_count >= BLOCKDEV_MAX_DEVICES) {
        printk("BlockDev: Maximum devices reached\n");
        return NULL;
    }
    
    blockdev_t* dev = (blockdev_t*)kmalloc(sizeof(blockdev_t));
    if (dev == NULL) {
        printk("BlockDev: Failed to allocate device\n");
        return NULL;
    }
    
    memset(dev, 0, sizeof(blockdev_t));
    strncpy(dev->name, name, sizeof(dev->name) - 1);
    dev->type = type;
    dev->sector_size = sector_size;
    dev->sector_count = sector_count;
    dev->size_mb = (uint32_t)((uint64_t)sector_count * sector_size / (1024 * 1024));
    dev->start_lba = 0;
    dev->parent = NULL;
    dev->driver_data = driver_data;
    dev->driver_id = driver_id;
    dev->ops = ops;
    dev->partition_count = 0;
    
    /* Find free slot */
    for (int i = 0; i < BLOCKDEV_MAX_DEVICES; i++) {
        if (devices[i] == NULL) {
            devices[i] = dev;
            device_count++;
            printk("BlockDev: Registered '%s' (%u MB)\n", name, dev->size_mb);
            return dev;
        }
    }
    
    kfree(dev);
    return NULL;
}

/*
 * Unregister a block device
 */
void blockdev_unregister(blockdev_t* dev) {
    if (dev == NULL) return;
    
    for (int i = 0; i < BLOCKDEV_MAX_DEVICES; i++) {
        if (devices[i] == dev) {
            devices[i] = NULL;
            device_count--;
            kfree(dev);
            return;
        }
    }
}

/*
 * Get block device by name
 */
blockdev_t* blockdev_get_by_name(const char* name) {
    for (int i = 0; i < BLOCKDEV_MAX_DEVICES; i++) {
        if (devices[i] != NULL && strcmp(devices[i]->name, name) == 0) {
            return devices[i];
        }
    }
    return NULL;
}

/*
 * Get block device by index
 */
blockdev_t* blockdev_get(uint8_t index) {
    uint8_t count = 0;
    for (int i = 0; i < BLOCKDEV_MAX_DEVICES; i++) {
        if (devices[i] != NULL) {
            if (count == index) {
                return devices[i];
            }
            count++;
        }
    }
    return NULL;
}

/*
 * Get number of registered block devices
 */
uint8_t blockdev_count(void) {
    return device_count;
}

/*
 * Read sectors from block device
 */
bool blockdev_read(blockdev_t* dev, uint32_t lba, uint32_t count, void* buffer) {
    if (dev == NULL || dev->ops == NULL || dev->ops->read == NULL) {
        return false;
    }
    
    /* For partitions, adjust LBA relative to parent */
    if (dev->type == BLOCKDEV_TYPE_PARTITION && dev->parent != NULL) {
        return blockdev_read(dev->parent, dev->start_lba + lba, count, buffer);
    }
    
    return dev->ops->read(dev, lba, count, buffer);
}

/*
 * Write sectors to block device
 */
bool blockdev_write(blockdev_t* dev, uint32_t lba, uint32_t count, const void* buffer) {
    if (dev == NULL || dev->ops == NULL || dev->ops->write == NULL) {
        return false;
    }
    
    /* For partitions, adjust LBA relative to parent */
    if (dev->type == BLOCKDEV_TYPE_PARTITION && dev->parent != NULL) {
        return blockdev_write(dev->parent, dev->start_lba + lba, count, buffer);
    }
    
    return dev->ops->write(dev, lba, count, buffer);
}

/*
 * Flush block device cache
 */
bool blockdev_flush(blockdev_t* dev) {
    if (dev == NULL || dev->ops == NULL || dev->ops->flush == NULL) {
        return false;
    }
    
    /* For partitions, flush parent */
    if (dev->type == BLOCKDEV_TYPE_PARTITION && dev->parent != NULL) {
        return blockdev_flush(dev->parent);
    }
    
    return dev->ops->flush(dev);
}

/*
 * Get partition type name
 */
const char* blockdev_partition_type_name(uint8_t type) {
    switch (type) {
        case PART_TYPE_EMPTY:       return "Empty";
        case PART_TYPE_FAT12:       return "FAT12";
        case PART_TYPE_FAT16_SMALL: return "FAT16 (<32MB)";
        case PART_TYPE_EXTENDED:    return "Extended";
        case PART_TYPE_FAT16:       return "FAT16";
        case PART_TYPE_NTFS:        return "NTFS/exFAT";
        case PART_TYPE_FAT32:       return "FAT32";
        case PART_TYPE_FAT32_LBA:   return "FAT32 LBA";
        case PART_TYPE_FAT16_LBA:   return "FAT16 LBA";
        case PART_TYPE_EXTENDED_LBA: return "Extended LBA";
        case PART_TYPE_LINUX:       return "Linux";
        case PART_TYPE_LINUX_SWAP:  return "Linux Swap";
        case PART_TYPE_LINUX_LVM:   return "Linux LVM";
        default:                    return "Unknown";
    }
}

/*
 * Probe and parse MBR partition table
 */
int blockdev_probe_partitions(blockdev_t* dev) {
    if (dev == NULL || dev->type != BLOCKDEV_TYPE_DISK) {
        return -1;
    }
    
    /* Read MBR (sector 0) */
    uint8_t mbr_buf[512];
    if (!blockdev_read(dev, 0, 1, mbr_buf)) {
        printk("BlockDev: Failed to read MBR from %s\n", dev->name);
        return -1;
    }
    
    mbr_t* mbr = (mbr_t*)mbr_buf;
    
    /* Check MBR signature */
    if (mbr->signature != 0xAA55) {
        printk("BlockDev: No valid MBR on %s (sig=0x%04x)\n", dev->name, mbr->signature);
        return 0;  /* Not an error, just no partition table */
    }
    
    printk("BlockDev: Parsing MBR on %s\n", dev->name);
    
    int found = 0;
    for (int i = 0; i < 4; i++) {
        mbr_partition_entry_t* entry = &mbr->partitions[i];
        
        if (entry->type == PART_TYPE_EMPTY) {
            continue;
        }
        
        if (dev->partition_count >= BLOCKDEV_MAX_PARTITIONS) {
            break;
        }
        
        partition_info_t* part = &dev->partitions[dev->partition_count];
        part->active = (entry->status == 0x80);
        part->type = entry->type;
        part->start_lba = entry->lba_first;
        part->sector_count = entry->sector_count;
        part->size_mb = (uint32_t)((uint64_t)entry->sector_count * 512 / (1024 * 1024));
        
        printk("  Partition %d: type=0x%02x start=%u size=%u MB%s\n",
               dev->partition_count + 1,
               part->type,
               part->start_lba,
               part->size_mb,
               part->active ? " (active)" : "");
        
        /* Create partition block device */
        char part_name[16];
        snprintf(part_name, sizeof(part_name), "%sp%d", dev->name, dev->partition_count + 1);
        
        blockdev_t* part_dev = blockdev_register(part_name, BLOCKDEV_TYPE_PARTITION,
                                                  dev->sector_size, entry->sector_count,
                                                  dev->ops, dev->driver_data, dev->driver_id);
        if (part_dev) {
            part_dev->start_lba = entry->lba_first;
            part_dev->parent = dev;
        }
        
        dev->partition_count++;
        found++;
    }
    
    return found;
}
