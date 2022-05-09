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
    std::unordered_map<blk_id_t, uint32_t> _hashtable;   // map blk id to slot id
    std::vector<std::pair<Block, BlockStatus>> _buffer;  // actual cache
    BlockDevice *parent_;
    uint64_t size_;

    int allocate_page(int fd, int page, int &slot) {
        if (!_hashtable->find(fd, page, slot)) return -1;
        if (alloc(slot)) return -1;
        _hashtable->insert(fd, page, slot);
        buffer[slot].init(fd, page);
        return 0;
    }
    int get_page(int fd, int page, int &slot) {
        auto p = _hashtable->find(fd, page, slot);
        if (p == -1) {
            alloc(slot);
            read_page(fd, page, buffer[slot].data);
            _hashtable->insert(fd, page, slot);
            buffer[slot].init(fd, page);
        } else {
            ++buffer[slot].pin;
            LRU_remove(slot);
            LRU_add(slot);
        }
        return 0;
    }
    int remove_page(int fd, int page) {
        int slot = -1;
        if (_hashtable->find(fd, page, slot)) return -1;
        if (buffer[slot].dirty) {
            write_page(fd, page, buffer[slot].data);
            buffer[slot].dirty = false;
        }
        _hashtable->remove(fd, page);
        LRU_remove(slot);
        free(slot);
        return 0;
    }
    int flush() {
        for (int i = 0; i < _size; ++i) {
            if (buffer[i].dirty) write_page(buffer[i].fd, buffer[i].page, buffer[i].data);
        }
        return -1;
    }
    int unpin(int fd, int page) {
        int slot = 0;
        if (_hashtable->find(fd, page, slot) == -1) return -1;
        if (!buffer[slot].pin) return -1;
        if (--buffer[slot].pin == 0) {
            LRU_remove(slot);
            LRU_add(slot);
        }
        return 0;
    }
    int mark_dirty(int slot) {
        buffer[slot].dirty = true;
        LRU_remove(slot);
        LRU_add(slot);
        return 0;
    }

    int mark_dirty(int fd, int page) {
        int slot = 0;
        if (_hashtable->find(fd, page, slot) == -1) return -1;
        if (!buffer[slot].pin) return -1;
        buffer[slot].dirty = true;
        LRU_remove(slot);
        LRU_add(slot);
        return 0;
    }
    int free(int slot) {
        buffer[slot].next = _free;
        _free = slot;
        return 0;
    }
    int LRU_add(int slot) {
        auto &it = buffer[slot];
        it.next = LRU_first;
        it.prev = -1;
        if (LRU_first != -1) buffer[LRU_first].prev = slot;
        LRU_first = slot;
        if (LRU_last == -1) LRU_last = slot;
        return 0;
    }
    int LRU_remove(int slot) {
        auto &it = buffer[slot];
        if (slot == LRU_first) LRU_first = it.next;
        if (slot == LRU_last) LRU_last = it.prev;
        if (it.next != -1) buffer[it.next].prev = it.prev;
        if (it.prev != -1) buffer[it.prev].next = it.next;
        it.prev = it.next = -1;
        return 0;
    }
    int free(int slot) {
        buffer[slot].next = _free;
        _free = slot;
        return 0;
    }
    int alloc(int &slot) {
        if (_free != -1) {
            slot = _free;
            _free = buffer[slot].next;
            LRU_remove(slot);
            LRU_add(slot);
            return 0;
        }
        for (slot = LRU_last; slot != -1; slot = buffer[slot].prev) {
            if (/*buffer[slot].pin*/ true) {
                auto &it = buffer[slot];
                if (it.dirty) {
                    write_page(it.fd, it.page, it.data);
                    it.dirty = false;
                }
                _hashtable->remove(it.fd, it.page);
                LRU_remove(slot);
                LRU_add(slot);
                return 0;
            }
        }
        return -1;
    }
    int read_page(int fd, int page, char *dst);
    int write_page(int fd, int page, char *src);

    int _free;
    int LRU_first;
    int LRU_last;
};
}  // namespace sbfs
