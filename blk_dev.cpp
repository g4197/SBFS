#include "blk_dev.h"
#include <fcntl.h>
#include <unistd.h>

namespace sbfs {
BlockDevice::BlockDevice(const char *path, const uint64_t size) : 
    blk_cache_mgr_(kBlockCacheSize, this) {
    rt_assert(size % kBlockSize == 0, "size must be multiple of kBlockSize");

    fd_ = open(path, O_DIRECT | O_RDWR | O_NOATIME | O_CREAT, 0644);
    
    if (fd_ < 0) {
        DLOG(ERROR) << "open " << path << " failed";
        return;
    }

    if (ftruncate(fd_, size) < 0) {
        DLOG(ERROR) << "ftruncate " << path << " failed";
        return;
    }
    
    num_data_blocks_ = size / kBlockSize;
}

BlockDevice::~BlockDevice() {
    close(fd_);
}

int BlockDevice::read(blk_id_t block_id, Block *buf) {
    rt_assert(block_id < num_data_blocks_, "block_id out of range");
    rt_assert(buf != nullptr, "buf is nullptr");

    if (blk_cache_mgr_.get(block_id, buf) == kFail) {
        if (pread(fd_, buf, kBlockSize, block_id * kBlockSize) != kBlockSize) {
            DLOG(ERROR) << "pread " << block_id << " failed";
            return kFail;
        }
        blk_cache_mgr_.upsert(block_id, buf);
    }
    return kSuccess;
}

int BlockDevice::write(blk_id_t block_id, const Block *buf) {
    rt_assert(block_id < num_data_blocks_, "block_id out of range");
    rt_assert(buf != nullptr, "buf is nullptr");

    if (blk_cache_mgr_.upsert(block_id, buf) == kFail) {
        DLOG(ERROR) << "upsert " << block_id << " failed";
        return kFail;
    }
    return kSuccess;
}

int BlockDevice::write_to_disk(blk_id_t block_id, const Block *buf) {
    rt_assert(block_id < num_data_blocks_, "block_id out of range");
    rt_assert(buf != nullptr, "buf is nullptr");

    if (pwrite(fd_, buf, kBlockSize, block_id * kBlockSize) != kBlockSize) {
        DLOG(ERROR) << "pwrite " << block_id << " failed";
        return kFail;
    }
    return kSuccess;
}

int BlockDevice::write_tx(const std::vector<blk_id_t> &block_ids, 
                          const std::vector<const Block *> &bufs) {
    rt_assert(block_ids.size() == bufs.size(), "block_ids and bufs size not match");

    for (size_t i = 0; i < block_ids.size(); ++i) {
        rt_assert(block_ids[i] < num_data_blocks_, "block_id out of range");
        /* TODO: transaction */
        if (write(block_ids[i], bufs[i]) == kFail) {
            DLOG(ERROR) << "write " << block_ids[i] << " failed";
            return kFail;
        }
    }
    return kSuccess;
}

int BlockDevice::sync(blk_id_t block_id) {
    rt_assert(block_id < num_data_blocks_, "block_id out of range");

    return blk_cache_mgr_.sync(block_id);
}

};