


#include <linux/kernel.h>
#include "configuration_interface.h"

#define SSI_DELAY 1


inline void __delay_cycles(unsigned long cycles){
	while(cycles != 0){
		cycles -- ;	
	}
}


extern int io_setup();
extern int io_free();

extern void io_set_clk(void);
extern void io_clear_clk(void);
extern void io_set_prog(void);
extern void io_clear_prog(void);
extern void io_set_data(void);
extern void io_clear_data(void);
extern unsigned char io_get_init();
extern unsigned char io_get_done();





inline void serialConfigWriteByte(unsigned char val){
	unsigned char bitCount = 0 ;
	unsigned char valBuf = val ;
	for(bitCount = 0 ; bitCount < 8 ; bitCount ++){
		io_clear_clk();
		if((valBuf & 0x80) != 0){
			io_set_data();
		}else{
			io_clear_data();
		}
		io_set_clk();
		valBuf = (valBuf << 1);			
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

	setup_io();
	io_clear_clk();
	io_set_prog();
	__delay_cycles(10*SSI_DELAY);
	io_clear_prog();
	__delay_cycles(5*SSI_DELAY);
	while(io_get_init() > 0 && timer < 200) timer ++; // waiting for init pin to go down
	if(timer >= 200){
		printk("FPGA did not answer to prog request, init pin not going low \n");
		io_set_prog();
		io_free();
		return -ENOTTY;	
	}
	timer = 0;
	__delay_cycles(5*SSI_DELAY);
	io_set_prog();
	while(io_get_init() == 0 && timer < 256){ // need to find a better way ...
		 timer ++; // waiting for init pin to go up
	}
	if(timer >= 256){
		printk("FPGA did not answer to prog request, init pin not going high \n");
		io_free();
		return -ENOTTY;	
	}
	timer = 0;
	printk("Starting configuration of %d bits \n", length*8);
	for(i = 0 ; i < length ; i ++){
		serialConfigWriteByte(bitBuffer[i]);	
		schedule();
	}
	printk("Waiting for done pin to go high \n");
	while(timer < 50){
		io_clear_clk();
		__delay_cycles(SSI_DELAY);	
		io_set_clk();
		__delay_cycles(SSI_DELAY);	
		timer ++ ;
	}
	io_clear_clk();
	io_clear_data();	
	if(io_get_done()== 0){
		printk("FPGA prog failed, done pin not going high \n");
		io_free();
		return -ENOTTY;		
	}
	io_free();
	kfree(bitBuffer) ;
	return length ;
}
