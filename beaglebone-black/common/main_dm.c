#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <linux/cdev.h>
#include <linux/memory.h>
#include <linux/dma-mapping.h>
#include <linux/edma.h>
#include <linux/platform_data/edma.h>
#include <linux/delay.h>

//device tree support
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_dma.h>
#include <linux/of_gpio.h>
#include "generic.h"
#include "config.h"
#include "drvr.h"
#include "ioctl.h"


static int dm_open(struct inode *inode, struct file *filp);
static int dm_release(struct inode *inode, struct file *filp);
static ssize_t dm_write(struct file *filp, const char *buf, size_t count, loff_t *f_pos);
static ssize_t dm_read(struct file *filp, char *buf, size_t count, loff_t *f_pos);


static struct i2c_board_info io_exp_info= {
	I2C_BOARD_INFO("fpga_ctrl", I2C_IO_EXP_ADDR),
};

static struct file_operations dm_ops = {
	.read = dm_read,
	.write = dm_write,
	.compat_ioctl = dm_ioctl,
	.unlocked_ioctl = dm_ioctl,
	.open = dm_open,
	.release = dm_release,
};


static unsigned char gDrvrMajor = 0;
static struct device * prog_device;
static struct class * drvr_class;
static struct drvr_device * drvr_devices;


static inline ssize_t writeMem(struct file *filp, const char *buf, size_t count, loff_t *f_pos)
{
	unsigned short sBuf;
	struct drvr_mem * mem_to_write = &(((struct drvr_device *) filp->private_data)->data.mem);

	if (count == 2) {
		if (copy_from_user(&sBuf, buf, count))
			return -EFAULT;

		mem_to_write->virt_addr[(*f_pos) / 2] = sBuf;

		return count;
	}

	if (copy_from_user((void *) &(mem_to_write->virt_addr[(*f_pos) / 2]), buf, count)) {
		return -EFAULT;
	}

	return count;
}

static inline ssize_t readMem(struct file *filp, char *buf, size_t count, loff_t *f_pos)
{
	struct drvr_mem * mem_to_read = &(((struct drvr_device *) filp->private_data)->data.mem);

	if (copy_to_user(buf, (void *) &(mem_to_read->virt_addr[(*f_pos) / 2]), count)) {
		return -EFAULT;
	}

	return count;
}

static int dm_open(struct inode *inode, struct file *filp)
{
	struct drvr_device* dev = container_of(inode->i_cdev, struct drvr_device, cdev);

	filp->private_data = dev; /* for other methods */

	if (dev == NULL) {
		DBG_LOG("Failed to retrieve driver structure !\n");

		return -ENODEV;
	}

	if (dev->opened != 1) {
		if (dev->type != prog) {
			struct drvr_mem* mem_dev = &((dev->data).mem);

			if (request_mem_region((unsigned long) mem_dev->base_addr, FPGA_MEM_SIZE, DEVICE_NAME)==NULL) {
				DBG_LOG("Failed to request I/O memory region\n");

				return -ENOMEM;
			}

			mem_dev->virt_addr = ioremap_nocache(((unsigned long) mem_dev->base_addr), FPGA_MEM_SIZE);

			if (mem_dev->virt_addr == NULL) {
				DBG_LOG("Failed to remap I/O memory\n");

				return -ENOMEM;
			}

			DBG_LOG("mem interface opened\n");
		}

		dev->opened = 1;
	}

	return 0;
}

static int dm_release(struct inode *inode, struct file *filp)
{
	struct drvr_device* dev = container_of(inode->i_cdev, struct drvr_device, cdev);;

	if (dev->opened != 0) {
		if (dev->type == mem) {
			iounmap((dev->data.mem).virt_addr);
			release_mem_region(((unsigned long) (dev->data.mem).base_addr), FPGA_MEM_SIZE);
			DBG_LOG("module released\n");
		}

		dev->opened = 0;
	}

	return 0;
}

