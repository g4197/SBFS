#include "fs_layout.h"

using namespace std;
using namespace sbfs;

/**
 * @brief find a zero in x, may not be leading one
 *
 * @param x
 * @return int position
 */
int leading_zero(uint64_t x) {
    for (int i = 0; i < 64; i++) {
        if ((x >> i) ^ 1) {
            return i;
        }
    }
    return -1;
}

Bitmap::Bitmap(blk_id_t start_block_id, blk_id_t num_blocks, blk_id_t data_segment_offset)
    : start_block_id(start_block_id), num_blocks(num_blocks), data_segment_offset(data_segment_offset) {
    // init();
}

blk_id_t Bitmap::alloc(BlockDevice *dev) {
    auto buf = new Block;
    int slot_per_block = kBlockSize * 8;
    for (blk_id_t i = 0; i < num_blocks; i++) {
        if (dev->read(start_block_id + i, buf) != kSuccess) {
            DLOG(WARNING) << "bitmap read " << start_block_id + i << " failed";
            return kFail;
        }
        auto sz = sizeof(uint64_t);
        auto p = (uint64_t *)(buf->data);
        for (int j = 0; j < kBlockSize / sz; j++) {
            int k = leading_zero(p[j]);
            // int k = -1;
            if (k != -1) {
                p[j] |= (1 << k);
                if (dev->write(start_block_id + i, buf) != kSuccess) {
                    DLOG(WARNING) << "bitmap write " << start_block_id + i << " failed";
                    return kFail;
                }
                return i * slot_per_block + j * sz + k + data_segment_offset;
            }
        }
    }
    DLOG(WARNING) << "bitmap alloc failed: no empty block found";
    return kFail;
}

int Bitmap::free(blk_id_t block_id, BlockDevice *dev) {
    block_id -= data_segment_offset;
    auto buf = new Block;
    int slot_per_block = kBlockSize * 8;
    blk_id_t block_id_in_bitmap = block_id / slot_per_block;
    int slot_id_in_bitmap = block_id % slot_per_block;
    rt_assert(block_id_in_bitmap < num_blocks, "block_id out of bitmap range");
    if (dev->read(start_block_id + block_id_in_bitmap, buf) != kSuccess) {
        DLOG(WARNING) << "bitmap read " << start_block_id + block_id_in_bitmap << " failed";
        return kFail;
    }
    auto sz = sizeof(uint64_t);
    auto p = (uint64_t *)(buf->data);
    p[slot_id_in_bitmap / sz] &= ~(1 << (slot_id_in_bitmap % sz));
    if (dev->write(start_block_id + block_id_in_bitmap, buf) != kSuccess) {
        DLOG(WARNING) << "bitmap write " << start_block_id + block_id_in_bitmap << " failed";
        return kFail;
    }
    return kSuccess;
}