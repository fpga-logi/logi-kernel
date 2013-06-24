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

#define DEVICE_NAME "logibone"

unsigned int nb_fifo = 1 ;

module_param(nb_fifo , int, S_IRUGO);
MODULE_PARM_DESC(nb_fifo, "number of fifo to instantiate");


static char * gDrvrName = DEVICE_NAME;
static unsigned char gDrvrMajor = 0 ;


int dma_ch ;
dma_addr_t dmaphysbuf = 0;
dma_addr_t dmapGpmcbuf = FPGA_BASE_ADDR;
static volatile int irqraised1 = 0;

unsigned char * readBuffer ;
unsigned char * writeBuffer ;


unsigned int fifo_size ;


ssize_t readFifo(struct file *filp, char *buf, size_t count, loff_t *f_pos);
ssize_t writeFifo(struct file *filp, const char *buf, size_t count, loff_t *f_pos);
int edma_memtomemcpy(int count, unsigned long src_addr, unsigned long trgt_addr,  int event_queue,  enum address_mode src_mode,  enum address_mode trgt_mode);
static void dma_callback(unsigned lch, u16 ch_status, void *data);


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

enum logibone_type{
	main,
	fifo
};

struct logibone_main{
	unsigned short * base_addr ;
	unsigned short * virt_addr ;	
};

struct logibone_fifo{
	unsigned int id ;
	unsigned int size ;
	unsigned short * base_addr ;
	unsigned short * virt_addr ;	
};

union logibone_data{
	struct logibone_main main;
	struct logibone_fifo fifo;
};

struct logibone_device{
	enum logibone_type type ;
	union logibone_data data ;
	struct cdev cdev;
};


struct device * main_device ;
struct class * logibone_class ;
struct logibone_device * logibone_devices ;


int loadBitFile(const unsigned char * bitBuffer_user, unsigned int length);
unsigned short int getNbFree(struct logibone_fifo * fifop);
unsigned short int getNbAvailable(struct logibone_fifo * fifop);
unsigned short int getSize(struct logibone_fifo * fifop);



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



ssize_t writeFifo(struct file *filp, const char *buf, size_t count,
                       loff_t *f_pos)
{
	unsigned short int transfer_size  ;
	ssize_t transferred = 0 ;
	unsigned long src_addr, trgt_addr ;
	unsigned int ret = 0;
	struct logibone_fifo * fifo_to_write = &(((struct logibone_device *) filp->private_data)->data.fifo) ;
	if(count%2 != 0){
		 printk("%s: LOGIBONE_fifo write: Transfer must be 16bits aligned.\n",gDrvrName);
		 return -1;
	}
	if(count < FIFO_BLOCK_SIZE){
		transfer_size = count ;
	}else{
		transfer_size = FIFO_BLOCK_SIZE ;
	}
	writeBuffer =  (unsigned char *) dma_alloc_coherent (NULL, count, &dmaphysbuf, 0);
	if(writeBuffer == NULL){
		printk("failed to alloacted DMA buffer \n");
		return -1 ;	
	}
	trgt_addr = (unsigned long) fifo_to_write->base_addr ;
	src_addr = (unsigned long) dmaphysbuf ;
	// Now it is safe to copy the data from user space.
	if (writeBuffer == NULL || copy_from_user(writeBuffer, buf, count) )  {
		ret = -1;
		printk("%s: LOGIBONE_fifo write: Failed copy from user.\n",gDrvrName);
		goto exit;
	}
	while(transferred < count){
		while(getNbFree(fifo_to_write) < transfer_size) schedule() ; 
		if(edma_memtomemcpy(transfer_size, src_addr , trgt_addr, 0, INCR, INCR) < 0){
			printk("%s: LOGIBONE_fifo write: Failed to trigger EDMA transfer.\n",gDrvrName);		
			ret = -1 ;			
			goto exit;		
		}
		src_addr += transfer_size ;
		transferred += transfer_size ;
		if((count - transferred) < FIFO_BLOCK_SIZE){
			transfer_size = count - transferred ;
		}else{
			transfer_size = FIFO_BLOCK_SIZE ;
		}
	}
	ret = transferred;
	exit:
	dma_free_coherent(NULL, count, writeBuffer,
				dmaphysbuf); // free coherent before copy to user
	return (ret);
}


