#include <linux/init.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/ioctl.h>
#include <asm/uaccess.h>   /* copy_to_user */
#include <asm/io.h>
#include <linux/cdev.h>
#include <linux/sched.h>
#include <linux/memory.h>
#include <linux/gpio.h>
#include <linux/dma-mapping.h>
#include <linux/edma.h>
#include <linux/platform_data/edma.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/i2c.h>
//device tree support
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_dma.h>
#include <linux/of_gpio.h>
#include <linux/of_i2c.h>


#define SSI_CLK 02
#define SSI_DATA 04
#define SSI_DONE 72
#define SSI_PROG 76
#define SSI_INIT 74


#define FPGA_BASE_ADDR	 0x01000000
#define MEM_SIZE	 131072

#define DEVICE_NAME "logibone"


static char * gDrvrName = DEVICE_NAME;
static unsigned char gDrvrMajor = 0 ;

unsigned char * readBuffer ;
unsigned char * writeBuffer ;


static int LOGIBONE_dm_open(struct inode *inode, struct file *filp);
static int LOGIBONE_dm_release(struct inode *inode, struct file *filp);
static ssize_t LOGIBONE_dm_write(struct file *filp, const char *buf, size_t count,
                       loff_t *f_pos);
static ssize_t LOGIBONE_dm_read(struct file *filp, char *buf, size_t count, loff_t *f_pos);
static long LOGIBONE_dm_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);



static struct file_operations LOGIBONE_dm_ops = {
    .read =   LOGIBONE_dm_read,
    .write =  LOGIBONE_dm_write,
    .compat_ioctl =  LOGIBONE_dm_ioctl,
    .unlocked_ioctl = LOGIBONE_dm_ioctl,
    .open =   LOGIBONE_dm_open,
    .release =  LOGIBONE_dm_release,
};

enum logibone_type{
	prog,
	mem
};

struct logibone_prog{
	unsigned int dummy ;
};


struct logibone_mem{
	unsigned short * base_addr ;
	unsigned short * virt_addr ;
};

union logibone_data{
	struct logibone_prog prog;
	struct logibone_mem mem;
};

struct logibone_device{
	enum logibone_type type ;
	union logibone_data data ;
	struct cdev cdev;
	unsigned char opened ;
};


struct device * prog_device ;
struct class * logibone_class ;
struct logibone_device * logibone_devices ;


int loadBitFile(const unsigned char * bitBuffer_user, unsigned int length);




#define SSI_DELAY 1
inline void __delay_cycles(unsigned long cycles){
	while(cycles != 0){
		cycles -- ;	
	}
}


inline void serialConfigWriteByte(unsigned char val){
	unsigned char bitCount = 0 ;
	unsigned char valBuf = val ;
	for(bitCount = 0 ; bitCount < 8 ; bitCount ++){
		gpio_set_value(SSI_CLK, 0);
		if((valBuf & 0x80) != 0){
			gpio_set_value(SSI_DATA, 1);
		}else{
			gpio_set_value(SSI_DATA, 0);
		}
		//__delay_cycles(SSI_DELAY);	
		gpio_set_value(SSI_CLK, 1);
		valBuf = (valBuf << 1);
		//__delay_cycles(SSI_DELAY);			
	}
}


