/*
 * Copyright (C) 2006, 2012 Freescale Semiconductor, Inc. All rights reserved.
 *
 * Authors:	Shlomi Gridish <gridish@freescale.com>
 *		Li Yang <leoli@freescale.com>
 *
 * Description:
 * QE UCC Fast API Set - UCC Fast specific routines implementations.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/stddef.h>
#include <linux/interrupt.h>
#include <linux/err.h>
#include <linux/export.h>

#include <linux/io.h>
#include <linux/fsl/immap_qe.h>
#include <linux/fsl/qe.h>

#include <linux/fsl/ucc.h>
#include <linux/fsl/ucc_fast.h>

void ucc_fast_dump_regs(struct ucc_fast_private *uccf)
{
	pr_info("UCC%u Fast registers:\n", uccf->uf_info->ucc_num);
	pr_info("Base address: 0x%p\n", uccf->uf_regs);

	pr_info("gumr  : addr=0x%p, val=0x%08x\n",
		  &uccf->uf_regs->gumr, ioread32be(&uccf->uf_regs->gumr));
	pr_info("upsmr : addr=0x%p, val=0x%08x\n",
		  &uccf->uf_regs->upsmr, ioread32be(&uccf->uf_regs->upsmr));
	pr_info("utodr : addr=0x%p, val=0x%04x\n",
		  &uccf->uf_regs->utodr, ioread16be(&uccf->uf_regs->utodr));
	pr_info("udsr  : addr=0x%p, val=0x%04x\n",
		  &uccf->uf_regs->udsr, ioread16be(&uccf->uf_regs->udsr));
	pr_info("ucce  : addr=0x%p, val=0x%08x\n",
		  &uccf->uf_regs->ucce, ioread32be(&uccf->uf_regs->ucce));
	pr_info("uccm  : addr=0x%p, val=0x%08x\n",
		  &uccf->uf_regs->uccm, ioread32be(&uccf->uf_regs->uccm));
	pr_info("uccs  : addr=0x%p, val=0x%02x\n",
		  &uccf->uf_regs->uccs, ioread8(&uccf->uf_regs->uccs));
	pr_info("urfb  : addr=0x%p, val=0x%08x\n",
		  &uccf->uf_regs->urfb, ioread32be(&uccf->uf_regs->urfb));
	pr_info("urfs  : addr=0x%p, val=0x%04x\n",
		  &uccf->uf_regs->urfs, ioread16be(&uccf->uf_regs->urfs));
	pr_info("urfet : addr=0x%p, val=0x%04x\n",
		  &uccf->uf_regs->urfet, ioread16be(&uccf->uf_regs->urfet));
	pr_info("urfset: addr=0x%p, val=0x%04x\n",
		  &uccf->uf_regs->urfset, ioread16be(&uccf->uf_regs->urfset));
	pr_info("utfb  : addr=0x%p, val=0x%08x\n",
		  &uccf->uf_regs->utfb, ioread32be(&uccf->uf_regs->utfb));
	pr_info("utfs  : addr=0x%p, val=0x%04x\n",
		  &uccf->uf_regs->utfs, ioread16be(&uccf->uf_regs->utfs));
	pr_info("utfet : addr=0x%p, val=0x%04x\n",
		  &uccf->uf_regs->utfet, ioread16be(&uccf->uf_regs->utfet));
	pr_info("utftt : addr=0x%p, val=0x%04x\n",
		  &uccf->uf_regs->utftt, ioread16be(&uccf->uf_regs->utftt));
	pr_info("utpt  : addr=0x%p, val=0x%04x\n",
		  &uccf->uf_regs->utpt, ioread16be(&uccf->uf_regs->utpt));
	pr_info("urtry : addr=0x%p, val=0x%08x\n",
		  &uccf->uf_regs->urtry, ioread32be(&uccf->uf_regs->urtry));
	pr_info("guemr : addr=0x%p, val=0x%02x\n",
		  &uccf->uf_regs->guemr, ioread8(&uccf->uf_regs->guemr));
}
EXPORT_SYMBOL(ucc_fast_dump_regs);

u32 ucc_fast_get_qe_cr_subblock(int uccf_num)
{
	switch (uccf_num) {
	case 0: return QE_CR_SUBBLOCK_UCCFAST1;
	case 1: return QE_CR_SUBBLOCK_UCCFAST2;
	case 2: return QE_CR_SUBBLOCK_UCCFAST3;
	case 3: return QE_CR_SUBBLOCK_UCCFAST4;
	case 4: return QE_CR_SUBBLOCK_UCCFAST5;
	case 5: return QE_CR_SUBBLOCK_UCCFAST6;
	case 6: return QE_CR_SUBBLOCK_UCCFAST7;
	case 7: return QE_CR_SUBBLOCK_UCCFAST8;
	default: return QE_CR_SUBBLOCK_INVALID;
	}
}
EXPORT_SYMBOL(ucc_fast_get_qe_cr_subblock);

void ucc_fast_transmit_on_demand(struct ucc_fast_private *uccf)
{
	iowrite16be(UCC_FAST_TOD, &uccf->uf_regs->utodr);
}
EXPORT_SYMBOL(ucc_fast_transmit_on_demand);

void ucc_fast_enable(struct ucc_fast_private *uccf, enum comm_dir mode)
{
	struct ucc_fast __iomem *uf_regs;
	u32 gumr;

	uf_regs = uccf->uf_regs;

	/* Enable reception and/or transmission on this UCC. */
	gumr = ioread32be(&uf_regs->gumr);
	if (mode & COMM_DIR_TX) {
		gumr |= UCC_FAST_GUMR_ENT;
		uccf->enabled_tx = 1;
	}
	if (mode & COMM_DIR_RX) {
		gumr |= UCC_FAST_GUMR_ENR;
		uccf->enabled_rx = 1;
	}
	iowrite32be(gumr, &uf_regs->gumr);
}
EXPORT_SYMBOL(ucc_fast_enable);

