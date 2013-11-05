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


#define SSI_CLK 02 // to be verified ...
#define SSI_DATA 04
#define SSI_DONE 0x03
#define SSI_PROG 0x05
#define SSI_INIT 0x06
#define MODE0	0
#define MODE1 1


#define FPGA_BASE_ADDR	 0x01000000
#define MEM_SIZE	 131072

#define DEVICE_NAME "mark1"

#define I2_IO_EXP_ADDR	0x70

static char * gDrvrName = DEVICE_NAME;
static unsigned char gDrvrMajor = 0 ;

unsigned char * readBuffer ;
unsigned char * writeBuffer ;


static int mark1_dm_open(struct inode *inode, struct file *filp);
static int mark1_dm_release(struct inode *inode, struct file *filp);
static ssize_t mark1_dm_write(struct file *filp, const char *buf, size_t count,
                       loff_t *f_pos);
static ssize_t mark1_dm_read(struct file *filp, char *buf, size_t count, loff_t *f_pos);
static long mark1_dm_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);



static struct i2c_board_info io_exp_info= {
	I2C_BOARD_INFO("fpga_ctrl", I2_IO_EXP_ADDR),
};

static struct file_operations mark1_dm_ops = {
    .read =   mark1_dm_read,
    .write =  mark1_dm_write,
    .compat_ioctl =  mark1_dm_ioctl,
    .unlocked_ioctl = mark1_dm_ioctl,
    .open =   mark1_dm_open,
    .release =  mark1_dm_release,
};

enum mark1_type{
	prog,
	mem
};

struct mark1_prog{
	struct i2c_client * i2c_io;
};


struct mark1_mem{
	unsigned short * base_addr ;
	unsigned short * virt_addr ;
};

union mark1_data{
	struct mark1_prog prog;
	struct mark1_mem mem;
};

struct mark1_device{
	enum mark1_type type ;
	union mark1_data data ;
	struct cdev cdev;
	unsigned char opened ;
};


struct device * prog_device ;
struct class * mark1_class ;
struct mark1_device * mark1_devices ;


int loadBitFile(struct i2c_client * io_cli, const unsigned char * bitBuffer_user, unsigned int length);




#define SSI_DELAY 300


inline void __delay_cycles(unsigned long cycles){
	while(cycles != 0){
		cycles -- ;	
	}
}

volatile unsigned * gpio_regs ;

#define GPIO0_BASE 0x44E07000
#define GPIO0_SETDATAOUT *(gpio_regs+1)
#define GPIO0_CLEARDATAOUT *(gpio_regs)
inline void ssiSetClk(void){
	//gpio_set_value(SSI_CLK, 1);
	GPIO0_SETDATAOUT = (1 << 2) ;
}

inline void ssiClearClk(void){
	//gpio_set_value(SSI_CLK, 0);
	GPIO0_CLEARDATAOUT = (1 << 2);
}

inline void ssiSetData(void){
	//gpio_set_value(SSI_DATA, 1);
	GPIO0_SETDATAOUT= (1 << 4) ;
}

inline void ssiClearData(void){
	//gpio_set_value(SSI_DATA, 0);
	GPIO0_CLEARDATAOUT = (1 << 4);
}

inline void serialConfigWriteByte(unsigned char val){
	unsigned char bitCount = 0 ;
	unsigned char valBuf = val ;
	for(bitCount = 0 ; bitCount < 8 ; bitCount ++){
		ssiClearClk();
		if((valBuf & 0x80) != 0){
			ssiSetData();
		}else{
			ssiClearData();
		}
		__delay_cycles(SSI_DELAY);	
		ssiSetClk();
		valBuf = (valBuf << 1);
		__delay_cycles(SSI_DELAY);			
	}
}




inline void i2c_set_pin(struct i2c_client * io_cli, unsigned char pin, unsigned char val){
	unsigned char i2c_buffer [2] ;
	i2c_buffer[0] = pin;
	i2c_master_send(io_cli, i2c_buffer, 1); 
}

inline unsigned char i2c_get_pin(struct i2c_client * io_cli, unsigned char pin){
	unsigned char i2c_buffer [2] ;
	i2c_buffer[0] = pin;
	//i2c_master_send(io_cli, &i2c_buffer, 1); 
	i2c_master_recv(io_cli, &i2c_buffer, 2); 
	//printk("reading value %x \n", i2c_buffer);
	return i2c_buffer[0] ;
}

