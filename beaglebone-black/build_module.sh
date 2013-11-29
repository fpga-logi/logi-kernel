#!/bin/sh

CC_PREFIX=/home/jpiat/development/KERNEL/ARM/beaglebone-black/linux-dev/dl/gcc-linaro-arm-linux-gnueabihf-4.7-2013.04-20130415_linux/bin/arm-linux-gnueabihf-

make ARCH=arm CROSS_COMPILE=${CC_PREFIX} KERNELDIR=~/development/KERNEL/ARM/beaglebone-black/linux-dev/KERNEL
mkdir beaglebone_black_build
cp *.ko beaglebone_black_build
make clean

make ARCH=arm CROSS_COMPILE=${CC_PREFIX} KERNELDIR=~/development/KERNEL/ARM/beaglebone/linux-dev/KERNEL
mkdir beaglebone_build
cp *.ko  beaglebone_build
make clean


