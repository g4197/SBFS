#include <sys/stat.h>

#include "fs_layout.h"
#include "inode.h"
using namespace std;
using namespace sbfs;

DiskInode::DiskInode(DiskInodeType type_) {
    memset(this, 0, sizeof(DiskInode));
    type = type_;
    access_time = time(nullptr);
    modify_time = time(nullptr);
    change_time = time(nullptr);
    uid = getuid();
    gid = getgid();
    mode = type_ == kDirectory ? S_IFDIR : S_IFREG;
    link_cnt = 1;
    // TODO update trivial metadata
}

constexpr int INODE_DIRECT_COUNT = kInodeDirectCnt;
constexpr int INODE_INDIRECT_COUNT = kBlockSize / sizeof(uint32_t);
constexpr uint32_t MAX_BLOCK_SIZE =
    INODE_DIRECT_COUNT + INODE_INDIRECT_COUNT + INODE_INDIRECT_COUNT * INODE_INDIRECT_COUNT;
constexpr uint32_t MAX_FILE_SIZE = min((uint64_t)UINT32_MAX, ((uint64_t)MAX_BLOCK_SIZE) * kBlockSize);

blk_id_t DiskInode::block_id(uint32_t inner_id, BlockDevice *dev) {
    if (inner_id >= MAX_BLOCK_SIZE) {
        // rt_assert(inner_id < MAX_BLOCK_SIZE, "inner_id out of range, max file size exceeded");
        return kFail;
    }
    Block buf;
    if (inner_id < INODE_DIRECT_COUNT) {
        return direct[inner_id];
    } else if (inner_id < INODE_DIRECT_COUNT + INODE_INDIRECT_COUNT) {
        int idx = inner_id - INODE_DIRECT_COUNT;
        if (dev->read(indirect1, &buf) != kSuccess) {
            DLOG(WARNING) << "read indirect1 block " << indirect1 << " failed";
            return kFail;
        }
        auto p = (uint32_t *)(buf.data);
        auto ret = p[idx];
        DLOG(WARNING) << "indirect1 block " << indirect1 << " offset " << idx << " ret " << ret;
        return ret;
    } else {
        int idx = inner_id - INODE_DIRECT_COUNT - INODE_INDIRECT_COUNT;
        int blk = idx / INODE_INDIRECT_COUNT;
        if (dev->read(indirect2, &buf) != kSuccess) {
            DLOG(WARNING) << "read indirect2 block " << indirect2 << " failed";
            return kFail;
        }
        auto data = (uint32_t *)buf.data;
        auto blk_id = data[blk];
        Block buf2;
        if (dev->read(blk_id, &buf2) != kSuccess) {
            DLOG(WARNING) << "read second level of indirect2 block " << blk_id << " failed";
            return kFail;
        }
        auto data2 = (uint32_t *)buf2.data;
        auto ret_id = data2[idx % INODE_INDIRECT_COUNT];
        DLOG(WARNING) << "inner id" << inner_id << "indirect2" << indirect2 << " at offset\n\t " << blk << " indirect1 "
                      << blk_id << " at offset\n\t " << idx % INODE_INDIRECT_COUNT << " ret= " << ret_id;
        return ret_id;
    }
    DLOG(WARNING) << "should never reach here";
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
    if (data - INODE_DIRECT_COUNT < INODE_INDIRECT_COUNT)  // need indirect1
    {
        return data + 1;
    }
    auto rest = data - INODE_DIRECT_COUNT - INODE_INDIRECT_COUNT;
    // data for all data blocks
    // +1 for indirect1
    // +1 for indirect2
    // (rest+ ...) / .. for indirect21
    return data + 2 + ((rest + INODE_INDIRECT_COUNT - 1) / INODE_INDIRECT_COUNT);  // need indirect2
}

