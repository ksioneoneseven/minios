#include "../include/ext2.h"
#include "../include/heap.h"
#include "../include/string.h"
#include "../include/stdio.h"
#include "../include/timer.h"
#include "../include/serial.h"

#define EXT2_SUPERBLOCK_OFFSET 1024
#define EXT2_SUPER_MAGIC 0xEF53

#define EXT2_S_IFDIR  0x4000
#define EXT2_S_IFREG  0x8000

#define EXT2_FT_UNKNOWN 0
#define EXT2_FT_REG_FILE 1
#define EXT2_FT_DIR 2

typedef struct {
    uint32_t s_inodes_count;
    uint32_t s_blocks_count;
    uint32_t s_r_blocks_count;
    uint32_t s_free_blocks_count;
    uint32_t s_free_inodes_count;
    uint32_t s_first_data_block;
    uint32_t s_log_block_size;
    uint32_t s_log_frag_size;
    uint32_t s_blocks_per_group;
    uint32_t s_frags_per_group;
    uint32_t s_inodes_per_group;
    uint32_t s_mtime;
    uint32_t s_wtime;
    uint16_t s_mnt_count;
    uint16_t s_max_mnt_count;
    uint16_t s_magic;
    uint16_t s_state;
    uint16_t s_errors;
    uint16_t s_minor_rev_level;
    uint32_t s_lastcheck;
    uint32_t s_checkinterval;
    uint32_t s_creator_os;
    uint32_t s_rev_level;
    uint16_t s_def_resuid;
    uint16_t s_def_resgid;
    uint32_t s_first_ino;
    uint16_t s_inode_size;
    uint16_t s_block_group_nr;
    uint32_t s_feature_compat;
    uint32_t s_feature_incompat;
    uint32_t s_feature_ro_compat;
    uint8_t  s_uuid[16];
    char     s_volume_name[16];
    char     s_last_mounted[64];
    uint32_t s_algo_bitmap;
} __attribute__((packed)) ext2_superblock_t;

typedef struct {
    uint32_t bg_block_bitmap;
    uint32_t bg_inode_bitmap;
    uint32_t bg_inode_table;
    uint16_t bg_free_blocks_count;
    uint16_t bg_free_inodes_count;
    uint16_t bg_used_dirs_count;
    uint16_t bg_pad;
    uint8_t  bg_reserved[12];
} __attribute__((packed)) ext2_group_desc_t;

typedef struct {
    uint16_t i_mode;
    uint16_t i_uid;
    uint32_t i_size;
    uint32_t i_atime;
    uint32_t i_ctime;
    uint32_t i_mtime;
    uint32_t i_dtime;
    uint16_t i_gid;
    uint16_t i_links_count;
    uint32_t i_blocks;
    uint32_t i_flags;
    uint32_t i_osd1;
    uint32_t i_block[15];
    uint32_t i_generation;
    uint32_t i_file_acl;
    uint32_t i_dir_acl;
    uint32_t i_faddr;
    uint8_t  i_osd2[12];
} __attribute__((packed)) ext2_inode_t;

typedef struct {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t  name_len;
    uint8_t  file_type;
    char     name[];
} __attribute__((packed)) ext2_dirent_t;

typedef struct {
    blockdev_t* bdev;
    ext2_superblock_t sb;
    ext2_group_desc_t* groups;
    uint32_t block_size;
    uint32_t groups_count;
} ext2_fs_t;

static bool ext2_read_bytes(ext2_fs_t* fs, uint64_t offset, uint32_t size, void* out) {
    uint8_t* dst = (uint8_t*)out;
    uint8_t sector[512];

    while (size > 0) {
        uint64_t lba = offset / 512;
        uint32_t off = (uint32_t)(offset % 512);
        uint32_t chunk = 512 - off;
        if (chunk > size) chunk = size;

        if (!blockdev_read(fs->bdev, (uint32_t)lba, 1, sector)) {
            return false;
        }

        memcpy(dst, sector + off, chunk);
        dst += chunk;
        offset += chunk;
        size -= chunk;
    }

    return true;
}

static bool ext2_read_block(ext2_fs_t* fs, uint32_t block, void* out) {
    uint64_t offset = (uint64_t)block * fs->block_size;
    return ext2_read_bytes(fs, offset, fs->block_size, out);
}

static bool ext2_write_bytes(ext2_fs_t* fs, uint64_t offset, uint32_t size, const void* data) {
    const uint8_t* src = (const uint8_t*)data;
    uint8_t sector[512];

    while (size > 0) {
        uint64_t lba = offset / 512;
        uint32_t off = (uint32_t)(offset % 512);
        uint32_t chunk = 512 - off;
        if (chunk > size) chunk = size;

        /* Read-modify-write if partial sector */
        if (off != 0 || chunk < 512) {
            if (!blockdev_read(fs->bdev, (uint32_t)lba, 1, sector)) {
                return false;
            }
        }

        memcpy(sector + off, src, chunk);

        if (!blockdev_write(fs->bdev, (uint32_t)lba, 1, sector)) {
            return false;
        }

        src += chunk;
        offset += chunk;
        size -= chunk;
    }

    return true;
}

static bool ext2_write_block(ext2_fs_t* fs, uint32_t block, const void* data) {
    uint64_t offset = (uint64_t)block * fs->block_size;
    if (!ext2_write_bytes(fs, offset, fs->block_size, data))
        return false;
    blockdev_flush(fs->bdev);
    return true;
}

/* Forward declarations for functions used before definition */
static uint32_t ext2_alloc_block(ext2_fs_t* fs);

/*
 * Get the number of block pointers per block.
 * For 4KB blocks: 1024 pointers (4096 / 4)
 * For 1KB blocks: 256 pointers (1024 / 4)
 */
static inline uint32_t ext2_get_pointers_per_block(ext2_fs_t* fs) {
    return fs->block_size / sizeof(uint32_t);
}

/*
 * Get current Unix timestamp (approximation).
 * Note: MiniOS timer counts ticks at 100Hz, not real Unix time.
 * This is an approximation - real implementation would need RTC.
 */
static uint32_t ext2_get_current_time(void) {
    return timer_get_ticks() / 100;  /* Convert 100Hz ticks to "seconds" */
}

/*
 * Get physical block number for a logical block index within a file.
 * Handles direct, single indirect, and double indirect blocks.
 *
 * block_index: Logical block number (0, 1, 2, ... N)
 * allocate: If true, allocate missing indirect blocks
 *
 * Returns: Physical block number, or 0 if not allocated (sparse file)
 */