ssize_t readFifo(struct file *filp, char *buf, size_t count, loff_t *f_pos)
{
	unsigned short int transfer_size ;
	ssize_t transferred = 0 ;
	unsigned long src_addr, trgt_addr ;
	int ret = 0 ;
	struct logibone_fifo * fifo_to_read = &(((struct logibone_device *) filp->private_data)->data.fifo) ; 

	if(count%2 != 0){
		 printk("%s: LOGIBONE_fifo read: Transfer must be 16bits aligned.\n",gDrvrName);
		 return -1 ;
	}
	if(count < FIFO_BLOCK_SIZE){
		transfer_size = count ;
	}else{
		transfer_size = FIFO_BLOCK_SIZE ;
	}
	readBuffer = (unsigned char *) dma_alloc_coherent (NULL, count, &dmaphysbuf, 0);
	if(readBuffer == NULL){
		printk("failed to alloacted DMA buffer \n");
		return -1 ;	
	}
	src_addr = (unsigned long) fifo_to_read->base_addr;
	trgt_addr = (unsigned long) dmaphysbuf ;
	while(transferred < count){
		while(getNbAvailable(fifo_to_read) < transfer_size) schedule() ; 
		if(edma_memtomemcpy(transfer_size, src_addr, trgt_addr, 0, INCR, INCR) < 0){
			printk("%s: LOGIBONE_fifo read: Failed to trigger EDMA transfer.\n",gDrvrName);
			goto exit ;
		}	
		trgt_addr += transfer_size ;
		transferred += transfer_size ;
		if((count - transferred) < FIFO_BLOCK_SIZE){
			transfer_size = (count - transferred) ;
		}else{
			transfer_size = FIFO_BLOCK_SIZE ;
		}
	}
	if (copy_to_user(buf, readBuffer, transferred) )  {
		ret = -1;
		goto exit;
	}		
	dma_free_coherent(NULL,  count, readBuffer,
			dmaphysbuf);	
	ret = transferred ;
	exit:
	return ret;
}



static int LOGIBONE_fifo_open(struct inode *inode, struct file *filp)
{
	struct logibone_device * dev ;
	dev = container_of(inode->i_cdev, struct logibone_device, cdev);
	filp->private_data = dev; /* for other methods */
	if(dev == NULL){
		printk("Failed to retrieve logibone fifo structure !\n");
		return -1 ;	
	}
	if(dev->type == main){
		
	}else{
		struct logibone_fifo * fifo_dev = &((dev->data).fifo) ;
		printk("Opening fifo %d @%x \n", fifo_dev->id, (unsigned long) fifo_dev->base_addr);
		request_mem_region((unsigned long) fifo_dev->base_addr, FIFO_BLOCK_SIZE, gDrvrName); //TODO: may block EDMA transfer ...
		fifo_dev->virt_addr = ioremap_nocache((((unsigned long) fifo_dev->base_addr) + FIFO_CMD_OFFSET ), 16); //TODO: may block EDMA transfer ...
		fifo_dev->size = fifo_dev->virt_addr[FIFO_SIZE_OFFSET] ;
		printk("%s: Open: module opened, with fifo size %d @%x\n",gDrvrName, fifo_dev->size, (unsigned long) fifo_dev->virt_addr);
	}
/*
	request_mem_region(FPGA_BASE_ADDR, FIFO_BLOCK_SIZE, gDrvrName); //TODO: may block EDMA transfer ...
	gpmc_cs1_virt = ioremap_nocache(FPGA_BASE_ADDR + FIFO_CMD_OFFSET, 16); //TODO: may block EDMA transfer ...
	gpmc_cs1_pointer = (unsigned short *) FPGA_BASE_ADDR ;
	fifo_size = gpmc_cs1_virt[FIFO_SIZE_OFFSET] ;
	printk("%s: Open: module opened, with fifo size %d\n",gDrvrName, fifo_size);
*/
	return 0;
}