// todo: write back all at once to reduce overhead
int DiskInode::increase(int old_blocks, int old_data_blocks, int new_blocks, int new_data_blocks, BlockDevice *dev,
                        Bitmap *data_bitmap) {
    int new_direct_blocks = new_blocks - old_blocks - (new_data_blocks - old_data_blocks);
    rt_assert(new_direct_blocks >= 0, "new direct blocks should be non-negative");

    // we will do this in three steps

    // step one, increase direct blocks
    for (int i = old_data_blocks; i < min(new_data_blocks, INODE_DIRECT_COUNT); i++) {
        blk_id_t new_blk = data_bitmap->alloc(dev);
        if (new_blk == kFail) {
            DLOG(WARNING) << "alloc data bitmap failed at increase 1";
            return kFail;
        }
        direct[i] = new_blk;
    }

    // step two, increase indirect1 blocks
    if (new_data_blocks > INODE_DIRECT_COUNT) {
        if (!indirect1) {
            indirect1 = data_bitmap->alloc(dev);
            if (indirect1 == kFail) {
                DLOG(WARNING) << "alloc indirect1 bitmap failed at increase 2";
                return kFail;
            }
        }
        Block ind1;
        if (dev->read(indirect1, &ind1) != kSuccess) {
            DLOG(WARNING) << "read indirect1 block " << indirect1 << " failed at increase 2";
            return kFail;
        }
        auto p = (uint32_t *)(ind1.data);
        for (int i = max(old_data_blocks, INODE_DIRECT_COUNT);
             i < min(new_data_blocks, INODE_DIRECT_COUNT + INODE_INDIRECT_COUNT); i++) {
            blk_id_t new_blk = data_bitmap->alloc(dev);
            DLOG(WARNING) << "alloc indirect1 " << new_blk;
            if (new_blk == kFail) {
                DLOG(WARNING) << "alloc data bitmap failed at increase 2";
                return kFail;
            }
            p[i - INODE_DIRECT_COUNT] = new_blk;
        }
        if (dev->write(indirect1, &ind1) != kSuccess) {
            DLOG(WARNING) << "write indirect1 block " << indirect1 << " failed at increase 2";
            return kFail;
        }
    }
    // step three, increase indirect2 blocks

    if (new_data_blocks > INODE_DIRECT_COUNT + INODE_INDIRECT_COUNT) {
        if (!indirect2) {
            indirect2 = data_bitmap->alloc(dev);
            if (indirect2 == kFail) {
                DLOG(WARNING) << "alloc indirect2 bitmap failed at increase 3";
                return kFail;
            }
        }
        Block ind2;
        if (dev->read(indirect2, &ind2) != kSuccess) {
            DLOG(WARNING) << "read indirect2 block " << indirect2 << " failed at increase 3";
            return kFail;
        }
        auto p = (uint32_t *)(ind2.data);
        int old_idx_limit = old_data_blocks - INODE_DIRECT_COUNT - INODE_INDIRECT_COUNT - 1;
        int blk_limit = old_idx_limit / INODE_INDIRECT_COUNT;
        if (old_idx_limit < 0) blk_limit = -1;
        DLOG(WARNING) << "increase indirect2 "
                      << " old idx " << old_idx_limit << " blk " << blk_limit << endl;
        for (int i = max(old_data_blocks, INODE_DIRECT_COUNT + INODE_INDIRECT_COUNT); i < new_data_blocks; i++) {
            int j = i - INODE_DIRECT_COUNT - INODE_INDIRECT_COUNT;
            int blk = j / INODE_INDIRECT_COUNT;
            // at indirect2, we have to allocate a new indirect1 block that is not previously allocated
            if (blk > blk_limit) {
                blk_id_t new_ind1 = data_bitmap->alloc(dev);
                DLOG(WARNING) << "alloc indirect2->1 " << new_ind1;

                if (new_ind1 == kFail) {
                    DLOG(WARNING) << "alloc indirect1 bitmap failed at increase 3";
                    return kFail;
                }
                p[blk] = new_ind1;
                ++blk_limit;
                // todo : we may write back all at once, which might reduce overhead
                if (dev->write(indirect2, &ind2) != kSuccess) {
                    DLOG(WARNING) << "write indirect2 block " << indirect2 << " failed at increase 3";
                    return kFail;
                }
            }
            Block ind21;
            if (dev->read(p[blk], &ind21) != kSuccess) {
                DLOG(WARNING) << "read indirect1 block " << p[blk] << " failed at increase 3";
                return kFail;
            }

            blk_id_t new_blk = data_bitmap->alloc(dev);
            DLOG(WARNING) << "alloc new block at ind2 " << j << " from indirect21 " << blk << " ret = " << new_blk;
            if (new_blk == kFail) {
                DLOG(WARNING) << "alloc data bitmap failed at increase 3";
                return kFail;
            }

            auto p2 = (uint32_t *)(ind21.data);
            p2[j % INODE_INDIRECT_COUNT] = new_blk;
            if (dev->write(p[blk], &ind21) != kSuccess) {
                DLOG(WARNING) << "write indirect1 block " << p[blk] << " failed at increase 3";
                return kFail;
            }
        }
    }
    return kSuccess;
}