static uint32_t ext2_get_block_number(ext2_fs_t* fs, ext2_inode_t* ino,
                                       uint32_t block_index, bool allocate) {
    uint32_t ptrs_per_block = ext2_get_pointers_per_block(fs);
    uint32_t* indirect_block = NULL;
    uint32_t* double_indirect_block = NULL;
    uint32_t result = 0;

    /* Direct blocks: 0-11 */
    if (block_index < 12) {
        return ino->i_block[block_index];
    }
    block_index -= 12;

    /* Single indirect: i_block[12] */
    if (block_index < ptrs_per_block) {
        if (ino->i_block[12] == 0) {
            if (!allocate) return 0;

            /* Allocate indirect block */
            ino->i_block[12] = ext2_alloc_block(fs);
            if (ino->i_block[12] == 0) return 0;

            /* Zero the indirect block */
            uint8_t* zero_buf = (uint8_t*)kmalloc(fs->block_size);
            if (!zero_buf) {
                ino->i_block[12] = 0;
                return 0;
            }
            memset(zero_buf, 0, fs->block_size);
            ext2_write_block(fs, ino->i_block[12], zero_buf);
            kfree(zero_buf);
        }

        /* Read indirect block */
        indirect_block = (uint32_t*)kmalloc(fs->block_size);
        if (!indirect_block) return 0;

        if (!ext2_read_block(fs, ino->i_block[12], indirect_block)) {
            kfree(indirect_block);
            return 0;
        }

        result = indirect_block[block_index];
        kfree(indirect_block);
        return result;
    }
    block_index -= ptrs_per_block;

    /* Double indirect: i_block[13] */
    uint32_t double_limit = ptrs_per_block * ptrs_per_block;
    if (block_index < double_limit) {
        if (ino->i_block[13] == 0) {
            if (!allocate) return 0;

            /* Allocate double indirect block */
            ino->i_block[13] = ext2_alloc_block(fs);
            if (ino->i_block[13] == 0) return 0;

            /* Zero the double indirect block */
            uint8_t* zero_buf = (uint8_t*)kmalloc(fs->block_size);
            if (!zero_buf) {
                ino->i_block[13] = 0;
                return 0;
            }
            memset(zero_buf, 0, fs->block_size);
            ext2_write_block(fs, ino->i_block[13], zero_buf);
            kfree(zero_buf);
        }

        /* Calculate indices */
        uint32_t indirect_index = block_index / ptrs_per_block;
        uint32_t block_offset = block_index % ptrs_per_block;

        /* Read double indirect block */
        double_indirect_block = (uint32_t*)kmalloc(fs->block_size);
        if (!double_indirect_block) return 0;

        if (!ext2_read_block(fs, ino->i_block[13], double_indirect_block)) {
            kfree(double_indirect_block);
            return 0;
        }

        uint32_t indirect_block_num = double_indirect_block[indirect_index];
        if (indirect_block_num == 0) {
            if (!allocate) {
                kfree(double_indirect_block);
                return 0;
            }

            /* Allocate new indirect block */
            indirect_block_num = ext2_alloc_block(fs);
            if (indirect_block_num == 0) {
                kfree(double_indirect_block);
                return 0;
            }

            /* Update double indirect block */
            double_indirect_block[indirect_index] = indirect_block_num;
            ext2_write_block(fs, ino->i_block[13], double_indirect_block);

            /* Zero the new indirect block */
            uint8_t* zero_buf = (uint8_t*)kmalloc(fs->block_size);
            if (zero_buf) {
                memset(zero_buf, 0, fs->block_size);
                ext2_write_block(fs, indirect_block_num, zero_buf);
                kfree(zero_buf);
            }
        }

        /* Read indirect block */
        indirect_block = (uint32_t*)kmalloc(fs->block_size);
        if (!indirect_block) {
            kfree(double_indirect_block);
            return 0;
        }

        if (!ext2_read_block(fs, indirect_block_num, indirect_block)) {
            kfree(indirect_block);
            kfree(double_indirect_block);
            return 0;
        }

        result = indirect_block[block_offset];
        kfree(indirect_block);
        kfree(double_indirect_block);
        return result;
    }

    /* Triple indirect: i_block[14] - not implemented yet */
    /* For most use cases, double indirect is sufficient (4GB file size) */

    return 0;  /* Block index too large */
}

/*
 * Set physical block number for a logical block index.
 * Used during block allocation in write operations.
 */