int loadBitFile(const unsigned char * bitBuffer_user, const unsigned int length){
	unsigned char cfg = 1 ;	
	unsigned long int i ;
	unsigned long int timer = 0;
	unsigned char * bitBuffer ;	

	bitBuffer = kmalloc(length, GFP_KERNEL);
	if(bitBuffer == NULL || copy_from_user(bitBuffer, bitBuffer_user, length)){
		printk("Failed allocate buffer for configuration file \n");
		return -ENOTTY;	
	}

	cfg = gpio_request(SSI_CLK, "ssi_clk");
	if(cfg < 0){
		printk("Failed to take control over ssi_clk pin \n");
		return -ENOTTY;
	}
	cfg = gpio_request(SSI_DATA, "ssi_data");
	if(cfg < 0){
		printk("Failed to take control over ssi_data pin \n");
		return -ENOTTY;
	}
	cfg = gpio_request(SSI_PROG, "ssi_prog");
	if(cfg < 0){
		printk("Failed to take control over ssi_prog pin \n");
		return -ENOTTY;
	}
	cfg = gpio_request(SSI_INIT, "ssi_init");
	if(cfg < 0){
		printk("Failed to take control over ssi_init pin \n");
		return -ENOTTY;
	}
	cfg = gpio_request(SSI_DONE, "ssi_done");
	if(cfg < 0){
		printk("Failed to take control over ssi_done pin \n");
		return -ENOTTY;
	}
	

	gpio_direction_output(SSI_CLK, 0);
	gpio_direction_output(SSI_DATA, 0);
	gpio_direction_output(SSI_PROG, 0);

	gpio_direction_input(SSI_INIT);
	gpio_direction_input(SSI_DONE);
	
	gpio_set_value(SSI_CLK, 0);
	gpio_set_value(SSI_PROG, 1);
	__delay_cycles(10*SSI_DELAY);	
	gpio_set_value(SSI_PROG, 0);
	__delay_cycles(5*SSI_DELAY);		
	while(gpio_get_value(SSI_INIT) > 0 && timer < 200) timer ++; // waiting for init pin to go down
	if(timer >= 200){
		printk("FPGA did not answer to prog request, init pin not going low \n");
		gpio_set_value(SSI_PROG, 1);
		return -ENOTTY;	
	}
	timer = 0;
	__delay_cycles(5*SSI_DELAY);
	gpio_set_value(SSI_PROG, 1);
	while(gpio_get_value(SSI_INIT) == 0 && timer < 0xFFFFFF){
		 timer ++; // waiting for init pin to go up
	}
	if(timer >= 0xFFFFFF){
		printk("FPGA did not answer to prog request, init pin not going high \n");
		return -ENOTTY;	
	}
	timer = 0;
	printk("Starting configuration of %d bits \n", length*8);
	for(i = 0 ; i < length ; i ++){
		serialConfigWriteByte(bitBuffer[i]);	
		schedule();
	}
	printk("Waiting for done pin to go high \n");
	while(timer < 50 && gpio_get_value(SSI_DONE) == 0){
		gpio_set_value(SSI_CLK, 0);
		__delay_cycles(SSI_DELAY);	
		gpio_set_value(SSI_CLK, 1);
		__delay_cycles(SSI_DELAY);	
		timer ++ ;
	}
	gpio_set_value(SSI_CLK, 0);
	gpio_set_value(SSI_DATA, 1);	
	if(gpio_get_value(SSI_DONE) == 0 && timer >= 255){
		printk("FPGA prog failed, done pin not going high \n");
		return -ENOTTY;		
	}

	gpio_free(SSI_CLK);
	gpio_free(SSI_DATA);
	gpio_free(SSI_PROG);
	gpio_free(SSI_INIT);
	gpio_free(SSI_DONE);

	kfree(bitBuffer) ;
	return length ;
}


ssize_t writeMem(struct file *filp, const char *buf, size_t count,
                       loff_t *f_pos)
{
	unsigned short sBuf ;
	struct logibone_mem * mem_to_write = &(((struct logibone_device *) filp->private_data)->data.mem) ;
	if(count == 2){
		if(copy_from_user(&sBuf, buf, count)) return -1 ;
		mem_to_write->virt_addr[(*f_pos)/2] = sBuf ; 
		return count ;	
	}
	if (copy_from_user((void *) &(mem_to_write->virt_addr[(*f_pos)/2]), buf, count) ) {
		return -1 ;		
	}
	return count;
}


ssize_t readMem(struct file *filp, char *buf, size_t count, loff_t *f_pos)
{
	struct logibone_mem * mem_to_read = &(((struct logibone_device *) filp->private_data)->data.mem) ;
	if (copy_to_user(buf, (void *) &(mem_to_read->virt_addr[(*f_pos)/2]), count) ) {
		return -1 ;
	}
	return count;
}



static int LOGIBONE_dm_open(struct inode *inode, struct file *filp)
{
	struct logibone_device * dev ;
	struct logibone_mem * mem_dev ;
	dev = container_of(inode->i_cdev, struct logibone_device, cdev);
	filp->private_data = dev; /* for other methods */
	if(dev == NULL){
		printk("Failed to retrieve logibone structure !\n");
		return -1 ;	
	}
	if(dev->opened == 1){
		printk("%s: module already opened\n", gDrvrName);
		return 0 ;
	}

	if(dev->type == prog){
		
	}else{
		mem_dev = &((dev->data).mem) ;
		request_mem_region((unsigned long) mem_dev->base_addr, MEM_SIZE, gDrvrName);
		mem_dev->virt_addr = ioremap_nocache(((unsigned long) mem_dev->base_addr), MEM_SIZE);
		printk("mem interface opened \n");	
	}
	dev->opened = 1 ;
	return 0;
}

static int LOGIBONE_dm_release(struct inode *inode, struct file *filp)
{
	struct logibone_device * dev ;
	dev = container_of(inode->i_cdev, struct logibone_device, cdev);
	if(dev->opened == 0){
		printk("%s: module already released\n", gDrvrName);
		return 0 ;
	}
	if(dev->type == mem){
		iounmap((dev->data.mem).virt_addr);
		release_mem_region(((unsigned long) (dev->data.mem).base_addr), MEM_SIZE );
		printk("%s: Release: module released\n",gDrvrName);
	}
	dev->opened = 0 ;
	return 0;
}


