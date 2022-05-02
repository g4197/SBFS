#ifndef BLK_CACHE_H_
#define BLK_CACHE_H_

#include "config.h"
#include "blk.h"

namespace sbfs {
class BlockDevice;

class BlockCacheManager {
public:
    BlockCacheManager(const uint64_t cache_size, BlockDevice *parent);
    ~BlockCacheManager();
    /* 
     * insert or update a block to cache, if full, evict and write back (another) one.
     * return 0 if success, kFail if failed.
     * if update, set "dirty" (Inconsistent to )
     */
    int upsert(blk_id_t block_id, const Block *block);
    /* get a block from cache. returns kFail if failed. */
    int get(blk_id_t block_id, Block *block);
    /* remove a block from cache, if dirty, write back. */
    int remove(blk_id_t block_id);
    /* write back block. */
    int sync(blk_id_t block_id);
private:
    /* 
     * this data structure can be replaced. 
     * block_id to (Block and Block status)
     */
    std::map<blk_id_t, std::pair<Block, BlockStatus>> map_;
    BlockDevice *parent_;
    uint64_t size_;
};
}

#endif // BLK_CACHE_H_