static bool ext2_set_block_number(ext2_fs_t* fs, ext2_inode_t* ino,
                                   uint32_t block_index, uint32_t block_num) {
    uint32_t ptrs_per_block = ext2_get_pointers_per_block(fs);

    /* Direct blocks: 0-11 */
    if (block_index < 12) {
        ino->i_block[block_index] = block_num;
        return true;
    }
    block_index -= 12;

    /* Single indirect: i_block[12] */
    if (block_index < ptrs_per_block) {
        if (ino->i_block[12] == 0) {
            /* Allocate indirect block */
            ino->i_block[12] = ext2_alloc_block(fs);
            if (ino->i_block[12] == 0) return false;

            /* Zero the indirect block */
            uint8_t* zero_buf = (uint8_t*)kmalloc(fs->block_size);
            if (!zero_buf) {
                ino->i_block[12] = 0;
                return false;
            }
            memset(zero_buf, 0, fs->block_size);
            ext2_write_block(fs, ino->i_block[12], zero_buf);
            kfree(zero_buf);
        }

        /* Read indirect block */
        uint32_t* indirect_block = (uint32_t*)kmalloc(fs->block_size);
        if (!indirect_block) return false;

        if (!ext2_read_block(fs, ino->i_block[12], indirect_block)) {
            kfree(indirect_block);
            return false;
        }

        /* Set block number */
        indirect_block[block_index] = block_num;
        bool success = ext2_write_block(fs, ino->i_block[12], indirect_block);
        kfree(indirect_block);
        return success;
    }
    block_index -= ptrs_per_block;

    /* Double indirect: i_block[13] */
    uint32_t double_limit = ptrs_per_block * ptrs_per_block;
    if (block_index < double_limit) {
        if (ino->i_block[13] == 0) {
            /* Allocate double indirect block */
            ino->i_block[13] = ext2_alloc_block(fs);
            if (ino->i_block[13] == 0) return false;

            /* Zero the double indirect block */
            uint8_t* zero_buf = (uint8_t*)kmalloc(fs->block_size);
            if (!zero_buf) {
                ino->i_block[13] = 0;
                return false;
            }
            memset(zero_buf, 0, fs->block_size);
            ext2_write_block(fs, ino->i_block[13], zero_buf);
            kfree(zero_buf);
        }

        /* Calculate indices */
        uint32_t indirect_index = block_index / ptrs_per_block;
        uint32_t block_offset = block_index % ptrs_per_block;

        /* Read double indirect block */
        uint32_t* double_indirect_block = (uint32_t*)kmalloc(fs->block_size);
        if (!double_indirect_block) return false;

        if (!ext2_read_block(fs, ino->i_block[13], double_indirect_block)) {
            kfree(double_indirect_block);
            return false;
        }

        uint32_t indirect_block_num = double_indirect_block[indirect_index];
        if (indirect_block_num == 0) {
            /* Allocate new indirect block */
            indirect_block_num = ext2_alloc_block(fs);
            if (indirect_block_num == 0) {
                kfree(double_indirect_block);
                return false;
            }

            /* Update double indirect block */
            double_indirect_block[indirect_index] = indirect_block_num;
            ext2_write_block(fs, ino->i_block[13], double_indirect_block);

            /* Zero the new indirect block */
            uint8_t* zero_buf = (uint8_t*)kmalloc(fs->block_size);
            if (zero_buf) {
                memset(zero_buf, 0, fs->block_size);
                ext2_write_block(fs, indirect_block_num, zero_buf);
                kfree(zero_buf);
            }
        }

        /* Read indirect block */
        uint32_t* indirect_block = (uint32_t*)kmalloc(fs->block_size);
        if (!indirect_block) {
            kfree(double_indirect_block);
            return false;
        }

        if (!ext2_read_block(fs, indirect_block_num, indirect_block)) {
            kfree(indirect_block);
            kfree(double_indirect_block);
            return false;
        }

        /* Set block number */
        indirect_block[block_offset] = block_num;
        bool success = ext2_write_block(fs, indirect_block_num, indirect_block);

        kfree(indirect_block);
        kfree(double_indirect_block);
        return success;
    }

    /* Triple indirect: not implemented */
    return false;
}

static bool ext2_write_inode(ext2_fs_t* fs, uint32_t inode_num, const ext2_inode_t* ino) {
    if (inode_num == 0) return false;

    uint32_t idx = inode_num - 1;
    uint32_t group = idx / fs->sb.s_inodes_per_group;
    uint32_t index_in_group = idx % fs->sb.s_inodes_per_group;

    if (group >= fs->groups_count) return false;

    uint32_t inode_table_block = fs->groups[group].bg_inode_table;
    uint32_t inode_size = fs->sb.s_inode_size ? fs->sb.s_inode_size : 128;

    uint64_t inode_table_offset = (uint64_t)inode_table_block * fs->block_size;
    uint64_t inode_offset = inode_table_offset + (uint64_t)index_in_group * inode_size;

    return ext2_write_bytes(fs, inode_offset, sizeof(ext2_inode_t), ino);
}

static uint32_t ext2_alloc_block(ext2_fs_t* fs) {
    uint8_t* bitmap = (uint8_t*)kmalloc(fs->block_size);
    if (!bitmap) return 0;

    for (uint32_t g = 0; g < fs->groups_count; g++) {
        if (fs->groups[g].bg_free_blocks_count == 0) continue;

        if (!ext2_read_block(fs, fs->groups[g].bg_block_bitmap, bitmap)) {
            kfree(bitmap);
            return 0;
        }

        uint32_t blocks_in_group = fs->sb.s_blocks_per_group;
        for (uint32_t i = 0; i < blocks_in_group; i++) {
            uint32_t byte = i / 8;
            uint32_t bit = i % 8;

            if (!(bitmap[byte] & (1 << bit))) {
                /* Found free block */
                bitmap[byte] |= (1 << bit);

                if (!ext2_write_block(fs, fs->groups[g].bg_block_bitmap, bitmap)) {
                    kfree(bitmap);
                    return 0;
                }

                /* Update group descriptor */
                fs->groups[g].bg_free_blocks_count--;
                uint32_t gdt_block = (fs->block_size == 1024) ? 2 : 1;
                ext2_write_bytes(fs, (uint64_t)gdt_block * fs->block_size + g * sizeof(ext2_group_desc_t),
                                 sizeof(ext2_group_desc_t), &fs->groups[g]);

                /* Update superblock */
                fs->sb.s_free_blocks_count--;
                ext2_write_bytes(fs, EXT2_SUPERBLOCK_OFFSET, sizeof(ext2_superblock_t), &fs->sb);

                kfree(bitmap);
                uint32_t block_num = g * fs->sb.s_blocks_per_group + i + fs->sb.s_first_data_block;
                return block_num;
            }
        }
    }

    kfree(bitmap);
    return 0;
}

static uint32_t ext2_alloc_inode(ext2_fs_t* fs) {
    uint8_t* bitmap = (uint8_t*)kmalloc(fs->block_size);
    if (!bitmap) return 0;

    for (uint32_t g = 0; g < fs->groups_count; g++) {
        if (fs->groups[g].bg_free_inodes_count == 0) continue;

        if (!ext2_read_block(fs, fs->groups[g].bg_inode_bitmap, bitmap)) {
            kfree(bitmap);
            return 0;
        }

        uint32_t inodes_in_group = fs->sb.s_inodes_per_group;
        for (uint32_t i = 0; i < inodes_in_group; i++) {
            uint32_t byte = i / 8;
            uint32_t bit = i % 8;

            if (!(bitmap[byte] & (1 << bit))) {
                /* Found free inode */
                bitmap[byte] |= (1 << bit);

                if (!ext2_write_block(fs, fs->groups[g].bg_inode_bitmap, bitmap)) {
                    kfree(bitmap);
                    return 0;
                }

                /* Update group descriptor */
                fs->groups[g].bg_free_inodes_count--;
                uint32_t gdt_block = (fs->block_size == 1024) ? 2 : 1;
                ext2_write_bytes(fs, (uint64_t)gdt_block * fs->block_size + g * sizeof(ext2_group_desc_t),
                                 sizeof(ext2_group_desc_t), &fs->groups[g]);

                /* Update superblock */
                fs->sb.s_free_inodes_count--;
                ext2_write_bytes(fs, EXT2_SUPERBLOCK_OFFSET, sizeof(ext2_superblock_t), &fs->sb);

                kfree(bitmap);
                uint32_t inode_num = g * fs->sb.s_inodes_per_group + i + 1;
                return inode_num;
            }
        }
    }

    kfree(bitmap);
    return 0;
}

