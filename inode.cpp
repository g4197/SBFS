#include "inode.h"

#include "fs.h"

#define CHECK_RET(ret) \
    if (ret == kFail)  \
    return kFail

namespace sbfs {

int Inode::read_inode(DiskInode *buf) const {
    Block blk;
    CHECK_RET(fs->device()->read(pos.block_id, &blk));
    memcpy(buf, blk.data + pos.block_offset, sizeof(DiskInode));
    return kSuccess;
}

int Inode::write_inode(const DiskInode *buf) const {
    Block blk;
    CHECK_RET(fs->device()->read(pos.block_id, &blk));
    memcpy(blk.data + pos.block_offset, buf, sizeof(DiskInode));
    CHECK_RET(fs->device()->write(pos.block_id, &blk));
    return kSuccess;
}

int Inode::read_data(uint32_t offset, uint8_t *buf, uint32_t size) const {
    DiskInode disk_inode;
    CHECK_RET(read_inode(&disk_inode));
    CHECK_RET(disk_inode.read_data(offset, buf, size, fs->device()));
    // TODO: update access time
    return write_inode(&disk_inode);
}

int Inode::write_data(uint32_t offset, const uint8_t *buf, uint32_t size) const {
    DiskInode disk_inode;
    CHECK_RET(read_inode(&disk_inode));
    CHECK_RET(disk_inode.write_data(offset, buf, size, fs->device()));
    // TODO: update modify time
    return write_inode(&disk_inode);
}

int Inode::create(const char *name, DiskInode *disk_inode, Inode *inode) const {
    DiskInode cur_disk_inode;
    CHECK_RET(read_inode(&cur_disk_inode));
    if (cur_disk_inode.type != kDirectory) {
        return kFail;
    }
    // allocate inode id
    auto new_inode_id = fs->alloc_inode();
    inode->pos = fs->getDiskInodePos(new_inode_id);
    // allocate block
    CHECK_RET(cur_disk_inode.resize(cur_disk_inode.size + sizeof(DirBlock), fs->data_bitmap_, fs->device()));
    DirBlock dir_blk;
    dir_blk.entries[0] = DirEntry(name, new_inode_id);
    CHECK_RET(cur_disk_inode.write_data(
        cur_disk_inode.size - sizeof(DirBlock), (uint8_t *)&dir_blk, sizeof(DirBlock), fs->device()));
    if (disk_inode->type == kDirectory) {
        DirBlock new_dir_blk;
        new_dir_blk.entries[0] = DirEntry(".", new_inode_id);
        new_dir_blk.entries[1] = DirEntry("..", 0);
        CHECK_RET(disk_inode->resize(disk_inode->size + sizeof(DirBlock), fs->data_bitmap_, fs->device()));
        CHECK_RET(inode->write_data(disk_inode->size - sizeof(DirBlock), (uint8_t *)&new_dir_blk, sizeof(DirBlock)));
    }
}

int Inode::find(const char *name, Inode *inode) const {
    DiskInode disk_inode;
    CHECK_RET(read_inode(&disk_inode));
    if (disk_inode.type != kDirectory) {
        return kFail;
    }
    auto tot_blocks = disk_inode.total_blocks(disk_inode.size);
    DirBlock dir_blk;
    for (auto i = 0; i < tot_blocks; ++i) {
        CHECK_RET(read_data(i * sizeof(DirBlock), (uint8_t *)&dir_blk, sizeof(DirBlock)));
        for (auto &entry : dir_blk.entries) {
            if (strcmp(entry.name, name) == 0) {
                inode->pos = fs->getDiskInodePos(entry.inode);
                return kSuccess;
            }
        }
    }
    return kFail;
}

int Inode::resize(uint32_t new_size) const {
    DiskInode disk_inode;
    CHECK_RET(read_inode(&disk_inode));
    if (disk_inode.type != kFile) {
        return kFail;
    }
    return disk_inode.resize(new_size, fs->data_bitmap_, fs->device());
}

int Inode::sync(bool metadata) const {
    DiskInode disk_inode;
    CHECK_RET(read_inode(&disk_inode));
    CHECK_RET(disk_inode.sync_data(fs->device()));
    if (metadata) {
        CHECK_RET(fs->device()->sync(pos.block_id));
    }
    return kSuccess;
}

}  // namespace sbfs