void ucc_fast_disable(struct ucc_fast_private *uccf, enum comm_dir mode)
{
	struct ucc_fast __iomem *uf_regs;
	u32 gumr;

	uf_regs = uccf->uf_regs;

	/* Disable reception and/or transmission on this UCC. */
	gumr = ioread32be(&uf_regs->gumr);
	if (mode & COMM_DIR_TX) {
		gumr &= ~UCC_FAST_GUMR_ENT;
		uccf->enabled_tx = 0;
	}
	if (mode & COMM_DIR_RX) {
		gumr &= ~UCC_FAST_GUMR_ENR;
		uccf->enabled_rx = 0;
	}
	iowrite32be(gumr, &uf_regs->gumr);
}
EXPORT_SYMBOL(ucc_fast_disable);

int ucc_fast_init(struct ucc_fast_info *uf_info,
		  struct ucc_fast_private **uccf_ret)
{
	struct ucc_fast_private *uccf;
	struct ucc_fast __iomem *uf_regs;
	u32 gumr;
	int ret;

	if (!uf_info)
		return -EINVAL;

	/* check if the UCC port number is in range. */
	if ((uf_info->ucc_num < 0) || (uf_info->ucc_num > UCC_MAX_NUM - 1)) {
		pr_err("%s: illegal UCC number\n", __func__);
		return -EINVAL;
	}

	/* Check that 'max_rx_buf_length' is properly aligned (4). */
	if (uf_info->max_rx_buf_length & (UCC_FAST_MRBLR_ALIGNMENT - 1)) {
		pr_err("%s: max_rx_buf_length not aligned\n",
			__func__);
		return -EINVAL;
	}

	/* Validate Virtual Fifo register values */
	if (uf_info->urfs < UCC_FAST_URFS_MIN_VAL) {
		pr_err("%s: urfs is too small\n", __func__);
		return -EINVAL;
	}

	if (uf_info->urfs & (UCC_FAST_VIRT_FIFO_REGS_ALIGNMENT - 1)) {
		pr_err("%s: urfs is not aligned\n", __func__);
		return -EINVAL;
	}

	if (uf_info->urfet & (UCC_FAST_VIRT_FIFO_REGS_ALIGNMENT - 1)) {
		pr_err("%s: urfet is not aligned.\n", __func__);
		return -EINVAL;
	}

	if (uf_info->urfset & (UCC_FAST_VIRT_FIFO_REGS_ALIGNMENT - 1)) {
		pr_err("%s: urfset is not aligned\n", __func__);
		return -EINVAL;
	}

	if (uf_info->utfs & (UCC_FAST_VIRT_FIFO_REGS_ALIGNMENT - 1)) {
		pr_err("%s: utfs is not aligned\n", __func__);
		return -EINVAL;
	}

	if (uf_info->utfet & (UCC_FAST_VIRT_FIFO_REGS_ALIGNMENT - 1)) {
		pr_err("%s: utfet is not aligned\n", __func__);
		return -EINVAL;
	}

