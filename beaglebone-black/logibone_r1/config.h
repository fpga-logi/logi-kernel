#ifndef __CONFIG_H__
#define __CONFIG_H__

#include <linux/i2c.h>


int loadBitFile(struct i2c_client * io_cli, const unsigned char * bitBuffer_user, const unsigned int length);

#endif
