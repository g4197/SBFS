#include "blk_cache.h"
#include "blk_dev.h"
#include "config.h"

namespace sbfs {
class BlockDevice;

class LRUCacheManager : BlockCacheManager {
public:
    LRUCacheManager(const uint64_t cache_size, BlockDevice *parent) : BlockCacheManager(cache_size, parent) {
        _size = cache_size;
        _dev = parent;
    };
    ~LRUCacheManager();
    /*
     * insert or update a block to cache, if full, evict and write back (another) one.
     * return 0 if success, kFail if failed.
     * if update, set "dirty" (Inconsistent to )
     */
    int upsert(blk_id_t block_id, const Block *block, bool is_update = false) {
        int slot = -1;
        if (get_page(block_id, slot) != kSuccess) {
            DLOG(ERROR) << "upsert " << block_id << " failed";
            return kFail;
        } else {
            _buffer[slot].first = *block;
            if (is_update && !_buffer[slot].second.is_dirty()) {
                _buffer[slot].second.rev_dirty();
            }
            return kSuccess;
        }
        rt_assert(false, "should not reach here");
        return kFail;
    }
    /* get a block from cache. returns kFail if failed. */
    int get(blk_id_t block_id, Block *block) {
        int slot = -1;
        if (get_page(block_id, slot) != kSuccess) {
            DLOG(ERROR) << "get " << block_id << " failed";
            block = nullptr;
            return kFail;
        } else {
            block = &_buffer[slot].first;
            return kSuccess;
        }
        rt_assert(false, "should not reach here");
        return kFail;
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

    int allocate_page(int fd, int page, int &slot) {
        if (!_hashtable->find(fd, page, slot)) return -1;
        if (alloc(slot)) return -1;
        _hashtable->insert(fd, page, slot);
        buffer[slot].init(fd, page);
        return 0;
    }
    /**
     * @brief Get the page object
     *
     * @param id
     * @param slot
     * @return int
     */
    int get_page(blk_id_t id, int &slot) {
        auto p = _hashtable.find(id);
        if (p == _hashtable.end()) {
            alloc(slot);
            _buffer[slot].second.id = id;
            if (_dev->read(id, &_buffer[slot].first) != kSuccess) {
                DLOG(ERROR) << "read block failed at cache get_page";
                return kFail;
            }
            _hashtable[id] = slot;
        } else {
            slot = p->second;
            // ++buffer[slot].pin;
            LRU_remove(slot);
            LRU_add(slot);
        }
        return kSuccess;
    }
    /**
     * @brief evict a block from cache
     *
     * @param id
     * @return int
     */
    int remove_page(blk_id_t id) {
        int slot = -1;
        auto p = _hashtable.find(id);
        if (p == _hashtable.end()) {
            DLOG(ERROR) << "remove_page: block not found in cache";
            return kFail;
        } else {
            slot = p->second;
        }
        auto &stu = _buffer[slot].second;
        if (stu.is_dirty()) {
            if (_dev->write_to_disk(id, &_buffer[slot].first) != kSuccess) {
                DLOG(ERROR) << "write block dirty failed at cache remove_page";
                return kFail;
            }
        }
        _hashtable.erase(id);
        LRU_remove(slot);
        FREE_add(slot);
        return kSuccess;
    }

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
    int alloc(int &slot) {
        if (FREE_first != -1) {
            slot = FREE_first;
            FREE_first = _buffer[slot].second.next;
            _buffer[slot].second.init();
            LRU_remove(slot);
            LRU_add(slot);
            return 0;
        }
        for (slot = LRU_last; slot != -1; slot = _buffer[slot].second.prev) {
            if (/*buffer[slot].pin*/ true) {
                auto &stu = _buffer[slot].second;
                if (stu.is_dirty()) {
                    if (_dev->write_to_disk(_buffer[slot].first.id, &_buffer[slot].first)) {
                        DLOG(ERROR) << "write block dirty failed at cache alloc";
                        return kFail;
                    }
                }
                _hashtable.erase(_buffer[slot].second.id);
                _buffer[slot].second.init();
                LRU_remove(slot);
                LRU_add(slot);
                return kSuccess;
            }
        }
        DLOG(ERROR) << "no free block in cache";
        return kFail;
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
