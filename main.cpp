#include <fuse3/fuse.h>
#include <glog/logging.h>

#include "vfs.h"

using namespace sbfs::vfs;

static struct options {
    const char *disk_path;
    int is_open;
} opt;

#define OPTION(t, p) \
    { t, offsetof(struct options, p), 1 }
static const struct fuse_opt option_spec[] = { OPTION("--disk_path=%s", disk_path), OPTION("--open=%d", is_open),
                                               FUSE_OPT_END };

fuse_operations sb_op;

int main(int argc, char **argv) {
    /* Init glog */
    FLAGS_log_dir = "/tmp/log";
    // FLAGS_alsologtostderr = 1;
    google::InitGoogleLogging(argv[0]);

    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
    opt.disk_path = "/tmp/disk";
    opt.is_open = false;

    DLOG(WARNING) << "start parse args";
    if (fuse_opt_parse(&args, &opt, option_spec, nullptr) == -1) {
        LOG(ERROR) << "Failed to parse options";
    }

    DLOG(WARNING) << "Disk path: " << opt.disk_path << ", is open: " << opt.is_open;
    init_vfs(opt.disk_path, kDiskSize, opt.is_open);

    sb_op.readdir = sb_readdir;
    sb_op.getattr = sb_getattr;
    sb_op.mkdir = sb_mkdir;
    sb_op.rmdir = sb_rmdir;
    sb_op.destroy = sb_destroy;
    sb_op.create = sb_create;
    sb_op.unlink = sb_unlink;
    sb_op.rename = sb_rename;
    sb_op.open = sb_open;
    sb_op.release = sb_release;
    sb_op.read = sb_read;
    sb_op.write = sb_write;
    sb_op.truncate = sb_truncate;
    sb_op.fsync = sb_fsync;

    DLOG(WARNING) << "start fuse_main";
    fuse_main(args.argc, args.argv, &sb_op, nullptr);
    return 0;
}