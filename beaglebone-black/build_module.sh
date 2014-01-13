#!/bin/sh



CC_PREFIX=$1 #fill with the location of toolchain and target triplet
KERNEL_DIR=$2 #fill with the location of you kernel sources


cd logibone_ra1
make clean
make ARCH=arm CROSS_COMPILE=${CC_PREFIX} KERNELDIR=${KERNEL_DIR}
cd ../logibone_ra2
make clean
make ARCH=arm CROSS_COMPILE=${CC_PREFIX} KERNELDIR=${KERNEL_DIR}
cd ../mark1
make clean
make ARCH=arm CROSS_COMPILE=${CC_PREFIX} KERNELDIR=${KERNEL_DIR}