/*
 * Free a block back to the bitmap.
 * Reverse of ext2_alloc_block().
 */
static bool ext2_free_block(ext2_fs_t* fs, uint32_t block_num) {
    if (block_num < fs->sb.s_first_data_block || block_num >= fs->sb.s_blocks_count) {
        printk("ext2: attempt to free invalid block %u\n", block_num);
        return false;
    }

    uint32_t relative_block = block_num - fs->sb.s_first_data_block;
    uint32_t group = relative_block / fs->sb.s_blocks_per_group;
    uint32_t index_in_group = relative_block % fs->sb.s_blocks_per_group;

    if (group >= fs->groups_count) return false;

    uint8_t* bitmap = (uint8_t*)kmalloc(fs->block_size);
    if (!bitmap) return false;

    if (!ext2_read_block(fs, fs->groups[group].bg_block_bitmap, bitmap)) {
        kfree(bitmap);
        return false;
    }

    uint32_t byte = index_in_group / 8;
    uint32_t bit = index_in_group % 8;

    /* Check if block is actually allocated before freeing */
    if (!(bitmap[byte] & (1 << bit))) {
        printk("ext2: warning - freeing already-free block %u\n", block_num);
        kfree(bitmap);
        return false;
    }

    /* Clear the bit */
    bitmap[byte] &= ~(1 << bit);

    if (!ext2_write_block(fs, fs->groups[group].bg_block_bitmap, bitmap)) {
        kfree(bitmap);
        return false;
    }

    /* Update group descriptor */
    fs->groups[group].bg_free_blocks_count++;
    uint32_t gdt_block = (fs->block_size == 1024) ? 2 : 1;
    ext2_write_bytes(fs, (uint64_t)gdt_block * fs->block_size + group * sizeof(ext2_group_desc_t),
                     sizeof(ext2_group_desc_t), &fs->groups[group]);

    /* Update superblock */
    fs->sb.s_free_blocks_count++;
    ext2_write_bytes(fs, EXT2_SUPERBLOCK_OFFSET, sizeof(ext2_superblock_t), &fs->sb);

    kfree(bitmap);
    return true;
}

/*
 * Free an inode back to the bitmap.
 * Reverse of ext2_alloc_inode().
 */
static bool ext2_free_inode(ext2_fs_t* fs, uint32_t inode_num) {
    if (inode_num == 0) return false;

    uint32_t idx = inode_num - 1;
    uint32_t group = idx / fs->sb.s_inodes_per_group;
    uint32_t index_in_group = idx % fs->sb.s_inodes_per_group;

    if (group >= fs->groups_count) return false;

    uint8_t* bitmap = (uint8_t*)kmalloc(fs->block_size);
    if (!bitmap) return false;

    if (!ext2_read_block(fs, fs->groups[group].bg_inode_bitmap, bitmap)) {
        kfree(bitmap);
        return false;
    }

    uint32_t byte = index_in_group / 8;
    uint32_t bit = index_in_group % 8;

    /* Check if inode is actually allocated before freeing */
    if (!(bitmap[byte] & (1 << bit))) {
        printk("ext2: warning - freeing already-free inode %u\n", inode_num);
        kfree(bitmap);
        return false;
    }

    /* Clear the bit */
    bitmap[byte] &= ~(1 << bit);

    if (!ext2_write_block(fs, fs->groups[group].bg_inode_bitmap, bitmap)) {
        kfree(bitmap);
        return false;
    }

    /* Update group descriptor */
    fs->groups[group].bg_free_inodes_count++;
    uint32_t gdt_block = (fs->block_size == 1024) ? 2 : 1;
    ext2_write_bytes(fs, (uint64_t)gdt_block * fs->block_size + group * sizeof(ext2_group_desc_t),
                     sizeof(ext2_group_desc_t), &fs->groups[group]);

    /* Update superblock */
    fs->sb.s_free_inodes_count++;
    ext2_write_bytes(fs, EXT2_SUPERBLOCK_OFFSET, sizeof(ext2_superblock_t), &fs->sb);

    kfree(bitmap);
    return true;
}

/*
 * Free all blocks associated with an inode, including indirect blocks.
 */
static bool ext2_free_inode_blocks(ext2_fs_t* fs, ext2_inode_t* ino) {
    uint32_t ptrs_per_block = ext2_get_pointers_per_block(fs);

    /* Free direct blocks */
    for (int i = 0; i < 12; i++) {
        if (ino->i_block[i] != 0) {
            ext2_free_block(fs, ino->i_block[i]);
            ino->i_block[i] = 0;
        }
    }

    /* Free single indirect */
    if (ino->i_block[12] != 0) {
        uint32_t* indirect = (uint32_t*)kmalloc(fs->block_size);
        if (indirect) {
            if (ext2_read_block(fs, ino->i_block[12], indirect)) {
                for (uint32_t i = 0; i < ptrs_per_block; i++) {
                    if (indirect[i] != 0) {
                        ext2_free_block(fs, indirect[i]);
                    }
                }
            }
            kfree(indirect);
        }
        ext2_free_block(fs, ino->i_block[12]);
        ino->i_block[12] = 0;
    }

    /* Free double indirect */
    if (ino->i_block[13] != 0) {
        uint32_t* double_indirect = (uint32_t*)kmalloc(fs->block_size);
        uint32_t* indirect = (uint32_t*)kmalloc(fs->block_size);

        if (double_indirect && indirect) {
            if (ext2_read_block(fs, ino->i_block[13], double_indirect)) {
                for (uint32_t i = 0; i < ptrs_per_block; i++) {
                    if (double_indirect[i] != 0) {
                        if (ext2_read_block(fs, double_indirect[i], indirect)) {
                            for (uint32_t j = 0; j < ptrs_per_block; j++) {
                                if (indirect[j] != 0) {
                                    ext2_free_block(fs, indirect[j]);
                                }
                            }
                        }
                        ext2_free_block(fs, double_indirect[i]);
                    }
                }
            }
        }

        if (double_indirect) kfree(double_indirect);
        if (indirect) kfree(indirect);
        ext2_free_block(fs, ino->i_block[13]);
        ino->i_block[13] = 0;
    }

    /* Free triple indirect - if we ever implement it */
    if (ino->i_block[14] != 0) {
        /* For now, just free the triple indirect block itself */
        ext2_free_block(fs, ino->i_block[14]);
        ino->i_block[14] = 0;
    }

    return true;
}

