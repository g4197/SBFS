#ifndef VFS_H_
#define VFS_H_

#include "fd_manager.h"
#include "path_resolver.h"

namespace sbfs {
namespace vfs {
extern SBFileSystem *sbfs;
extern PathResolver *path_resolver;
extern FDManager *fd_manager;

void init_vfs(const char *path, const uint64_t size, bool is_open);

void sb_destroy(void *private_data);

int sb_mkdir(const char *path, mode_t mode);

int sb_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, fuse_file_info *fi,
               fuse_readdir_flags flags);

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

int sb_utimens(const char *, const struct timespec tv[2], struct fuse_file_info *fi);

int sb_chmod(const char *path, mode_t mode, struct fuse_file_info *fi);

int sb_chown(const char *path, uid_t uid, gid_t gid, struct fuse_file_info *fi);

/* TODO: more interfaces */
};      // namespace vfs
};      // namespace sbfs
#endif  // VFS_H_