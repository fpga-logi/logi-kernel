#!/bin/sh


CAPE_MNGR="$(ls /sys/devices/ | grep bone_capemgr)"

rm /lib/firmware/BB-BONE-LOGIBONE-00R1.dtbo
dtc -O dtb -o BB-BONE-LOGIBONE-00R1.dtbo -b 0 -@ BB-BONE-LOGIBONE-00R1.dts
cp BB-BONE-LOGIBONE-00R1.dtbo /lib/firmware
sh -c "echo -4 > /sys/devices/"${CAPE_MNGR}"/slots "
sh -c "echo -5 > /sys/devices/"${CAPE_MNGR}"/slots "
sh -c "echo -6 > /sys/devices/"${CAPE_MNGR}"/slots "
sh -c "echo BB-BONE-LOGIBONE:00R1 > /sys/devices/"${CAPE_MNGR}"/slots "
cat  /sys/devices/${CAPE_MNGR}/slots
insmod logibone_r1_dma.ko
