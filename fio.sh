./run.sh
touch build/disk/1
fio -filename=build/disk/1 -direct=1 -iodepth 1 -thread -rw=randrw -rwmixread=70 -ioengine=psync -bs=16k -size=128M -numjobs=1 -runtime=100 -group_reporting -name=mytest -ioscheduler=noop