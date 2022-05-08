#include <sys/stat.h>

#include "fs.h"

namespace sbfs {
constexpr uint32_t kInodesInABlock = kBlockSize / sizeof(DiskInode);

SBFileSystem SBFileSystem::create(const char *path, const uint64_t size, uint32_t total_blocks,
                                  uint32_t inode_bitmap_blocks) {
    SBFileSystem fs;
    fs.device_ = new BlockDevice(path, size);

    /* init super block */
    fs.super_block_.magic = kFSMagic;
    fs.super_block_.total_blocks = total_blocks;
    fs.super_block_.inode_bitmap_blocks = inode_bitmap_blocks;
    fs.super_block_.inode_area_blocks = inode_bitmap_blocks * 8 * kInodesInABlock;
    uint32_t remaining_blocks = total_blocks - inode_bitmap_blocks - fs.super_block_.inode_area_blocks;
    /* 1 data bitmap <-> 8 * kBlockSize data block */
    fs.super_block_.data_bitmap_blocks = remaining_blocks / (1 + 8 * kBlockSize);
    fs.super_block_.data_area_blocks = fs.super_block_.data_bitmap_blocks * 8 * kBlockSize;
    fs.super_block_.root_inode_pos = Position::invalid();

    fs.super_block_.print();
    DLOG(INFO) << "unusable_blocks: "
               << remaining_blocks - fs.super_block_.data_bitmap_blocks - fs.super_block_.data_area_blocks;
    /* stage 1: super block initialize, but root inode pos is invalid. */
    fs.device_->write(0, (Block *)&fs.super_block_);

    fs.initBitmapAndBlock();
    fs.createRoot();

    return fs;
}

/* Open an existing SBFS. */
SBFileSystem SBFileSystem::open(const char *path) {
    SBFileSystem fs;

    struct stat stbuf;
    if (stat(path, &stbuf) < 0) {
        LOG(ERROR) << "Can't open disk file: " << path;
        return fs;
    }

    fs.device_ = new BlockDevice(path, stbuf.st_size);
    fs.device_->read(0, (Block *)&fs.super_block_);
    if (!fs.super_block_.isValid()) {
        LOG(ERROR) << "Invalid magic number: " << fs.super_block_.magic;
        return fs;
    }
    fs.super_block_.print();

    fs.initBitmapAndBlock();
    return fs;
}

void SBFileSystem::initBitmapAndBlock() {
    /* init inode bitmap */
    uint32_t inode_bitmap_offset = 1;
    uint32_t inode_area_offset = inode_bitmap_offset + super_block_.inode_bitmap_blocks;
    uint32_t data_bitmap_offset = inode_area_offset + super_block_.inode_area_blocks;
    uint32_t data_area_offset = data_bitmap_offset + super_block_.data_bitmap_blocks;
    inode_bitmap_ = new Bitmap(inode_bitmap_offset, super_block_.inode_bitmap_blocks, inode_area_offset);
    data_bitmap_ = new Bitmap(data_bitmap_offset, super_block_.data_bitmap_blocks, data_area_offset);

    /* init block num */
    inode_area_start_block_ = inode_area_offset;
    data_area_start_block_ = data_area_offset;
}

void SBFileSystem::createRoot() {
    /* create root inode */
    uint32_t root_inode_id = alloc_inode();
    uint32_t root_data_id = alloc_data();
    DiskInode root_inode_data(DiskInodeType::kDirectory);
    DirBlock root_dir_block;
    strcpy(root_dir_block.entries[0].name, ".");
    root_dir_block.entries[0].inode = root_inode_id;
    strcpy(root_dir_block.entries[1].name, "..");
    root_dir_block.entries[1].inode = root_inode_id;
    root_inode_data.size = 2 * sizeof(DirEntry);
    root_inode_data.direct[0] = root_data_id;

    Inode inode{ getDiskInodePos(root_inode_id), this };
    inode.write_inode(&root_inode_data);
    device_->write(root_data_id, (Block *)&root_dir_block);

    /* set root inode pos */
    super_block_.root_inode_pos = getDiskInodePos(root_inode_id);
    device_->write(0, (Block *)&super_block_);
}

/* get root Inode. */
Inode SBFileSystem::root() {
    return Inode{ super_block_.root_inode_pos, this };
}

/* get block device. */
BlockDevice *SBFileSystem::device() {
    return device_;
}

/* get actual inode position by inode id. */
Position SBFileSystem::getDiskInodePos(uint32_t inode_id) {
    Position pos;
    pos.block_id = inode_area_start_block_ + inode_id / kInodesInABlock;
    pos.block_offset = (inode_id % kInodesInABlock) * sizeof(DiskInode);
    return pos;
}

uint32_t SBFileSystem::getDiskInodeId(const Position &pos) {
    return (pos.block_id - inode_area_start_block_) * kInodesInABlock + pos.block_offset / sizeof(DiskInode);
}

/* Allocate an inode, returns inode id. */
uint32_t SBFileSystem::alloc_inode() {
    return inode_bitmap_->alloc(device_);
}

/* Allocate a data block, returns block id (not block_id - data_area_start). */
uint32_t SBFileSystem::alloc_data() {
    return data_bitmap_->alloc(device_);
}

/* Deallocate an inode. */
int SBFileSystem::free_inode(uint32_t inode_id) {
    return inode_bitmap_->free(inode_id, device_);
}

/* Deallocate a data block. */
int SBFileSystem::free_data(uint32_t block_id) {
    return data_bitmap_->free(block_id, device_);
}
};  // namespace sbfs