#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <asm/io.h>
#include <linux/gpio.h>


//SSI
#define SSI_CLK 02 // to be verified
#define SSI_DATA 04
#define SSI_DONE 0x03
#define SSI_PROG 0x05
#define SSI_INIT 0x06
#define MODE0	0
#define MODE1 1
#define SSI_DELAY 300

//GPIO
#define GPIO0_BASE 0x44E07000
#define GPIO0_SETDATAOUT *(gpio_regs+1)
#define GPIO0_CLEARDATAOUT *(gpio_regs)


volatile unsigned * gpio_regs;


static inline void __delay_cycles(unsigned long cycles){
	while(cycles != 0){
		cycles --;	
	}
}

static inline void ssiSetClk(void){
	//gpio_set_value(SSI_CLK, 1);
	GPIO0_SETDATAOUT = (1 << 2);
}

static inline void ssiClearClk(void){
	//gpio_set_value(SSI_CLK, 0);
	GPIO0_CLEARDATAOUT = (1 << 2);
}

static inline void ssiSetData(void){
	//gpio_set_value(SSI_DATA, 1);
	GPIO0_SETDATAOUT= (1 << 4);
}

static inline void ssiClearData(void){
	//gpio_set_value(SSI_DATA, 0);
	GPIO0_CLEARDATAOUT = (1 << 4);
}

static inline void serialConfigWriteByte(unsigned char val){
	unsigned char bitCount = 0;
	unsigned char valBuf = val;

	for(bitCount = 0; bitCount < 8; bitCount ++){
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

static inline void i2c_set_pin(struct i2c_client * io_cli, unsigned char pin, unsigned char val){
	unsigned char i2c_buffer[2];

	i2c_buffer[0] = pin;
	i2c_master_send(io_cli, i2c_buffer, 1);
}

static inline unsigned char i2c_get_pin(struct i2c_client * io_cli, unsigned char pin){
	unsigned char i2c_buffer[2];

	i2c_buffer[0] = pin;
	//i2c_master_send(io_cli, &i2c_buffer, 1);
	i2c_master_recv(io_cli, i2c_buffer, 2);
	//printk("reading value %x \n", i2c_buffer);

	return i2c_buffer[0];
}

static inline unsigned char i2c_get_pin_ex(struct i2c_client * io_cli, unsigned char pin){
	unsigned char i2c_buffer[2];

	i2c_master_send(io_cli, &pin, 1);
	i2c_master_recv(io_cli, i2c_buffer, 2);

	return i2c_buffer[1];
}

int loadBitFile(struct i2c_client * io_cli, const unsigned char * bitBuffer_user, const unsigned int length){
	unsigned char cfg = 1;
	unsigned char i2c_test;
	unsigned long int i;
	unsigned long int timer = 0;
	unsigned char * bitBuffer;	
	//unsigned char i2c_buffer[4];

	//request_mem_region(GPIO0_BASE + 0x190, 8, DEVICE_NAME);
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
	printk("loop finished with 0x%x from LPC; iter=%lu\n", i2c_test, timer);

	timer = 0;
	printk("Starting configuration of %d bits \n", length*8);

	for(i = 0; i < length; i ++){
		serialConfigWriteByte(bitBuffer[i]);	
		schedule();
	}

	printk("Waiting for done pin to go high \n");

	while(timer < 50){
		ssiClearClk();
		__delay_cycles(SSI_DELAY);	
		ssiSetClk();
		__delay_cycles(SSI_DELAY);	
		timer ++;
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
	kfree(bitBuffer);

	return length;
}

