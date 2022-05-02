# SBFS
Storage Basis FS

# 注意的事情
* 在`ubuntu`上装`libfuse3-dev`

# 一些学习资料

* https://www.cs.nmsu.edu/~pfeiffer/fuse-tutorial/html/
* https://github.com/alperakcan/fuse-ext2/tree/master/fuse-ext2 (仅作参考)

# TODO list

* 暂时先做单线程的，线程安全暂时不考虑。

## Block部分 (blk开头的文件)

* 只需要向上暴露`blk.h`中的`read`、`write`、`sync`接口即可，其他实现可以自行修改。（添加cpp文件之类也可以）

## layout部分 (fs_layout.h及fs.h SBFileSystem类)

* 利用上述三个接口管理超级块、`DiskInode`等结构，构建一个可以灵活分配和释放`DiskInode`及`data block`的系统。

## Single Folder部分 (inode.h)

* 利用`SBFileSystem`中分配和释放`Inode`的函数构建单文件夹下的所有文件操作。

## VFS部分 (fs.h namespace vfs内部分及主函数)

* 利用`Inode`类提供的单文件夹操作实现路径解析、fd管理等功能，并整合到主函数