#ifndef FS_LAYOUT_H_
#define FS_LAYOUT_H_

#include "blk_dev.h"

namespace sbfs {

struct Position {
    blk_id_t block_id;
    uint32_t block_offset;

    static inline Position invalid() {
        return Position{ UINT32_MAX, UINT32_MAX };
    }

    inline bool isValid() {
        return block_id != UINT32_MAX;
    }
};

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
    Position root_inode_pos;
    inline bool isValid() {
        return magic == kFSMagic;
    }

    inline void print() {
        DLOG(INFO) << "total_blocks: " << total_blocks;
        DLOG(INFO) << "inode_bitmap_blocks: " << inode_bitmap_blocks;
        DLOG(INFO) << "inode_area_blocks: " << inode_area_blocks;
        DLOG(INFO) << "data_bitmap_blocks: " << data_bitmap_blocks;
        DLOG(INFO) << "data_area_blocks: " << data_area_blocks;
        DLOG(INFO) << "root inode pos: " << root_inode_pos.block_id << " " << root_inode_pos.block_offset;
    }
    uint8_t padding[kBlockSize - 32];
};
static_assert(sizeof(SuperBlock) == kBlockSize, "SuperBlock size error");

/*
 * One bitmap block can manage (kBlockSize * 8) blocks.
 * this data structure is in memory, but the allocate record is in disk.
 * only two bitmaps needed. (DiskInode, data)
 * TODO: store the bitmap in memory at boost time, reduce disk access overhead.
 */
struct Bitmap {
    /* Where the bitmap starts */
    blk_id_t start_block_id;
    /* How many blocks of this bitmap */
    uint32_t num_blocks;

    /* to return an absolute block_id, we need to know the initial offset of its data_segment */
    blk_id_t data_segment_offset;
    Bitmap(blk_id_t start_block_id, blk_id_t num_blocks, blk_id_t data_segment_offset);

    /**
     * @brief alloc a block bitmap, and set that place to 1
     *
     * @return blk_id_t the ABSOLUTE block id
     *
     * @return kFail if failed
     */
    blk_id_t alloc(BlockDevice *dev);
    /**
     * @brief free a block
     *
     * @param block_id the ABSOLUTE block id
     * @return kFail if failed, kSuccess if success
     */
    int free(blk_id_t block_id, BlockDevice *dev);
};

enum DiskInodeType : uint32_t { kFile, kDirectory };

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

    uint16_t uid;       // Owner user id (may be used in ext tasks)
    uint16_t gid;       // Owner group id (may be used in ext tasks)
    uint16_t link_cnt;  // Hard link count (may be used in ext tasks)

    /* used in chmod, also contains file type (no need to implement now). */
    uint16_t mode;

    blk_id_t direct[kInodeDirectCnt];
    blk_id_t indirect1;
    blk_id_t indirect2;
    DiskInodeType type;

    DiskInode() = default;
    /* Metadata (create time etc.) should be updated. */
    explicit DiskInode(DiskInodeType type_);
    /**
     * @brief get actual block_id of an inner_id
     *
     * @param inner_id the block offset within a file
     * @return blk_id_t the ABSOLUTE block id
     * @return kFail if failed, i.e. the inner_id is out of range
     */
    blk_id_t block_id(uint32_t inner_id, BlockDevice *dev);

    /* calculate how many blocks needed by a file/dir with "size" */
    uint32_t total_blocks(uint32_t size);

    /**
     * @brief resize the size of a file to new_size,
     * could be increase or decrease
     * decrease to 0 will redirect to clear, which is much faster
     * @attention metadata will be updated
     */
    int resize(uint32_t new_size, Bitmap *data_bitmap, BlockDevice *dev);

    /**
     * @brief read 'len' byte from data start from 'offset' to 'buf', metadata will be updated
     * @attention: offset is relatively to the file that this inode governs
     * @param offset offset must be smaller than the file size
     * @param buf we don't check the size of buf, so it's your responsibility
     * @param len 'offset + len' is larger than the file size, it will be truncated
     * @return number of bytes read on success, kFail on failure
     */
    int read_data(uint32_t offset, uint8_t *buf, uint32_t len, BlockDevice *dev);
    /**
     * @brief write 'len' byte from 'buf' to data start from 'offset', metadata will be updated
     * @attention: offset is relatively to the file that this inode governs
     * @param offset offset must be smaller than the file size
     * @param buf we don't check the size of buf, so it's your responsibility
     * @param len if 'offset + len' is larger than the file size, it will be truncated
     * @return number of bytes write on success, kFail on failure
     */
    int write_data(uint32_t offset, const uint8_t *buf, uint32_t len, BlockDevice *dev);

    /**
     * @brief sync all data blocks to disk, disk inode itself are not synced
     * @param direct if true, sync indirect1 and indirect2 to disk if exists
     * @return int kSuccess on success, kFail on failure
     */
    int sync_data(BlockDevice *dev, bool indirect = false);

private:
    int clear(Bitmap *data_bitmap, BlockDevice *dev);
    int increase(int, int, int, int, BlockDevice *, Bitmap *);
    int decrease(int, int, int, int, BlockDevice *, Bitmap *);
    void update_meta();
};

static_assert(sizeof(DiskInode) <= kBlockSize, "DiskInode size error");

struct DirEntry {
    char name[kMaxDirNameLength + 1];
    uint32_t inode;
    DirEntry() {
        name[0] = '\0';
        inode = 0;
    }
    DirEntry(const char *name, uint32_t inode) {
        memset(this, 0, sizeof(DirEntry));
        strncpy(this->name, name, kMaxDirNameLength);
        this->inode = inode;
    }
    inline bool isValid() {
        // Is inode == 0 reasonable? in most cases, inode 0 is used for root.
        return name[0] != '\0';
    }
};

};  // namespace sbfs

#endif  // FS_LAYOUT_H_