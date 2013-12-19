#include <linux/init.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/ioctl.h>
#include <asm/uaccess.h>   /* copy_to_user */
#include <linux/cdev.h>
#include <linux/sched.h>
#include <linux/memory.h>
#include <linux/dma-mapping.h>
#include <linux/edma.h>
#include <linux/platform_data/edma.h>
#include <linux/delay.h>
#include <linux/mutex.h>

//device tree support
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_dma.h>
#include <linux/of_gpio.h>
#include <linux/of_i2c.h>
#include "../mark1_dma/generic.h"
#include "../mark1_dma/config.h"


static int dm_open(struct inode *inode, struct file *filp);
static int dm_release(struct inode *inode, struct file *filp);
static ssize_t dm_write(struct file *filp, const char *buf, size_t count, loff_t *f_pos);
static ssize_t dm_read(struct file *filp, char *buf, size_t count, loff_t *f_pos);
static long dm_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);


static struct i2c_board_info io_exp_info= {
	I2C_BOARD_INFO("fpga_ctrl", I2_IO_EXP_ADDR),
};

static struct file_operations dm_ops = {
    .read =   dm_read,
    .write =  dm_write,
    .compat_ioctl =  dm_ioctl,
    .unlocked_ioctl = dm_ioctl,
    .open =   dm_open,
    .release =  dm_release,
};

enum drvr_type{
	prog,
	mem
};

struct drvr_prog{
	struct i2c_client * i2c_io;
};

struct drvr_mem{
	unsigned short * base_addr;
	unsigned short * virt_addr;
};

union drvr_data{
	struct drvr_prog prog;
	struct drvr_mem mem;
};

struct drvr_device{
	enum drvr_type type;
	union drvr_data data;
	struct cdev cdev;
	unsigned char opened;
};


static unsigned char gDrvrMajor = 0;

unsigned char * readBuffer;
unsigned char * writeBuffer;

struct device * prog_device;
struct class * drvr_class;
struct drvr_device * drvr_devices;


ssize_t writeMem(struct file *filp, const char *buf, size_t count, loff_t *f_pos)
{
	unsigned short sBuf;
	struct drvr_mem * mem_to_write = &(((struct drvr_device *) filp->private_data)->data.mem);

	if(count == 2){
		if(copy_from_user(&sBuf, buf, count)) return -1;
		mem_to_write->virt_addr[(*f_pos)/2] = sBuf;
		return count;	
	}

	if (copy_from_user((void *) &(mem_to_write->virt_addr[(*f_pos)/2]), buf, count) ) {
		return -1;		
	}

	return count;
}

ssize_t readMem(struct file *filp, char *buf, size_t count, loff_t *f_pos)
{
	struct drvr_mem * mem_to_read = &(((struct drvr_device *) filp->private_data)->data.mem);

	if (copy_to_user(buf, (void *) &(mem_to_read->virt_addr[(*f_pos)/2]), count) ) {
		return -1;
	}

	return count;
}

static int dm_open(struct inode *inode, struct file *filp)
{
	struct drvr_device * dev = container_of(inode->i_cdev, struct drvr_device, cdev);
	struct drvr_mem * mem_dev;

	filp->private_data = dev; /* for other methods */

	if(dev == NULL){
		printk("%s: Failed to retrieve driver structure !\n", DEVICE_NAME);

		return -1;	
	}

	if(dev->opened == 1){
		printk("%s: module already opened\n", DEVICE_NAME);

		return 0;
	}

	if(dev->type != prog){
		mem_dev = &((dev->data).mem);
		request_mem_region((unsigned long) mem_dev->base_addr, FPGA_MEM_SIZE, DEVICE_NAME);
		mem_dev->virt_addr = ioremap_nocache(((unsigned long) mem_dev->base_addr), FPGA_MEM_SIZE);
		printk("mem interface opened \n");	
	}

	dev->opened = 1;

	return 0;
}

static int dm_release(struct inode *inode, struct file *filp)
{
	struct drvr_device * dev = container_of(inode->i_cdev, struct drvr_device, cdev);;

	if(dev->opened == 0){
		printk("%s: module already released\n", DEVICE_NAME);
		return 0;
	}

	if(dev->type == mem){
		iounmap((dev->data.mem).virt_addr);
		release_mem_region(((unsigned long) (dev->data.mem).base_addr), FPGA_MEM_SIZE );
		printk("%s: Release: module released\n",DEVICE_NAME);
	}

	dev->opened = 0;

	return 0;
}

static ssize_t dm_write(struct file *filp, const char *buf, size_t count, loff_t *f_pos)
{
	struct drvr_device * dev = filp->private_data; /* for other methods */

	switch(dev->type){
		case prog :		
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

	switch(dev->type){
		case prog :
			return -1;

		case mem:
			return readMem(filp, buf, count, f_pos);

		default:
			return -1;	
	};
}

static long dm_ioctl(struct file *filp, unsigned int cmd, unsigned long arg){
	printk("ioctl failed \n");

	return -ENOTTY;
}

static void dm_exit(void)
{
	int i;
	dev_t devno = MKDEV(gDrvrMajor, 0);

	/* Get rid of our char dev entries */
	if (drvr_devices) {
		for (i = 0; i < 2; i++) {
			if(i == 0){
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
                 printk(KERN_ALERT "Registering char device failed with %d\n", gDrvrMajor);
                return result;
        }

        drvr_devices = kmalloc(2 * sizeof(struct drvr_device), GFP_KERNEL);

        if (! drvr_devices) {
                result = -ENOMEM;
                goto fail;  /* Make this more graceful */
        }

	drvr_class = class_create(THIS_MODULE,DEVICE_NAME);
        memset(drvr_devices, 0, 2 * sizeof(struct drvr_device));
	
	/*Initializing main mdevice for prog*/
	devno = MKDEV(gDrvrMajor, 0);
	drvr_devices[0].type = prog;
	progDev = &(drvr_devices[0].data.prog);
	prog_device = device_create(drvr_class, NULL, devno, NULL, DEVICE_NAME);	// should create /dev entry for main node
	drvr_devices[0].opened = 0;
	
	/*Do the i2c stuff*/
	i2c_adap = i2c_get_adapter(1); // todo need to check i2c adapter id

	if(i2c_adap == NULL){
		printk("Cannot get adapter 1 \n");
		goto fail;
	}

	progDev->i2c_io = i2c_new_device(i2c_adap , &io_exp_info);
	i2c_put_adapter(i2c_adap); //don't know what it does, seems to release the adapter ...
	
	if(prog_device == NULL){
		class_destroy(drvr_class);
   	 	result = -ENOMEM;
		drvr_devices[0].opened = 0;
                goto fail;
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

	return 0;

fail:
        dm_exit();

        return -1;

}

static const struct of_device_id drvr_of_match[] = {
	{ .compatible = DEVICE_NAME, },
	{ },
};

MODULE_DEVICE_TABLE(of, drvr_of_match);
MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Jonathan Piat <piat.jonathan@gmail.com>");

module_init(dm_init);
module_exit(dm_exit);
