/*
 * MiniOS ATA/IDE Disk Driver
 * 
 * PIO mode driver for IDE hard disks.
 * Supports primary and secondary IDE buses, master and slave drives.
 */

#include "../include/ata.h"
#include "../include/io.h"
#include "../include/string.h"
#include "../include/stdio.h"
#include "../include/blockdev.h"

/* Detected drives */
static ata_drive_t drives[ATA_MAX_DRIVES];
static uint8_t drive_count = 0;

/*
 * Wait for drive to be ready (not busy)
 */
static bool ata_wait_ready(uint16_t port) {
    int timeout = 500000;
    while (timeout-- > 0) {
        uint8_t status = inb(port + 7);
        if (!(status & ATA_STATUS_BSY)) {
            return true;
        }
    }
    return false;
}

/*
 * Wait for DRQ (data request) or error
 */
static bool ata_wait_drq(uint16_t port) {
    int timeout = 500000;
    while (timeout-- > 0) {
        uint8_t status = inb(port + 7);
        if (status & ATA_STATUS_ERR) {
            return false;
        }
        if (status & ATA_STATUS_DRQ) {
            return true;
        }
    }
    return false;
}

/*
 * Software reset an ATA bus
 */
static void ata_soft_reset(uint16_t ctrl_port) {
    outb(ctrl_port, 0x04);  /* Set SRST bit */
    for (int i = 0; i < 1000; i++) {
        inb(ctrl_port);     /* Delay */
    }
    outb(ctrl_port, 0x00);  /* Clear SRST bit */
    for (int i = 0; i < 1000; i++) {
        inb(ctrl_port);     /* Delay */
    }
}

/*
 * Select a drive on the bus
 */
static void ata_select_drive(uint16_t port, uint8_t drive) {
    outb(port + 6, 0xA0 | (drive << 4));
    /* Wait 400ns by reading status 4 times */
    for (int i = 0; i < 4; i++) {
        inb(port + 7);
    }
}

/*
 * Copy and clean an ATA identification string
 */
static void ata_copy_string(char* dest, uint16_t* src, int words) {
    for (int i = 0; i < words; i++) {
        dest[i * 2] = (char)(src[i] >> 8);
        dest[i * 2 + 1] = (char)(src[i] & 0xFF);
    }
    dest[words * 2] = '\0';
    
    /* Trim trailing spaces */
    int len = words * 2 - 1;
    while (len >= 0 && dest[len] == ' ') {
        dest[len--] = '\0';
    }
}

/*
 * Identify a drive
 */
static bool ata_identify(uint16_t port, uint16_t ctrl_port, uint8_t drive, ata_drive_t* info) {
    uint16_t identify_data[256];
    
    /* Select drive */
    ata_select_drive(port, drive);
    
    /* Clear sector count and LBA registers */
    outb(port + 2, 0);
    outb(port + 3, 0);
    outb(port + 4, 0);
    outb(port + 5, 0);
    
    /* Send IDENTIFY command */
    outb(port + 7, ATA_CMD_IDENTIFY);
    
    /* Check if drive exists */
    uint8_t status = inb(port + 7);
    if (status == 0) {
        return false;  /* No drive */
    }
    
    /* Wait for BSY to clear */
    if (!ata_wait_ready(port)) {
        return false;
    }
    
    /* Check for ATAPI */
    uint8_t lba_mid = inb(port + 4);
    uint8_t lba_hi = inb(port + 5);
    
    if (lba_mid == 0x14 && lba_hi == 0xEB) {
        /* ATAPI device - send IDENTIFY PACKET command */
        outb(port + 7, ATA_CMD_IDENTIFY_PACKET);
        if (!ata_wait_ready(port)) {
            return false;
        }
        info->type = ATA_TYPE_ATAPI;
    } else if (lba_mid == 0 && lba_hi == 0) {
        info->type = ATA_TYPE_ATA;
    } else {
        return false;  /* Unknown device */
    }
    
    /* Wait for DRQ */
    if (!ata_wait_drq(port)) {
        return false;
    }
    
    /* Read identification data */
    for (int i = 0; i < 256; i++) {
        identify_data[i] = inw(port);
    }
    
    /* Extract information */
    ata_copy_string(info->serial, &identify_data[10], 10);
    ata_copy_string(info->firmware, &identify_data[23], 4);
    ata_copy_string(info->model, &identify_data[27], 20);
    
    /* Get sector count */
    if (identify_data[83] & (1 << 10)) {
        /* 48-bit LBA supported */
        info->lba48_supported = true;
        info->sectors_48 = ((uint64_t)identify_data[103] << 48) |
                           ((uint64_t)identify_data[102] << 32) |
                           ((uint64_t)identify_data[101] << 16) |
                           (uint64_t)identify_data[100];
        info->sectors = (uint32_t)info->sectors_48;
    } else {
        /* 28-bit LBA only */
        info->lba48_supported = false;
        info->sectors = ((uint32_t)identify_data[61] << 16) | identify_data[60];
        info->sectors_48 = info->sectors;
    }
    
    /* Calculate size in MB */
    info->size_mb = (uint32_t)(info->sectors / 2048);
    
    info->present = true;
    info->base_port = port;
    info->ctrl_port = ctrl_port;
    
    return true;
}

