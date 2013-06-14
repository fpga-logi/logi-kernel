#!/bin/sh

CC_PREFIX=/home/jpiat/development/KERNEL/ARM/beaglebone-black/linux-dev/dl/gcc-linaro-arm-linux-gnueabihf-4.7-2013.04-20130415_linux/bin/arm-linux-gnueabihf-
make ARCH=arm CROSS_COMPILE=${CC_PREFIX} KERNELDIR=~/development/KERNEL/ARM/beaglebone-black/linux-dev/KERNEL
