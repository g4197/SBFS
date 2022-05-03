#include "fs_layout.h"
using namespace std;
using namespace sbfs;

DiskInode::DiskInode(DiskInodeType type_) {
    memset(this, 0, sizeof(DiskInode));
    type = type_;
    // TODO update trivial metadata
}

constexpr int INODE_DIRECT_COUNT = kInodeDirectCnt;
constexpr int INODE_INDIRECT_COUNT = kBlockSize / sizeof(uint32_t);
constexpr int MAX_BLOCK_SIZE = INODE_DIRECT_COUNT + INODE_INDIRECT_COUNT + INODE_INDIRECT_COUNT * INODE_INDIRECT_COUNT;
constexpr int MAX_FILE_SIZE = MAX_BLOCK_SIZE * kBlockSize;

blk_id_t DiskInode::block_id(uint32_t inner_id, BlockDevice *dev) {
    if (inner_id >= MAX_BLOCK_SIZE) {
        // rt_assert(inner_id < MAX_BLOCK_SIZE, "inner_id out of range, max file size exceeded");
        return kFail;
    }
    auto buf = new Block;
    if (inner_id < INODE_DIRECT_COUNT) {
        delete buf;
        return direct[inner_id];
    } else if (inner_id < INODE_DIRECT_COUNT + INODE_INDIRECT_COUNT) {
        int idx = inner_id - INODE_DIRECT_COUNT;
        if (dev->read(indirect1, buf) != kSuccess) {
            DLOG(ERROR) << "read indirect1 block " << indirect1 << " failed";
            return kFail;
        }
        auto p = (uint32_t *)(buf->data);
        auto ret = p[idx];
        delete buf;
        return ret;
    } else {
        int idx = inner_id - INODE_DIRECT_COUNT - INODE_INDIRECT_COUNT;
        int blk = idx / INODE_INDIRECT_COUNT;
        if (dev->read(indirect2, buf) != kSuccess) {
            DLOG(ERROR) << "read indirect2 block " << indirect2 << " failed";
            return kFail;
        }
        auto *data = (uint32_t *)buf->data;
        auto blk_id = data[blk];
        if (dev->read(blk_id, buf) != kSuccess) {
            DLOG(ERROR) << "read second level of indirect2 block " << blk_id << " failed";
            return kFail;
        }
        data = (uint32_t *)buf->data;
        auto ret_id = data[idx % INODE_INDIRECT_COUNT];
        delete buf;

        return ret_id;
    }
    DLOG(ERROR) << "should never reach here";
    return kFail;
}

static uint32_t data_blocks(uint32_t size) {
    return (size + kBlockSize - 1) / kBlockSize;
}

uint32_t DiskInode::total_blocks(uint32_t size) {
    rt_assert(size <= MAX_FILE_SIZE, "max file size exceeded");
    auto data = data_blocks(size);
    if (data < INODE_DIRECT_COUNT)  // only direct is enough
    {
        return data;
    }
    data -= INODE_DIRECT_COUNT;
    if (data < INODE_INDIRECT_COUNT)  // need indirect1
    {
        return data + 1;
    }
    data -= INODE_INDIRECT_COUNT;
    return data + 1 + (data / INODE_INDIRECT_COUNT);  // need indirect2
}

