#ifndef VFS_H_
#define VFS_H_

#include "path_resolver.h"

namespace sbfs {
namespace vfs {
void sb_init(const char *path, const uint64_t size);

void sb_destroy();

int sb_mkdir(const char *path, mode_t mode);

int sb_rmdir(const char *path);

int sb_create(const char *path, mode_t mode, struct fuse_file_info *fi);

int sb_unlink(const char *path);

int sb_rename(const char *oldpath, const char *newpath, unsigned int flags);

int sb_open(const char *path, struct fuse_file_info *fi);

int sb_close(const char *path, struct fuse_file_info *fi);

int sb_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi);

int sb_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi);

int sb_truncate(const char *path, off_t size);

int sb_statfs(const char *path, struct statvfs *stbuf);

int sb_fsync(const char *path, int datasync, struct fuse_file_info *fi);

/* TODO: more interfaces */
};
};
#endif // VFS_H_