/*
 * Initialize ATA driver and detect drives
 */
void ata_init(void) {
    drive_count = 0;
    memset(drives, 0, sizeof(drives));
    
    printk("ATA: Detecting drives...\n");
    
    /* Probe primary bus */
    ata_soft_reset(ATA_PRIMARY_CONTROL);
    
    /* Primary master */
    if (ata_identify(ATA_PRIMARY_DATA, ATA_PRIMARY_CONTROL, ATA_MASTER, &drives[0])) {
        drives[0].bus = 0;
        drives[0].drive = 0;
        drive_count++;
        printk("ATA: Found %s on primary master: %s (%u MB)\n",
               drives[0].type == ATA_TYPE_ATA ? "HDD" : "CDROM",
               drives[0].model, drives[0].size_mb);
    }
    
    /* Primary slave */
    if (ata_identify(ATA_PRIMARY_DATA, ATA_PRIMARY_CONTROL, ATA_SLAVE, &drives[1])) {
        drives[1].bus = 0;
        drives[1].drive = 1;
        drive_count++;
        printk("ATA: Found %s on primary slave: %s (%u MB)\n",
               drives[1].type == ATA_TYPE_ATA ? "HDD" : "CDROM",
               drives[1].model, drives[1].size_mb);
    }
    
    /* Probe secondary bus */
    ata_soft_reset(ATA_SECONDARY_CONTROL);
    
    /* Secondary master */
    if (ata_identify(ATA_SECONDARY_DATA, ATA_SECONDARY_CONTROL, ATA_MASTER, &drives[2])) {
        drives[2].bus = 1;
        drives[2].drive = 0;
        drive_count++;
        printk("ATA: Found %s on secondary master: %s (%u MB)\n",
               drives[2].type == ATA_TYPE_ATA ? "HDD" : "CDROM",
               drives[2].model, drives[2].size_mb);
    }
    
    /* Secondary slave */
    if (ata_identify(ATA_SECONDARY_DATA, ATA_SECONDARY_CONTROL, ATA_SLAVE, &drives[3])) {
        drives[3].bus = 1;
        drives[3].drive = 1;
        drive_count++;
        printk("ATA: Found %s on secondary slave: %s (%u MB)\n",
               drives[3].type == ATA_TYPE_ATA ? "HDD" : "CDROM",
               drives[3].model, drives[3].size_mb);
    }
    
    if (drive_count == 0) {
        printk("ATA: No drives detected\n");
    } else {
        printk("ATA: %u drive(s) detected\n", drive_count);
    }
    
    /* Register block devices for detected drives */
    ata_register_blockdevs();
}

/*
 * Block device operations for ATA
 */
static bool ata_blockdev_read(blockdev_t* dev, uint32_t lba, uint32_t count, void* buffer) {
    if (dev == NULL) return false;
    uint8_t drive_id = dev->driver_id;
    
    /* Read in chunks of 255 sectors (max for 28-bit LBA command) */
    uint8_t* buf = (uint8_t*)buffer;
    while (count > 0) {
        uint8_t chunk = (count > 255) ? 255 : (uint8_t)count;
        if (!ata_read_sectors(drive_id, lba, chunk, buf)) {
            return false;
        }
        lba += chunk;
        count -= chunk;
        buf += chunk * 512;
    }
    return true;
}

static bool ata_blockdev_write(blockdev_t* dev, uint32_t lba, uint32_t count, const void* buffer) {
    if (dev == NULL) return false;
    uint8_t drive_id = dev->driver_id;
    
    const uint8_t* buf = (const uint8_t*)buffer;
    while (count > 0) {
        uint8_t chunk = (count > 255) ? 255 : (uint8_t)count;
        if (!ata_write_sectors(drive_id, lba, chunk, buf)) {
            return false;
        }
        lba += chunk;
        count -= chunk;
        buf += chunk * 512;
    }
    return true;
}

static bool ata_blockdev_flush(blockdev_t* dev) {
    if (dev == NULL) return false;
    return ata_flush(dev->driver_id);
}

static const blockdev_ops_t ata_blockdev_ops = {
    .read = ata_blockdev_read,
    .write = ata_blockdev_write,
    .flush = ata_blockdev_flush
};

/*
 * Register block devices for all detected ATA drives
 */