int DiskInode::decrease(int old_blocks, int old_data_blocks, int new_blocks, int new_data_blocks, BlockDevice *dev,
                        Bitmap *data_bitmap, const Inode *inode) {
    int new_direct_blocks = new_blocks - old_blocks - (new_data_blocks - old_data_blocks);
    rt_assert(new_direct_blocks <= 0, "new direct blocks should be non-positive");
    // we will do this in three steps

    vector<int> bitmap_to_free;  // for consistency issue,
                                 // we must free bitmap only after the data of inode has been write_back

    // step one, decrease direct blocks
    for (int i = new_data_blocks; i < min(old_data_blocks, INODE_DIRECT_COUNT); i++) {
        bitmap_to_free.push_back(direct[i]);
        // auto ret = data_bitmap->free(direct[i], dev);
        direct[i] = -1;  // set to -1 is unnecessary, but for better debugging
                         // since this will make sbfs crash faster
        // if (ret != kSuccess) {
        //     DLOG(WARNING) << "free data bitmap failed at decrease 1";
        //     return kFail;
        // }
    }

    // step two, decrease indirect1 blocks
    if (indirect1 != 0) {
        Block ind1;
        if (dev->read(indirect1, &ind1) != kSuccess) {
            DLOG(WARNING) << "read indirect1 block " << indirect1 << " failed at decrease 2";
            return kFail;
        }
        auto p = (uint32_t *)(ind1.data);
        for (int i = max(new_data_blocks, INODE_DIRECT_COUNT);
             i < min(old_data_blocks, INODE_DIRECT_COUNT + INODE_INDIRECT_COUNT); i++) {
            bitmap_to_free.push_back(p[i - INODE_DIRECT_COUNT]);
            // auto ret = data_bitmap->free(p[i - INODE_DIRECT_COUNT], dev);
            // if (ret != kSuccess) {
            //     DLOG(WARNING) << "free data bitmap failed at decrease 2";
            //     return kFail;
            // }
            p[i - INODE_DIRECT_COUNT] = -1;
        }
        if (new_data_blocks <= INODE_DIRECT_COUNT) {
            bitmap_to_free.push_back(indirect1);
            // auto ret = data_bitmap->free(indirect1, dev);
            // if (ret != kSuccess) {
            //     DLOG(WARNING) << "free indirect1 bitmap failed at decrease 2";
            //     return kFail;
            // }
            indirect1 = 0;
        } else {
            if (dev->write(indirect1, &ind1) != kSuccess) {
                DLOG(WARNING) << "write indirect1 block " << indirect1 << " failed at decrease 2";
                return kFail;
            }
        }
    }

    // step three, decrease indirect2 blocks
    if (indirect2 != 0) {
        // the overall idea is simple
        // iterate through indirect2, and for each indirect1 block,
        // iterate through indirect1, and free the corresponding data block
        // if the indirect1 block is empty, free the indirect1 block
        // if the indirect1 block is not empty, write back the indirect1 block
        // but the boundary cases are very complex,  we should be careful
        int ri = new_data_blocks - INODE_DIRECT_COUNT - INODE_INDIRECT_COUNT - 1;
        int rj = old_data_blocks - INODE_DIRECT_COUNT - INODE_INDIRECT_COUNT - 1;
        pair<int, int> posi = make_pair(ri / INODE_INDIRECT_COUNT, ri % INODE_INDIRECT_COUNT),
                       posj = make_pair(rj / INODE_INDIRECT_COUNT, rj % INODE_INDIRECT_COUNT);
        if (ri < 0 && rj >= 0) {  // the case where indirect2 should be freed
            Block ind2;
            if (dev->read(indirect2, &ind2) != kSuccess) {
                DLOG(WARNING) << "read indirect2 block " << indirect2 << " failed at decrease 3";
                return kFail;
            }
            auto p = (uint32_t *)(ind2.data);
            for (int i = 0; i <= posj.first; i++) {
                Block ind1;
                if (dev->read(p[i], &ind1) != kSuccess) {
                    DLOG(WARNING) << "read indirect1 block " << p[i] << " failed at decrease 3";
                    return kFail;
                }
                auto p2 = (uint32_t *)(ind1.data);
                if (i != posj.first) {
                    for (int j = 0; j < INODE_INDIRECT_COUNT; ++j) {
                        bitmap_to_free.push_back(p2[j]);
                        // auto ret = data_bitmap->free(p2[j], dev);
                        // if (ret != kSuccess) {
                        //     DLOG(WARNING) << "free data bitmap failed at decrease 3";
                        //     return kFail;
                        // }
                        p2[j] = -1;
                    }
                } else {
                    for (int j = 0; j <= posj.second; ++j) {
                        bitmap_to_free.push_back(p2[j]);
                        // auto ret = data_bitmap->free(p2[j], dev);
                        // if (ret != kSuccess) {
                        //     DLOG(WARNING) << "free data bitmap failed at decrease 3";
                        //     return kFail;
                        // }
                        p2[j] = -1;
                    }
                }
                bitmap_to_free.push_back(p[i]);
                // auto ret = data_bitmap->free(p[i], dev);
                // if (ret != kSuccess) {
                //     DLOG(WARNING) << "free direct1 failed at decrease 3";
                //     return kFail;
                // }
            }
            bitmap_to_free.push_back(indirect2);
            // auto ret = data_bitmap->free(indirect2, dev);
            // if (ret != kSuccess) {
            //     DLOG(WARNING) << "free indirect2 failed at decrease 3";
            //     return kFail;
            // }
            indirect2 = 0;
        } else if (ri >= 0 && rj >= 0) {  // the case where indirect2 should be updated
            Block ind2;
            if (dev->read(indirect2, &ind2) != kSuccess) {
                DLOG(WARNING) << "read indirect2 block " << indirect2 << " failed at decrease 3";
                return kFail;
            }
            auto p = (uint32_t *)(ind2.data);
            for (int i = posi.first; i <= posj.first; i++) {
                Block ind1;
                if (dev->read(p[i], &ind1) != kSuccess) {
                    DLOG(WARNING) << "read indirect1 block " << p[i] << " failed at decrease 3";
                    return kFail;
                }
                auto p2 = (uint32_t *)(ind1.data);
                if (i != posi.first && i != posj.first) {
                    for (int j = 0; j < INODE_INDIRECT_COUNT; ++j) {
                        bitmap_to_free.push_back(p2[j]);
                        // auto ret = data_bitmap->free(p2[j], dev);
                        // if (ret != kSuccess) {
                        //     DLOG(WARNING) << "free data bitmap failed at decrease 3";
                        //     return kFail;
                        // }
                        p2[j] = -1;
                    }
                    bitmap_to_free.push_back(p[i]);
                    // auto ret = data_bitmap->free(p[i], dev);
                    // if (ret != kSuccess) {
                    //     DLOG(WARNING) << "free direct1 failed at decrease 3";
                    //     return kFail;
                    // }
                } else if (i == posi.first && i != posj.first) {
                    for (int j = posi.second + 1; j < INODE_INDIRECT_COUNT; ++j) {
                        bitmap_to_free.push_back(p2[j]);
                        // auto ret = data_bitmap->free(p2[j], dev);
                        // if (ret != kSuccess) {
                        //     DLOG(WARNING) << "free data bitmap failed at decrease 3";
                        //     return kFail;
                        // }
                        p2[j] = -1;
                    }
                    if (dev->write(i, &ind1) != kSuccess) {
                        DLOG(WARNING) << "write indirect1 block " << i << " failed at decrease 3";
                        return kFail;
                    }
                } else if (i == posj.first && i != posi.first) {
                    for (int j = 0; j <= posj.second; ++j) {
                        bitmap_to_free.push_back(p2[j]);
                        // auto ret = data_bitmap->free(p2[j], dev);
                        // if (ret != kSuccess) {
                        //     DLOG(WARNING) << "free data bitmap failed at decrease 3";
                        //     return kFail;
                        // }
                        p2[j] = -1;
                    }
                    bitmap_to_free.push_back(p[i]);
                    // auto ret = data_bitmap->free(p[i], dev);
                    // if (ret != kSuccess) {
                    //     DLOG(WARNING) << "free direct1 failed at decrease 3";
                    //     return kFail;
                    // }
                } else if (i == posi.first && i == posj.first) {
                    for (int j = posi.second + 1; j <= posj.second; ++j) {
                        bitmap_to_free.push_back(p2[j]);
                        // auto ret = data_bitmap->free(p2[j], dev);
                        // if (ret != kSuccess) {
                        //     DLOG(WARNING) << "free data bitmap failed at decrease 3";
                        //     return kFail;
                        // }
                        p2[j] = -1;
                    }
                    if (dev->write(i, &ind1) != kSuccess) {
                        DLOG(WARNING) << "write indirect1 block " << i << " failed at decrease 3";
                        return kFail;
                    }
                }
            }
            if (dev->write(indirect2, &ind2) != kSuccess) {
                DLOG(WARNING) << "write indirect2 block " << indirect2 << " failed at decrease 3";
                return kFail;
            }
        } else {
            rt_assert(false, "should not be here");
        }
    }
    if (inode != nullptr) {
        inode->write_inode(this);
    }
    for (auto i : bitmap_to_free) {
        auto ret = data_bitmap->free(i, dev);
        if (ret != kSuccess) {
            DLOG(WARNING) << "free data bitmap failed at decrease";
            return kFail;
        }
    }
    return kSuccess;
}

