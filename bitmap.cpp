#include "fs_layout.h"

using namespace std;
using namespace sbfs;

/**
 * @brief Get the leading zero of a int64
 * should change to some build-in function later
 * src:https://stackoverflow.com/questions/6234533/how-to-find-the-leading-number-of-zeros-in-a-number-using-c
 * @param x
 * @return int the position of leading zero
 */
int nlz(uint64_t x)
{
    uint64_t y;
    int n = 64, c = 32;
    do
    {
        y = x >> c;
        if (y)
        {
            n = n - c;
            x = y;
        }
        c = c >> 1;
    } while (c != 0);
    return n - x;
}

Bitmap::Bitmap(blk_id_t start_block_id, blk_id_t num_blocks, blk_id_t data_segment_offset)
    : start_block_id(start_block_id), num_blocks(num_blocks), data_segment_offset(data_segment_offset)
{
    // init();
}

blk_id_t Bitmap::alloc(BlockDevice *dev)
{
    auto buf = new Block;
    int slot_per_block = kBlockSize * 8;
    for (blk_id_t i = 0; i < num_blocks; i++)
    {
        if (dev->read(start_block_id + i, buf) != kSuccess)
        {
            DLOG(ERROR) << "bitmap read " << start_block_id + i << " failed";
            return kFail;
        }
        auto sz = sizeof(uint64_t);
        auto p = static_cast<uint64_t *>(buf->data);
        for (int j = 0; j < kBlockSize / sz; j++)
        {
            int k = nlz(p[j]);
            if (k != 64)
            {
                p[j] |= (1 << k);
                if (dev->write(start_block_id + i, buf) != kSuccess)
                {
                    DLOG(ERROR) << "bitmap write " << start_block_id + i << " failed";
                    return kFail;
                }
                return i * slot_per_block + j * sz + k + data_segment_offset;
            }
        }
    }
    DLOG(ERROR) << "bitmap alloc failed: no empty block found";
    return kFail;
}

int Bitmap::free(BlockDevice *dev, blk_id_t block_id)
{
    block_id -= data_segment_offset;
    auto buf = new Block;
    int slot_per_block = kBlockSize * 8;
    blk_id_t block_id_in_bitmap = block_id / slot_per_block;
    int slot_id_in_bitmap = block_id % slot_per_block;
    rt_assert(block_id_in_bitmap < num_blocks, "block_id out of bitmap range");
    if (dev->read(start_block_id + block_id_in_bitmap, buf) != kSuccess)
    {
        DLOG(ERROR) << "bitmap read " << start_block_id + block_id_in_bitmap << " failed";
        return kFail;
    }
    auto sz = sizeof(uint64_t);
    auto p = static_cast<uint64_t *>(buf->data);
    p[slot_id_in_bitmap / sz] &= ~(1 << (slot_id_in_bitmap % sz));
    if (dev->write(start_block_id + block_id_in_bitmap, buf) != kSuccess)
    {
        DLOG(ERROR) << "bitmap write " << start_block_id + block_id_in_bitmap << " failed";
        return kFail;
    }
    return kSuccess;
}