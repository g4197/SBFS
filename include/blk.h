#ifndef BLK_H_
#define BLK_H_

#include "config.h"

namespace sbfs {
struct alignas(kBlockSize) Block {
    uint8_t data[kBlockSize];
    /* TODO: maybe some helper functions */
};

/*
 * this data structure can be modified,
 * for it is transparent to upper layer.
 */
struct BlockStatus {
    uint8_t status;
    int prev;
    int next;
    int id;
    // int file_id;?
    bool is_dirty() const {
        return status & 1;
    }
    void rev_dirty() {
        status ^= 1;
    }
    void init() {
        status = 0;
        prev = next = -1;
        id = -1;
    }
};
};  // namespace sbfs

#endif  // BLK_H_