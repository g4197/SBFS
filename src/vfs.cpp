#include "vfs.h"

#include <glog/logging.h>

#include <mutex>
#include <string>

#include "inode.h"

namespace sbfs {
namespace vfs {
SBFileSystem *sbfs;
PathResolver *path_resolver;
FDManager *fd_manager;
mutex mtx;

using std::string;

void splitFromLastSlash(string &path, string &parent, string &child) {
    size_t pos = path.rfind('/');
    if (pos == string::npos) {
        parent = "";
        child = path;
    } else {
        parent = path.substr(0, pos + 1);
        child = path.substr(pos + 1);
    }
}

void init_vfs(const char *path, const uint64_t size, bool is_open) {
    DLOG(WARNING) << "Initializing VFS at " << path << " with size " << size;
    void *t = sbfs;
    if (posix_memalign(&t, kBlockSize, sizeof(SBFileSystem)) != 0) {
        DLOG(ERROR) << "posix_memalign failed";
        abort();
    }
    sbfs = reinterpret_cast<SBFileSystem *>(t);
    // sbfs = (SBFileSystem *)malloc(sizeof(SBFileSystem));
    if (!is_open) {
        *sbfs = SBFileSystem::create(path, size, kFSDataBlocks, kInodeBitmapBlocks);
    } else {
        *sbfs = SBFileSystem::open(path);
    }
    path_resolver = new PathResolver(sbfs, kPathCacheSize);
    fd_manager = new FDManager();
}

int sb_rmw_diskinode(const char *path, struct fuse_file_info *fi, function<int(DiskInode &)> func) {
    DLOG(WARNING) << "read-modify-write diskinode " << path;
    Inode inode;
    if (!fi || !fi->fh || !fd_manager->get(fi->fh, &inode)) {
        /* not open, resolve path. */
        inode = path_resolver->resolve(string(path));
    }
    if (!inode.isValid()) {
        return -ENOENT;
    }
    DiskInode disk_inode;
    if (inode.read_inode(&disk_inode) == kFail) {
        return -EIO;
    }
    int ret = func(disk_inode);
    if (ret != kSuccess) return ret;

    if (inode.write_inode(&disk_inode) == kFail) {
        return -EIO;
    }
    return 0;
}

void sb_destroy(void *private_data) {
    auto guard = lock_guard(mtx);
    delete path_resolver;
    sbfs->device()->sync_all();
    free(sbfs);
}

int sb_mkdir(const char *path, mode_t mode) {
    auto guard = lock_guard(mtx);
    DLOG(WARNING) << "mkdir " << path << " with mode " << mode;
    /* resolve path and create inode */
    string dir = string(path), parent, child;
    splitFromLastSlash(dir, parent, child);
    Inode parent_inode = path_resolver->resolve(parent), child_inode;
    if (!parent_inode.isValid()) {
        return -ENOENT;
    }
    /* write information */
    DiskInode disk_inode(DiskInodeType::kDirectory);
    disk_inode.mode |= mode & 0777;

    auto ret = parent_inode.create(child.c_str(), &disk_inode, &child_inode);

    rt_assert(ret != kFail, "mkdir failed");
    DLOG(WARNING) << "mkdir success";
    return 0;
}

int sb_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, fuse_file_info *fi,
               fuse_readdir_flags flags) {
    auto guard = lock_guard(mtx);
    DLOG(WARNING) << "readdir " << path << " with offset " << offset;
    /* resolve path */
    string dir = string(path);
    Inode inode = path_resolver->resolve(dir);
    if (!inode.isValid()) {
        return -ENOENT;
    }
    DiskInode disk_inode(DiskInodeType::kDirectory);

    auto inode_ret = inode.read_inode(&disk_inode);
    rt_assert(inode_ret != kFail, "read inode failed");

    if (disk_inode.type != DiskInodeType::kDirectory) {
        return -ENOTDIR;
    }

    /* Read all blocks and list each of them. */
    uint32_t tot_blocks = disk_inode.total_blocks(disk_inode.size);
    DLOG(INFO) << "start listing with total blocks " << tot_blocks;
    for (uint32_t block_id = 0; block_id < tot_blocks; ++block_id) {
        DirBlock dir_block;  // Don't move it outside because we should reset this block every time.
        auto dir_ret = inode.read_data(block_id * sizeof(DirBlock), (uint8_t *)&dir_block, sizeof(DirBlock));
        rt_assert(dir_ret != kFail, "read dir block failed");
        for (auto &entry : dir_block.entries) {
            DLOG(INFO) << "Cur entry " << entry.inode << " " << entry.name;
            if (entry.isValid()) {
                filler(buf, entry.name, nullptr, 0, (fuse_fill_dir_flags)0);
            }
        }
    }
    return 0;
}

int sb_getattr(const char *path, struct stat *stbuf, fuse_file_info *fi) {
    auto guard = lock_guard(mtx);
    return sb_rmw_diskinode(path, fi, [&](DiskInode &disk_inode) {
        stbuf->st_mode = disk_inode.mode;
        stbuf->st_atime = disk_inode.access_time;
        stbuf->st_mtime = disk_inode.modify_time;
        stbuf->st_ctime = disk_inode.change_time;
        stbuf->st_size = disk_inode.size;
        stbuf->st_mode = disk_inode.mode;
        stbuf->st_nlink = disk_inode.link_cnt;
        stbuf->st_uid = disk_inode.uid;
        stbuf->st_gid = disk_inode.gid;
        stbuf->st_blocks = disk_inode.total_blocks(disk_inode.size);
        stbuf->st_blksize = kBlockSize;

        return kSuccess;
    });
}

int sb_rmdir(const char *path) {
    auto guard = lock_guard(mtx);
    DLOG(WARNING) << "rmdir " << path;
    /* resolve path */
    string dir = string(path), parent, child;
    splitFromLastSlash(dir, parent, child);
    Inode parent_inode = path_resolver->resolve(parent), child_inode;
    if (!parent_inode.isValid()) {
        return -ENOENT;
    }
    int find_ret = parent_inode.find(child.c_str(), &child_inode);
    /* TODO: not a directory */
    if (find_ret == kFail) {
        return -ENOENT;
    }

    DiskInode disk_inode(DiskInodeType::kDirectory);
    auto inode_ret = child_inode.read_inode(&disk_inode);
    rt_assert(inode_ret != kFail, "read inode failed");
    if (disk_inode.type != DiskInodeType::kDirectory) {
        return -ENOTDIR;
    }

    uint32_t tot_blocks = disk_inode.total_blocks(disk_inode.size);
    if (tot_blocks > 1) {
        return -ENOTEMPTY;
    }

    DirBlock dir_block;
    auto dir_ret = child_inode.read_data(0, (uint8_t *)&dir_block, sizeof(DirBlock));
    rt_assert(dir_ret != kFail, "read dir block failed");
    for (auto &entry : dir_block.entries) {
        if (entry.isValid() && strcmp(entry.name, ".") != 0 && strcmp(entry.name, "..") != 0) {
            return -ENOTEMPTY;
        }
    }

    /* It's an empty dir, delete it. */
    parent_inode.remove(child.c_str());
    path_resolver->removePrefix(dir);

    return 0;
}

int sb_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
    auto guard = lock_guard(mtx);
    DLOG(WARNING) << "create " << path << " with mode " << mode << " and fi " << fi;
    /* resolve path and create inode */
    string dir = string(path), parent, child;
    splitFromLastSlash(dir, parent, child);
    Inode parent_inode = path_resolver->resolve(parent), child_inode;
    if (!parent_inode.isValid()) {
        return -ENOENT;
    }
    /* write information */
    DiskInode disk_inode(DiskInodeType::kFile);
    disk_inode.mode |= mode & 0777;
    /* TODO: parent not a directory, file exists... */
    parent_inode.create(child.c_str(), &disk_inode, &child_inode);
    int flags = fi->flags;
    fi->fh = fd_manager->open(child_inode);
    return 0;
}