static bool ext2_read_inode(ext2_fs_t* fs, uint32_t inode_num, ext2_inode_t* out) {
    if (inode_num == 0) return false;

    uint32_t idx = inode_num - 1;
    uint32_t group = idx / fs->sb.s_inodes_per_group;
    uint32_t index_in_group = idx % fs->sb.s_inodes_per_group;

    if (group >= fs->groups_count) return false;

    uint32_t inode_table_block = fs->groups[group].bg_inode_table;
    uint32_t inode_size = fs->sb.s_inode_size ? fs->sb.s_inode_size : 128;

    uint64_t inode_table_offset = (uint64_t)inode_table_block * fs->block_size;
    uint64_t inode_offset = inode_table_offset + (uint64_t)index_in_group * inode_size;

    memset(out, 0, sizeof(*out));
    return ext2_read_bytes(fs, inode_offset, sizeof(ext2_inode_t), out);
}

static uint32_t ext2_inode_type_to_vfs_flags(uint16_t mode) {
    if (mode & EXT2_S_IFDIR) return VFS_DIRECTORY;
    if (mode & EXT2_S_IFREG) return VFS_FILE;
    return VFS_FILE;
}

/* Forward declarations for VFS ops */
dirent_t* ext2_vfs_readdir(vfs_node_t* node, uint32_t index);
static vfs_node_t* ext2_vfs_finddir(vfs_node_t* node, const char* name);
static int32_t ext2_vfs_read(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer);
static int32_t ext2_vfs_write(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer);

static bool ext2_add_dir_entry(ext2_fs_t* fs, uint32_t dir_inode, uint32_t child_inode, 
                                const char* name, uint8_t file_type) {
    ext2_inode_t dir_ino;
    if (!ext2_read_inode(fs, dir_inode, &dir_ino)) return false;

    uint32_t name_len = strlen(name);
    uint32_t entry_size = sizeof(ext2_dirent_t) + name_len;
    /* Align to 4 bytes */
    entry_size = (entry_size + 3) & ~3;

    uint32_t block_size = fs->block_size;
    uint8_t* block_buf = (uint8_t*)kmalloc(block_size);
    if (!block_buf) return false;

    /* Try to find space in existing directory blocks */
    for (uint32_t bi = 0; bi < 12; bi++) {
        uint32_t blk = dir_ino.i_block[bi];
        if (blk == 0) {
            /* Allocate new block for directory */
            blk = ext2_alloc_block(fs);
            if (blk == 0) {
                kfree(block_buf);
                return false;
            }
            dir_ino.i_block[bi] = blk;
            dir_ino.i_size += block_size;
            dir_ino.i_blocks += block_size / 512;

            /* Initialize new block with single entry spanning whole block */
            memset(block_buf, 0, block_size);
            ext2_dirent_t* de = (ext2_dirent_t*)block_buf;
            de->inode = child_inode;
            de->rec_len = (uint16_t)block_size;
            de->name_len = (uint8_t)name_len;
            de->file_type = file_type;
            memcpy(de->name, name, name_len);

            if (!ext2_write_block(fs, blk, block_buf)) {
                kfree(block_buf);
                return false;
            }
            if (!ext2_write_inode(fs, dir_inode, &dir_ino)) {
                kfree(block_buf);
                return false;
            }
            kfree(block_buf);
            return true;
        }

        if (!ext2_read_block(fs, blk, block_buf)) {
            kfree(block_buf);
            return false;
        }

        /* Scan for space in this block */
        uint32_t off = 0;
        while (off < block_size) {
            ext2_dirent_t* de = (ext2_dirent_t*)(block_buf + off);
            if (de->rec_len == 0) break;

            /* Calculate actual size needed by this entry */
            uint32_t actual_size = sizeof(ext2_dirent_t) + de->name_len;
            actual_size = (actual_size + 3) & ~3;

            /* Check if there's room after this entry */
            if (de->rec_len >= actual_size + entry_size) {
                /* Split this entry */
                uint16_t new_rec_len = de->rec_len - (uint16_t)actual_size;
                de->rec_len = (uint16_t)actual_size;

                ext2_dirent_t* new_de = (ext2_dirent_t*)(block_buf + off + actual_size);
                new_de->inode = child_inode;
                new_de->rec_len = new_rec_len;
                new_de->name_len = (uint8_t)name_len;
                new_de->file_type = file_type;
                memcpy(new_de->name, name, name_len);

                if (!ext2_write_block(fs, blk, block_buf)) {
                    kfree(block_buf);
                    return false;
                }
                kfree(block_buf);
                return true;
            }

            off += de->rec_len;
        }
    }

    kfree(block_buf);
    return false;
}

/*
 * Remove a directory entry from a directory.
 */
static bool ext2_remove_dir_entry(ext2_fs_t* fs, uint32_t dir_inode, const char* name) {
    ext2_inode_t dir_ino;
    if (!ext2_read_inode(fs, dir_inode, &dir_ino)) return false;

    uint32_t block_size = fs->block_size;
    uint8_t* block_buf = (uint8_t*)kmalloc(block_size);
    if (!block_buf) return false;

    /* Scan directory blocks */
    for (uint32_t bi = 0; bi < 12; bi++) {
        uint32_t blk = dir_ino.i_block[bi];
        if (blk == 0) continue;

        if (!ext2_read_block(fs, blk, block_buf)) {
            kfree(block_buf);
            return false;
        }

        uint32_t off = 0;
        ext2_dirent_t* prev = NULL;

        while (off < block_size) {
            ext2_dirent_t* de = (ext2_dirent_t*)(block_buf + off);
            if (de->rec_len == 0) break;

            if (de->inode != 0 && de->name_len > 0) {
                char tmp[VFS_MAX_NAME];
                uint32_t n = de->name_len;
                if (n >= VFS_MAX_NAME) n = VFS_MAX_NAME - 1;
                memcpy(tmp, de->name, n);
                tmp[n] = '\0';

                if (strcmp(tmp, name) == 0) {
                    /* Found it! Mark as deleted by setting inode to 0 */
                    /* And extend previous entry's rec_len to cover this one */
                    if (prev) {
                        prev->rec_len += de->rec_len;
                    } else {
                        /* First entry - just zero the inode */
                        de->inode = 0;
                    }

                    if (!ext2_write_block(fs, blk, block_buf)) {
                        kfree(block_buf);
                        return false;
                    }

                    kfree(block_buf);
                    return true;
                }
            }

            prev = de;
            off += de->rec_len;
        }
    }

    kfree(block_buf);
    return false;
}

