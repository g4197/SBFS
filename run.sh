mkdir build
cd build
cmake ..
make -j
mkdir disk
./bin/main --open=0 disk
# sudo umount disk