int DiskInode::resize(uint32_t new_size, Bitmap *data_bitmap, BlockDevice *dev, const Inode *inode) {
    update_meta(7);
    // if (new_size == 0) return clear(data_bitmap, dev);
    auto old_size = size;
    size = new_size;
    auto old_data_blocks = data_blocks(old_size);
    auto new_data_blocks = data_blocks(size);
    auto old_blocks = total_blocks(old_size);
    auto new_blocks = total_blocks(size);
    DLOG(WARNING) << "old_size: " << old_size << " new_size: " << new_size << " old_blocks: " << old_blocks
                  << " new_blocks: " << new_blocks << " old_data_blocks: " << old_data_blocks
                  << " new_data_blocks: " << new_data_blocks;
    if (old_blocks == new_blocks) return kSuccess;
    if (old_blocks > new_blocks) {
        return decrease(old_blocks, old_data_blocks, new_blocks, new_data_blocks, dev, data_bitmap, inode);
    } else {
        return increase(old_blocks, old_data_blocks, new_blocks, new_data_blocks, dev, data_bitmap);
    }
}

int DiskInode::clear(Bitmap *data_bitmap, BlockDevice *dev) {
    int data_blks = data_blocks(size);
    int blks = total_blocks(size);
    for (int i = 0; i < min(data_blks, INODE_DIRECT_COUNT); ++i) {
        auto ret = data_bitmap->free(direct[i], dev);
        if (ret != kSuccess) {
            DLOG(WARNING) << "free data bitmap failed at clear";
            return kFail;
        }
        direct[i] = -1;
    }
    if (indirect1 != 0 && data_blks > INODE_INDIRECT_COUNT) {  // redundant condition
        Block ind;
        if (dev->read(indirect1, &ind) != kSuccess) {
            DLOG(WARNING) << "read indirect block " << indirect1 << " failed at clear";
            return kFail;
        }
        auto p = (uint32_t *)(ind.data);
        for (int i = INODE_DIRECT_COUNT; i < min(data_blks, INODE_DIRECT_COUNT + INODE_INDIRECT_COUNT); ++i) {
            auto ret = data_bitmap->free(p[i - INODE_DIRECT_COUNT], dev);
            if (ret != kSuccess) {
                DLOG(WARNING) << "free data bitmap failed at clear";
                return kFail;
            }
        }
        auto ret = data_bitmap->free(indirect1, dev);
        if (ret != kSuccess) {
            DLOG(WARNING) << "free indirect1 bitmap failed at clear";
            return kFail;
        }
    }
    if (indirect2 != 0 && data_blks > INODE_DIRECT_COUNT + INODE_INDIRECT_COUNT) {
        Block ind2;
        if (dev->read(indirect2, &ind2) != kSuccess) {
            DLOG(WARNING) << "read indirect2 block " << indirect2 << " failed at clear";
            return kFail;
        }
        auto p2 = (uint32_t *)(ind2.data);
        int rest = data_blks - INODE_DIRECT_COUNT - INODE_INDIRECT_COUNT;
        for (int i = 0; i < rest; i += INODE_INDIRECT_COUNT) {
            Block ind1;
            if (dev->read(p2[i], &ind1) != kSuccess) {
                DLOG(WARNING) << "read indirect1 block " << p2[i] << " failed at clear";
                return kFail;
            }
            auto p = (uint32_t *)(ind1.data);
            for (int j = 0; j < INODE_INDIRECT_COUNT && i + j < rest; ++j) {
                auto ret = data_bitmap->free(p[j], dev);
                if (ret != kSuccess) {
                    DLOG(WARNING) << "free data bitmap failed at clear";
                    return kFail;
                }
            }
            auto ret = data_bitmap->free(p2[i], dev);
            if (ret != kSuccess) {
                DLOG(WARNING) << "free indirect1 bitmap failed at clear";
                return kFail;
            }
        }
        auto ret = data_bitmap->free(indirect2, dev);
        if (ret != kSuccess) {
            DLOG(WARNING) << "free indirect2 bitmap failed at clear";
            return kFail;
        }
    }
    // todo release inode, but we don't have the access here
    // api change required
    return kSuccess;
}

