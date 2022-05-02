#ifndef FS_LAYOUT_H_
#define FS_LAYOUT_H_

#include "blk_dev.h"

namespace sbfs {
/*
 * General layout:
 * Super block -> Inode Bitmap -> Inodes -> Data Bitmap -> Data 
 */

/* Only one, located at Block 0 of disk. */
struct SuperBlock {
    uint32_t magic;
    uint32_t total_blocks;
    uint32_t inode_bitmap_blocks;
    uint32_t inode_area_blocks;
    uint32_t data_bitmap_blocks;
    uint32_t data_area_blocks;
    inline bool isValid() {
        return magic == kFSMagic;
    }
    uint8_t padding[kBlockSize - 24];
};
static_assert(sizeof(SuperBlock) == kBlockSize, "SuperBlock size error");

/* 
* One bitmap block can manage (kBlockSize * 8) blocks.
* this data structure is in memory, but the allocate record is in disk.
* only two bitmaps needed. (DiskInode, data) 
*/
struct Bitmap {
    /* Where the bitmap starts */
    blk_id_t start_block_id;
    /* How many blocks of this bitmap */
    uint32_t num_blocks;
    Bitmap(blk_id_t start_block_id, blk_id_t num_blocks) : 
        start_block_id(start_block_id), num_blocks(num_blocks) {}
    
    /* Allocate a block from this bitmap, and set the place to 1. */
    blk_id_t alloc(BlockDevice *dev);
    /* free a block. */
    int free(blk_id_t block_id, BlockDevice *dev);
};

enum DiskInodeType : uint32_t {
    kFile,
    kDirectory
};

/* Index should be stored in data region. */
struct IndirectIndex1 {
    blk_id_t direct[kBlockSize / sizeof(blk_id_t)];
};

struct IndirectIndex2 {
    blk_id_t indirect1[kBlockSize / sizeof(blk_id_t)];
};

/* Same to DiskInode in rCore */
struct DiskInode {
    /* Bytes for dir/file, use total_blocks to get block num. */
    uint32_t size;
    /* some metadata */
    uint32_t access_time;
    uint32_t create_time;
    uint32_t modify_time;

    blk_id_t direct[kInodeDirectCnt];
    blk_id_t indirect1;
    blk_id_t indirect2;
    DiskInodeType type;

    /* Metadata (create time etc.) should be updated. */
    DiskInode(DiskInodeType type_) {
        memset(this, 0, sizeof(DiskInode));
        type = type_;
    }
    /* 
    * get actual block id by "inner_id" relative to this inode 
    * maybe a 3-layer query is needed.
    */
    blk_id_t block_id(uint32_t inner_id, BlockDevice *dev);
    /* calculate how many blocks needed by a file/dir with "size" */
    static uint32_t total_blocks(uint32_t size);
    /* 
     * This interface is different from rCore's, for it should handle both increase and decrease, 
     * and recycle is done in this function.
     * if decrease size, recycle data blocks but reserve inode blocks.
     */
    int resize(uint32_t new_size, Bitmap *data_bitmap, BlockDevice *dev);
    /*
     * Recycle both data blocks and inode blocks.
     */
    int clear(Bitmap *data_bitmap, BlockDevice *dev);
    /*
     * Read "size" bytes from offset to "buf".
     * Metadata (access time etc.) should be updated.
     * attention: offset is relative to data managed by this inode.
     */
    int read_data(uint32_t offset, uint8_t *buf, uint32_t size, BlockDevice *dev);
    /*
     * Write "size" bytes from "buf" to offset.
     * Metadata (access time etc.) should be updated.
     * attention: offset is relative to data managed by this inode.
     */
    int write_data(uint32_t offset, const uint8_t *buf, uint32_t size, BlockDevice *dev);
};

static_assert(sizeof(DiskInode) <= kBlockSize, "DiskInode size error");

struct DirEntry {
    char name[kMaxDirNameLength + 1];
    uint32_t inode;
    DirEntry(const char *name, uint32_t inode) {
        memset(this, 0, sizeof(DirEntry));
        strncpy(this->name, name, kMaxDirNameLength);
        this->inode = inode;
    }
};

};

#endif // FS_LAYOUT_H_