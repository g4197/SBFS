#ifndef FD_MANAGER_H_
#define FD_MANAGER_H_

#include <atomic>
#include <list>
#include <map>

#include "inode.h"

namespace sbfs {
namespace vfs {
class FDManager {
public:
    FDManager() {
        fd_manager.clear();
        fd_counter.store(10, std::memory_order_relaxed);  // jump stdin and stdout
    }

    uint64_t open(const Inode &inode) {
        uint64_t fd = fd_counter.fetch_add(1, std::memory_order_relaxed);
        fd_manager.insert(std::make_pair(fd, inode));
        deltaRefCnt(inode, 1);
        return fd;
    }

    bool get(uint64_t fd, Inode *inode) {
        if (fd == 0) return false;
        auto it = fd_manager.find(fd);
        if (it == fd_manager.end()) {
            return false;
        }
        *inode = it->second;
        return true;
    }

    void close(uint64_t fd) {
        if (fd == 0) return;
        if (fd_manager.find(fd) != fd_manager.end()) {
            Inode inode = fd_manager[fd];
            deltaRefCnt(inode, -1);
            fd_manager.erase(fd);
        }
    }

    bool isOpen(const Inode &inode) {
        return (reference_count.find(inode) != reference_count.end()) && (reference_count[inode] > 0);
    }

    void deltaRefCnt(const Inode &inode, int delta) {
        if (reference_count.find(inode) == reference_count.end()) {
            reference_count.insert(std::make_pair(inode, (uint64_t)delta));
        } else {
            reference_count[inode] += delta;
        }
    }

private:
    /* File handler -> Inode */
    std::map<uint64_t, Inode> fd_manager;
    std::map<Inode, uint64_t> reference_count;
    /* Start from 1 for 0 is reserved for not-open */
    std::atomic<uint64_t> fd_counter;
};
};      // namespace vfs
};      // namespace sbfs
#endif  // FD_MANAGER_H_