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

//device tree support
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_dma.h>
#include <linux/of_gpio.h>
#include <linux/of_i2c.h>


#define SSI_CLK 02
#define SSI_DATA 03
#define SSI_DONE 48
#define SSI_PROG 30
#define SSI_INIT 31

/* Use 'p' as magic number */
#define LOGIBONE_FIFO_IOC_MAGIC 'p'
/* Please use a different 8-bit number in your code */
#define LOGIBONE_FIFO_RESET _IO(LOGIBONE_FIFO_IOC_MAGIC, 0)


#define LOGIBONE_FIFO_PEEK _IOR(LOGIBONE_FIFO_IOC_MAGIC, 1, short)
#define LOGIBONE_FIFO_NB_FREE _IOR(LOGIBONE_FIFO_IOC_MAGIC, 2, short)
#define LOGIBONE_FIFO_NB_AVAILABLE _IOR(LOGIBONE_FIFO_IOC_MAGIC, 3, short)
#define LOGIBONE_FIFO_SIZE _IOR(LOGIBONE_FIFO_IOC_MAGIC, 4, short)
#define LOGIBONE_FIFO_MODE _IO(LOGIBONE_FIFO_IOC_MAGIC, 5)
#define LOGIBONE_DIRECT_MODE _IO(LOGIBONE_FIFO_IOC_MAGIC, 6)


#define FPGA_BASE_ADDR	 0x01000000
#define FIFO_BASE_ADDR	 0x00000000
#define FIFO_CMD_OFFSET  512
#define FIFO_SIZE_OFFSET	(FIFO_CMD_OFFSET)
#define FIFO_NB_AVAILABLE_A_OFFSET	(FIFO_CMD_OFFSET + 1)
#define FIFO_NB_AVAILABLE_B_OFFSET	(FIFO_CMD_OFFSET + 2)
#define FIFO_PEEK_OFFSET	(FIFO_CMD_OFFSET + 3)
#define FIFO_READ_OFFSET	0
#define FIFO_WRITE_OFFSET	0
#define FIFO_BLOCK_SIZE	1024  //512 * 16 bits


static char * gDrvrName = "logibone";
static unsigned char gDrvrMajor = 0 ;


volatile unsigned short * gpmc_cs1_pointer ;
volatile unsigned short * gpmc_cs1_virt ;



unsigned int fifo_size ;

static int LOGIBONE_fifo_open(struct inode *inode, struct file *filp);
static int LOGIBONE_fifo_release(struct inode *inode, struct file *filp);
static ssize_t LOGIBONE_fifo_write(struct file *filp, const char *buf, size_t count,
                       loff_t *f_pos);
static ssize_t LOGIBONE_fifo_read(struct file *filp, char *buf, size_t count, loff_t *f_pos);
static long LOGIBONE_fifo_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);


static struct file_operations LOGIBONE_fifo_ops = {
    .read =   LOGIBONE_fifo_read,
    .write =  LOGIBONE_fifo_write,
    .compat_ioctl =  LOGIBONE_fifo_ioctl,
    .unlocked_ioctl = LOGIBONE_fifo_ioctl,
    .open =   LOGIBONE_fifo_open,
    .release =  LOGIBONE_fifo_release,
};

int loadBitFile(const unsigned char * bitBuffer_user, unsigned int length);
unsigned short int getNbFree(void);
unsigned short int getNbAvailable(void);

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


static int LOGIBONE_fifo_open(struct inode *inode, struct file *filp)
{
    request_mem_region(FPGA_BASE_ADDR, FIFO_BLOCK_SIZE, gDrvrName); //TODO: may block EDMA transfer ...
    gpmc_cs1_virt = ioremap_nocache(FPGA_BASE_ADDR + FIFO_CMD_OFFSET, 16); //TODO: may block EDMA transfer ...
    gpmc_cs1_pointer = (unsigned short *) FPGA_BASE_ADDR ;
    fifo_size = gpmc_cs1_virt[FIFO_SIZE_OFFSET] ;
    printk("%s: Open: module opened, with fifo size %d\n",gDrvrName, fifo_size);
    return 0;
}

