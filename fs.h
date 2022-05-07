#ifndef FS_H_
#define FS_H_

#include <fuse3/fuse.h>

#include "config.h"
#include "fs_layout.h"
#include "inode.h"

namespace sbfs {

class SBFileSystem {
public:
    /* Create a new SBFS. */
    static SBFileSystem create(const char *path,
                               const uint64_t size,
                               uint32_t total_blocks,
                               uint32_t inode_bitmap_blocks);

    /* Open an existing SBFS. */
    static SBFileSystem open(const char *path);

    /* get root Inode. */
    Inode root();

    /* get block device. */
    BlockDevice *device();

    /* get actual inode position by inode id. */
    Position getDiskInodePos(uint32_t inode_id);

    /* Allocate an inode, returns inode id. */
    uint32_t alloc_inode();

    /* Allocate a data block, returns block id (not block_id - data_area_start). */
    uint32_t alloc_data();

    /* Deallocate an inode. */
    int free_inode(uint32_t inode_id);

    /* Deallocate a data block. */
    int free_data(uint32_t block_id);

    Bitmap *data_bitmap_;  /* Bitmap for data, attention: data block size is kBlockSize. */

private:
    BlockDevice *device_;
    Bitmap *inode_bitmap_; /* Bitmap for inodes, attention: inode size may be < kBlockSize. */
    uint32_t inode_area_start_block_;
    uint32_t data_area_start_block_;
};
};  // namespace sbfs

#endif