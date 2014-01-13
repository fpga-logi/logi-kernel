EXTRA_CFLAGS=-I$(PWD)

# If KERNELRELEASE is defined, we've been invoked from the
# kernel build system and can use its language.
ifneq ($(KERNELRELEASE),)
	obj-m := logibone_ra2_dma.o
	logibone_ra2_dma-objs := ../common/main_dma.o config.o

	obj-m += logibone_ra2_dm.o
	logibone_ra2_dm-objs := ../common/main_dm.o config.o
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