/*
 * Unlink (delete) a file from the filesystem.
 * Public API function.
 */
bool ext2_unlink(vfs_node_t* parent, const char* name) {
    ext2_fs_t* fs = (ext2_fs_t*)parent->impl;
    if (!fs) return false;

    /* Find the file */
    vfs_node_t* node = ext2_vfs_finddir(parent, name);
    if (!node) return false;  /* File doesn't exist */

    uint32_t inode_num = node->inode;

    ext2_inode_t ino;
    if (!ext2_read_inode(fs, inode_num, &ino)) {
        kfree(node);
        return false;
    }

    /* Don't allow unlinking directories (would need rmdir logic) */
    if (ino.i_mode & EXT2_S_IFDIR) {
        kfree(node);
        return false;
    }

    /* Decrement link count */
    ino.i_links_count--;

    /* If link count reaches zero, free the inode and its blocks */
    if (ino.i_links_count == 0) {
        /* Set deletion time */
        ino.i_dtime = ext2_get_current_time();

        /* Free all data blocks */
        ext2_free_inode_blocks(fs, &ino);

        /* Free the inode itself */
        ext2_free_inode(fs, inode_num);
    } else {
        /* Still has other hard links - just update the inode */
        ext2_write_inode(fs, inode_num, &ino);
    }

    /* Remove directory entry from parent */
    ext2_remove_dir_entry(fs, parent->inode, name);

    kfree(node);
    return true;
}

static vfs_node_t* ext2_create_node(vfs_node_t* parent, const char* name, uint16_t mode, uint8_t file_type) {
    ext2_fs_t* fs = (ext2_fs_t*)parent->impl;
    if (!fs) return NULL;

    /* Allocate inode */
    uint32_t new_inode = ext2_alloc_inode(fs);
    if (new_inode == 0) return NULL;

    /* Initialize inode */
    ext2_inode_t ino;
    memset(&ino, 0, sizeof(ino));
    ino.i_mode = mode;
    ino.i_uid = 0;
    ino.i_gid = 0;
    ino.i_size = 0;
    ino.i_links_count = 1;

    /* Set timestamps */
    uint32_t now = ext2_get_current_time();
    ino.i_atime = now;
    ino.i_mtime = now;
    ino.i_ctime = now;
    ino.i_dtime = 0;

    if (mode & EXT2_S_IFDIR) {
        /* Directory needs . and .. entries */
        uint32_t blk = ext2_alloc_block(fs);
        if (blk == 0) return NULL;

        ino.i_block[0] = blk;
        ino.i_size = fs->block_size;
        ino.i_blocks = fs->block_size / 512;
        ino.i_links_count = 2;

        /* Create . and .. entries */
        uint8_t* block_buf = (uint8_t*)kmalloc(fs->block_size);
        if (!block_buf) return NULL;

        memset(block_buf, 0, fs->block_size);

        /* . entry */
        ext2_dirent_t* de = (ext2_dirent_t*)block_buf;
        de->inode = new_inode;
        de->rec_len = 12;
        de->name_len = 1;
        de->file_type = EXT2_FT_DIR;
        de->name[0] = '.';

        /* .. entry */
        de = (ext2_dirent_t*)(block_buf + 12);
        de->inode = parent->inode;
        de->rec_len = (uint16_t)(fs->block_size - 12);
        de->name_len = 2;
        de->file_type = EXT2_FT_DIR;
        de->name[0] = '.';
        de->name[1] = '.';

        if (!ext2_write_block(fs, blk, block_buf)) {
            kfree(block_buf);
            return NULL;
        }
        kfree(block_buf);

        /* Increment parent link count */
        ext2_inode_t parent_ino;
        if (ext2_read_inode(fs, parent->inode, &parent_ino)) {
            parent_ino.i_links_count++;
            ext2_write_inode(fs, parent->inode, &parent_ino);
        }

        /* Update group used_dirs_count */
        uint32_t group = (new_inode - 1) / fs->sb.s_inodes_per_group;
        fs->groups[group].bg_used_dirs_count++;
        uint32_t gdt_block = (fs->block_size == 1024) ? 2 : 1;
        ext2_write_bytes(fs, (uint64_t)gdt_block * fs->block_size + group * sizeof(ext2_group_desc_t),
                         sizeof(ext2_group_desc_t), &fs->groups[group]);
    }

    if (!ext2_write_inode(fs, new_inode, &ino)) return NULL;

    /* Add directory entry to parent */
    if (!ext2_add_dir_entry(fs, parent->inode, new_inode, name, file_type)) return NULL;

    /* Create VFS node */
    vfs_node_t* node = (vfs_node_t*)kmalloc(sizeof(vfs_node_t));
    if (!node) return NULL;

    memset(node, 0, sizeof(*node));
    strncpy(node->name, name, VFS_MAX_NAME - 1);
    node->name[VFS_MAX_NAME - 1] = '\0';
    node->inode = new_inode;
    node->length = ino.i_size;
    node->flags = ext2_inode_type_to_vfs_flags(mode);
    node->mode = (uint16_t)(mode & 0777);
    node->uid = ino.i_uid;
    node->gid = ino.i_gid;
    node->impl = (uint32_t)fs;
    node->parent = parent;

    if (node->flags & VFS_DIRECTORY) {
        node->readdir = ext2_vfs_readdir;
        node->finddir = ext2_vfs_finddir;
    } else {
        node->read = ext2_vfs_read;
        node->write = ext2_vfs_write;
    }

    return node;
}

vfs_node_t* ext2_create_file(vfs_node_t* parent, const char* name) {
    return ext2_create_node(parent, name, EXT2_S_IFREG | 0644, EXT2_FT_REG_FILE);
}

vfs_node_t* ext2_create_dir(vfs_node_t* parent, const char* name) {
    return ext2_create_node(parent, name, EXT2_S_IFDIR | 0755, EXT2_FT_DIR);
}

