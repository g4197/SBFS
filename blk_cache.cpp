#include "blk_cache.h"
#include "blk_dev.h"

namespace sbfs {
BlockCacheManager::BlockCacheManager(const uint64_t cache_size, BlockDevice *parent)
    : map_(), size_(cache_size), parent_(parent) {}

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
    if (map_.find(block_id) != map_.end()) {
        parent_->write_to_disk(block_id, &map_[block_id].first);
    }
    return kSuccess;
}

};