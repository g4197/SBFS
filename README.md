# SBFS
Storage Basis FS

## Dependencies
* `libfuse3-dev`, `librocksdb-dev`, `libgoogle-glog-dev`, `libgoogle-gflags-dev`

## Quick Start

* `run.sh` will create a file system using `/tmp/disk`, and mount it to `build/disk`.
* `reopen.sh` will open the `Storage Basis FS` corresponding to `/tmp/disk`.
* `rocksdb.sh` and `fio.sh` are used to test the correctness and I/O performance for `rocksdb`, respectively.
