#ifndef __DRVR_H__
#define __DRVR_H__

#include <linux/cdev.h>

#define DBG_LOG(fmt, args...) printk(KERN_INFO DEVICE_NAME ": " fmt, ## args)

struct drvr_mem{
	unsigned short * base_addr;
	unsigned short * virt_addr;
	unsigned char * dma_buf;
	int dma_chan;
};


struct drvr_device{
	struct drvr_mem data ;
	struct cdev cdev;
	unsigned char opened;
};

#endif
