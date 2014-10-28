/*
 * Common QE code
 *
 * Author: Scott Wood <scottwood@freescale.com>
 *
 * Copyright 2007-2008,2010 Freescale Semiconductor, Inc.
 *
 * Some parts derived from commproc.c/cpm2_common.c, which is:
 * Copyright (c) 1997 Dan error_act (dmalek@jlc.net)
 * Copyright (c) 1999-2001 Dan Malek <dan@embeddedalley.com>
 * Copyright (c) 2000 MontaVista Software, Inc (source@mvista.com)
 * 2006 (c) MontaVista Software, Inc.
 * Vitaly Bordug <vbordug@ru.mvista.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/of_device.h>
#include <linux/spinlock.h>
#include <linux/export.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/slab.h>

#include <linux/io.h>
#include <linux/fsl/rheap.h>
#include <linux/fsl/qe.h>

static spinlock_t qe_muram_lock;
static struct _rh_block qe_boot_muram_rh_block[16];
static struct _rh_info qe_muram_info;
static u8 __iomem *muram_vbase;
static phys_addr_t muram_pbase;

/* Max address size we deal with */
#define OF_MAX_ADDR_CELLS	4

int qe_muram_init(void)
{
	struct device_node *np;
	struct resource r;
	u32 zero[OF_MAX_ADDR_CELLS] = {};
	resource_size_t max = 0;
	int i = 0;
	int ret = 0;

	if (muram_pbase)
		return 0;

	spin_lock_init(&qe_muram_lock);
	/* initialize the info header */
	rh_init(&qe_muram_info, 1,
		sizeof(qe_boot_muram_rh_block) /
		sizeof(qe_boot_muram_rh_block[0]),
		qe_boot_muram_rh_block);

	np = of_find_compatible_node(NULL, NULL, "fsl,qe-muram-data");
	if (!np) {
		/* try legacy bindings */
		np = of_find_node_by_name(NULL, "data-only");
		if (!np) {
			pr_err("Cannot find CPM muram data node");
			ret = -ENODEV;
			goto out;
		}
	}

	muram_pbase = (phys_addr_t)of_translate_address(np, zero);
	if (muram_pbase == (phys_addr_t)OF_BAD_ADDR) {
		pr_err("Cannot translate zero through CPM muram node");
		ret = -ENODEV;
		goto out;
	}

	while (of_address_to_resource(np, i++, &r) == 0) {
		if (r.end > max)
			max = r.end;

		rh_attach_region(&qe_muram_info, r.start - muram_pbase,
				 resource_size(&r));
	}

	muram_vbase = ioremap(muram_pbase, max - muram_pbase + 1);
	if (!muram_vbase) {
		pr_err("Cannot map CPM muram");
		ret = -ENOMEM;
	}

out:
	of_node_put(np);
	return ret;
}

/**
 * qe_muram_alloc - allocate the requested size worth of multi-user ram
 * @size: number of bytes to allocate
 * @align: requested alignment, in bytes
 *
 * This function returns an offset into the muram area.
 * Use qe_dpram_addr() to get the virtual address of the area.
 * Use qe_muram_free() to free the allocation.
 */
unsigned long qe_muram_alloc(unsigned long size, unsigned long align)
{
	unsigned long start;
	unsigned long flags;

	spin_lock_irqsave(&qe_muram_lock, flags);
	qe_muram_info.alignment = align;
	start = rh_alloc(&qe_muram_info, size, "commproc");
	memset(qe_muram_addr(start), 0, size);
	spin_unlock_irqrestore(&qe_muram_lock, flags);

	return start;
}
EXPORT_SYMBOL(qe_muram_alloc);

/**
 * qe_muram_free - free a chunk of multi-user ram
 * @offset: The beginning of the chunk as returned by qe_muram_alloc().
 */
int qe_muram_free(unsigned long offset)
{
	int ret;
	unsigned long flags;

	spin_lock_irqsave(&qe_muram_lock, flags);
	ret = rh_free(&qe_muram_info, offset);
	spin_unlock_irqrestore(&qe_muram_lock, flags);

	return ret;
}
EXPORT_SYMBOL(qe_muram_free);

/**
 * qe_muram_alloc_fixed - reserve a specific region of multi-user ram
 * @offset: the offset into the muram area to reserve
 * @size: the number of bytes to reserve
 *
 * This function returns "start" on success, -ENOMEM on failure.
 * Use qe_dpram_addr() to get the virtual address of the area.
 * Use qe_muram_free() to free the allocation.
 */
unsigned long qe_muram_alloc_fixed(unsigned long offset, unsigned long size)
{
	unsigned long start;
	unsigned long flags;

	spin_lock_irqsave(&qe_muram_lock, flags);
	qe_muram_info.alignment = 1;
	start = rh_alloc_fixed(&qe_muram_info, offset, size, "commproc");
	spin_unlock_irqrestore(&qe_muram_lock, flags);

	return start;
}
EXPORT_SYMBOL(qe_muram_alloc_fixed);

/**
 * qe_muram_addr - turn a muram offset into a virtual address
 * @offset: muram offset to convert
 */
void __iomem *qe_muram_addr(unsigned long offset)
{
	return muram_vbase + offset;
}
EXPORT_SYMBOL(qe_muram_addr);

unsigned long qe_muram_offset(void __iomem *addr)
{
	return addr - (void __iomem *)muram_vbase;
}
EXPORT_SYMBOL(qe_muram_offset);

/**
 * qe_muram_dma - turn a muram virtual address into a DMA address
 * @offset: virtual address from qe_muram_addr() to convert
 */
dma_addr_t qe_muram_dma(void __iomem *addr)
{
	return muram_pbase + ((u8 __iomem *)addr - muram_vbase);
}
EXPORT_SYMBOL(qe_muram_dma);