int loadBitFile(struct i2c_client * io_cli, const unsigned char * bitBuffer_user, const unsigned int length){
	unsigned char cfg = 1 ;	
	unsigned char i2c_test ;
	unsigned long int i ;
	unsigned long int timer = 0;
	unsigned char * bitBuffer ;	
	unsigned char i2c_buffer [4] ;
	//request_mem_region(GPIO0_BASE + 0x190, 8, gDrvrName);
	gpio_regs = ioremap_nocache(GPIO0_BASE + 0x190, 2*sizeof(int));

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
	/*i2c_set_pin(io_cli, MODE0, 1);
	i2c_set_pin(io_cli, MODE1, 1);
	i2c_set_pin(io_cli, SSI_PROG, 0);*/

	gpio_direction_output(SSI_CLK, 0);
	gpio_direction_output(SSI_DATA, 0);

	
	gpio_set_value(SSI_CLK, 0);
	//i2c_set_pin(io_cli, SSI_PROG, 1);
	//__delay_cycles(10*SSI_DELAY);	
	i2c_set_pin(io_cli, SSI_PROG, 0);
	__delay_cycles(5*SSI_DELAY);
	
	//wait for FPGA to successfully enter configuration mode
	do {
		i2c_test = i2c_get_pin_ex(io_cli, SSI_INIT);
	}
	while(i2c_test!= 0x01 && timer++ < 100);

	if(timer>=100){
		printk("FPGA did not answer to prog request, init pin not going high \n");
		gpio_free(SSI_CLK);
		gpio_free(SSI_DATA);

		return -ENOTTY;
	}

	//debug only
	printk("loop finished with %x from LPC; iter=%i\n", i2c_test, timer);

	timer = 0;
	printk("Starting configuration of %d bits \n", length*8);
	for(i = 0 ; i < length ; i ++){
		serialConfigWriteByte(bitBuffer[i]);	
		schedule();
	}
	printk("Waiting for done pin to go high \n");
	while(timer < 50){
		ssiClearClk();
		__delay_cycles(SSI_DELAY);	
		ssiSetClk();
		__delay_cycles(SSI_DELAY);	
		timer ++ ;
	}
	gpio_set_value(SSI_CLK, 0);
	gpio_set_value(SSI_DATA, 1);	
	if(i2c_get_pin(io_cli, SSI_DONE) == 0){
		printk("FPGA prog failed, done pin not going high \n");
		gpio_direction_input(SSI_CLK);
		gpio_direction_input(SSI_DATA);
		gpio_free(SSI_CLK);
		gpio_free(SSI_DATA);
		return -ENOTTY;		
	}

	/*i2c_buffer[0] = I2C_IO_EXP_CONFIG_REG;
	i2c_buffer[1] = 0xDC;
	i2c_master_send(io_cli, i2c_buffer, 2); // set all unused config pins as input (keeping mode pins and PROG as output)*/
	gpio_direction_input(SSI_CLK);
	gpio_direction_input(SSI_DATA);
	gpio_free(SSI_CLK);
	gpio_free(SSI_DATA);
	iounmap(gpio_regs);
	//release_mem_region(GPIO0_BASE + 0x190, 8);
	kfree(bitBuffer) ;
	return length ;
}



ssize_t writeMem(struct file *filp, const char *buf, size_t count,
                       loff_t *f_pos)
{
	unsigned int ret = 0;
	unsigned short sBuf ;
	struct mark1_mem * mem_to_write = &(((struct mark1_device *) filp->private_data)->data.mem) ;
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
	unsigned int ret = 0;
	struct mark1_mem * mem_to_read = &(((struct mark1_device *) filp->private_data)->data.mem) ;
	if (copy_to_user(buf, (void *) &(mem_to_read->virt_addr[(*f_pos)/2]), count) ) {
		return -1 ;
	}
	return count;
}