int sb_unlink(const char *path) {
    auto guard = lock_guard(mtx);
    DLOG(WARNING) << "unlink " << path;
    /* resolve path */
    string dir = string(path), parent, child;
    splitFromLastSlash(dir, parent, child);
    Inode parent_inode = path_resolver->resolve(parent), child_inode;
    if (!parent_inode.isValid()) {
        return -ENOENT;
    }
    int find_ret = parent_inode.find(child.c_str(), &child_inode);
    /* TODO: Not a directory... */
    if (find_ret == kFail) {
        return -ENOENT;
    }

    /* Remove the file. */
    parent_inode.remove(child.c_str());
    path_resolver->removePrefix(dir);

    return 0;
}

int sb_rename(const char *oldpath, const char *newpath, unsigned int flags) {
    auto guard = lock_guard(mtx);
    DLOG(WARNING) << "rename " << oldpath << " to " << newpath;
    /* resolve path */
    string old_dir = string(oldpath), old_parent, old_child;
    splitFromLastSlash(old_dir, old_parent, old_child);
    Inode old_parent_inode = path_resolver->resolve(old_parent), old_child_inode;
    if (!old_parent_inode.isValid()) {
        return -ENOENT;
    }

    string new_dir = string(newpath), new_parent, new_child;
    splitFromLastSlash(new_dir, new_parent, new_child);
    DLOG(WARNING) << "new_parent: " << new_parent << " new_child: " << new_child;
    DLOG(WARNING) << "old_parent: " << old_parent << " old_child: " << old_child;
    Inode new_parent_inode = path_resolver->resolve(new_parent), new_child_inode;

    int unlink_ret = old_parent_inode.unlink(old_child.c_str(), &old_child_inode);
    if (unlink_ret == kFail) {
        return -ENOENT;
    }

    bool replace = !(flags & RENAME_NOREPLACE);
    bool exchange = flags & RENAME_EXCHANGE;

    /* link the new inode to old inode. */
    if (exchange) {
        new_parent_inode.unlink(new_child.c_str(), &new_child_inode);
        old_parent_inode.link(old_child.c_str(), &new_child_inode);
    }

    /* link the old inode to new. */
    new_parent_inode.link(new_child.c_str(), &old_child_inode, replace);

    path_resolver->removePrefix(old_dir);
    path_resolver->removePrefix(new_dir);
    return 0;
}

