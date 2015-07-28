#!/bin/sh

CAPE_MNGR="$(ls /sys/devices/ | grep bone_capemgr)"

while getopts ds opt
do
	case $opt in
		d) echo "Using DMA mode" && bDma=true;;
		s) echo "Synchronous mode" && bSync=true;;
		*) echo "Unknown option specified"
	esac
done

rm /lib/firmware/BB-BONE-MARK1-00A1.dtbo

if [ "$bSync" ];then
	dtc -O dtb -o BB-BONE-MARK1-00A1.dtbo -b 0 -@ BB-BONE-MARK1-00A1_sync.dts
else
	dtc -O dtb -o BB-BONE-MARK1-00A1.dtbo -b 0 -@ BB-BONE-MARK1-00A1.dts
fi

cp BB-BONE-MARK1-00A1.dtbo /lib/firmware
sh -c "echo -4 > /sys/devices/"${CAPE_MNGR}"/slots "
sh -c "echo -5 > /sys/devices/"${CAPE_MNGR}"/slots "
sh -c "echo -6 > /sys/devices/"${CAPE_MNGR}"/slots "
sh -c "echo BB-BONE-MARK1:00A1 > /sys/devices/"${CAPE_MNGR}"/slots "
cat  /sys/devices/bone_capemgr.*/slots

if [ "$bDma" ];then
	insmod mark1_dma.ko
else
	insmod mark1_dm.ko
fi