// todo: write back all at once to reduce overhead
int DiskInode::increase(
    int old_blocks, int old_data_blocks, int new_blocks, int new_data_blocks, BlockDevice *dev, Bitmap *data_bitmap) {
    int new_direct_blocks = new_blocks - old_blocks - (new_data_blocks - old_data_blocks);
    rt_assert(new_direct_blocks >= 0, "new direct blocks should be non-negative");

    // we will do this in three steps

    // step one, increase direct blocks
    for (int i = old_data_blocks; i < min(new_data_blocks, INODE_DIRECT_COUNT); i++) {
        blk_id_t new_blk = data_bitmap->alloc(dev);
        if (new_blk == kFail) {
            DLOG(ERROR) << "alloc data bitmap failed at increase 1";
            return kFail;
        }
        direct[i] = new_blk;
    }

    // step two, increase indirect1 blocks
    if (!indirect1) {
        indirect1 = data_bitmap->alloc(dev);
        if (indirect1 == kFail) {
            DLOG(ERROR) << "alloc indirect1 bitmap failed at increase 2";
            return kFail;
        }
    }
    auto ind1 = new Block;
    if (dev->read(indirect1, ind1) != kSuccess) {
        DLOG(ERROR) << "read indirect1 block " << indirect1 << " failed at increase 2";
        return kFail;
    }
    auto p = (uint32_t *)(ind1->data);
    for (int i = max(old_data_blocks, INODE_DIRECT_COUNT);
         i < min(new_data_blocks, INODE_DIRECT_COUNT + INODE_INDIRECT_COUNT);
         i++) {
        blk_id_t new_blk = data_bitmap->alloc(dev);
        if (new_blk == kFail) {
            DLOG(ERROR) << "alloc data bitmap failed at increase 2";
            return kFail;
        }
        p[i - INODE_DIRECT_COUNT] = new_blk;
    }
    delete ind1;

    // step three, increase indirect2 blocks

    if (!indirect2) {
        indirect2 = data_bitmap->alloc(dev);
        if (indirect2 == kFail) {
            DLOG(ERROR) << "alloc indirect2 bitmap failed at increase 3";
            return kFail;
        }
    }
    auto ind2 = new Block;
    if (dev->read(indirect2, ind2) != kSuccess) {
        DLOG(ERROR) << "read indirect2 block " << indirect2 << " failed at increase 3";
        return kFail;
    }
    p = (uint32_t *)(ind2->data);
    int old_idx_limit = old_data_blocks - INODE_DIRECT_COUNT - INODE_INDIRECT_COUNT;
    int blk_limit = old_idx_limit / INODE_INDIRECT_COUNT;
    for (int i = max(old_data_blocks, INODE_DIRECT_COUNT + INODE_INDIRECT_COUNT); i < new_data_blocks; i++) {
        int j = i - INODE_DIRECT_COUNT - INODE_INDIRECT_COUNT;
        int blk = j / INODE_INDIRECT_COUNT;
        // at indirect2, we have to allocate a new indirect1 block that is not previously allocated
        if (blk > blk_limit) {
            blk_id_t new_ind1 = data_bitmap->alloc(dev);
            if (new_ind1 == kFail) {
                DLOG(ERROR) << "alloc indirect1 bitmap failed at increase 3";
                return kFail;
            }
            p[blk] = new_ind1;
            ++blk_limit;
            // todo : we may write back all at once, which might reduce overhead
            if (dev->write(indirect2, ind2) != kSuccess) {
                DLOG(ERROR) << "write indirect2 block " << indirect2 << " failed at increase 3";
                return kFail;
            }
        }
        auto ind21 = new Block;
        if (dev->read(p[blk], ind21) != kSuccess) {
            DLOG(ERROR) << "read indirect1 block " << p[blk] << " failed at increase 3";
            return kFail;
        }

        blk_id_t new_blk = data_bitmap->alloc(dev);
        if (new_blk == kFail) {
            DLOG(ERROR) << "alloc data bitmap failed at increase 3";
            return kFail;
        }

        auto p2 = (uint32_t *)(ind21->data);
        p2[j % INODE_INDIRECT_COUNT] = new_blk;
        if (dev->write(p[blk], ind21) != kSuccess) {
            DLOG(ERROR) << "write indirect1 block " << p[blk] << " failed at increase 3";
            return kFail;
        }
    }
    return kSuccess;
}