int sb_open(const char *path, struct fuse_file_info *fi) {
    auto guard = lock_guard(mtx);
    DLOG(WARNING) << "open " << path;
    /* resolve path */
    Inode inode = path_resolver->resolve(string(path));
    if (!inode.isValid()) {
        return -ENOENT;
    }
    /* TODO: handle O_RDONLY, O_WRONLY, O_RDWR, O_EXEC, O_SEARCH */
    int flags = fi->flags;
    fi->fh = fd_manager->open(inode);
    return 0;
}

int sb_release(const char *path, struct fuse_file_info *fi) {
    auto guard = lock_guard(mtx);
    DLOG(WARNING) << "release " << path << " " << fi << " " << fi->fh;
    fd_manager->close(fi->fh);
    fi->fh = 0;
    return 0;
}

int sb_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    auto guard = lock_guard(mtx);
    DLOG(WARNING) << "read " << path << " with size " << size << " and offset " << offset;
    if (size > UINT32_MAX) {
        /* Temporarily not support read > 4GB */
        DLOG(WARNING) << "read size > 4GB";
        return -EINVAL;
    }
    Inode inode;
    if (!fd_manager->get(fi->fh, &inode)) {
        DLOG(WARNING) << "invalid fd";
        return -EBADF;
    }
    int ret = 0;
    if ((ret = inode.read_data(offset, (uint8_t *)buf, size)) == kFail) {
        DLOG(WARNING) << "read data failed";
        return -EIO;
    }
    DLOG(WARNING) << "read " << size << " bytes"
                  << " actually " << ret << " bytes";
    return ret;
}

