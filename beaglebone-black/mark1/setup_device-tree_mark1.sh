#!/bin/sh

CAPE_MNGR="$(ls /sys/devices/ | grep bone_capemgr)"

if [ -z "$CAPE_MNGR" ]; then
	SLOTS=/sys/devices/platform/bone_capemgr/slots
else
	SLOTS=/sys/devices/$CAPE_MNGR/slots
fi

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

#sh -c "echo -4 > ${SLOTS}"
#sh -c "echo -5 > ${SLOTS}"
#sh -c "echo -6 > ${SLOTS}"
sh -c "echo BB-BONE-MARK1:00A1 > ${SLOTS}"

cat ${SLOTS}

if [ "$bDma" ];then
	insmod mark1_dma.ko
else
	insmod mark1_dm.ko
fi