static ssize_t LOGIBONE_dm_write(struct file *filp, const char *buf, size_t count,
                       loff_t *f_pos)
{
	struct logibone_device * dev ;
	dev = filp->private_data ; /* for other methods */
	switch(dev->type){
		case prog :
			
			return loadBitFile(buf, count);
		case mem:
			return writeMem(filp, buf, count, f_pos);
		default:
			return loadBitFile( buf, count);	
	};
	
}


static ssize_t LOGIBONE_dm_read(struct file *filp, char *buf, size_t count, loff_t *f_pos)
{
	struct logibone_device * dev ;
	dev = filp->private_data ; /* for other methods */
	switch(dev->type){
		case prog :
			return -1;
		case mem:
			return readMem(filp, buf, count, f_pos);
		default:
			return -1 ;	
	};
}


static long LOGIBONE_dm_ioctl(struct file *filp, unsigned int cmd, unsigned long arg){
	printk("ioctl failed \n");
	return -ENOTTY;
}



static void LOGIBONE_dm_exit(void)
{
	int i;
	dev_t devno = MKDEV(gDrvrMajor, 0);
	/* Get rid of our char dev entries */
	if (logibone_devices) {
		for (i = 0; i < 2; i++) {
			device_destroy(logibone_class, MKDEV(gDrvrMajor, i));
			cdev_del(&logibone_devices[i].cdev);
		}
		kfree(logibone_devices);
	}
	class_destroy(logibone_class);
	/* cleanup_module is never called if registering failed */
	unregister_chrdev_region(devno, 2);
}

static int LOGIBONE_dm_init(void)
{
		
	int result;
	int devno ;
	struct logibone_mem * memDev ;
	struct logibone_prog * progDev ;
        dev_t dev = 0;
	result = alloc_chrdev_region(&dev, 0, 2,
                                gDrvrName);
        gDrvrMajor = MAJOR(dev);
        if (result < 0) {
                 printk(KERN_ALERT "Registering char device failed with %d\n", gDrvrMajor);
                return result;
        }
        logibone_devices = kmalloc(2 * sizeof(struct logibone_device), GFP_KERNEL);
        if (! logibone_devices) {
                result = -ENOMEM;
                goto fail;  /* Make this more graceful */
        }

	logibone_class = class_create(THIS_MODULE,DEVICE_NAME);

        memset(logibone_devices, 0, 2 * sizeof(struct logibone_device));
	
	/*Initializing main mdevice for prog*/
	devno = MKDEV(gDrvrMajor, 0);
	logibone_devices[0].type = prog ;
	progDev = &(logibone_devices[0].data.prog);
	prog_device = device_create(logibone_class, NULL, devno, NULL, DEVICE_NAME);	// should create /dev entry for main node
	logibone_devices[0].opened = 0 ;
	
	
	if(prog_device == NULL){
		class_destroy(logibone_class);
   	 	result = -ENOMEM;
		logibone_devices[0].opened = 0 ;
                goto fail;
	}
	cdev_init(&(logibone_devices[0].cdev), &LOGIBONE_dm_ops);
	logibone_devices[0].cdev.owner = THIS_MODULE;
	logibone_devices[0].cdev.ops = &LOGIBONE_dm_ops;
	cdev_add(&(logibone_devices[0].cdev), devno, 1);
	//printk(KERN_INFO "'mknod /dev/%s c %d %d'.\n", gDrvrName, gDrvrMajor, 0);
	/* Initialize each device. */
	devno = MKDEV(gDrvrMajor, 1);
	logibone_devices[1].type = mem ;
	memDev = &(logibone_devices[1].data.mem);
	memDev->base_addr = (unsigned short *) (FPGA_BASE_ADDR);
	device_create(logibone_class, prog_device, devno, NULL, "logibone_mem"); 
	cdev_init(&(logibone_devices[1].cdev), &LOGIBONE_dm_ops);
	(logibone_devices[1].cdev).owner = THIS_MODULE;
	(logibone_devices[1].cdev).ops = &LOGIBONE_dm_ops;
	cdev_add(&(logibone_devices[1].cdev), devno, 1);
	logibone_devices[1].opened = 0 ;
	return 0 ;
  fail:
        LOGIBONE_dm_exit();
        return -1 ;

}

static const struct of_device_id logibone_of_match[] = {
	{ .compatible = "logibone", },
	{ },
};
MODULE_DEVICE_TABLE(of, logibone_of_match);


MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Jonathan Piat <piat.jonathan@gmail.com>");

module_init(LOGIBONE_dm_init);
module_exit(LOGIBONE_dm_exit);
