#include "path_resolver.h"

namespace sbfs {
PathResolver::PathResolver(SBFileSystem *fs, uint64_t path_cache_size)
    : fs_(fs), cur_cache_size_(0), max_cache_size_(path_cache_size) {
    clock_iter_ = path_cache_.end();
}

PathResolver::~PathResolver() {
}

Inode PathResolver::resolve(const std::string &path) {
    if (path[0] != '/') {
        return Inode();
    }
    string path_no_both_slash = removeEndSlash(path).substr(1);
    vector<string> path_vec = split(path_no_both_slash, '/', false);
    vector<string> cache_vec = split(path_no_both_slash, '/', true);
    /* lookup cache first. */
    Inode cur_inode = fs_->root();
    int cur_path_index = 0;
#ifdef PATH_CACHE
    for (int i = 0; i < cache_vec.size(); ++i) {
        string cache_path = cache_vec[i];
        path_cache_t::iterator iter = path_cache_.find(cache_path);
        if (iter != path_cache_.end()) {
            DLOG(INFO) << "Found in cache: " << cache_path;
            iter->second.visited = true;  // Mark as visited.
            cur_inode = iter->second.inode;
            cur_path_index = i + 1;  // Now ready for next part of path.
        }
    }
#endif
    /* continue resolving path. */
    for (int i = cur_path_index; i < path_vec.size(); ++i) {
        string path_part = path_vec[i];
        Inode next_inode;
        if (cur_inode.find(path_part.c_str(), &next_inode) == kFail) {
            return Inode();
        }
        cur_inode = next_inode;
#ifdef PATH_CACHE
        /* Update cache. */
        size_t cur_size_increase = cache_vec[i].size() + sizeof(path_cache_key_t) + sizeof(path_cache_val_t);
        size_t size_to_evict = cur_cache_size_ + cur_size_increase - max_cache_size_;
        if (size_to_evict > 0) {
            this->evict(size_to_evict);
        }
        path_cache_[cache_vec[i]] = path_cache_val_t{ cur_inode, true };
#endif
    }
    return cur_inode;
}

void PathResolver::removePrefix(const std::string &prefix) {
    string prefix_no_both_slash = removeEndSlash(prefix).substr(1);
}

void PathResolver::evict(size_t size) {
    size_t cur_size = 0;
    while (cur_size < size && !path_cache_.empty()) {
        if (clock_iter_ == path_cache_.end()) {
            clock_iter_ = path_cache_.begin();
        }
        while (clock_iter_->second.visited) {
            clock_iter_->second.visited = false;
            clock_iter_ = next(clock_iter_);
        }
        cur_size += clock_iter_->first.size() + sizeof(path_cache_key_t) + sizeof(path_cache_val_t);
        DLOG(INFO) << "Evicting " << clock_iter_->first << " cur evict size: " << cur_size
                   << " max evict size: " << size;
        clock_iter_ = path_cache_.erase(clock_iter_);
    }
    cur_cache_size_ = cur_size;
}

PathResolver::path_cache_t::iterator PathResolver::next(path_cache_t::iterator iter) {
    if (++iter == path_cache_.end()) {
        return path_cache_.begin();
    }
    return iter;
}

string PathResolver::removeEndSlash(const string &path) {
    return path.rfind('/') == path.size() - 1 ? path.substr(0, path.size() - 1) : path;
}

vector<string> PathResolver::split(const string &s, char delim, bool reserve_prev) {
    size_t start = 0, end = 0;
    vector<string> ret;
    while ((end = s.find(delim, start)) != string::npos) {
        ret.push_back(reserve_prev ? s.substr(0, end) : s.substr(start, end - start));
        start = end + 1;
    }
    ret.push_back(s.substr(start));
    return ret;
}

}  // namespace sbfs