int DiskInode::decrease(
    int old_blocks, int old_data_blocks, int new_blocks, int new_data_blocks, BlockDevice *dev, Bitmap *data_bitmap) {
    int new_direct_blocks = new_blocks - old_blocks - (new_data_blocks - old_data_blocks);
    rt_assert(new_direct_blocks <= 0, "new direct blocks should be non-positive");

    // we will do this in three steps

    // step one, decrease direct blocks
    for (int i = new_data_blocks; i < min(old_data_blocks, INODE_DIRECT_COUNT); i++) {
        auto ret = data_bitmap->free(direct[i], dev);
        direct[i] = -1;  // set to -1 is unnecessary, but for better debugging
                         // since this will make sbfs crash faster
        if (ret != kSuccess) {
            DLOG(ERROR) << "free data bitmap failed at decrease 1";
            return kFail;
        }
    }

    // step two, decrease indirect1 blocks
    if (indirect1 != 0) {
        auto ind1 = new Block;
        if (dev->read(indirect1, ind1) != kSuccess) {
            DLOG(ERROR) << "read indirect1 block " << indirect1 << " failed at decrease 2";
            return kFail;
        }
        auto p = (uint32_t *)(ind1->data);
        for (int i = max(new_data_blocks, INODE_DIRECT_COUNT);
             i < min(old_data_blocks, INODE_DIRECT_COUNT + INODE_INDIRECT_COUNT);
             i++) {
            auto ret = data_bitmap->free(p[i - INODE_DIRECT_COUNT], dev);
            if (ret != kSuccess) {
                DLOG(ERROR) << "free data bitmap failed at decrease 2";
                return kFail;
            }
            p[i - INODE_DIRECT_COUNT] = -1;
        }
        if (dev->write(indirect1, ind1) != kSuccess) {
            DLOG(ERROR) << "write indirect1 block " << indirect1 << " failed at decrease 2";
            return kFail;
        }
        delete ind1;
    }

    // step three, decrease indirect2 blocks
    if (indirect2 != 0) {
        auto ind2 = new Block;
        if (dev->read(indirect2, ind2) != kSuccess) {
            DLOG(ERROR) << "read indirect2 block " << indirect2 << " failed at decrease 3";
            return kFail;
        }
        auto p = (uint32_t *)(ind2->data);
        int old_idx_limit = old_data_blocks - INODE_DIRECT_COUNT - INODE_INDIRECT_COUNT;
        int blk_limit = old_idx_limit / INODE_INDIRECT_COUNT;

        // note that we preserve that allocated indirect1 blocks
        // we only free data blocks
        for (int i = max(new_data_blocks, INODE_DIRECT_COUNT + INODE_INDIRECT_COUNT); i < old_data_blocks; i++) {
            int j = i - INODE_DIRECT_COUNT - INODE_INDIRECT_COUNT;
            int blk = j / INODE_INDIRECT_COUNT;
            // need to focus on detail here
            if (i == max(new_data_blocks, INODE_DIRECT_COUNT + INODE_INDIRECT_COUNT) || j % INODE_INDIRECT_COUNT == 0) {
                auto ind1 = new Block;
                if (dev->read(p[blk], ind1) != kSuccess) {
                    DLOG(ERROR) << "read indirect1 block " << p[blk] << " failed at decrease 3";
                    return kFail;
                }
                auto p2 = (uint32_t *)(ind1->data);
                for (int k = 0; k < INODE_INDIRECT_COUNT; k++) {
                    if (k + i >= old_data_blocks)
                        break;
                    if ((k + j) / INODE_INDIRECT_COUNT != blk)  // when j is not a multiple of INODE_INDIRECT_COUNT
                        break;
                    auto ret = data_bitmap->free(p2[k], dev);
                    if (ret != kSuccess) {
                        DLOG(ERROR) << "free data bitmap failed at decrease 3";
                        return kFail;
                    }
                }

                // we don't need to write back the indirect1 block
                // since those old data should not be accessed
                // again, set to -1 is unnecessary, but in this case, we better not
                // for marginal condition is too complex
                // and we don't need to write back the indirect2 block as well
            }
        }
    }
    return kSuccess;
}

int DiskInode::resize(uint32_t new_size, Bitmap *data_bitmap, BlockDevice *dev) {
    auto old_size = size;
    size = new_size;
    auto old_data_blocks = data_blocks(old_size);
    auto new_data_blocks = data_blocks(size);
    auto old_blocks = total_blocks(old_size);
    auto new_blocks = total_blocks(size);
    if (old_blocks == new_blocks)
        return kSuccess;
    if (old_blocks > new_blocks) {
        return decrease(old_blocks, old_data_blocks, new_blocks, new_data_blocks, dev, data_bitmap);
    } else {
        return increase(old_blocks, old_data_blocks, new_blocks, new_data_blocks, dev, data_bitmap);
    }
}