static int32_t ext2_vfs_write(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
    ext2_fs_t* fs = (ext2_fs_t*)node->impl;
    if (!fs) { serial_write_string("EXT2W: no fs\n"); return -1; }

    ext2_inode_t ino;
    if (!ext2_read_inode(fs, node->inode, &ino)) { serial_write_string("EXT2W: read_inode fail\n"); return -1; }

    uint32_t block_size = fs->block_size;
    uint32_t bytes_written = 0;

    uint8_t* block_buf = (uint8_t*)kmalloc(block_size);
    if (!block_buf) { serial_write_string("EXT2W: kmalloc fail\n"); return -1; }

    while (bytes_written < size) {
        uint32_t cur_off = offset + bytes_written;
        uint32_t block_index = cur_off / block_size;
        uint32_t block_off = cur_off % block_size;
        uint32_t to_write = block_size - block_off;
        if (to_write > (size - bytes_written)) to_write = size - bytes_written;

        /* Use abstraction to get block number (handles indirect blocks) */
        uint32_t blk = ext2_get_block_number(fs, &ino, block_index, false);

        if (blk == 0) {
            /* Allocate new block */
            blk = ext2_alloc_block(fs);
            if (blk == 0) {
                serial_write_string("EXT2W: alloc_block fail at bi=");
                serial_write_hex(block_index);
                serial_write_string("\n");
                kfree(block_buf);
                return bytes_written > 0 ? (int32_t)bytes_written : -1;
            }

            /* Set the block number using abstraction (handles indirect blocks) */
            if (!ext2_set_block_number(fs, &ino, block_index, blk)) {
                serial_write_string("EXT2W: set_block_number fail at bi=");
                serial_write_hex(block_index);
                serial_write_string("\n");
                kfree(block_buf);
                return bytes_written > 0 ? (int32_t)bytes_written : -1;
            }

            ino.i_blocks += block_size / 512;
            memset(block_buf, 0, block_size);
        } else if (block_off != 0 || to_write < block_size) {
            /* Partial write - read existing block first */
            if (!ext2_read_block(fs, blk, block_buf)) {
                serial_write_string("EXT2W: read_block fail\n");
                kfree(block_buf);
                return -1;
            }
        }

        memcpy(block_buf + block_off, buffer + bytes_written, to_write);

        if (!ext2_write_block(fs, blk, block_buf)) {
            serial_write_string("EXT2W: write_block fail at bi=");
            serial_write_hex(block_index);
            serial_write_string("\n");
            kfree(block_buf);
            return -1;
        }

        bytes_written += to_write;
    }

    /* Update file size if needed */
    if (offset + bytes_written > ino.i_size) {
        ino.i_size = offset + bytes_written;
    }

    /* Update modification and change times */
    uint32_t now = ext2_get_current_time();
    ino.i_mtime = now;
    ino.i_ctime = now;

    /* Write inode back */
    if (!ext2_write_inode(fs, node->inode, &ino)) {
        kfree(block_buf);
        return -1;
    }

    /* Update VFS node length */
    node->length = ino.i_size;

    kfree(block_buf);
    return (int32_t)bytes_written;
}

static int32_t ext2_vfs_read(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
    ext2_fs_t* fs = (ext2_fs_t*)node->impl;
    if (!fs) return -1;

    ext2_inode_t ino;
    if (!ext2_read_inode(fs, node->inode, &ino)) return -1;

    if (offset >= ino.i_size) return 0;
    if (offset + size > ino.i_size) {
        size = ino.i_size - offset;
    }

    uint32_t bytes_read = 0;
    uint32_t block_size = fs->block_size;

    uint8_t* block_buf = (uint8_t*)kmalloc(block_size);
    if (!block_buf) return -1;

    while (bytes_read < size) {
        uint32_t cur_off = offset + bytes_read;
        uint32_t block_index = cur_off / block_size;
        uint32_t block_off = cur_off % block_size;
        uint32_t to_copy = block_size - block_off;
        if (to_copy > (size - bytes_read)) to_copy = size - bytes_read;

        /* Use abstraction to get block number (handles indirect blocks) */
        uint32_t blk = ext2_get_block_number(fs, &ino, block_index, false);

        if (blk == 0) {
            /* Sparse file - return zeros */
            memset(buffer + bytes_read, 0, to_copy);
        } else {
            if (!ext2_read_block(fs, blk, block_buf)) {
                kfree(block_buf);
                return -1;
            }
            memcpy(buffer + bytes_read, block_buf + block_off, to_copy);
        }

        bytes_read += to_copy;
    }

    kfree(block_buf);
    return (int32_t)bytes_read;
}

static dirent_t ext2_dirent;

dirent_t* ext2_vfs_readdir(vfs_node_t* node, uint32_t index) {
    ext2_fs_t* fs = (ext2_fs_t*)node->impl;
    if (!fs) return NULL;

    ext2_inode_t ino;
    if (!ext2_read_inode(fs, node->inode, &ino)) return NULL;

    if (!(ino.i_mode & EXT2_S_IFDIR)) return NULL;

    uint32_t block_size = fs->block_size;
    uint8_t* block_buf = (uint8_t*)kmalloc(block_size);
    if (!block_buf) return NULL;

    uint32_t seen = 0;

    for (uint32_t bi = 0; bi < 12; bi++) {
        uint32_t blk = ino.i_block[bi];
        if (blk == 0) continue;

        if (!ext2_read_block(fs, blk, block_buf)) {
            kfree(block_buf);
            return NULL;
        }

        uint32_t off = 0;
        while (off + sizeof(ext2_dirent_t) <= block_size) {
            ext2_dirent_t* de = (ext2_dirent_t*)(block_buf + off);
            if (de->rec_len == 0) break;

            if (de->inode != 0 && de->name_len != 0) {
                if (seen == index) {
                    uint32_t n = de->name_len;
                    if (n >= VFS_MAX_NAME) n = VFS_MAX_NAME - 1;
                    memcpy(ext2_dirent.name, de->name, n);
                    ext2_dirent.name[n] = '\0';
                    ext2_dirent.inode = de->inode;
                    kfree(block_buf);
                    return &ext2_dirent;
                }
                seen++;
            }

            off += de->rec_len;
            if (off >= block_size) break;
        }
    }

    kfree(block_buf);
    return NULL;
}