static int mark1_dm_open(struct inode *inode, struct file *filp)
{
	struct mark1_device * dev ;
	struct mark1_mem * mem_dev ;
	dev = container_of(inode->i_cdev, struct mark1_device, cdev);
	filp->private_data = dev; /* for other methods */
	if(dev == NULL){
		printk("Failed to retrieve mark1 structure !\n");
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

static int mark1_dm_release(struct inode *inode, struct file *filp)
{
	struct mark1_device * dev ;
	dev = container_of(inode->i_cdev, struct mark1_device, cdev);
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


static ssize_t mark1_dm_write(struct file *filp, const char *buf, size_t count,
                       loff_t *f_pos)
{
	struct mark1_device * dev ;
	dev = filp->private_data ; /* for other methods */
	switch(dev->type){
		case prog :
			
			return loadBitFile((dev->data.prog.i2c_io), buf, count);
		case mem:
			return writeMem(filp, buf, count, f_pos);
		default:
			return loadBitFile((dev->data.prog.i2c_io), buf, count);	
	};
	
}


static ssize_t mark1_dm_read(struct file *filp, char *buf, size_t count, loff_t *f_pos)
{
	struct mark1_device * dev ;
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


static long mark1_dm_ioctl(struct file *filp, unsigned int cmd, unsigned long arg){
	printk("ioctl failed \n");
	return -ENOTTY;
}



static void mark1_dm_exit(void)
{
	int i;
	dev_t devno = MKDEV(gDrvrMajor, 0);
	/* Get rid of our char dev entries */
	if (mark1_devices) {
		for (i = 0; i < 2; i++) {
			if(i == 0){
				i2c_unregister_device(mark1_devices[i].data.prog.i2c_io);			
			}
			device_destroy(mark1_class, MKDEV(gDrvrMajor, i));
			cdev_del(&mark1_devices[i].cdev);
		}
		kfree(mark1_devices);
	}
	class_destroy(mark1_class);
	/* cleanup_module is never called if registering failed */
	unregister_chrdev_region(devno, 2);
}

static int mark1_dm_init(void)
{
		
	int result;
	int devno ;
	struct mark1_mem * memDev ;
	struct mark1_prog * progDev ;
	struct i2c_adapter *i2c_adap;
        dev_t dev = 0;
	result = alloc_chrdev_region(&dev, 0, 2,
                                gDrvrName);
        gDrvrMajor = MAJOR(dev);
        if (result < 0) {
                 printk(KERN_ALERT "Registering char device failed with %d\n", gDrvrMajor);
                return result;
        }
        mark1_devices = kmalloc(2 * sizeof(struct mark1_device), GFP_KERNEL);
        if (! mark1_devices) {
                result = -ENOMEM;
                goto fail;  /* Make this more graceful */
        }

	mark1_class = class_create(THIS_MODULE,DEVICE_NAME);

        memset(mark1_devices, 0, 2 * sizeof(struct mark1_device));
	
	/*Initializing main mdevice for prog*/
	devno = MKDEV(gDrvrMajor, 0);
	mark1_devices[0].type = prog ;
	progDev = &(mark1_devices[0].data.prog);
	prog_device = device_create(mark1_class, NULL, devno, NULL, DEVICE_NAME);	// should create /dev entry for main node
	mark1_devices[0].opened = 0 ;
	
	/*
	Do the i2c stuff
	*/
	i2c_adap = i2c_get_adapter(1); // todo need to check i2c adapter id
	if(i2c_adap == NULL){
		printk("Cannot get adapter 1 \n");
		goto fail ;
	}
	progDev->i2c_io = i2c_new_device(i2c_adap , &io_exp_info);
	i2c_put_adapter(i2c_adap); //don't know what it does, seems to release the adapter ...
	
	if(prog_device == NULL){
		class_destroy(mark1_class);
   	 	result = -ENOMEM;
		mark1_devices[0].opened = 0 ;
                goto fail;
	}
	cdev_init(&(mark1_devices[0].cdev), &mark1_dm_ops);
	mark1_devices[0].cdev.owner = THIS_MODULE;
	mark1_devices[0].cdev.ops = &mark1_dm_ops;
	cdev_add(&(mark1_devices[0].cdev), devno, 1);
	//printk(KERN_INFO "'mknod /dev/%s c %d %d'.\n", gDrvrName, gDrvrMajor, 0);
	/* Initialize each device. */
	devno = MKDEV(gDrvrMajor, 1);
	mark1_devices[1].type = mem ;
	memDev = &(mark1_devices[1].data.mem);
	memDev->base_addr = (unsigned short *) (FPGA_BASE_ADDR);
	device_create(mark1_class, prog_device, devno, NULL, "mark1_mem"); 
	cdev_init(&(mark1_devices[1].cdev), &mark1_dm_ops);
	(mark1_devices[1].cdev).owner = THIS_MODULE;
	(mark1_devices[1].cdev).ops = &mark1_dm_ops;
	cdev_add(&(mark1_devices[1].cdev), devno, 1);
	mark1_devices[1].opened = 0 ;
	return 0 ;
  fail:
        mark1_dm_exit();
        return -1 ;

}

static const struct of_device_id mark1_of_match[] = {
	{ .compatible = "mark1", },
	{ },
};
MODULE_DEVICE_TABLE(of, mark1_of_match);


MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Jonathan Piat <piat.jonathan@gmail.com>");

module_init(mark1_dm_init);
module_exit(mark1_dm_exit);
