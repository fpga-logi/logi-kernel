#!/bin/sh

CAPE_MNGR="$(ls /sys/devices/ | grep bone_capemgr)"

if [ -z "$CAPE_MNGR" ]; then
	SLOTS=/sys/devices/platform/bone_capemgr/slots
else
	SLOTS=/sys/devices/$CAPE_MNGR/slots
fi

rm /lib/firmware/BB-BONE-LOGIBONE-00R1.dtbo
dtc -O dtb -o BB-BONE-LOGIBONE-00R1.dtbo -b 0 -@ BB-BONE-LOGIBONE-00R1.dts
cp BB-BONE-LOGIBONE-00R1.dtbo /lib/firmware

#sh -c "echo -4 > ${SLOTS}"
#sh -c "echo -5 > ${SLOTS}"
#sh -c "echo -6 > ${SLOTS}"
sh -c "echo BB-BONE-LOGIBONE:00A3 > ${SLOTS}"

cat ${SLOTS}

insmod logibone_r1_dma.ko
