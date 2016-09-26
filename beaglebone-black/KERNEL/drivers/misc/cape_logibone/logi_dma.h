#ifndef __LOGI_DMA_H__
#define __LOGI_DMA_H__

#include "drvr.h"


void logi_dma_init(void);
int logi_dma_open(struct drvr_mem* mem_dev, dma_addr_t *physbuf);
void logi_dma_release(struct drvr_mem* mem_dev);
int logi_dma_copy(struct drvr_mem* mem_dev, unsigned long trgt_addr,
		  unsigned long src_addr, int count);

#endif
