#include "blk_cache.h"
#include "blk_dev.h"

namespace sbfs {
BlockCacheManager::BlockCacheManager(const uint64_t cache_size, BlockDevice *parent)
    : cache_map_(), cache_size_(cache_size), parent_(parent) {
    
}
};