static int LOGIBONE_fifo_release(struct inode *inode, struct file *filp)
{
	struct logibone_device * dev ;
	dev = container_of(inode->i_cdev, struct logibone_device, cdev);
	if(dev->type == fifo){
		iounmap((dev->data.fifo).virt_addr);
		release_mem_region(((unsigned long) (dev->data.fifo).base_addr), FIFO_BLOCK_SIZE );
		printk("%s: Release: module released\n",gDrvrName);	
	}
	/*
	iounmap(gpmc_cs1_virt);
	release_mem_region(FPGA_BASE_ADDR, FIFO_BLOCK_SIZE );
	printk("%s: Release: module released\n",gDrvrName);*/
	return 0;
}


static ssize_t LOGIBONE_fifo_write(struct file *filp, const char *buf, size_t count,
                       loff_t *f_pos)
{
	struct logibone_device * dev ;
	dev = filp->private_data ; /* for other methods */
	switch(dev->type){
		case main :
			return loadBitFile(buf, count);
		case fifo:
			return writeFifo(filp, buf, count, f_pos);
		default:
			return loadBitFile(buf, count);	
	};
	
}


static ssize_t LOGIBONE_fifo_read(struct file *filp, char *buf, size_t count, loff_t *f_pos)
{
	struct logibone_device * dev ;
	dev = filp->private_data ; /* for other methods */
	switch(dev->type){
		case main :
			return -1;
		case fifo:
			return readFifo(filp, buf, count, f_pos);
		default:
			return -1 ;	
	};
}

unsigned short int getNbAvailable(struct logibone_fifo * fifop){
	return (fifop->virt_addr[FIFO_NB_AVAILABLE_B_OFFSET]*2) ;  
}

unsigned short int getNbFree(struct logibone_fifo * fifop){
	fifo_size = getSize(fifop) ;
	return (fifo_size - (fifop->virt_addr[FIFO_NB_AVAILABLE_A_OFFSET])*2) ;
}

unsigned short int getSize(struct logibone_fifo * fifop){
	return (fifop->virt_addr[FIFO_SIZE_OFFSET]*2) ;
}


static long LOGIBONE_fifo_ioctl(struct file *filp, unsigned int cmd, unsigned long arg){
	struct logibone_device * dev ;
	struct logibone_fifo * fifo_to_ctl ;
	//printk("calling ioctl %d on logibone \n", cmd);
	dev = filp->private_data ; /* for other methods */
	if(dev->type == fifo){
		fifo_to_ctl = &(((struct logibone_device *) filp->private_data)->data.fifo) ; 
		switch(cmd){
			case LOGIBONE_FIFO_RESET :
				//printk("performing reset on fifo ! \n");
				fifo_to_ctl->virt_addr[FIFO_NB_AVAILABLE_A_OFFSET] = 0 ;
				fifo_to_ctl->virt_addr[FIFO_NB_AVAILABLE_B_OFFSET] = 0 ;
				return 0 ;
			case LOGIBONE_FIFO_PEEK :
				//printk("performing peek on fifo ! \n");
				return  fifo_to_ctl->virt_addr[FIFO_PEEK_OFFSET] ;
			case LOGIBONE_FIFO_NB_FREE :
				//printk("performing getNbFRee() on fifo ! \n");
				return getNbFree(fifo_to_ctl) ;
			case LOGIBONE_FIFO_NB_AVAILABLE :
				//printk("performing getNbAvailable() on fifo ! \n");
				return getNbAvailable(fifo_to_ctl) ;
			case LOGIBONE_FIFO_SIZE :
				//printk("calling fifo size ! \n");
				return getSize(fifo_to_ctl) ;
			default: /* redundant, as cmd was checked against MAXNR */
				printk("unknown command %d \n", cmd);
				return -ENOTTY;
		}	
	}
	printk("ioctl failed \n");
	return -ENOTTY;
}



