# SBFS
Storage Basis FS

## 依赖库
* `libfuse3-dev`, `librocksdb-dev`, `libgoogle-glog-dev`, `libgoogle-gflags-dev`

## Quick Start

* `./run.sh`会自动使用`/tmp/disk`文件创建一个文件系统，并挂载到根目录`build/disk`目录。
* `./reopen.sh`会打开`/tmp/disk`文件对应的`Storage Basis FS`文件系统。
* `./rocksdb.sh`, `./fio.sh`分别用以测试`rocksdb`的运行正确性和磁盘读写速度。