static int LOGIBONE_fifo_release(struct inode *inode, struct file *filp)
{
    iounmap(gpmc_cs1_virt);
    release_mem_region(FPGA_BASE_ADDR, FIFO_BLOCK_SIZE );
    printk("%s: Release: module released\n",gDrvrName);
    return 0;
}


static ssize_t LOGIBONE_fifo_write(struct file *filp, const char *buf, size_t count,
                       loff_t *f_pos)
{
	return loadBitFile(buf, count);
}


static ssize_t LOGIBONE_fifo_read(struct file *filp, char *buf, size_t count, loff_t *f_pos)
{
	return -1 ;
}

unsigned short int getNbAvailable(void){
	return (gpmc_cs1_virt[FIFO_NB_AVAILABLE_B_OFFSET]*2) ;  
}

unsigned short int getNbFree(void){
	fifo_size = gpmc_cs1_virt[FIFO_SIZE_OFFSET] ;
	return ((fifo_size - gpmc_cs1_virt[FIFO_NB_AVAILABLE_A_OFFSET])*2) ;
}

unsigned short int getSize(void){
	return (gpmc_cs1_virt[FIFO_SIZE_OFFSET]*2) ;
}


static long LOGIBONE_fifo_ioctl(struct file *filp, unsigned int cmd, unsigned long arg){
	printk("calling ioctl %d on logibone \n", cmd);
	switch(cmd){
		case LOGIBONE_FIFO_RESET :
			printk("performing reset on fifo ! \n");
			gpmc_cs1_virt[FIFO_NB_AVAILABLE_A_OFFSET] = 0 ;
			gpmc_cs1_virt[FIFO_NB_AVAILABLE_B_OFFSET] = 0 ;
			return 0 ;
		case LOGIBONE_FIFO_PEEK :
			printk("performing peek on fifo ! \n");
			return  gpmc_cs1_virt[FIFO_PEEK_OFFSET] ;
		case LOGIBONE_FIFO_NB_FREE :
			printk("performing getNbFRee() on fifo ! \n");
			return getNbFree() ;
		case LOGIBONE_FIFO_NB_AVAILABLE :
			printk("performing getNbAvailable() on fifo ! \n");
			return getNbAvailable() ;
		case LOGIBONE_FIFO_SIZE :
			printk("calling fifo size ! \n");
			return getSize() ;
		case LOGIBONE_FIFO_MODE :
			printk("calling fifo mode! \n");
			return 0 ;	
		case LOGIBONE_DIRECT_MODE :
			printk("calling direct mode! \n");
			return 0 ;
		default: /* redundant, as cmd was checked against MAXNR */
			printk("unknown command %d \n", cmd);
			return -ENOTTY;
	}
	printk("ioctl failed \n");
}


static int LOGIBONE_fifo_init(void)
{
	gDrvrMajor = register_chrdev(0, gDrvrName, &LOGIBONE_fifo_ops );
 	/* Fail gracefully if need be */
	if (gDrvrMajor < 0) {
	  printk(KERN_ALERT "Registering char device failed with %d\n", gDrvrMajor);
	  return gDrvrMajor;
	}
	printk(KERN_INFO "logibone was assigned major number %d. To talk to\n", gDrvrMajor);
	printk(KERN_INFO "the driver, create a dev file with\n");
	printk(KERN_INFO "'mknod /dev/%s c %d 0'.\n", gDrvrName, gDrvrMajor);
	return 0;
}

static void LOGIBONE_fifo_exit(void)
{
    unregister_chrdev(gDrvrMajor, gDrvrName);
    printk(/*KERN_ALERT*/ "%s driver is unloaded\n", gDrvrName);
}


static const struct of_device_id logibone_of_match[] = {
	{ .compatible = "logibone", },
	{ },
};
MODULE_DEVICE_TABLE(of, logibone_of_match);


MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Jonathan Piat <piat.jonathan@gmail.com>");

module_init(LOGIBONE_fifo_init);
module_exit(LOGIBONE_fifo_exit);
