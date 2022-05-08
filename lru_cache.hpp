#include "blk_cache.h"
#include "config.h"

namespace sbfs {
class BlockDevice;

class LRUCacheManager : BlockCacheManager {
public:
    LRUCacheManager(const uint64_t cache_size, BlockDevice *parent)
        : BlockCacheManager(cache_size, parent){

          };
    ~LRUCacheManager();
    /*
     * insert or update a block to cache, if full, evict and write back (another) one.
     * return 0 if success, kFail if failed.
     * if update, set "dirty" (Inconsistent to )
     */
    int upsert(blk_id_t block_id, const Block *block) {
        auto slot = std::hash<blk_id_t>()(block_id) % size_;
        auto it = map_.find(slot);
        if (it == map_.end()) {
                        return kFail;
        } else {
            *block = it->second.first;
            update_status(it->second.second);
            LRU_remove(slot);
            LRU_add(slot);
            return kSuccess;
        }
    }
    /* get a block from cache. returns kFail if failed. */
    int get(blk_id_t block_id, Block *block) {
        auto slot = std::hash<blk_id_t>()(block_id) % size_;
        auto it = map_.find(slot);
        if (it == map_.end()) {
            return kFail;
        } else {
            *block = it->second.first;
            update_status(it->second.second);
            LRU_remove(slot);
            LRU_add(slot);
            return kSuccess;
        }
    }
    /* remove a block from cache, if dirty, write back. */
    int remove(blk_id_t block_id);
    /* write back block. */
    int sync(blk_id_t block_id);

private:
    void update_status(BlockStatus &stu) {
        // do something
    }
    /*
     * this data structure can be replaced.
     * block_id to (Block and Block status)
     */
    std::unordered_map<blk_id_t, std::pair<Block, BlockStatus>> map_;
    BlockDevice *parent_;
    uint64_t size_;

    int free(int slot);
    int LRU_add(int slot);
    int LRU_remove(int slot);
    int alloc(int &slot);
    int read_page(int fd, int page, char *dst);
    int write_page(int fd, int page, char *src);
    int _free;
    int LRU_first;
    int LRU_last;
};
}  // namespace sbfs
