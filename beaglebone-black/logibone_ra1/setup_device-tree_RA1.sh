#!/bin/sh
rm /lib/firmware/BB-BONE-LOGIBONE-00A$1.dtbo
dtc -O dtb -o BB-BONE-LOGIBONE-00A$1.dtbo -b 0 -@ BB-BONE-LOGIBONE-00A$1.dts
cp BB-BONE-LOGIBONE-00A$1.dtbo /lib/firmware
sh -c "echo -4 > /sys/devices/bone_capemgr.8/slots "
sh -c "echo -5 > /sys/devices/bone_capemgr.8/slots "
sh -c "echo -6 > /sys/devices/bone_capemgr.8/slots "
sh -c "echo BB-BONE-LOGIBONE:00A1 > /sys/devices/bone_capemgr.8/slots "
cat  /sys/devices/bone_capemgr.8/slots
insmod logibone_ra$1_dm.ko
