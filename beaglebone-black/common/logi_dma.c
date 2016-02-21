/*
 * logi_dma.c - DMA Engine API support for Logi-kernel
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/completion.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/platform_data/edma.h>
#include "config.h"
#include "drvr.h"
#include "generic.h"

static volatile int irqraised1;
static dma_addr_t dmaphysbuf;
static struct completion dma_comp;
#if LOGI_USE_DMAENGINE
static dma_cookie_t cookie;
#endif

#if LOGI_USE_DMAENGINE
static void dma_callback(void *param)
{
	struct drvr_mem *mem_dev = (struct drvr_mem*) param;
	struct dma_chan *chan = mem_dev->dma.chan;
	enum dma_status status;

	status = dma_async_is_tx_complete(chan, cookie, NULL, NULL);
	switch (status) {
		case DMA_COMPLETE:
			irqraised1 = 1;
			break;

		case DMA_ERROR:
			irqraised1 = -1;
			break;

		default:
			irqraised1 = -1;
			break;
	}

	complete(&dma_comp);
}
#else
static void dma_callback(unsigned lch, u16 ch_status, void *data)
{
	switch (ch_status) {
		case EDMA_DMA_COMPLETE:
			irqraised1 = 1;
			break;

		case EDMA_DMA_CC_ERROR:
			irqraised1 = -1;
			break;

		default:
			irqraised1 = -1;
			break;
	}

	complete(&dma_comp);
}
#endif /* LOGI_USE_DMAENGINE */

int logi_dma_init(struct drvr_mem* mem_dev, dma_addr_t *physbuf)
{
#if LOGI_USE_DMAENGINE
	struct dma_slave_config	conf;
	dma_cap_mask_t mask;
#endif

	/* Allocate DMA buffer */
	mem_dev->dma.buf = dma_alloc_coherent(NULL, MAX_DMA_TRANSFER_IN_BYTES,
					      &dmaphysbuf, 0);
	if (!mem_dev->dma.buf) {
		DBG_LOG("failed to allocate DMA buffer\n");
		return -ENOMEM;
	}
	*physbuf = dmaphysbuf;

#if LOGI_USE_DMAENGINE
	/* Allocate DMA channel */
	dma_cap_zero(mask);
	dma_cap_set(DMA_MEMCPY, mask);
	mem_dev->dma.chan = dma_request_channel(mask, NULL, NULL);
	if (!mem_dev->dma.chan)
		return -ENODEV;

	/* Configure DMA channel */
	conf.direction = DMA_MEM_TO_MEM;
	/*conf.dst_addr_width = DMA_SLAVE_BUSWIDTH_2_BYTES;*/
	dmaengine_slave_config(mem_dev->dma.chan, &conf);
#else
	mem_dev->dma.dma_chan = edma_alloc_channel(EDMA_CHANNEL_ANY, dma_callback,
					       NULL, EVENTQ_0);
	if (mem_dev->dma.dma_chan < 0) {
		DBG_LOG("edma_alloc_channel failed for dma_ch, error: %d\n",
			mem_dev->dma.dma_chan);
		return mem_dev->dma.dma_chan;
	}

	DBG_LOG("EDMA channel %d reserved\n", mem_dev->dma.dma_chan);
#endif /* LOGI_USE_DMAENGINE */

	init_completion(&dma_comp);
	return 0;
}

void logi_dma_release(struct drvr_mem* mem_dev)
{
#if LOGI_USE_DMAENGINE
	dma_release_channel(mem_dev->dma.chan);
#else
	edma_free_channel(mem_dev->dma.dma_chan);
#endif /* LOGI_USE_DMAENGINE */
	dma_free_coherent(NULL, MAX_DMA_TRANSFER_IN_BYTES, mem_dev->dma.buf,
			  dmaphysbuf);
}

int logi_dma_copy(struct drvr_mem* mem_dev, unsigned long trgt_addr,
		  unsigned long src_addr, int count)
{
	int result = 0;

#if LOGI_USE_DMAENGINE
	struct dma_chan *chan;
	struct dma_device *dev;
	struct dma_async_tx_descriptor *tx;
	unsigned long flags;

	chan = mem_dev->dma.chan;
	dev = chan->device;
	flags = DMA_CTRL_ACK | DMA_PREP_INTERRUPT;
	tx = dev->device_prep_dma_memcpy(chan, trgt_addr, src_addr, count, flags);
	if (!tx) {
		DBG_LOG("device_prep_dma_memcpy failed\n");
		return -ENODEV;
	}

	irqraised1 = 0u;
	dma_comp.done = 0;
	/* set the callback and submit the transaction */
	tx->callback = dma_callback;
	tx->callback_param = mem_dev;
	cookie = dmaengine_submit(tx);
	dma_async_issue_pending(chan);
#else
	struct edmacc_param param_set;
	int dma_ch = mem_dev->dma.dma_chan;

	edma_set_src(dma_ch, src_addr, INCR, W256BIT);
	edma_set_dest(dma_ch, trgt_addr, INCR, W256BIT);
	edma_set_src_index(dma_ch, 1, 1);
	edma_set_dest_index(dma_ch, 1, 1);
	/* A Sync Transfer Mode */
	edma_set_transfer_params(dma_ch, count, 1, 1, 1, ASYNC);//one block of one frame of one array of count bytes

	/* Enable the Interrupts on Channel 1 */
	edma_read_slot(dma_ch, &param_set);
	param_set.opt |= ITCINTEN;
	param_set.opt |= TCINTEN;
	param_set.opt |= EDMA_TCC(EDMA_CHAN_SLOT(dma_ch));
	edma_write_slot(dma_ch, &param_set);
	irqraised1 = 0u;
	dma_comp.done = 0;
	result = edma_start(dma_ch);
	if (result != 0) {
		DBG_LOG("edma copy failed\n");
		return result;
	}

#endif /* LOGI_USE_DMAENGINE */

	wait_for_completion(&dma_comp);

	/* Check the status of the completed transfer */
	if (irqraised1 < 0) {
		DBG_LOG("edma copy: Event Miss Occured!!!\n");
#if LOGI_USE_DMAENGINE
		dmaengine_terminate_all(chan);
#else
		edma_stop(dma_ch);
#endif /* LOGI_USE_DMAENGINE */
		result = -EAGAIN;
	}

	return result;
}

