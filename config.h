#ifndef CONFIG_H_
#define CONFIG_H_

#include <bits/stdc++.h>

// Some config of the FS.
// Only define constants or inline functions here!

inline constexpr uint64_t KB(uint64_t x) {
    return x * 1024;
}

inline constexpr uint64_t MB(uint64_t x) {
    return x * 1024 * 1024;
}

constexpr uint64_t kBlockSize = 512; // block is 512B
constexpr uint64_t kBlockCacheSize = MB(32);

using blk_id_t = uint32_t;

constexpr int kFail = -1; // Maybe used in all return values.

// Generated by Copilot, I don't know what it means.
constexpr uint32_t kFSMagic = 0x53425355;
constexpr uint64_t kInodeDirectCnt = 25;
constexpr uint64_t kMaxDirNameLength = 27;

#endif // CONFIG_H_