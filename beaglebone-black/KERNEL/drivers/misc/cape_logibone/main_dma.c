#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <asm/uaccess.h>
#include <linux/cdev.h>
#include <linux/memory.h>
#include <linux/delay.h>

//device tree support
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_dma.h>
#include <linux/of_gpio.h>
#include "generic.h"
#include "drvr.h"
#include "logi_dma.h"
#include "ioctl.h"

//#define PROFILE //uncoment to enable code profile


static int dm_open(struct inode *inode, struct file *filp);
static int dm_release(struct inode *inode, struct file *filp);
static ssize_t dm_write(struct file *filp, const char *buf, size_t count, loff_t *f_pos);
static ssize_t dm_read(struct file *filp, char *buf, size_t count, loff_t *f_pos);

static struct file_operations dm_ops = {
	.read = dm_read,
	.write = dm_write,
	.compat_ioctl = dm_ioctl,
	.unlocked_ioctl = dm_ioctl,
	.open = dm_open,
	.release = dm_release,
};

static dma_addr_t dmaphysbuf = 0;
static unsigned char gDrvrMajor = 0;
static struct device * prog_device;
static struct class * drvr_class;
static struct drvr_device * drvr_devices;


#ifdef PROFILE

static struct timespec start_ts, end_ts;//profile timer

static inline void start_profile() {
	getnstimeofday(&start_ts);
}

static inline void stop_profile() {
	getnstimeofday(&end_ts);
}

static inline void compute_bandwidth(const unsigned int nb_byte) {
	struct timespec dt=timespec_sub(end_ts,start_ts);
	long elapsed_u_time=dt.tv_sec*1000000+dt.tv_nsec/1000;

	DBG_LOG("Time=%ld us\n",elapsed_u_time);
	DBG_LOG("Bandwidth=%d kBytes/s\n",1000000*(nb_byte>>10)/elapsed_u_time);
}

#endif


static inline ssize_t writeMem(struct file *filp, const char *buf, size_t count, loff_t *f_pos)
{
	unsigned short int transfer_size;
	ssize_t transferred = 0;
	unsigned long src_addr, trgt_addr;
	struct drvr_mem * mem_to_write = &(((struct drvr_device *) filp->private_data)->data);

#ifdef USE_WORD_ADDRESSING
	if (count % 2 != 0) {
		DBG_LOG("write: Transfer must be 16bits aligned\n");

		return -EFAULT;
	}
#endif

	if (count < MAX_DMA_TRANSFER_IN_BYTES) {
		transfer_size = count;
	} else {
		transfer_size = MAX_DMA_TRANSFER_IN_BYTES;
	}

#ifdef USE_WORD_ADDRESSING
	trgt_addr = (unsigned long) &(mem_to_write->base_addr[(*f_pos) / 2]);
#else
	trgt_addr = (unsigned long) &(mem_to_write->base_addr[(*f_pos)]);
#endif

	src_addr = (unsigned long) dmaphysbuf;

	if (copy_from_user(mem_to_write->dma.buf, buf, transfer_size)) {
		return -EFAULT;
	}

	if(transfer_size <= 256){		
		memcpy((void *) &(mem_to_write->virt_addr[(*f_pos)]), mem_to_write->dma.buf,transfer_size);

		return transfer_size ;
	}

	while (transferred < count) {
		int res;

#ifdef PROFILE
		DBG_LOG("Write\n");
		start_profile();
#endif

		res = logi_dma_copy(mem_to_write, trgt_addr, src_addr,
				    transfer_size);
		if (res < 0) {
			DBG_LOG("write: Failed to trigger EDMA transfer\n");

			return res;
		}

#ifdef PROFILE
		stop_profile();
		compute_bandwidth(transfer_size);
#endif

		trgt_addr += transfer_size;
		transferred += transfer_size;

		if ((count - transferred) < MAX_DMA_TRANSFER_IN_BYTES) {
			transfer_size = count - transferred;
		} else {
			transfer_size = MAX_DMA_TRANSFER_IN_BYTES;
		}

		if (copy_from_user(mem_to_write->dma.buf, &buf[transferred], transfer_size)) {
			return -EFAULT;
		}
	}

	return transferred;
}

static inline ssize_t readMem(struct file *filp, char *buf, size_t count, loff_t *f_pos)
{
	unsigned short int transfer_size;
	ssize_t transferred = 0;
	unsigned long src_addr, trgt_addr;

	struct drvr_mem * mem_to_read = &(((struct drvr_device *) filp->private_data)->data);

#ifdef USE_WORD_ADDRESSING
	if (count % 2 != 0) {
		DBG_LOG("read: Transfer must be 16bits aligned\n");

		return -EFAULT;
	}
#endif

	if (count < MAX_DMA_TRANSFER_IN_BYTES) {
		transfer_size = count;
	} else {
		transfer_size = MAX_DMA_TRANSFER_IN_BYTES;
	}

#ifdef USE_WORD_ADDRESSING
	src_addr = (unsigned long) &(mem_to_read->base_addr[(*f_pos) / 2]);
#else
	src_addr = (unsigned long) &(mem_to_read->base_addr[(*f_pos)]);
#endif

	trgt_addr = (unsigned long) dmaphysbuf;

	while (transferred < count) {

#ifdef PROFILE
		DBG_LOG("Read\n");
		start_profile();
#endif

		if(transfer_size <= 256) {
			memcpy(mem_to_read->dma.buf, (void *) &(mem_to_read->virt_addr[(*f_pos)+transferred/2]), transfer_size);
		}
		else {
			int res = logi_dma_copy(mem_to_read, trgt_addr, src_addr, transfer_size);

			if (res < 0) {
				DBG_LOG("read: Failed to trigger EDMA transfer\n");

				return res;
			}
		}

		if (copy_to_user(&buf[transferred], mem_to_read->dma.buf, transfer_size)) {
			return -EFAULT;
		}

#ifdef PROFILE
		stop_profile();
		compute_bandwidth(transfer_size);
#endif

		src_addr += transfer_size;
		transferred += transfer_size;

		if ((count - transferred) < MAX_DMA_TRANSFER_IN_BYTES) {
			transfer_size = (count - transferred);
		} else {
			transfer_size = MAX_DMA_TRANSFER_IN_BYTES;
		}
	}

	return transferred;
}