static void LOGIBONE_fifo_exit(void)
{
	/*unregister_chrdev(gDrvrMajor, gDrvrName);
	printk("%s driver is unloaded\n", gDrvrName);*/
	int i;
	dev_t devno = MKDEV(gDrvrMajor, 0);
	/* Get rid of our char dev entries */
	if (logibone_devices) {
		for (i = 0; i <= nb_fifo; i++) {
			device_destroy(logibone_class, MKDEV(gDrvrMajor, i));
			cdev_del(&logibone_devices[i].cdev);
		}
		kfree(logibone_devices);
	}
	class_destroy(logibone_class);
	/* cleanup_module is never called if registering failed */
	unregister_chrdev_region(devno, nb_fifo+1);
}

static int LOGIBONE_fifo_init(void)
{
		
	//gDrvrMajor = register_chrdev(0, gDrvrName, &LOGIBONE_fifo_ops );
 	/* Fail gracefully if need be */
	/*if (gDrvrMajor < 0) {
	  printk(KERN_ALERT "Registering char device failed with %d\n", gDrvrMajor);
	  return gDrvrMajor;
	}
	printk(KERN_INFO "logibone was assigned major number %d. To talk to\n", gDrvrMajor);
	printk(KERN_INFO "the driver, create a dev file with\n");
	printk(KERN_INFO "'mknod /dev/%s c %d 0'.\n", gDrvrName, gDrvrMajor);
	return 0;
	*/

	int result, i;
	int devno ;
	struct logibone_fifo * newFifo ;
	struct logibone_main * newMain ;
        dev_t dev = 0;
	result = alloc_chrdev_region(&dev, 0, nb_fifo,
                                gDrvrName);
        gDrvrMajor = MAJOR(dev);
        if (result < 0) {
                 printk(KERN_ALERT "Registering char device failed with %d\n", gDrvrMajor);
                return result;
        }
        logibone_devices = kmalloc(nb_fifo * sizeof(struct logibone_device), GFP_KERNEL);
        if (! logibone_devices) {
                result = -ENOMEM;
                goto fail;  /* Make this more graceful */
        }

	logibone_class = class_create(THIS_MODULE,DEVICE_NAME);

        memset(logibone_devices, 0, nb_fifo * sizeof(struct logibone_device));
	
	/*Initializing main mdevice for prog and direct memory access*/
	devno = MKDEV(gDrvrMajor, 0);
	logibone_devices[0].type = main ;
	newMain = &(logibone_devices[0].data.main);
	newMain->base_addr = (unsigned short *) (FPGA_BASE_ADDR); //todo need to check on that ...
	main_device = device_create(logibone_class, NULL, devno, NULL, DEVICE_NAME);	// should create /dev entry for main node
	if(main_device == NULL){
		class_destroy(logibone_class);
   	 	result = -ENOMEM;
                goto fail;
	}
	cdev_init(&(logibone_devices[0].cdev), &LOGIBONE_fifo_ops);
	logibone_devices[0].cdev.owner = THIS_MODULE;
	logibone_devices[0].cdev.ops = &LOGIBONE_fifo_ops;
	cdev_add(&(logibone_devices[0].cdev), devno, 1);
	//printk(KERN_INFO "'mknod /dev/%s c %d %d'.\n", gDrvrName, gDrvrMajor, 0);
	/* Initialize each device. */
        for (i = 1; i <=  nb_fifo ; i++) {
		devno = MKDEV(gDrvrMajor, i);
		logibone_devices[i].type = fifo ;
		newFifo = &(logibone_devices[i].data.fifo);
               	newFifo->id = i;
		newFifo->size = 0;
                newFifo->base_addr = (unsigned short *) (FPGA_BASE_ADDR + ((i-1)* FIFO_BLOCK_SIZE)); //todo need to check on that ...
		device_create(logibone_class, main_device, devno, NULL, "logibone%d", i); // should create /dev entry for each device as child of main node
    		cdev_init(&(logibone_devices[i].cdev), &LOGIBONE_fifo_ops);
        	(logibone_devices[i].cdev).owner = THIS_MODULE;
        	(logibone_devices[i].cdev).ops = &LOGIBONE_fifo_ops;
        	cdev_add(&(logibone_devices[i].cdev), devno, 1);
		//printk(KERN_INFO "'mknod /dev/%s%d c %d %d'.\n", gDrvrName, i , gDrvrMajor, i);
	}
        return 0;

  fail:
        LOGIBONE_fifo_exit();
        return -1 ;

}