int sb_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    auto guard = lock_guard(mtx);
    DLOG(WARNING) << "write " << path << " with size " << size << " and offset " << offset << " and fh " << fi->fh;
    if (size > UINT32_MAX) {
        /* Temporarily not support write > 4GB */
        DLOG(WARNING) << "write size > 4GB";
        return -EINVAL;
    }
    Inode inode;
    if (!fd_manager->get(fi->fh, &inode)) {
        DLOG(WARNING) << "invalid fd";
        return -EBADF;
    }
    int ret = 0;
    if ((ret = inode.write_data(offset, (uint8_t *)buf, size)) == kFail) {
        DLOG(WARNING) << "write data failed";
        return -EIO;
    }
    DLOG(WARNING) << "write " << size << " bytes to " << path << " at offset " << offset << " actually: " << ret;
    return ret;
}

int sb_truncate(const char *path, off_t off, struct fuse_file_info *fi) {
    auto guard = lock_guard(mtx);
    DLOG(WARNING) << "truncate " << path << " with offset " << off;
    if (off > UINT32_MAX) {
        /* Temporarily not support truncate > 4GB */
        DLOG(WARNING) << "truncate offset > 4GB";
        return -EINVAL;
    }
    Inode inode;
    if (!fd_manager->get(fi->fh, &inode)) {
        /* resolve path */
        inode = path_resolver->resolve(string(path));
        if (!inode.isValid()) {
            return -ENOENT;
        }
    }
    if (inode.resize(off) == kFail) {
        DLOG(WARNING) << "truncate failed";
        return -EIO;
    }
    return 0;
}

int sb_statfs(const char *path, struct statvfs *stbuf) {
    auto guard = lock_guard(mtx);
    DLOG(WARNING) << "statfs " << path;
    /* TODO: unimplemented, need block info */
    return 0;
}

int sb_fsync(const char *path, int datasync, struct fuse_file_info *fi) {
    auto guard = lock_guard(mtx);
    DLOG(WARNING) << "fsync " << path;
    Inode inode;
    if (!fd_manager->get(fi->fh, &inode)) {
        DLOG(WARNING) << "invalid fd";
        return -EBADF;
    }
    if (inode.sync(datasync == 0) == kFail) {
        DLOG(WARNING) << "sync failed";
        return -EIO;
    }
    return 0;
}

int sb_utimens(const char *path, const struct timespec tv[2], struct fuse_file_info *fi) {
    auto guard = lock_guard(mtx);
    DLOG(WARNING) << "utimens " << path;
    return sb_rmw_diskinode(path, fi, [=](DiskInode &disk_inode) {
        disk_inode.access_time = tv[0].tv_sec + tv[0].tv_nsec / 1000000000.0;
        disk_inode.modify_time = tv[1].tv_sec + tv[1].tv_nsec / 1000000000.0;
        return kSuccess;
    });
}

int sb_chmod(const char *path, mode_t mode, struct fuse_file_info *fi) {
    auto guard = lock_guard(mtx);
    DLOG(WARNING) << "chmod " << path;
    return sb_rmw_diskinode(path, fi, [=](DiskInode &disk_inode) {
        disk_inode.mode = mode;
        return kSuccess;
    });
}

int sb_chown(const char *path, uid_t uid, gid_t gid, struct fuse_file_info *fi) {
    auto guard = lock_guard(mtx);
    DLOG(WARNING) << "chown " << path;
    return sb_rmw_diskinode(path, fi, [=](DiskInode &disk_inode) {
        disk_inode.uid = uid;
        disk_inode.gid = gid;
        return kSuccess;
    });
}

}  // namespace vfs
}  // namespace sbfs