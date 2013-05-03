#!/bin/sh
CC_PATH=/home/jpiat/development/KERNEL/ARM/beaglebone/toolchain/arm-2011.03/bin/
make ARCH=arm CROSS_COMPILE=${CC_PATH}arm-none-linux-gnueabi- KERNELDIR=~/development/KERNEL/ARM/beaglebone/kernels/ti33x-psp-3.1/
