#ifndef CONFIG_H_
#define CONFIG_H_

#include <bits/stdc++.h>
#include <glog/logging.h>

// Some config of the FS.
// Only define constants or inline functions here!

inline constexpr uint64_t KB(uint64_t x) {
    return x * 1024;
}

inline constexpr uint64_t MB(uint64_t x) {
    return x * 1024 * 1024;
}

inline constexpr uint64_t GB(uint64_t x) {
    return x * 1024 * 1024 * 1024;
}

#ifndef NDEBUG
#define rt_assert(cond, msg)      \
    do {                          \
        if (!(cond)) {            \
            DLOG(ERROR) << (msg); \
        }                         \
    } while (0)
#else
#define rt_assert(cond, msg) \
    do {                     \
    } while (0)
#endif

constexpr uint64_t kBlockSize = 4096;          // block is 4kb
constexpr uint64_t kBlockCacheSize = MB(768);  // block cache

using blk_id_t = uint32_t;

constexpr int kFail = -1;  // Maybe used in all return values.
constexpr int kSuccess = 0;

// Generated by Copilot, I don't know what it means.
constexpr uint32_t kFSMagic = 0x53425355;
constexpr uint64_t kInodeDirectCnt = 23;
constexpr uint64_t kMaxDirNameLength = 251;

constexpr uint64_t kPathCacheSize = MB(32);
constexpr uint64_t kDiskSize = GB(16);
constexpr uint32_t kLogBlocks = 0;
constexpr uint32_t kFSDataBlocks = kDiskSize / kBlockSize - kLogBlocks;
constexpr uint32_t kInodeBitmapBlocks = 1;  // 4096 Inodes

#endif  // CONFIG_H_