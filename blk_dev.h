#ifndef BLK_DEV_H_
#define BLK_DEV_H_

#include "config.h"
#include "blk_cache.h"

namespace sbfs {
class BlockDevice {
public:
    /* Path is the simulated "disk file" path, size is its size. */
    BlockDevice(const char *path, const uint64_t size) : blk_cache_mgr_(kBlockCacheSize, this) {};
    ~BlockDevice();
    /* 
     * Read block_id to buf,
     * First find the block in BlockCacheManager,
     * if miss, read from disk, and put it in BlockCacheManager.
     */
    int read(blk_id_t block_id, Block *buf);
    /* 
     * Write buf to block_id 
     * (only write to the cache is OK)
     */
    int write(blk_id_t block_id, const Block *buf);
    /*
     * transactionally write "bufs" block to block "block_ids", all modifications should be to disk.
     * guarantee this write operation is atomic, disk shouldn't have any middle states.
     */
    int write_tx(const std::vector<blk_id_t> &block_ids, const std::vector<const Block *> &bufs);
    /* 
     * sync block_id to disk 
     * that is, if block_id exists in cache,
     * write it to disk.
     */
    int sync(blk_id_t block_id);
private:
    /* TODO: some data structures */
    BlockCacheManager blk_cache_mgr_;
};
};

#endif // BLK_DEV_H_