int edma_memtomemcpy(int count, unsigned long src_addr, unsigned long trgt_addr,  int event_queue,  enum address_mode src_mode, enum address_mode trg_mode)
{
	int result = 0;
	unsigned int dma_ch = 0;
	struct edmacc_param param_set;

	result = edma_alloc_channel (EDMA_CHANNEL_ANY, dma_callback, NULL, event_queue);
	if (result < 0) {
		printk ("\nedma3_memtomemcpytest_dma::edma_alloc_channel failed for dma_ch, error:%d\n", result);
		return result;
	}

	dma_ch = result;
	if(count % 16 == 0){
		edma_set_src (dma_ch, src_addr, src_mode, W128BIT);
		edma_set_dest (dma_ch, trgt_addr, trg_mode, W128BIT);
	}else if(count % 8 == 0){
		edma_set_src (dma_ch, src_addr, src_mode, W64BIT);
		edma_set_dest (dma_ch, trgt_addr, trg_mode, W64BIT);
	}else if(count % 4 == 0){
		edma_set_src (dma_ch, src_addr, src_mode, W32BIT);
		edma_set_dest (dma_ch, trgt_addr, trg_mode, W32BIT);
	}else{
		edma_set_src (dma_ch, src_addr, src_mode, W16BIT);
		edma_set_dest (dma_ch, trgt_addr, trg_mode, W16BIT);
	}
	edma_set_src_index (dma_ch, 0, 0); // always copy from same location
	edma_set_dest_index (dma_ch, count, count); // increase by transfer size on each copy
	/* A Sync Transfer Mode */
	edma_set_transfer_params (dma_ch, count, 1, 1, 1, ASYNC); //one block of one fame of one array of count bytes

	/* Enable the Interrupts on Channel 1 */
	edma_read_slot (dma_ch, &param_set);
	param_set.opt |= ITCINTEN;
	param_set.opt |= TCINTEN;
	param_set.opt |= EDMA_TCC(EDMA_CHAN_SLOT(dma_ch));
	edma_write_slot (dma_ch, &param_set);

	result = edma_start(dma_ch);
	if (result != 0) {
		printk ("edma copy for logibone_fifo failed \n");
		
	}
	while (irqraised1 == 0u) ;//schedule();
	irqraised1 = 0u;
	//irqraised1 = -1 ;

	/* Check the status of the completed transfer */
	if (irqraised1 < 0) {
		/* Some error occured, break from the FOR loop. */
		//printk ("edma copy for logibone_fifo: Event Miss Occured!!!\n");
	}else{
		//printk ("edma copy for logibone_fifo: Copy done !\n");
	}
	edma_stop(dma_ch);
	edma_free_channel(dma_ch);

	return result;
}

static void dma_callback(unsigned lch, u16 ch_status, void *data)
{	
	switch(ch_status) {
	case DMA_COMPLETE:
		irqraised1 = 1;
		/*printk ("\n From Callback 1: Channel %d status is: %u\n",
				lch, ch_status);*/
		break;
	case DMA_CC_ERROR:
		irqraised1 = -1;
		/*printk ("\nFrom Callback 1: DMA_CC_ERROR occured "
				"on Channel %d\n", lch);*/
		break;
	default:
		break;
	}
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