	if (uf_info->utftt & (UCC_FAST_VIRT_FIFO_REGS_ALIGNMENT - 1)) {
		pr_err("%s: utftt is not aligned\n", __func__);
		return -EINVAL;
	}

	uccf = kzalloc(sizeof(struct ucc_fast_private), GFP_KERNEL);
	if (!uccf) {
		pr_err("%s: Cannot allocate private data\n",
			__func__);
		return -ENOMEM;
	}

	/* Fill fast UCC structure */
	uccf->uf_info = uf_info;
	/* Set the PHY base address */
	uccf->uf_regs = ioremap(uf_info->regs, sizeof(struct ucc_fast));
	if (uccf->uf_regs == NULL) {
		pr_err("%s: Cannot map UCC registers\n", __func__);
		kfree(uccf);
		return -ENOMEM;
	}

	uccf->enabled_tx = 0;
	uccf->enabled_rx = 0;
	uccf->stopped_tx = 0;
	uccf->stopped_rx = 0;
	uf_regs = uccf->uf_regs;
	uccf->p_ucce = &uf_regs->ucce;
	uccf->p_uccm = &uf_regs->uccm;
#ifdef CONFIG_UGETH_TX_ON_DEMAND
	uccf->p_utodr = &uf_regs->utodr;
#endif
#ifdef STATISTICS
	uccf->tx_frames = 0;
	uccf->rx_frames = 0;
	uccf->rx_discarded = 0;
#endif				/* STATISTICS */

	/* Set UCC to fast type */
	ret = ucc_set_type(uf_info->ucc_num, UCC_SPEED_TYPE_FAST);
	if (ret) {
		pr_err("%s: cannot set UCC type\n", __func__);
		ucc_fast_free(uccf);
		return ret;
	}

	uccf->mrblr = uf_info->max_rx_buf_length;

	/* Set GUMR */
	/* For more details see the hardware spec. */
	gumr = uf_info->ttx_trx;
	if (uf_info->tci)
		gumr |= UCC_FAST_GUMR_TCI;
	if (uf_info->cdp)
		gumr |= UCC_FAST_GUMR_CDP;
	if (uf_info->ctsp)
		gumr |= UCC_FAST_GUMR_CTSP;
	if (uf_info->cds)
		gumr |= UCC_FAST_GUMR_CDS;
	if (uf_info->ctss)
		gumr |= UCC_FAST_GUMR_CTSS;
	if (uf_info->txsy)
		gumr |= UCC_FAST_GUMR_TXSY;
	if (uf_info->rsyn)
		gumr |= UCC_FAST_GUMR_RSYN;
	gumr |= uf_info->synl;
	if (uf_info->rtsm)
		gumr |= UCC_FAST_GUMR_RTSM;
	gumr |= uf_info->renc;
	if (uf_info->revd)
		gumr |= UCC_FAST_GUMR_REVD;
	gumr |= uf_info->tenc;
	gumr |= uf_info->tcrc;
	gumr |= uf_info->mode;
	iowrite32be(gumr, &uf_regs->gumr);

	/* Allocate memory for Tx Virtual Fifo */
	uccf->ucc_fast_tx_virtual_fifo_base_offset =
	    qe_muram_alloc(uf_info->utfs, UCC_FAST_VIRT_FIFO_REGS_ALIGNMENT);
	if (IS_ERR_VALUE(uccf->ucc_fast_tx_virtual_fifo_base_offset)) {
		pr_err("%s: cannot allocate MURAM for TX FIFO\n",
			__func__);
		uccf->ucc_fast_tx_virtual_fifo_base_offset = 0;
		ucc_fast_free(uccf);
		return -ENOMEM;
	}

	/* Allocate memory for Rx Virtual Fifo */
	uccf->ucc_fast_rx_virtual_fifo_base_offset =
		qe_muram_alloc(uf_info->urfs +
			   UCC_FAST_RECEIVE_VIRTUAL_FIFO_SIZE_FUDGE_FACTOR,
			   UCC_FAST_VIRT_FIFO_REGS_ALIGNMENT);
	if (IS_ERR_VALUE(uccf->ucc_fast_rx_virtual_fifo_base_offset)) {
		pr_err("%s: cannot allocate MURAM for RX FIFO\n",
			__func__);
		uccf->ucc_fast_rx_virtual_fifo_base_offset = 0;
		ucc_fast_free(uccf);
		return -ENOMEM;
	}

