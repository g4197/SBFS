#ifndef INODE_H_
#define INODE_H_

#include "blk_dev.h"
#include "config.h"
#include "fs_layout.h"

namespace sbfs {
/* Directory block stored in data area */
constexpr uint64_t kDirEntries = kBlockSize / sizeof(DirEntry);
struct alignas(kBlockSize) DirBlock {
    DirEntry entries[kDirEntries];
};

class SBFileSystem;

struct Inode {
    /* Place of DiskInode */
    Position pos;
    SBFileSystem *fs;
    static inline Inode invalid() {
        return Inode{ Position::invalid(), nullptr };
    }
    /*
     * Read "size" bytes from offset to "buf".
     * Metadata (access time etc.) should be updated.
     * attention: offset is relative to data managed by this inode.
     */
    int read_data(uint32_t offset, uint8_t *buf, uint32_t size) const;
    /*
     * Write "size" bytes from "buf" to offset.
     * Metadata (access time etc.) should be updated.
     * attention: offset is relative to data managed by this inode.
     */
    int write_data(uint32_t offset, const uint8_t *buf, uint32_t size) const;
    /*
     * Read disk inode of this inode to buf.
     */
    int read_inode(DiskInode *buf) const;
    /*
     * Write disk inode of this inode from buf.
     */
    int write_inode(const DiskInode *buf) const;
    /*
     * Create a file / directory with "name" in current dir.
     * its DiskInode ERROR is in disk_inode.
     * Only support directory type.
     * input: name, disk_inode
     * output: inode, 0 or -1
     * 1. allocate inode for new file / directory.
     * 2. allocate block for new directory.
     * 3. write directory data. (create . and ..)
     * 4. write inode of new file / directory.
     * 5. update parent directory's data block, add new entry.
     * 6. update parent directory's inode (size, access time etc.)
     */
    int create(const char *name, DiskInode *disk_inode, Inode *inode) const;
    /*
     * Find inode with "name" in current dir.
     * Only support directory type.
     * Inode saves the inode found.
     * return kFail if not found.
     */
    int find(const char *name, Inode *inode) const;
    /*
     * Resize current inode to "new_size".
     * Only support file type.
     */
    [[nodiscard]] int resize(uint32_t new_size) const;
    /*
     * Remove a file / directory with "name" in current dir.
     * Only support directory type.
     */
    int remove(const char *name) const;
    /*
     * Link parameter "inode" with "name" to this Inode.
     * Used in rename, etc.
     * if replace is true, replace if name already exists.
     * else return kFail if exists.
     */
    int link(const char *name, const Inode *inode, bool replace = false) const;
    /*
     * Unlink "name" inode from this Inode, and return the Inode.
     */
    int unlink(const char *name, Inode *inode) const;
    /*
     * sync data (and inode metadata) to disk.
     * if metadata is True, then should sync metadata.
     * else sync data only.
     * 1. sync data by calling disk inode's sync_data().
     * 2. sync metadata by directly call sync() with block ID.
     */
    [[nodiscard]] int sync(bool metadata = true) const;

    /* Judge if the Inode item is valid. */
    [[nodiscard]] inline bool isValid() const {
        return pos.isValid();
    }

    inline bool operator<(const Inode &other) const {
        return (pos.block_id < other.pos.block_id) ||
               (pos.block_id == other.pos.block_id && pos.block_offset < other.pos.block_offset);
    }
};
}  // namespace sbfs

#endif  // INODE_H_