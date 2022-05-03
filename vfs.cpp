#include "vfs.h"
#include "inode.h"
#include <string>
#include <glog/logging.h>

namespace sbfs {
namespace vfs {
SBFileSystem *sbfs;
PathResolver *path_resolver;
/* File handler -> Inode */
std::map<uint64_t, Inode> fd_manager;

using std::string;

void splitFromLastSlash(string &path, string &parent, string &child) {
    size_t pos = path.rfind('/');
    if (pos == string::npos) {
        parent = "";
        child = path;
    } else {
        parent = path.substr(0, pos);
        child = path.substr(pos + 1);
    }
}

void sb_init(const char *path, const uint64_t size, bool is_open) {
    sbfs = (SBFileSystem *)malloc(sizeof(SBFileSystem));
    if (is_open) {
        *sbfs = SBFileSystem::create(path, size, kFSDataBlocks, kInodeBitmapBlocks);
    } else {
        *sbfs = SBFileSystem::open(path);
    }
    path_resolver = new PathResolver(sbfs, kPathCacheSize);
}

void sb_destroy(void *private_data) {
    delete path_resolver;
    delete sbfs;
}

int sb_mkdir(const char *path, mode_t mode) {
    DLOG(INFO) << "mkdir " << path << " with mode " << mode;
    /* resolve path and create inode */
    string dir = string(path), parent, child;
    splitFromLastSlash(dir, parent, child);
    Inode parent_inode = path_resolver->resolve(parent), child_inode;

    /* write information */
    DiskInode disk_inode(DiskInodeType::kDirectory);
    disk_inode.mode = mode & 0777;

    auto ret = parent_inode.create(child.c_str(), &disk_inode, &child_inode);
    rt_assert(ret != kFail, "mkdir failed");
    return 0;
}

int sb_readdir(const char *path, void *buf, fuse_fill_dir_t filler, 
               off_t offset, fuse_file_info *fi, fuse_readdir_flags flags) {
    DLOG(INFO) << "readdir " << path << " with offset " << offset;
    /* resolve path */
    string dir = string(path);
    Inode inode = path_resolver->resolve(dir);

    DiskInode disk_inode(DiskInodeType::kDirectory);

    auto inode_ret = inode.read_inode(&disk_inode);
    rt_assert(inode_ret != kFail, "read inode failed");
    
    if (disk_inode.type != DiskInodeType::kDirectory) {
        return -ENOTDIR;
    }

    DirBlock dir_block;
    auto dir_ret = inode.read_data(0, (uint8_t *)&dir_block, sizeof(DirBlock));
    rt_assert(dir_ret != kFail, "read dir block failed");
    
    for (size_t i = 0; i < kDirEntries; i++) {
        DirEntry &entry = dir_block.entries[i];
        if (entry.isValid()) {
            filler(buf, entry.name, nullptr, 0, (fuse_fill_dir_flags)0);
        }
    }
    return 0;
}

int sb_getattr(const char *path, struct stat *stbuf, fuse_file_info *fi);

int sb_rmdir(const char *path);

int sb_create(const char *path, mode_t mode, struct fuse_file_info *fi);

int sb_unlink(const char *path);

int sb_rename(const char *oldpath, const char *newpath, unsigned int flags);

int sb_open(const char *path, struct fuse_file_info *fi);

int sb_release(const char *path, struct fuse_file_info *fi);

int sb_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi);

int sb_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi);

int sb_truncate(const char *path, off_t off, struct fuse_file_info *fi);

int sb_statfs(const char *path, struct statvfs *stbuf);

int sb_fsync(const char *path, int datasync, struct fuse_file_info *fi);

};
};