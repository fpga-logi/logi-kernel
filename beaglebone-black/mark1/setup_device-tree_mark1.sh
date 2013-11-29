#!/bin/sh
rm /lib/firmware/BB-BONE-MARK1-00A1.dtbo
dtc -O dtb -o BB-BONE-MARK1-00A1.dtbo -b 0 -@ BB-BONE-MARK1-00A1.dts
cp BB-BONE-MARK1-00A1.dtbo /lib/firmware
sh -c "echo -4 > /sys/devices/bone_capemgr.8/slots "
sh -c "echo -5 > /sys/devices/bone_capemgr.8/slots "
sh -c "echo -6 > /sys/devices/bone_capemgr.8/slots "
sh -c "echo BB-BONE-MARK1:00A1 > /sys/devices/bone_capemgr.8/slots "
cat  /sys/devices/bone_capemgr.8/slots
insmod mark1_dm.ko