	/* Set Virtual Fifo registers */
	iowrite16be(uf_info->urfs, &uf_regs->urfs);
	iowrite16be(uf_info->urfet, &uf_regs->urfet);
	iowrite16be(uf_info->urfset, &uf_regs->urfset);
	iowrite16be(uf_info->utfs, &uf_regs->utfs);
	iowrite16be(uf_info->utfet, &uf_regs->utfet);
	iowrite16be(uf_info->utftt, &uf_regs->utftt);
	/* utfb, urfb are offsets from MURAM base */
	iowrite32be(uccf->ucc_fast_tx_virtual_fifo_base_offset, &uf_regs->utfb);
	iowrite32be(uccf->ucc_fast_rx_virtual_fifo_base_offset, &uf_regs->urfb);

	/* Mux clocking */
	/* Grant Support */
	ucc_set_qe_mux_grant(uf_info->ucc_num, uf_info->grant_support);
	/* Breakpoint Support */
	ucc_set_qe_mux_bkpt(uf_info->ucc_num, uf_info->brkpt_support);
	/* Set Tsa or NMSI mode. */
	ucc_set_qe_mux_tsa(uf_info->ucc_num, uf_info->tsa);
	/* If NMSI (not Tsa), set Tx and Rx clock. */
	if (!uf_info->tsa) {
		/* Rx clock routing */
		if ((uf_info->rx_clock != QE_CLK_NONE) &&
		    ucc_set_qe_mux_rxtx(uf_info->ucc_num, uf_info->rx_clock,
					COMM_DIR_RX)) {
			pr_err("%s: illegal value for RX clock\n",
			       __func__);
			ucc_fast_free(uccf);
			return -EINVAL;
		}
		/* Tx clock routing */
		if ((uf_info->tx_clock != QE_CLK_NONE) &&
		    ucc_set_qe_mux_rxtx(uf_info->ucc_num, uf_info->tx_clock,
					COMM_DIR_TX)) {
			pr_err("%s: illegal value for TX clock\n",
			       __func__);
			ucc_fast_free(uccf);
			return -EINVAL;
		}
	} else {
		/* tdm Rx clock routing */
		if ((uf_info->rx_clock != QE_CLK_NONE) &&
			ucc_set_tdm_rxtx_clk(uf_info->tdm_num,
				uf_info->rx_clock, COMM_DIR_RX)) {
			pr_err("%s: illegal value for RX clock", __func__);
			ucc_fast_free(uccf);
			return -EINVAL;
		}

		/* tdm Tx clock routing */
		if ((uf_info->tx_clock != QE_CLK_NONE) &&
			ucc_set_tdm_rxtx_clk(uf_info->tdm_num,
				uf_info->tx_clock, COMM_DIR_TX)) {
			pr_err("%s:illegal value for TX clock", __func__);
			ucc_fast_free(uccf);
			return -EINVAL;
		}

		/* tdm Rx sync clock routing */
		if ((uf_info->rx_sync != QE_CLK_NONE) &&
			ucc_set_tdm_rxtx_sync(uf_info->tdm_num,
				uf_info->rx_sync, COMM_DIR_RX)) {
			pr_err("%s:illegal value for TX clock", __func__);
			ucc_fast_free(uccf);
			return -EINVAL;
		}

		/* tdm Tx sync clock routing */
		if ((uf_info->tx_sync != QE_CLK_NONE) &&
			ucc_set_tdm_rxtx_sync(uf_info->tdm_num,
				uf_info->tx_sync, COMM_DIR_TX)) {
			pr_err("%s:illegal value for TX clock", __func__);
			ucc_fast_free(uccf);
			return -EINVAL;
		}
	}

	/* Set interrupt mask register at UCC level. */
	iowrite32be(uf_info->uccm_mask, &uf_regs->uccm);

	/* First, clear anything pending at UCC level,
	 * otherwise, old garbage may come through
	 * as soon as the dam is opened. */

	/* Writing '1' clears */
	iowrite32be(0xffffffff, &uf_regs->ucce);

	*uccf_ret = uccf;
	return 0;
}
EXPORT_SYMBOL(ucc_fast_init);

void ucc_fast_free(struct ucc_fast_private *uccf)
{
	if (!uccf)
		return;

	if (uccf->ucc_fast_tx_virtual_fifo_base_offset)
		qe_muram_free(uccf->ucc_fast_tx_virtual_fifo_base_offset);

	if (uccf->ucc_fast_rx_virtual_fifo_base_offset)
		qe_muram_free(uccf->ucc_fast_rx_virtual_fifo_base_offset);

	if (uccf->uf_regs)
		iounmap(uccf->uf_regs);

	kfree(uccf);
}
EXPORT_SYMBOL(ucc_fast_free);