int DiskInode::read_data(uint32_t offset, uint8_t *buf, uint32_t len, BlockDevice *dev) {
    update_meta(1);

    if (len == 0) return kSuccess;
    if (offset > size) {
        DLOG(WARNING) << "read data offset out of range";
        return kFail;
    }
    if (offset + len >= size) len = size - offset;
    int l = offset, r = offset + len;
    int lid = l / kBlockSize, rid = r / kBlockSize;
    int loff = l % kBlockSize, roff = r % kBlockSize;
    rt_assert(lid <= rid, "lid should be <= rid");
    Block data;

    if (lid == rid) {
        auto blk = block_id(lid, dev);
        if (dev->read(blk, &data) != kSuccess) {
            DLOG(WARNING) << "read block " << blk << " failed at read_data";
            return kFail;
        }
        DLOG(WARNING) << "read block to buf from " << blk << " offset " << loff << " len " << roff - loff;
        memcpy(buf, data.data + loff, roff - loff);
    } else {
        auto lblk = block_id(lid, dev);
        int bufoffset = 0;
        if (dev->read(lblk, &data) != kSuccess) {
            DLOG(WARNING) << "read block " << lblk << " failed at read_data";
            return kFail;
        }
        memcpy(buf, data.data + loff, kBlockSize - loff);
        bufoffset += kBlockSize - loff;
        DLOG(WARNING) << "read block to buf from " << lblk << " offset " << loff << " len " << kBlockSize - loff;
        for (int i = lid + 1; i < rid; ++i) {
            auto blk = block_id(i, dev);
            if (dev->read(blk, &data) != kSuccess) {
                DLOG(WARNING) << "read block " << i << " failed at read_data";
                return kFail;
            }
            DLOG(WARNING) << "read block to buf + " << bufoffset << " from " << blk << " offset " << 0 << " len "
                          << kBlockSize;
            memcpy(buf + bufoffset, data.data, kBlockSize);
            bufoffset += kBlockSize;
        }
        if (roff > 0) {
            auto rblk = block_id(rid, dev);
            if (dev->read(rblk, &data) != kSuccess) {
                DLOG(WARNING) << "read block " << rblk << " failed at read_data";
                return kFail;
            }
            DLOG(WARNING) << "read block to buf + " << bufoffset << " from " << rblk << " offset " << 0 << " len "
                          << roff;
            memcpy(buf + bufoffset, data.data, roff);
        }
    }

    return len;
}

