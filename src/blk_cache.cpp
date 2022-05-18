#include "blk_cache.h"

#include "blk_dev.h"

namespace sbfs {
BlockCacheManager::BlockCacheManager(const uint64_t cache_size, BlockDevice *parent)
    : parent_(parent), size_(cache_size) {}

BlockCacheManager::~BlockCacheManager() {}

int BlockCacheManager::upsert(blk_id_t block_id, const Block *block) {
    parent_->write_to_disk(block_id, block);
    return kSuccess;
}

int BlockCacheManager::get(blk_id_t block_id, Block *block) {
    return kFail;
}

int BlockCacheManager::remove(blk_id_t block_id) {
    return kSuccess;
}

int BlockCacheManager::sync(blk_id_t block_id) {
    return kSuccess;
}

int BlockCacheManager::sync_all() {
    return kSuccess;
}

};  // namespace sbfs