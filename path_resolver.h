#ifndef PATH_RESOLVER_H_
#define PATH_RESOLVER_H_

#include "inode.h"
#include "fs.h"

namespace sbfs {
class PathResolver {
public:
    PathResolver(SBFileSystem *fs, uint64_t path_cache_size);
    ~PathResolver();

    Inode resolve(const char *path);
private:
    SBFileSystem *fs_;
    std::map<std::string, Inode> path_cache_;
};

};

#endif // PATH_RESOLVER_H_