int DiskInode::write_data(uint32_t offset, const uint8_t *buf, uint32_t len, BlockDevice *dev) {
    update_meta(3);
    if (len == 0) return kSuccess;
    DLOG(WARNING) << "disk inode write data offset " << offset << " len " << len;
    if (offset > size) {
        DLOG(WARNING) << "write data out of range";
        return kFail;
    }
    if (offset + len >= size) {
        len = size - offset;
    }
    int l = offset, r = offset + len;
    int lid = l / kBlockSize, rid = r / kBlockSize;
    int loff = l % kBlockSize, roff = r % kBlockSize;
    rt_assert(lid <= rid, "lid should be <= rid");
    DLOG(WARNING) << "disk inode write data lid " << lid << " loffset " << loff << " rid " << rid << " roffset "
                  << roff;

    Block data;
    if (lid == rid) {
        auto lblk = block_id(lid, dev);
        if (dev->read(lblk, &data) != kSuccess) {
            DLOG(WARNING) << "read block " << lblk << " failed at write_data";
            return kFail;
        }
        memcpy(data.data + loff, buf, roff - loff);
        DLOG(WARNING) << "write to block " << lblk << " offset " << loff << " from buf len " << roff - loff;

        if (dev->write(lblk, &data) != kSuccess) {
            DLOG(WARNING) << "write block " << lblk << " failed at write_data";
            return kFail;
        }
    } else {
        int bufoffset = 0;
        auto lblk = block_id(lid, dev);
        if (dev->read(lblk, &data) != kSuccess) {
            DLOG(WARNING) << "read block " << lblk << " failed at write_data";
            return kFail;
        }
        memcpy(data.data + loff, buf, kBlockSize - loff);
        bufoffset += kBlockSize - loff;
        DLOG(WARNING) << "write to block " << lblk << " offset " << loff << " from buf len " << kBlockSize - loff;
        if (dev->write(lblk, &data) != kSuccess) {
            DLOG(WARNING) << "write block " << lblk << " failed at write_data";
            return kFail;
        }

        for (int i = lid + 1; i < rid; ++i) {
            memcpy(data.data, buf + bufoffset, kBlockSize);
            bufoffset += kBlockSize;
            auto blk = block_id(i, dev);
            DLOG(WARNING) << "write to block " << blk << " from buf + " << bufoffset << " len " << kBlockSize;
            if (dev->write(blk, &data) != kSuccess) {
                DLOG(WARNING) << "write block " << i << " failed at write_data";
                return kFail;
            }
        }
        if (roff > 0) {
            auto rblk = block_id(rid, dev);
            if (dev->read(rblk, &data) != kSuccess) {
                DLOG(WARNING) << "read block " << rblk << " failed at write_data";
                return kFail;
            }
            memcpy(data.data, buf + bufoffset, roff);
            DLOG(WARNING) << "write to block " << rblk << " offset " << 0 << " from buf + " << bufoffset << " len "
                          << roff;
            if (dev->write(rblk, &data) != kSuccess) {
                DLOG(WARNING) << "write block " << rblk << " failed at write_data";
                return kFail;
            }
        }
    }
    return len;
}

