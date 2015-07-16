#The following definitions can be added to EXTRA_CFLAGS using -D<definition> to change the behaviour of certain modules
#PROFILE: Builds modules including profiling information
#USE_WORD_ADDRESSING: Enables WORD instead of BYTE addressing
EXTRA_CFLAGS=-I$(PWD) -Wall -O2

# If KERNELRELEASE is defined, we've been invoked from the
# kernel build system and can use its language.
ifneq ($(KERNELRELEASE),)
	obj-m := logibone_ra2_dma.o
	logibone_ra2_dma-objs := ../common/main_dma.o config.o ioctl.o

	obj-m += logibone_ra2_dm.o
	logibone_ra2_dm-objs := ../common/main_dm.o config.o ioctl.o
# Otherwise we were called directly from the command
# line; invoke the kernel build system.
else
	KERNELDIR ?= /lib/modules/$(shell uname -r)/build
	PWD := $(shell pwd)
default:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules
endif

clean:
	rm *.o *.ko .*.cmd *.order *.symvers
