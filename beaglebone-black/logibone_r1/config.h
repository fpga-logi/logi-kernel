#ifndef __CONFIG_H__
#define __CONFIG_H__

#include <linux/i2c.h>
#include <linux/version.h>

// /dev/i2c-1 is only available in Kernel 3.8.13 which is a bug (https://github.com/RobertCNelson/bb-kernel/issues/20)
#if LINUX_VERSION_CODE > KERNEL_VERSION(3,8,13)
	#define I2C_ADAPTER 2
#else
	#define I2C_ADAPTER 1
#endif

#define LOGI_USE_DMAENGINE 0

int loadBitFile(struct i2c_client * io_cli, const unsigned char * bitBuffer_user, const unsigned int length);

#endif
