#include "lru_cache.h"

#include "blk_dev.h"
using namespace std;
using namespace sbfs;

LRUCacheManager::LRUCacheManager(const uint64_t cache_size, BlockDevice *parent)
    : FREE_first(0), LRU_first(-1), LRU_last(-1), _hashtable(), _buffer(cache_size), _size(cache_size), _dev(parent) {
    for (int i = 0; i < cache_size; i++) {
        auto &t = _buffer[i].second;
        t.prev = i - 1;
        t.next = i + 1;
        if (i == 0) {
            t.prev = -1;
        }
        if (i == cache_size - 1) {
            t.next = -1;
        }
        _buffer[i].first = new Block;
    }
};

LRUCacheManager::~LRUCacheManager() {
    for (int i = 0; i < _size; i++) {
        delete _buffer[i].first;
    }
}

int LRUCacheManager::upsert(blk_id_t block_id, const Block *block, bool is_update) {
    int slot = -1;
    if (get_page(block_id, slot) != kSuccess) {
        DLOG(ERROR) << "upsert " << block_id << " failed";
        return kFail;
    } else {
        memcpy(_buffer[slot].first, block, sizeof(Block));
        //_buffer[slot].first = block;
        if (is_update && !_buffer[slot].second.is_dirty()) {
            _buffer[slot].second.rev_dirty();
        }
        return kSuccess;
    }
    rt_assert(false, "should not reach here");
    return kFail;
}

int LRUCacheManager::get(blk_id_t block_id, Block *block) {
    std::ignore = block;
    int slot = -1;
    if (get_page(block_id, slot) != kSuccess) {
        DLOG(ERROR) << "get " << block_id << " failed";
        block = nullptr;
        return kFail;
    } else {
        memcpy(block, _buffer[slot].first, sizeof(Block));
        // block = _buffer[slot].first;
        return kSuccess;
    }
    block = nullptr;
    rt_assert(false, "should not reach here");
    return kFail;
}

int LRUCacheManager::remove(blk_id_t block_id) {
    return remove_page(block_id);
}

int LRUCacheManager::sync(blk_id_t block_id) {
    int slot = -1;
    if (get_page(block_id, slot) != kSuccess) {
        DLOG(ERROR) << "sync block not found";
        return kFail;
    } else {
        if (_buffer[slot].second.is_dirty()) {
            _buffer[slot].second.rev_dirty();
            return _dev->write_to_disk(_buffer[slot].second.id, _buffer[slot].first);
        } else {
            return kSuccess;
        }
    }
    rt_assert(false, "should not reach here");
    return kFail;
}

int LRUCacheManager::get_page(blk_id_t id, int &slot) {
    auto p = _hashtable.find(id);
    if (p == _hashtable.end()) {
        alloc(slot);
        _buffer[slot].second.id = id;
        if (_dev->read(id, _buffer[slot].first) != kSuccess) {
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

int LRUCacheManager::remove_page(blk_id_t id) {
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
        if (_dev->write_to_disk(id, _buffer[slot].first) != kSuccess) {
            DLOG(ERROR) << "write block dirty failed at cache remove_page";
            return kFail;
        }
    }
    _hashtable.erase(id);
    LRU_remove(slot);
    FREE_add(slot);
    return kSuccess;
}

int LRUCacheManager::alloc(int &slot) {
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
                if (_dev->write_to_disk(_buffer[slot].second.id, _buffer[slot].first)) {
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