void ata_register_blockdevs(void) {
    for (int i = 0; i < ATA_MAX_DRIVES; i++) {
        if (!drives[i].present) continue;
        if (drives[i].type != ATA_TYPE_ATA) continue;  /* Skip ATAPI for now */
        
        char name[8];
        snprintf(name, sizeof(name), "hd%d", i);
        
        blockdev_t* bdev = blockdev_register(name, BLOCKDEV_TYPE_DISK,
                                              ATA_SECTOR_SIZE, drives[i].sectors,
                                              &ata_blockdev_ops, &drives[i], i);
        
        if (bdev) {
            /* Probe for partitions */
            blockdev_probe_partitions(bdev);
        }
    }
}

/*
 * Get drive information
 */
ata_drive_t* ata_get_drive(uint8_t drive_num) {
    if (drive_num >= ATA_MAX_DRIVES) {
        return NULL;
    }
    if (!drives[drive_num].present) {
        return NULL;
    }
    return &drives[drive_num];
}

/*
 * Get number of detected drives
 */
uint8_t ata_get_drive_count(void) {
    return drive_count;
}

/*
 * Read sectors from drive (28-bit LBA, PIO mode)
 */
bool ata_read_sectors(uint8_t drive_num, uint32_t lba, uint8_t count, void* buffer) {
    if (drive_num >= ATA_MAX_DRIVES || !drives[drive_num].present) {
        return false;
    }
    
    ata_drive_t* drive = &drives[drive_num];
    uint16_t port = drive->base_port;
    uint16_t* buf = (uint16_t*)buffer;
    
    /* Wait for drive ready */
    if (!ata_wait_ready(port)) {
        return false;
    }
    
    /* Select drive and set LBA mode */
    outb(port + 6, 0xE0 | (drive->drive << 4) | ((lba >> 24) & 0x0F));
    
    /* Set sector count and LBA */
    outb(port + 2, count);
    outb(port + 3, (uint8_t)(lba & 0xFF));
    outb(port + 4, (uint8_t)((lba >> 8) & 0xFF));
    outb(port + 5, (uint8_t)((lba >> 16) & 0xFF));
    
    /* Send read command */
    outb(port + 7, ATA_CMD_READ_PIO);
    
    /* Read sectors */
    for (int s = 0; s < count; s++) {
        /* Wait for DRQ */
        if (!ata_wait_drq(port)) {
            return false;
        }
        
        /* Read 256 words (512 bytes) */
        for (int i = 0; i < 256; i++) {
            buf[s * 256 + i] = inw(port);
        }
    }
    
    return true;
}

/*
 * Write sectors to drive (28-bit LBA, PIO mode)
 */
bool ata_write_sectors(uint8_t drive_num, uint32_t lba, uint8_t count, const void* buffer) {
    if (drive_num >= ATA_MAX_DRIVES || !drives[drive_num].present) {
        return false;
    }
    
    ata_drive_t* drive = &drives[drive_num];
    uint16_t port = drive->base_port;
    const uint16_t* buf = (const uint16_t*)buffer;
    
    /* Wait for drive ready */
    if (!ata_wait_ready(port)) {
        return false;
    }
    
    /* Select drive and set LBA mode */
    outb(port + 6, 0xE0 | (drive->drive << 4) | ((lba >> 24) & 0x0F));
    
    /* Set sector count and LBA */
    outb(port + 2, count);
    outb(port + 3, (uint8_t)(lba & 0xFF));
    outb(port + 4, (uint8_t)((lba >> 8) & 0xFF));
    outb(port + 5, (uint8_t)((lba >> 16) & 0xFF));
    
    /* Send write command */
    outb(port + 7, ATA_CMD_WRITE_PIO);
    
    /* Write sectors */
    for (int s = 0; s < count; s++) {
        /* Wait for DRQ */
        if (!ata_wait_drq(port)) {
            return false;
        }
        
        /* Write 256 words (512 bytes) */
        for (int i = 0; i < 256; i++) {
            outw(port, buf[s * 256 + i]);
        }
    }
    
    /* Wait for drive to finish processing */
    if (!ata_wait_ready(port)) {
        return false;
    }
    
    return true;
}

/*
 * Flush drive cache
 */
bool ata_flush(uint8_t drive_num) {
    if (drive_num >= ATA_MAX_DRIVES || !drives[drive_num].present) {
        return false;
    }
    
    ata_drive_t* drive = &drives[drive_num];
    uint16_t port = drive->base_port;
    
    /* Select drive */
    outb(port + 6, 0xE0 | (drive->drive << 4));
    
    /* Send cache flush command */
    outb(port + 7, ATA_CMD_CACHE_FLUSH);
    
    /* Wait for completion */
    return ata_wait_ready(port);
}
