#include "lru_cache.h"

#include "blk_dev.h"
using namespace std;
using namespace sbfs;

LRUCacheManager::LRUCacheManager(const uint64_t cache_size, BlockDevice *parent)
    : FREE_first(0), LRU_first(-1), LRU_last(-1), _hashtable(), _cache_size(cache_size), _dev(parent) {
    DLOG(INFO) << "cache mgr init start";
    _size = cache_size / kBlockSize;
    _buffer = vector<pair<Block *, BlockStatus>>(_size);
    for (int i = 0; i < _size; i++) {
        auto &t = _buffer[i].second;
        t.prev = i - 1;
        t.next = i + 1;
        if (i == 0) {
            t.prev = -1;
        }
        if (i == _size - 1) {
            t.next = -1;
        }
        _buffer[i].first = new Block;
    }

    DLOG(INFO) << "cache created with size " << _size << " blocks";
};

LRUCacheManager::~LRUCacheManager() {
    for (int i = 0; i < _size; i++) {
        delete _buffer[i].first;
    }
}

int LRUCacheManager::upsert(blk_id_t block_id, const Block *block, bool is_update) {
    int slot = -1;
    DLOG(INFO) << "cache receive upsert req id: " << block_id << " isupdate: " << is_update << " " << block;
    if (get_page(block_id, slot) != kSuccess) {
        DLOG(ERROR) << "upsert " << block_id << " failed";
        return kFail;
    } else {
        DLOG(INFO) << "upsert " << block_id << " slot " << slot;
        memcpy(_buffer[slot].first, block, sizeof(Block));
        //_buffer[slot].first = block;
        if (!_buffer[slot].second.is_dirty()) {
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
    DLOG(INFO) << "cache receive get req: " << block_id << " " << block;
    auto it = _hashtable.find(block_id);
    if (it != _hashtable.end()) {
        slot = it->second;
        LRU_remove(slot);
        LRU_add(slot);
        memcpy(block, _buffer[slot].first, sizeof(Block));
        return kSuccess;
    } else {
        return kFail;
    }
    // if (get_page(block_id, slot) != kSuccess) {
    //     DLOG(ERROR) << "get " << block_id << " failed";
    //     memset(block, 0, sizeof(Block));
    //     return kFail;
    // } else {
    //     DLOG(INFO) << "cache get " << block_id << " slot " << slot;
    //     memcpy(block, _buffer[slot].first, sizeof(Block));
    //     // block = _buffer[slot].first;
    //     return kSuccess;
    // }
    // rt_assert(false, "should not reach here");
    // return kFail;
}

int LRUCacheManager::remove(blk_id_t block_id) {
    DLOG(INFO) << "cache receive remove req: " << block_id;
    return remove_page(block_id);
}

int LRUCacheManager::sync(blk_id_t block_id) {
    int slot = -1;
    DLOG(INFO) << "cache receive sync req: " << block_id;
    auto it = _hashtable.find(block_id);
    if (it != _hashtable.end()) {
        slot = it->second;
        DLOG(INFO) << "cache sync " << block_id << " slot " << slot;
        LRU_remove(slot);
        LRU_add(slot);
        if (_buffer[slot].second.is_dirty()) {
            _buffer[slot].second.rev_dirty();
            return _dev->write_to_disk(block_id, _buffer[slot].first);
        } else {
            return kSuccess;
        }
    } else {
        DLOG(ERROR) << "sync block not found";
        return kFail;
    }
    // if (get_page(block_id, slot) != kSuccess) {
    //     DLOG(ERROR) << "sync block not found";
    //     return kFail;
    // } else {
    //     DLOG(INFO) << "cache sync " << block_id << " slot " << slot;
    //     if (_buffer[slot].second.is_dirty()) {
    //         _buffer[slot].second.rev_dirty();
    //         return _dev->write_to_disk(_buffer[slot].second.id, _buffer[slot].first);
    //     } else {
    //         return kSuccess;
    //     }
    // }
    rt_assert(false, "should not reach here");
    return kFail;
}

int LRUCacheManager::get_page(blk_id_t id, int &slot) {
    auto p = _hashtable.find(id);
    if (p == _hashtable.end()) {
        alloc(slot);
        _buffer[slot].second.id = id;
        if (_dev->read_from_disk(id, _buffer[slot].first) != kSuccess) {
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
    DLOG(INFO) << "cache remove_page: " << id << " slot " << slot;
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
        return kSuccess;
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
