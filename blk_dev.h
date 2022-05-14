#ifndef BLK_DEV_H_
#define BLK_DEV_H_

#include "config.h"
#include "lru_cache.h"

namespace sbfs {
class BlockDevice {
public:
    /* Path is the simulated "disk file" path, size is its size. */
    BlockDevice(const char *path, uint64_t size);
    ~BlockDevice();
    /*
     * Read block_id to buf,
     * First find the block in BlockCacheManager,
     * if missed, read from disk, and put it in BlockCacheManager.
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

    int write_to_disk(blk_id_t block_id, const Block *buf);
    int read_from_disk(blk_id_t block_id, Block *buf);

private:
    LRUCacheManager blk_cache_mgr_;
    // BlockCacheManager blk_cache_mgr_;
    int fd_;
    uint32_t num_data_blocks_;
    uint32_t num_log_blocks_; /* TODO: reserve log blocks */
};
};  // namespace sbfs

#endif  // BLK_DEV_H_