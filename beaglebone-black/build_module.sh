#!/bin/sh

BOARD_REV=r1

LINUX_DEV=$1

CC_PREFIX=${LINUX_DEV}/dl/gcc-linaro-arm-linux-gnueabihf-4.7-2013.04-20130415_linux/bin/arm-linux-gnueabihf-
KERNEL_DIR=${LINUX_DEV}/KERNEL


cd logibone_${BOARD_REV}
make clean
make ARCH=arm CROSS_COMPILE=${CC_PREFIX} KERNELDIR=${KERNEL_DIR}
cd ..
mkdir deploy
cp  logibone_${BOARD_REV}/logibone_${BOARD_REV}_dma.ko deploy/
cp logibone_${BOARD_REV}/setup_device-tree_${BOARD_REV^^}.sh deploy/
cp logibone_${BOARD_REV}/BB-BONE-LOGIBONE-00${BOARD_REV^^}.dts deploy/