int DiskInode::sync_data(BlockDevice *dev, bool indirect) {
    int data_blk = data_blocks(size);
    for (int i = 0; i < data_blk; ++i) {
        if (dev->sync(block_id(i, dev)) != kSuccess) {
            DLOG(WARNING) << "sync block " << block_id(i, dev) << " failed at sync_data";
            return kFail;
        }
    }
    if (indirect) {
        if (indirect1 != 0) {
            if (dev->sync(indirect1) != kSuccess) {
                DLOG(WARNING) << "sync block " << indirect1 << " failed at sync_data";
                return kFail;
            }
        }
        if (indirect2 != 0) {
            int rest = total_blocks(size) - data_blk - 2;  // remove indirect1 and indirect2
            Block buf;
            if (dev->read(indirect2, &buf) != kSuccess) {
                DLOG(WARNING) << "read block " << indirect2 << " failed at sync_data";
                return kFail;
            }
            auto p = (uint32_t *)buf.data;
            for (int i = 0; i < rest; ++i) {
                if (dev->sync(p[i]) != kSuccess) {
                    DLOG(WARNING) << "sync block " << p[i] << " failed at sync_data";
                    return kFail;
                }
            }
            if (dev->sync(indirect2) != kSuccess) {
                DLOG(WARNING) << "sync block " << indirect2 << " failed at sync_data";
                return kFail;
            }
        }
    }
    return kSuccess;
}

void DiskInode::update_meta(int flag) {
    if (flag & 1) access_time = time(nullptr);
    if (flag & 2) modify_time = time(nullptr);
    if (flag & 4) change_time = time(nullptr);
}