static int dm_open(struct inode *inode, struct file *filp)
{
	struct drvr_device* dev = container_of(inode->i_cdev, struct drvr_device, cdev);

	filp->private_data = dev; /* for other methods */

	if (dev == NULL) {
		DBG_LOG("Failed to retrieve driver structure!\n");

		return -ENODEV;
	}

	if (dev->opened != 1) {
		struct drvr_mem* mem_dev = &(dev->data);
		int result;

		if (request_mem_region((unsigned long) mem_dev->base_addr, FPGA_MEM_SIZE, DEVICE_NAME)==NULL) {
			DBG_LOG("Failed to request I/O memory region\n");

			return -ENOMEM;
		}

		mem_dev->virt_addr = ioremap_nocache(((unsigned long) mem_dev->base_addr), FPGA_MEM_SIZE);

		if (mem_dev->virt_addr == NULL) {
			DBG_LOG("Failed to remap I/O memory\n");

			return -ENOMEM;
		}

		result = logi_dma_open(mem_dev, &dmaphysbuf);

		if (result != 0)
			return result;

		DBG_LOG("mem interface opened \n");

		dev->opened = 1;
	}

	return 0;
}

static int dm_release(struct inode *inode, struct file *filp)
{
	struct drvr_device* dev = container_of(inode->i_cdev, struct drvr_device, cdev);
	struct drvr_mem* mem_dev = &(dev->data);

	if (dev->opened != 0) {
		iounmap((dev->data).virt_addr);
		release_mem_region(((unsigned long) (dev->data).base_addr), FPGA_MEM_SIZE);
		logi_dma_release(mem_dev);
		DBG_LOG("module released\n");
	}

	dev->opened = 0;

	return 0;
}

static ssize_t dm_write(struct file *filp, const char *buf, size_t count, loff_t *f_pos)
{
	return writeMem(filp, buf, count, f_pos);
}

static ssize_t dm_read(struct file *filp, char *buf, size_t count, loff_t *f_pos)
{
	return readMem(filp, buf, count, f_pos);
}

static void dm_exit(void)
{
	dev_t devno = MKDEV(gDrvrMajor, 0);

	/* Get rid of our char dev entries */
	if (drvr_devices) {
		device_destroy(drvr_class, MKDEV(gDrvrMajor, 0));
		cdev_del(&drvr_devices[0].cdev);
		kfree(drvr_devices);
	}

	class_destroy(drvr_class);
	/* cleanup_module is never called if registering failed */
	unregister_chrdev_region(devno, 2);

	ioctl_exit();
}

static int dm_init(void)
{
	int result;
	int devno;
	struct drvr_mem * memDev;

	dev_t dev = 0;
	result = alloc_chrdev_region(&dev, 0, 1, DEVICE_NAME);
	gDrvrMajor = MAJOR(dev);

	if (result < 0) {
		DBG_LOG("Registering char device failed with %d\n", gDrvrMajor);

		return result;
	}

	drvr_devices = kmalloc(sizeof(struct drvr_device), GFP_KERNEL);

	if (!drvr_devices) {
		dm_exit();

		return -ENOMEM;
	}

	drvr_class = class_create(THIS_MODULE, DEVICE_NAME);
	memset(drvr_devices, 0, sizeof(struct drvr_device));

	//printk(KERN_INFO "'mknod /dev/%s c %d %d'.\n", DEVICE_NAME, gDrvrMajor, 0);
	/* Initialize each device. */
	devno = MKDEV(gDrvrMajor, 0);
	memDev = &(drvr_devices[0].data);
	memDev->base_addr = (unsigned short *) (FPGA_BASE_ADDR);
	device_create(drvr_class, prog_device, devno, NULL, DEVICE_NAME_MEM);
	cdev_init(&(drvr_devices[0].cdev), &dm_ops);
	(drvr_devices[0].cdev).owner = THIS_MODULE;
	(drvr_devices[0].cdev).ops = &dm_ops;
	cdev_add(&(drvr_devices[0].cdev), devno, 1);
	drvr_devices[0].opened = 0;
	logi_dma_init();

	return ioctl_init();
}

static const struct of_device_id drvr_of_match[] = {
	{ .compatible = DEVICE_NAME, },
	{ },
};

MODULE_DEVICE_TABLE(of, drvr_of_match);
MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Jonathan Piat <piat.jonathan@gmail.com>");
MODULE_AUTHOR("Martin Schmitt <test051102@hotmail.com>");

module_init(dm_init);
module_exit(dm_exit);