static vfs_node_t* ext2_vfs_finddir(vfs_node_t* node, const char* name) {
    if (!name) return NULL;

    ext2_fs_t* fs = (ext2_fs_t*)node->impl;
    if (!fs) return NULL;

    ext2_inode_t ino;
    if (!ext2_read_inode(fs, node->inode, &ino)) return NULL;
    if (!(ino.i_mode & EXT2_S_IFDIR)) return NULL;

    uint32_t block_size = fs->block_size;
    uint8_t* block_buf = (uint8_t*)kmalloc(block_size);
    if (!block_buf) return NULL;

    for (uint32_t bi = 0; bi < 12; bi++) {
        uint32_t blk = ino.i_block[bi];
        if (blk == 0) continue;

        if (!ext2_read_block(fs, blk, block_buf)) {
            kfree(block_buf);
            return NULL;
        }

        uint32_t off = 0;
        while (off + sizeof(ext2_dirent_t) <= block_size) {
            ext2_dirent_t* de = (ext2_dirent_t*)(block_buf + off);
            if (de->rec_len == 0) break;

            if (de->inode != 0 && de->name_len != 0) {
                char tmp[VFS_MAX_NAME];
                uint32_t n = de->name_len;
                if (n >= VFS_MAX_NAME) n = VFS_MAX_NAME - 1;
                memcpy(tmp, de->name, n);
                tmp[n] = '\0';

                if (strcmp(tmp, name) == 0) {
                    ext2_inode_t child_ino;
                    if (!ext2_read_inode(fs, de->inode, &child_ino)) {
                        kfree(block_buf);
                        return NULL;
                    }

                    vfs_node_t* child = (vfs_node_t*)kmalloc(sizeof(vfs_node_t));
                    if (!child) {
                        kfree(block_buf);
                        return NULL;
                    }

                    memset(child, 0, sizeof(*child));
                    strncpy(child->name, tmp, VFS_MAX_NAME - 1);
                    child->name[VFS_MAX_NAME - 1] = '\0';
                    child->inode = de->inode;
                    child->length = child_ino.i_size;
                    child->flags = ext2_inode_type_to_vfs_flags(child_ino.i_mode);
                    child->mode = (uint16_t)(child_ino.i_mode & 0777);
                    child->uid = child_ino.i_uid;
                    child->gid = child_ino.i_gid;
                    child->impl = (uint32_t)fs;

                    if (child->flags & VFS_DIRECTORY) {
                        child->readdir = ext2_vfs_readdir;
                        child->finddir = ext2_vfs_finddir;
                    } else {
                        child->read = ext2_vfs_read;
                        child->write = ext2_vfs_write;
                    }

                    child->parent = node;

                    kfree(block_buf);
                    return child;
                }
            }

            off += de->rec_len;
            if (off >= block_size) break;
        }
    }

    kfree(block_buf);
    return NULL;
}

vfs_node_t* ext2_mount(blockdev_t* bdev) {
    if (!bdev) return NULL;

    ext2_fs_t* fs = (ext2_fs_t*)kmalloc(sizeof(ext2_fs_t));
    if (!fs) return NULL;

    memset(fs, 0, sizeof(*fs));
    fs->bdev = bdev;

    /* ext2 superblock starts at byte offset 1024 */
    if (!ext2_read_bytes(fs, EXT2_SUPERBLOCK_OFFSET, sizeof(ext2_superblock_t), &fs->sb)) {
        kfree(fs);
        return NULL;
    }

    if (fs->sb.s_magic != EXT2_SUPER_MAGIC) {
        printk("ext2: bad magic 0x%04x\n", fs->sb.s_magic);
        kfree(fs);
        return NULL;
    }

    fs->block_size = 1024U << fs->sb.s_log_block_size;
    if (fs->block_size < 1024 || fs->block_size > 4096) {
        printk("ext2: unsupported block size %u\n", fs->block_size);
        kfree(fs);
        return NULL;
    }

    fs->groups_count = (fs->sb.s_blocks_count + fs->sb.s_blocks_per_group - 1) / fs->sb.s_blocks_per_group;

    uint32_t gdt_block = (fs->block_size == 1024) ? 2 : 1;
    uint32_t gdt_size = fs->groups_count * sizeof(ext2_group_desc_t);
    fs->groups = (ext2_group_desc_t*)kmalloc(gdt_size);
    if (!fs->groups) {
        kfree(fs);
        return NULL;
    }

    if (!ext2_read_bytes(fs, (uint64_t)gdt_block * fs->block_size, gdt_size, fs->groups)) {
        kfree(fs->groups);
        kfree(fs);
        return NULL;
    }

    vfs_node_t* root = (vfs_node_t*)kmalloc(sizeof(vfs_node_t));
    if (!root) {
        kfree(fs->groups);
        kfree(fs);
        return NULL;
    }

    ext2_inode_t root_ino;
    if (!ext2_read_inode(fs, 2, &root_ino)) {
        kfree(root);
        kfree(fs->groups);
        kfree(fs);
        return NULL;
    }

    memset(root, 0, sizeof(*root));
    strncpy(root->name, "/", VFS_MAX_NAME - 1);
    root->inode = 2;
    root->length = root_ino.i_size;
    root->flags = VFS_DIRECTORY;
    root->mode = (uint16_t)(root_ino.i_mode & 0777);
    root->uid = root_ino.i_uid;
    root->gid = root_ino.i_gid;
    root->impl = (uint32_t)fs;
    root->readdir = ext2_vfs_readdir;
    root->finddir = ext2_vfs_finddir;

    printk("ext2: mounted blockdev %s (block=%u, groups=%u)\n", bdev->name, fs->block_size, fs->groups_count);
    return root;
}

/*
 * Get filesystem statistics from a mounted ext2 node
 */
bool ext2_get_fs_stats(vfs_node_t* node, ext2_fs_stats_t* stats) {
    if (!node || !stats) return false;

    ext2_fs_t* fs = (ext2_fs_t*)node->impl;
    if (!fs) return false;

    memset(stats, 0, sizeof(*stats));

    stats->total_blocks = fs->sb.s_blocks_count;
    stats->free_blocks = fs->sb.s_free_blocks_count;
    stats->total_inodes = fs->sb.s_inodes_count;
    stats->free_inodes = fs->sb.s_free_inodes_count;
    stats->block_size = fs->block_size;
    stats->blocks_per_group = fs->sb.s_blocks_per_group;
    stats->groups_count = fs->groups_count;
    stats->state = fs->sb.s_state;
    stats->rev_level = fs->sb.s_rev_level;

    /* Calculate sizes in KB (block_size / 1024 * count, or count * block_size / 1024) */
    uint32_t kb_per_block = fs->block_size / 1024;
    if (kb_per_block == 0) kb_per_block = 1;
    stats->total_size_kb = stats->total_blocks * kb_per_block;
    stats->free_size_kb = stats->free_blocks * kb_per_block;
    stats->used_size_kb = stats->total_size_kb - stats->free_size_kb;

    memcpy(stats->volume_name, fs->sb.s_volume_name, 16);
    stats->volume_name[15] = '\0';
    memcpy(stats->last_mounted, fs->sb.s_last_mounted, 64);
    stats->last_mounted[63] = '\0';

    return true;
}
