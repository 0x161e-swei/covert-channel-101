#! /usr/bin/env /bin/bash 
echo 1024| sudo tee /proc/sys/vm/nr_hugepages


for cpunum in $(cat /sys/devices/system/cpu/cpu*/topology/thread_siblings_list | cut -s -d, -f2- | tr ',' '\n' | sort -un)
do
    echo 0 > /sys/devices/system/cpu/cpu$cpunum/online
done
