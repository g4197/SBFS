#ifndef PATH_RESOLVER_H_
#define PATH_RESOLVER_H_

#include "fs.h"
#include "inode.h"
namespace sbfs {
using namespace std;
class PathResolver {
public:
    struct PathCacheValue {
        Inode inode;
        bool visited;
    };
    /*
     * Path cache is like this: if path is /home/gh/sbfs, then
     * Key is home/gh/sbfs, and value is its Inode and "visited" flag.
     * visited flag is used by "evict" function using clock algorithm.
     */
    using path_cache_key_t = std::string;
    using path_cache_val_t = PathCacheValue;
    using path_cache_t = std::map<path_cache_key_t, path_cache_val_t>;
    PathResolver(SBFileSystem *fs, uint64_t path_cache_size);
    ~PathResolver();
    /* Attention: end of path maybe / or not /. */
    Inode resolve(const std::string &path);
    /* Invalidate cache whose prefix is path. */
    void removePrefix(const std::string &prefix);
    /* Evict "size" bytes of data. */
    void evict(size_t size);

private:
    path_cache_t::iterator next(path_cache_t::iterator iter);
    string removeEndSlash(const string &path);
    vector<string> split(const string &s, char delim, bool reserve_prev);
    SBFileSystem *fs_;
    path_cache_t path_cache_;
    path_cache_t::iterator clock_iter_;
    uint64_t cur_cache_size_;
    uint64_t max_cache_size_;
};

};  // namespace sbfs

#endif  // PATH_RESOLVER_H_