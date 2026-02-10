/*
 * MiniOS ATA/IDE Disk Driver Header
 * 
 * PIO mode driver for IDE hard disks.
 */

#ifndef _ATA_H
#define _ATA_H

#include "types.h"

/* ATA I/O Ports (Primary Bus) */
#define ATA_PRIMARY_DATA        0x1F0
#define ATA_PRIMARY_ERROR       0x1F1
#define ATA_PRIMARY_SECCOUNT    0x1F2
#define ATA_PRIMARY_LBA_LO      0x1F3
#define ATA_PRIMARY_LBA_MID     0x1F4
#define ATA_PRIMARY_LBA_HI      0x1F5
#define ATA_PRIMARY_DRIVE       0x1F6
#define ATA_PRIMARY_STATUS      0x1F7
#define ATA_PRIMARY_COMMAND     0x1F7
#define ATA_PRIMARY_CONTROL     0x3F6

/* ATA I/O Ports (Secondary Bus) */
#define ATA_SECONDARY_DATA      0x170
#define ATA_SECONDARY_ERROR     0x171
#define ATA_SECONDARY_SECCOUNT  0x172
#define ATA_SECONDARY_LBA_LO    0x173
#define ATA_SECONDARY_LBA_MID   0x174
#define ATA_SECONDARY_LBA_HI    0x175
#define ATA_SECONDARY_DRIVE     0x176
#define ATA_SECONDARY_STATUS    0x177
#define ATA_SECONDARY_COMMAND   0x177
#define ATA_SECONDARY_CONTROL   0x376

/* ATA Status Register Bits */
#define ATA_STATUS_ERR          0x01    /* Error */
#define ATA_STATUS_IDX          0x02    /* Index */
#define ATA_STATUS_CORR         0x04    /* Corrected data */
#define ATA_STATUS_DRQ          0x08    /* Data request ready */
#define ATA_STATUS_SRV          0x10    /* Overlapped mode service request */
#define ATA_STATUS_DF           0x20    /* Drive fault */
#define ATA_STATUS_RDY          0x40    /* Drive ready */
#define ATA_STATUS_BSY          0x80    /* Drive busy */

/* ATA Commands */
#define ATA_CMD_READ_PIO        0x20
#define ATA_CMD_READ_PIO_EXT    0x24
#define ATA_CMD_WRITE_PIO       0x30
#define ATA_CMD_WRITE_PIO_EXT   0x34
#define ATA_CMD_CACHE_FLUSH     0xE7
#define ATA_CMD_CACHE_FLUSH_EXT 0xEA
#define ATA_CMD_IDENTIFY        0xEC
#define ATA_CMD_IDENTIFY_PACKET 0xA1

/* Drive selection */
#define ATA_MASTER              0x00
#define ATA_SLAVE               0x01

/* Sector size */
#define ATA_SECTOR_SIZE         512

/* Maximum drives */
#define ATA_MAX_DRIVES          4

/* Drive types */
typedef enum {
    ATA_TYPE_NONE = 0,
    ATA_TYPE_ATA,
    ATA_TYPE_ATAPI
} ata_drive_type_t;

/* Drive information structure */
typedef struct {
    bool present;               /* Drive is present */
    ata_drive_type_t type;      /* Drive type */
    uint8_t bus;                /* 0 = primary, 1 = secondary */
    uint8_t drive;              /* 0 = master, 1 = slave */
    uint16_t base_port;         /* Base I/O port */
    uint16_t ctrl_port;         /* Control port */
    
    /* Identification data */
    char model[41];             /* Model string */
    char serial[21];            /* Serial number */
    char firmware[9];           /* Firmware revision */
    
    uint32_t sectors;           /* Total sectors (28-bit LBA) */
    uint64_t sectors_48;        /* Total sectors (48-bit LBA) */
    bool lba48_supported;       /* 48-bit LBA support */
    uint32_t size_mb;           /* Size in megabytes */
} ata_drive_t;

/* Initialize ATA driver and detect drives */
void ata_init(void);

/* Get drive information */
ata_drive_t* ata_get_drive(uint8_t drive_num);

/* Get number of detected drives */
uint8_t ata_get_drive_count(void);

/* Read sectors from drive (28-bit LBA) */
bool ata_read_sectors(uint8_t drive_num, uint32_t lba, uint8_t count, void* buffer);

/* Write sectors to drive (28-bit LBA) */
bool ata_write_sectors(uint8_t drive_num, uint32_t lba, uint8_t count, const void* buffer);

/* Flush drive cache */
bool ata_flush(uint8_t drive_num);

/* Register ATA drives as block devices */
void ata_register_blockdevs(void);

#endif /* _ATA_H */
