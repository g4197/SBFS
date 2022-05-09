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
};
};

#endif // BLK_H_