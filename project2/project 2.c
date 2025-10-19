#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stdint.h>

#pragma pack(push, 1)
#define BLOCK_SIZE 4096
#define NUM_BLOCKS 64
#define INODE_SIZE 256
#define NUM_INODES 80
#define MAGIC_NUMBER 0xD34D
#define DATA_BLOCKS_START 8

typedef struct {
    uint16_t magic;
    uint32_t block_size;
    uint32_t num_blocks;
    uint32_t inode_bitmap_block;
    uint32_t data_bitmap_block;
    uint32_t inode_table_block;
    uint32_t first_data_block;
    uint32_t inode_size;
    uint32_t inode_count;
    uint8_t reserved[4058];
} Superblock;

typedef struct {
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
    uint32_t size;
    uint32_t access_time;
    uint32_t creation_time;
    uint32_t modification_time;
    uint32_t deletion_time;
    uint32_t num_links;
    uint32_t num_data_blocks;
    uint32_t direct[4];
    uint32_t single_indirect;
    uint32_t double_indirect;
    uint32_t triple_indirect;
    uint8_t reserved[156];
} Inode;

Superblock superblock;
Inode inode_table[NUM_INODES];
uint8_t inode_bitmap[NUM_INODES/8];
uint8_t data_bitmap[(NUM_BLOCKS - DATA_BLOCKS_START)/8];
uint8_t fs_image[NUM_BLOCKS * BLOCK_SIZE];


void report_error(const char *format, ...);
void read_fs_image();
void write_fs_image();
void check_superblock(int fix);
void check_inode_bitmap(int fix);
void check_data_bitmap(int fix);
void check_duplicate_blocks(int fix);
void check_bad_blocks(int fix);

void report_error(const char *format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    exit(1);
}

void read_fs_image() {
    FILE *file = fopen("vsfs.img", "rb");
    if (!file) report_error("Error opening filesystem image\n");
    fread(fs_image, 1, NUM_BLOCKS * BLOCK_SIZE, file);
    fclose(file);
    
    memcpy(&superblock, fs_image, sizeof(Superblock));
    memcpy(inode_bitmap, fs_image + BLOCK_SIZE, sizeof(inode_bitmap));
    memcpy(data_bitmap, fs_image + 2*BLOCK_SIZE, sizeof(data_bitmap));
    memcpy(inode_table, fs_image + 3*BLOCK_SIZE, sizeof(Inode)*NUM_INODES);
}

void write_fs_image() {
    memcpy(fs_image, &superblock, sizeof(Superblock));
    memcpy(fs_image + BLOCK_SIZE, inode_bitmap, sizeof(inode_bitmap));
    memcpy(fs_image + 2*BLOCK_SIZE, data_bitmap, sizeof(data_bitmap));
    memcpy(fs_image + 3*BLOCK_SIZE, inode_table, sizeof(Inode)*NUM_INODES);
    
    FILE *file = fopen("vsfs.img", "wb");
    if (!file) report_error("Error writing filesystem image\n");
    fwrite(fs_image, 1, NUM_BLOCKS * BLOCK_SIZE, file);
    fclose(file);
}

void check_superblock(int fix) {
    printf("Checking Superblock...\n");
    
    if (superblock.magic != MAGIC_NUMBER) {
        printf("Error: Invalid magic number (0x%04X)\n", superblock.magic);
        if (fix) {
            superblock.magic = MAGIC_NUMBER;
            printf("-> Fixed magic number to 0x%X\n", MAGIC_NUMBER);
        }
    } else {
        printf("Superblock magic number valid (0x%04X)\n", superblock.magic);
    }

    if (superblock.block_size != BLOCK_SIZE) {
        printf("Error: Invalid block size (%d)\n", superblock.block_size);
        if (fix) {
            superblock.block_size = BLOCK_SIZE;
            printf("-> Fixed block size to %d\n", BLOCK_SIZE);
        }
    } else {
        printf("Block size valid (%d bytes)\n", BLOCK_SIZE);
    }

    if (superblock.num_blocks != NUM_BLOCKS) {
        printf("Error: Total blocks mismatch. Expected %d, found %d\n", 
              NUM_BLOCKS, superblock.num_blocks);
        if (fix) {
            superblock.num_blocks = NUM_BLOCKS;
            printf("-> Fixed total blocks to %d\n", NUM_BLOCKS);
        }
    } else {
        printf("Total blocks valid (%d)\n", NUM_BLOCKS);
    }
}

