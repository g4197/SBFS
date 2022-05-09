mkdir build
cd build
cmake ..
make -j
rm /tmp/disk
mkdir disk
./bin/main --open=0 disk
# sudo umount disk