#ifndef __DRVR_H__
#define __DRVR_H__

#include <linux/cdev.h>
#include <linux/dma-mapping.h>

#define DBG_LOG(fmt, args...) printk(KERN_INFO DEVICE_NAME ": " fmt, ## args)

struct drvr_dma {
	void * buf;
	int dma_chan;
	struct dma_chan * chan;
};

struct drvr_mem{
	unsigned short * base_addr;
	unsigned short * virt_addr;

//new
	struct drvr_dma dma;

//old
	unsigned char * dma_buf;
	int dma_chan;
};

struct drvr_device{
	struct drvr_mem data ;
	struct cdev cdev;
	unsigned char opened;
};

#endif