void check_inode_bitmap(int fix) {
    printf("Checking Inode Bitmap...\n");
    int errors = 0;
    
    for (int i = 0; i < NUM_INODES; i++) {
        int byte_idx = i / 8;
        int bit_idx = i % 8;
        int is_used = (inode_bitmap[byte_idx] >> (7 - bit_idx)) & 1;
        
        if (is_used) {
            if (inode_table[i].num_links == 0 || inode_table[i].deletion_time != 0) {
                printf("Error: Inode %d marked used but invalid\n", i);
                if (fix) {
                    inode_bitmap[byte_idx] &= ~(1 << (7 - bit_idx));
                    printf("-> Fixed invalid inode %d\n", i);
                    errors++;
                }
            }
        } else {
            if (inode_table[i].num_links > 0 && inode_table[i].deletion_time == 0) {
                printf("Error: Valid inode %d not marked in bitmap\n", i);
                if (fix) {
                    inode_bitmap[byte_idx] |= (1 << (7 - bit_idx));
                    printf("-> Fixed missing inode %d\n", i);
                    errors++;
                }
            }
        }
    }
    
    if (!errors) printf("Inode bitmap consistency check passed.\n");
}

void check_data_bitmap(int fix) {
    printf("Checking Data Bitmap...\n");
    int errors = 0;
    
    for (int block = DATA_BLOCKS_START; block < NUM_BLOCKS; block++) {
        int idx = block - DATA_BLOCKS_START;
        int byte_idx = idx / 8;
        int bit_idx = idx % 8;
        int is_used = (data_bitmap[byte_idx] >> (7 - bit_idx)) & 1;
        
        int referenced = 0;
        for (int i = 0; i < NUM_INODES; i++) {
            for (int j = 0; j < 4; j++) {
                if (inode_table[i].direct[j] == block) {
                    referenced = 1;
                    break;
                }
            }
            if (referenced) break;
        }
        
        if (is_used && !referenced) {
            printf("Error: Block %d marked used but unreferenced\n", block);
            if (fix) {
                data_bitmap[byte_idx] &= ~(1 << (7 - bit_idx));
                printf("-> Fixed unreferenced block %d\n", block);
                errors++;
            }
        } else if (!is_used && referenced) {
            printf("Error: Block %d referenced but not marked\n", block);
            if (fix) {
                data_bitmap[byte_idx] |= (1 << (7 - bit_idx));
                printf("-> Fixed missing block %d\n", block);
                errors++;
            }
        }
    }
    
    if (!errors) printf("Data bitmap consistency check passed.\n");
}

void check_duplicate_blocks(int fix) {
    printf("Checking for duplicate blocks...\n");
    int errors = 0;
    uint8_t block_ref[NUM_BLOCKS] = {0};
    
    for (int i = 0; i < NUM_INODES; i++) {
        for (int j = 0; j < 4; j++) {
            uint32_t block = inode_table[i].direct[j];
            if (block >= DATA_BLOCKS_START && block < NUM_BLOCKS) {
                if (block_ref[block]++) {
                    printf("Error: Block %d referenced by multiple inodes\n", block);
                    if (fix) {
                        inode_table[i].direct[j] = 0;
                        printf("-> Fixed duplicate block %d in inode %d\n", block, i);
                        errors++;
                    }
                }
            }
        }
    }
    
    if (!errors) printf("Duplicate block check passed.\n");
}

void check_bad_blocks(int fix) {
    printf("Checking for bad blocks...\n");
    int errors = 0;
    
    for (int i = 0; i < NUM_INODES; i++) {
        for (int j = 0; j < 4; j++) {
            uint32_t block = inode_table[i].direct[j];
            if (block != 0 && (block < DATA_BLOCKS_START || block >= NUM_BLOCKS)) {
                printf("Error: Bad block %d in inode %d\n", block, i);
                if (fix) {
                    inode_table[i].direct[j] = 0;
                    printf("-> Fixed bad block %d in inode %d\n", block, i);
                    errors++;
                }
            }
        }
    }
    
    if (!errors) printf("Bad block check passed.\n");
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s <image_file>\n", argv[0]);
        return 1;
    }

    read_fs_image();
    
    check_superblock(1);
    check_inode_bitmap(1);
    check_data_bitmap(1);
    check_duplicate_blocks(1);
    check_bad_blocks(1);
    
    write_fs_image();
    
    printf("\nFile system consistency check completed successfully.\n");
    return 0;
}
