#include "vfs.h"

namespace sbfs {
namespace vfs {
/* File descriptor -> Inode */
std::map<int, Inode> fd_manager;
};
};