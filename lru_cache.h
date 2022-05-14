#ifndef LRU_CACHE_H_
#define LRU_CACHE_H_

#include "blk.h"
#include "config.h"

namespace sbfs {
class BlockDevice;
class LRUCacheManager {
public:
    LRUCacheManager(const uint64_t cache_size, BlockDevice *parent);
    ~LRUCacheManager() = default;
    /*
     * insert or update a block to cache, if full, evict and write back (another) one.
     * return 0 if success, kFail if failed.
     * if update, set "dirty" (Inconsistent to )
     */
    int upsert(blk_id_t block_id, const Block *block, bool is_update = false);
    /* get a block from cache. returns kFail if failed. */
    int get(blk_id_t block_id, Block *block);

    /* remove a block from cache, if dirty, write back. */
    int remove(blk_id_t block_id);
    /* write back block. */
    int sync(blk_id_t block_id);

private:
    /**
     * @brief Get the page object
     *
     * @param id
     * @param slot
     * @return int
     */
    int get_page(blk_id_t id, int &slot);
    /**
     * @brief evict a block from cache
     *
     * @param id
     * @return int
     */
    int remove_page(blk_id_t id);

    int alloc(int &slot);

    int FREE_add(int slot) {
        auto &stu = _buffer[slot].second;
        stu.next = FREE_first;
        FREE_first = slot;
        return 0;
    }
    int LRU_add(int slot) {
        auto &stu = _buffer[slot].second;
        stu.next = LRU_first;
        stu.prev = -1;
        if (LRU_first != -1) {
            _buffer[LRU_first].second.prev = slot;
        }
        LRU_first = slot;
        if (LRU_last == -1) {
            LRU_last = slot;
        }
        return 0;
    }
    int LRU_remove(int slot) {
        auto &stu = _buffer[slot].second;
        if (slot == LRU_first) {
            LRU_first = stu.next;
        }

        if (slot == LRU_last) {
            LRU_last = stu.prev;
        }
        if (stu.next != -1) {
            _buffer[stu.next].second.prev = stu.prev;
        }
        if (stu.prev != -1) {
            _buffer[stu.prev].second.next = stu.next;
        }
        stu.prev = stu.next = -1;
        return 0;
    }

    int FREE_first;
    int LRU_first;
    int LRU_last;
    std::unordered_map<blk_id_t, int> _hashtable;        // map blk id to slot id
    std::vector<std::pair<Block, BlockStatus>> _buffer;  // actual cache
    uint64_t _size;                                      // maxium slot of buffer
    BlockDevice *_dev;
};
}  // namespace sbfs

#endif  // LRU_CACHE_H_
