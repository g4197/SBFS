#ifndef INODE_H_
#define INODE_H_

#include "config.h"
#include "blk_dev.h"

namespace sbfs {
/* Directory block stored in data area */
struct DirBlock {
    DirEntry entries[kBlockSize / sizeof(DirEntry)];
};

class SBFileSystem;

struct Position {
    uint32_t block_id;
    uint32_t block_offset;
};

struct Inode {
    /* Place of DiskInode */
    Position pos;
    SBFileSystem *fs;
    /*
     * Read "size" bytes from offset to "buf".
     * Metadata (access time etc.) should be updated.
     * attention: offset is relative to data managed by this inode.
     */
    int read_data(uint32_t offset, uint8_t *buf, uint32_t size);
    /*
     * Write "size" bytes from "buf" to offset.
     * Metadata (access time etc.) should be updated.
     * attention: offset is relative to data managed by this inode.
     */
    int write_data(uint32_t offset, const uint8_t *buf, uint32_t size);
    /*
     * Read diskinode of this inode to buf.
     */
    int read_inode(DiskInode *buf);
    /*
     * Write diskinode of this inode from buf.
     */
    int write_inode(const DiskInode *buf);
    /*
     * Create a file / directory with "name" in current dir.
     * its DiskInode info is in disk_inode.
     * Only support directory type.
     * input: name, disk_inode
     * output: inode, 0 or 1
     * 1. allocate inode for new file / directory.
     * 2. allocate block for new directory.
     * 3. write directory data. (create . and ..)
     * 4. write inode of new file / directory.
     * 5. update parent directory's data block, add new entry.
     * 6. update parent directory's inode (size, access time etc.)
     */
    int create(const char *name, const DiskInode *disk_inode, Inode *inode);
    /* 
     * Find inode with "name" in current dir.
     * Only support directory type.
     * Inode saves the inode found.
     * return kFail if not found.
     */
    int find(const char *name, Inode *inode);
    /*
     * Resize current inode to "new_size".
     * Only support file type.
     */
    int resize(uint32_t new_size);
    /*
     * Remove a file / directory with "name" in current dir.
     * Only support directory type.
     */
    int remove(const char *name);
};
}

#endif // INODE_H_