static ssize_t dm_write(struct file *filp, const char *buf, size_t count, loff_t *f_pos)
{
	struct drvr_device * dev = filp->private_data; /* for other methods */

	switch (dev->type) {
		case prog:
			return loadBitFile((dev->data.prog.i2c_io), buf, count);

		case mem:
			return writeMem(filp, buf, count, f_pos);

		default:
			return loadBitFile((dev->data.prog.i2c_io), buf, count);
	};
}

static ssize_t dm_read(struct file *filp, char *buf, size_t count, loff_t *f_pos)
{
	struct drvr_device * dev = filp->private_data; /* for other methods */

	switch (dev->type) {
		case prog:
			return -EPERM;

		case mem:
			return readMem(filp, buf, count, f_pos);

		default:
			return -EPERM;
	};
}

static void dm_exit(void)
{
	dev_t devno = MKDEV(gDrvrMajor, 0);

	/* Get rid of our char dev entries */
	if (drvr_devices) {
		int i;

		for (i = 1; i >= 0; i--) {
			if (i == 0) {
				i2c_unregister_device(drvr_devices[i].data.prog.i2c_io);
			}

			device_destroy(drvr_class, MKDEV(gDrvrMajor, i));
			cdev_del(&drvr_devices[i].cdev);
		}

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
	struct drvr_prog * progDev;
	struct i2c_adapter *i2c_adap;

	dev_t dev = 0;
	result = alloc_chrdev_region(&dev, 0, 2, DEVICE_NAME);
	gDrvrMajor = MAJOR(dev);

	if (result < 0) {
		DBG_LOG("Registering char device failed with %d\n", gDrvrMajor);

		return result;
	}

	drvr_devices = kmalloc(2 * sizeof(struct drvr_device), GFP_KERNEL);

	if (!drvr_devices) {
		dm_exit();

		return -ENOMEM;
	}

	drvr_class = class_create(THIS_MODULE, DEVICE_NAME);
	memset(drvr_devices, 0, 2 * sizeof(struct drvr_device));

	/*Initializing main mdevice for prog*/
	devno = MKDEV(gDrvrMajor, 0);
	drvr_devices[0].type = prog;
	progDev = &(drvr_devices[0].data.prog);
	prog_device = device_create(drvr_class, NULL, devno, NULL, DEVICE_NAME);//should create /dev entry for main node
	drvr_devices[0].opened = 0;

	/*Do the i2c stuff*/
	i2c_adap = i2c_get_adapter(I2C_ADAPTER);

	if (i2c_adap == NULL) {
		DBG_LOG("Cannot get I2C adapter %i\n", I2C_ADAPTER);
		dm_exit();

		return -ENODEV;
	}

	progDev->i2c_io = i2c_new_device(i2c_adap, &io_exp_info);

	if (prog_device == NULL) {
		class_destroy(drvr_class);
		drvr_devices[0].opened = 0;
		dm_exit();

		return -ENOMEM;
	}

	cdev_init(&(drvr_devices[0].cdev), &dm_ops);
	drvr_devices[0].cdev.owner = THIS_MODULE;
	drvr_devices[0].cdev.ops = &dm_ops;
	cdev_add(&(drvr_devices[0].cdev), devno, 1);
	//printk(KERN_INFO "'mknod /dev/%s c %d %d'.\n", DEVICE_NAME, gDrvrMajor, 0);
	/* Initialize each device. */
	devno = MKDEV(gDrvrMajor, 1);
	drvr_devices[1].type = mem;
	memDev = &(drvr_devices[1].data.mem);
	memDev->base_addr = (unsigned short *) (FPGA_BASE_ADDR);
	device_create(drvr_class, prog_device, devno, NULL, DEVICE_NAME_MEM);
	cdev_init(&(drvr_devices[1].cdev), &dm_ops);
	(drvr_devices[1].cdev).owner = THIS_MODULE;
	(drvr_devices[1].cdev).ops = &dm_ops;
	cdev_add(&(drvr_devices[1].cdev), devno, 1);
	drvr_devices[1].opened = 0;

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
