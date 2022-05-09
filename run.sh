fusermount -u build/disk
mkdir build
cd build
cmake ..
make -j
rm /tmp/disk
mkdir /tmp/log
mkdir disk
gdb --args ./bin/main -d --open=0 disk
# sudo umount disk