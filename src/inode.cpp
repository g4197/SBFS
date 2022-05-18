#include "inode.h"

#include "fs.h"

#define CHECK_RET(ret)  \
    DLOG(INFO) << #ret; \
    if (ret == kFail) return kFail

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
    DLOG(WARNING) << "Read data: " << offset << " " << size;
    int len = disk_inode.read_data(offset, buf, size, fs->device());
    CHECK_RET(len);
    CHECK_RET(write_inode(&disk_inode));
    return len;
}

int Inode::write_data(uint32_t offset, const uint8_t *buf, uint32_t size) const {
    DiskInode disk_inode;
    CHECK_RET(read_inode(&disk_inode));
    if (disk_inode.size < offset + size) {  // increase
        disk_inode.resize(offset + size, fs->data_bitmap_, fs->device());
    }
    DLOG(WARNING) << "Write data: " << offset << " " << size;
    int len = disk_inode.write_data(offset, buf, size, fs->device());
    CHECK_RET(len);
    CHECK_RET(write_inode(&disk_inode));
    return len;
}

int Inode::create(const char *name, DiskInode *disk_inode, Inode *inode) const {
    DiskInode cur_disk_inode;
    CHECK_RET(read_inode(&cur_disk_inode));
    if (cur_disk_inode.type != kDirectory) {
        return kFail;
    }
    // allocate inode id
    auto new_inode_id = fs->alloc_inode();
    *inode = { .pos = fs->getDiskInodePos(new_inode_id), .fs = fs };
    // allocate block and update parent directory
    // increase
    CHECK_RET(cur_disk_inode.resize(cur_disk_inode.size + sizeof(DirEntry), fs->data_bitmap_, fs->device()));
    DirEntry entry(name, new_inode_id);
    CHECK_RET(cur_disk_inode.write_data(cur_disk_inode.size - sizeof(DirEntry), (uint8_t *)&entry, sizeof(DirEntry),
                                        fs->device()));
    if (disk_inode->type == kDirectory) {  // create . and ..
        DirEntry new_dir_entries[2] = { DirEntry(".", new_inode_id), DirEntry("..", fs->getDiskInodeId(pos)) };
        // increase
        CHECK_RET(disk_inode->resize(disk_inode->size + sizeof(DirEntry) * 2, fs->data_bitmap_, fs->device()));
        CHECK_RET(disk_inode->write_data(disk_inode->size - sizeof(DirEntry) * 2, (uint8_t *)&new_dir_entries,
                                         sizeof(DirEntry) * 2, fs->device()));
    }
    // write new inode
    CHECK_RET(inode->write_inode(disk_inode));
    return write_inode(&cur_disk_inode);
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
        CHECK_RET(disk_inode.read_data(i * sizeof(DirBlock), (uint8_t *)&dir_blk, sizeof(DirBlock), fs->device()));
        for (auto &entry : dir_blk.entries) {
            if (strcmp(entry.name, name) == 0) {
                *inode = { .pos = fs->getDiskInodePos(entry.inode), .fs = fs };
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
    if (new_size > disk_inode.size) {  // increase
        CHECK_RET(disk_inode.resize(new_size, fs->data_bitmap_, fs->device()));
        return write_inode(&disk_inode);
    } else {  // decrease
        CHECK_RET(disk_inode.resize(new_size, fs->data_bitmap_, fs->device()));
        return write_inode(&disk_inode);
    }
}

int Inode::remove(const char *name) const {
    DiskInode disk_inode;
    CHECK_RET(read_inode(&disk_inode));
    if (disk_inode.type != kDirectory) {
        return kFail;
    }
    auto tot_blocks = disk_inode.total_blocks(disk_inode.size);
    DirBlock dir_blk;
    for (auto i = 0; i < tot_blocks; ++i) {
        CHECK_RET(disk_inode.read_data(i * sizeof(DirBlock), (uint8_t *)&dir_blk, sizeof(DirBlock), fs->device()));
        for (auto j = 0; j < kDirEntries; ++j) {
            if (strcmp(dir_blk.entries[j].name, name) == 0) {
                Inode del_inode = { .pos = fs->getDiskInodePos(dir_blk.entries[j].inode), .fs = fs };
                DiskInode del_disk_inode;
                CHECK_RET(del_inode.read_inode(&del_disk_inode));
                --del_disk_inode.link_cnt;
                if (del_disk_inode.link_cnt == 0) {
                    if (i != tot_blocks - 1 || j != kDirEntries - 1) {  // move the last entry here
                        DirEntry last_entry;
                        CHECK_RET(disk_inode.read_data(disk_inode.size - sizeof(DirEntry), (uint8_t *)&last_entry,
                                                       sizeof(DirEntry), fs->device()));
                        CHECK_RET(disk_inode.write_data(i * sizeof(DirBlock) + j * sizeof(DirEntry),
                                                        (uint8_t *)&last_entry, sizeof(DirEntry), fs->device()));
                    }
                    // decrease
                    CHECK_RET(disk_inode.resize(disk_inode.size - sizeof(DirEntry), fs->data_bitmap_, fs->device()));
                    CHECK_RET(write_inode(&disk_inode));
                    // decrease
                    del_disk_inode.resize(0, del_inode.fs->data_bitmap_, del_inode.fs->device());
                    return fs->free_inode(dir_blk.entries[j].inode);
                } else {
                    return write_inode(&disk_inode);
                }
            }
        }
    }
    return kFail;
}

int Inode::link(const char *name, const Inode *inode, bool replace) const {
    DiskInode disk_inode;
    CHECK_RET(read_inode(&disk_inode));
    if (disk_inode.type != kDirectory) {
        return kFail;
    }
    auto update_link_cnt = [&]() {
        DiskInode raw_disk_inode;
        CHECK_RET(inode->read_inode(&raw_disk_inode));
        ++raw_disk_inode.link_cnt;
        return inode->write_inode(&raw_disk_inode);
    };
    // find entry with same name
    auto tot_blocks = disk_inode.total_blocks(disk_inode.size);
    DirBlock dir_blk;
    for (auto i = 0; i < tot_blocks; ++i) {
        CHECK_RET(disk_inode.read_data(i * sizeof(DirBlock), (uint8_t *)&dir_blk, sizeof(DirBlock), fs->device()));
        for (auto j = 0; j < kDirEntries; ++j) {
            if (strcmp(dir_blk.entries[j].name, name) == 0) {
                if (!replace) {
                    return kFail;
                }
                CHECK_RET(update_link_cnt());
                DirEntry entry(dir_blk.entries[j].name, fs->getDiskInodeId(inode->pos));
                CHECK_RET(disk_inode.write_data(i * sizeof(DirBlock) + j * sizeof(DirEntry), (uint8_t *)&entry,
                                                sizeof(DirEntry), fs->device()));
                // FIXME: release old inode?
                return write_inode(&disk_inode);
            }
        }
    }
    CHECK_RET(update_link_cnt());
    // create new entry
    // increase
    CHECK_RET(disk_inode.resize(disk_inode.size + sizeof(DirEntry), fs->data_bitmap_, fs->device()));
    DirEntry entry(name, inode->fs->getDiskInodeId(inode->pos));
    CHECK_RET(disk_inode.write_data(disk_inode.size - sizeof(DirEntry), (uint8_t *)&entry, sizeof(DirEntry),
                                    fs->device()));
    return write_inode(&disk_inode);
}

int Inode::unlink(const char *name, Inode *inode) const {
    auto ret = [&]() {  // find and remove it without releasing
        DiskInode disk_inode;
        CHECK_RET(read_inode(&disk_inode));
        if (disk_inode.type != kDirectory) {
            return kFail;
        }
        auto tot_blocks = disk_inode.total_blocks(disk_inode.size);
        DirBlock dir_blk;
        for (auto i = 0; i < tot_blocks; ++i) {
            CHECK_RET(disk_inode.read_data(i * sizeof(DirBlock), (uint8_t *)&dir_blk, sizeof(DirBlock), fs->device()));
            for (auto j = 0; j < kDirEntries; ++j) {
                if (strcmp(dir_blk.entries[j].name, name) == 0) {
                    *inode = { .pos = fs->getDiskInodePos(dir_blk.entries[j].inode), .fs = fs };
                    if (i != tot_blocks - 1 || j != kDirEntries - 1) {  // move the last entry here
                        DirEntry last_entry;
                        CHECK_RET(disk_inode.read_data(disk_inode.size - sizeof(DirEntry), (uint8_t *)&last_entry,
                                                       sizeof(DirEntry), fs->device()));
                        CHECK_RET(disk_inode.write_data(i * sizeof(DirBlock) + j * sizeof(DirEntry),
                                                        (uint8_t *)&last_entry, sizeof(DirEntry), fs->device()));
                    }
                    // decrease
                    CHECK_RET(disk_inode.resize(disk_inode.size - sizeof(DirEntry), fs->data_bitmap_, fs->device()));
                    return write_inode(&disk_inode);
                }
            }
        }
        return kFail;
    }();
    if (ret == kSuccess) {  // update link_cnt
        DiskInode raw_disk_inode;
        CHECK_RET(inode->read_inode(&raw_disk_inode));
        --raw_disk_inode.link_cnt;
        return inode->write_inode(&raw_disk_inode);
    } else {
        return ret;
    }
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