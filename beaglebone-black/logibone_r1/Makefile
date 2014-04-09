EXTRA_CFLAGS=-I$(PWD)

# If KERNELRELEASE is defined, we've been invoked from the
# kernel build system and can use its language.
ifneq ($(KERNELRELEASE),)
	obj-m := logibone_r1_dma.o
	logibone_r1_dma-objs := ../common/main_dma.o config.o ioctl.o

	obj-m += logibone_r1_dm.o
	logibone_r1_dm-objs := ../common/main_dm.o config.o ioctl.o
# Otherwise we were called directly from the command
# line; invoke the kernel build system.
else
	KERNELDIR ?= /lib/modules/$(shell uname -r)/build
	PWD := $(shell pwd)
default:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules
endif

clean:
	rm *.o *.ko *.order *.symvers
