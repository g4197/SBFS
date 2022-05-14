fusermount -u build/disk
sudo umount -l build/disk
mkdir build
cd build
cmake ..
make -j
rm /tmp/disk
mkdir /tmp/log
rm /tmp/log/*
mkdir disk
./bin/main -d --open=0 disk
# sudo umount disk