/*
 * Freescale eSDHC controller driver.
 *
 * Copyright (c) 2007, 2010, 2012 Freescale Semiconductor, Inc.
 * Copyright (c) 2009 MontaVista Software, Inc.
 *
 * Authors: Xiaobo Xie <X.Xie@freescale.com>
 *	    Anton Vorontsov <avorontsov@ru.mvista.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#include <linux/err.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/mmc/host.h>
#include "sdhci-pltfm.h"
#include "sdhci-esdhc.h"

#if defined CONFIG_PPC_OF
#include <asm/mpc85xx.h>
#endif

#define VENDOR_V_22	0x12
#define VENDOR_V_23	0x13

#if defined CONFIG_PPC_OF
static u32 svr;
#endif

static u32 adapter_type;
static bool peripheral_clk_available;

static u32 esdhc_readl(struct sdhci_host *host, int reg)
{
	u32 ret;

	if (reg == SDHCI_CAPABILITIES_1) {
		ret = sdhci_32bs_readl(host, ESDHC_CAPABILITIES_1);
		switch (adapter_type) {
		case ESDHC_ADAPTER_TYPE_3:
			if (ret & ESDHC_MODE_DDR50) {
				ret &= ESDHC_MODE_DDR50_SEL;
				/* enable 1/8V DDR capable */
				host->mmc->caps |= MMC_CAP_1_8V_DDR;
			} else
				ret &= ~ESDHC_MODE_MASK;
			break;
		default:
			ret &= ~ESDHC_MODE_MASK;
		}
	} else
		ret = sdhci_32bs_readl(host, reg);
	/*
	 * The bit of ADMA flag in eSDHC is not compatible with standard
	 * SDHC register, so set fake flag SDHCI_CAN_DO_ADMA2 when ADMA is
	 * supported by eSDHC.
	 * And for many FSL eSDHC controller, the reset value of field
	 * SDHCI_CAN_DO_ADMA1 is one, but some of them can't support ADMA,
	 * only these vendor version is greater than 2.2/0x12 support ADMA.
	 * For FSL eSDHC, must aligned 4-byte, so use 0xFC to read the
	 * the verdor version number, oxFE is SDHCI_HOST_VERSION.
	 */
	if ((reg == SDHCI_CAPABILITIES) && (ret & SDHCI_CAN_DO_ADMA1)) {
		u32 tmp = sdhci_32bs_readl(host, SDHCI_SLOT_INT_STATUS);
		tmp = (tmp & SDHCI_VENDOR_VER_MASK) >> SDHCI_VENDOR_VER_SHIFT;
		if (tmp > VENDOR_V_22)
			ret |= SDHCI_CAN_DO_ADMA2;
	}

	/*
	 * Clock of eSDHC host don't support to be disabled and enabled.
	 * So clock stable bit doesn't behave as it mean, So fix it to
	 * '1' to avoid misreading.
	 */
	if (reg == SDHCI_PRESENT_STATE)
		ret |= ESDHC_CLK_STABLE;

	return ret;
}

static u16 esdhc_readw(struct sdhci_host *host, int reg)
{
	u16 ret;
	int base = reg & ~0x3;
	int shift = (reg & 0x2) * 8;

	if (unlikely(reg == SDHCI_HOST_VERSION))
		ret = sdhci_32bs_readl(host, base) & 0xffff;
	else
		ret = (sdhci_32bs_readl(host, base) >> shift) & 0xffff;

#if defined CONFIG_PPC_OF
	/* T4240-R1.0-R2.0 had a incorrect vendor version and spec version */
	if ((reg == SDHCI_HOST_VERSION) &&
			(((SVR_SOC_VER(svr) == SVR_T4240) ||
			  (SVR_SOC_VER(svr) == SVR_T4160) ||
			  (SVR_SOC_VER(svr) == SVR_T4080)) &&
			 (SVR_REV(svr) <= 0x20)))
		ret = (VENDOR_V_23 << SDHCI_VENDOR_VER_SHIFT) | SDHCI_SPEC_200;
#endif

	return ret;
}

static u8 esdhc_readb(struct sdhci_host *host, int reg)
{
	int base = reg & ~0x3;
	int shift = (reg & 0x3) * 8;
	u32 ret;
	u8 val;

	ret = sdhci_32bs_readl(host, base);

	/*
	 * "DMA select" locates at offset 0x28 in SD specification, but on
	 * P5020 or P3041, it locates at 0x29.
	 */
	if (reg == SDHCI_HOST_CONTROL) {
		u32 dma_bits;

		/* DMA select is 22,23 bits in Protocol Control Register */
		dma_bits = (ret >> 5) & SDHCI_CTRL_DMA_MASK;

		/* fixup the result */
		ret &= ~SDHCI_CTRL_DMA_MASK;
		ret |= dma_bits;
		val = (ret & 0xff);
	}

	val = (ret >> shift) & 0xff;

	return val;
}

static void esdhc_writel(struct sdhci_host *host, u32 val, int reg)
{
	/*
	 * Enable IRQSTATEN[BGESEN] is just to set IRQSTAT[BGE]
	 * when SYSCTL[RSTD]) is set for some special operations.
	 * No any impact other operation.
	 */
	if (reg == SDHCI_INT_ENABLE)
		val |= SDHCI_INT_BLK_GAP;
	sdhci_32bs_writel(host, val, reg);
}

static void esdhc_writew(struct sdhci_host *host, u16 val, int reg)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);

	switch (reg) {
	case SDHCI_TRANSFER_MODE:
		/*
		 * Postpone this write, we must do it together with a
		 * command write that is down below.
		 */
		pltfm_host->xfer_mode_shadow = val;
		return;
	case SDHCI_COMMAND:
		sdhci_32bs_writel(host, val << 16 |
				  pltfm_host->xfer_mode_shadow,
				  SDHCI_TRANSFER_MODE);
		return;
	}

	if (reg == SDHCI_BLOCK_SIZE) {
		/*
		 * Two last DMA bits are reserved, and first one is used for
		 * non-standard blksz of 4096 bytes that we don't support
		 * yet. So clear the DMA boundary bits.
		 */
		val &= ~SDHCI_MAKE_BLKSZ(0x7, 0);
	}
	sdhci_clrsetbits(host, 0xffff, val, reg);
}

static void esdhc_writeb(struct sdhci_host *host, u8 val, int reg)
{
	/*
	 * "DMA select" location is offset 0x28 in SD specification, but on
	 * P5020 or P3041, it's located at 0x29.
	 */
	if (reg == SDHCI_HOST_CONTROL) {
		u32 dma_bits;

		/*
		 * If host control register is not standard, exit
		 * this function
		 */
		if (host->quirks2 & SDHCI_QUIRK2_BROKEN_HOST_CONTROL)
			return;

		/* DMA select is 22,23 bits in Protocol Control Register */
		dma_bits = (val & SDHCI_CTRL_DMA_MASK) << 5;
		sdhci_clrsetbits(host, SDHCI_CTRL_DMA_MASK << 5, dma_bits,
				 SDHCI_HOST_CONTROL);
		val &= ~SDHCI_CTRL_DMA_MASK;
		val |= sdhci_32bs_readl(host, reg) & SDHCI_CTRL_DMA_MASK;
	}

	/* Prevent SDHCI core from writing reserved bits (e.g. HISPD). */
	if (reg == SDHCI_HOST_CONTROL) {
		val &= ~ESDHC_HOST_CONTROL_RES;
		val &= ~SDHCI_CTRL_HISPD;
		val |= (sdhci_32bs_readl(host, reg) & SDHCI_CTRL_HISPD);
	}

	/*
	 * If we have this quirk:
	 * 1. Disabled the clock.
	 * 2. Perform reset all command.
	 * 3. Enable the clock.
	 */
	if ((reg == SDHCI_SOFTWARE_RESET) &&
			(host->quirks2 & SDHCI_QUIRK2_BROKEN_RESET_ALL) &&
			(val & SDHCI_RESET_ALL)) {
		u32 temp;

		temp = esdhc_readl(host, ESDHC_SYSTEM_CONTROL);
		temp &= ~ESDHC_CLOCK_CRDEN;
		esdhc_writel(host, temp, ESDHC_SYSTEM_CONTROL);

		sdhci_32bs_writeb(host, val, reg);

		temp |= ESDHC_CLOCK_CRDEN;
		esdhc_writel(host, temp, ESDHC_SYSTEM_CONTROL);

		return;
	}

	if (reg == SDHCI_POWER_CONTROL) {
		/* eSDHC don't support gate off power */
		if (!host->pwr || !val)
			return;

#if defined CONFIG_PPC_OF
		if (SVR_SOC_VER(svr) == SVR_T4240 ||
				SVR_SOC_VER(svr) == SVR_T4160 ||
				SVR_SOC_VER(svr) == SVR_T4080) {
			u8 vol;

			vol = sdhci_32bs_readb(host, reg);
			if (host->pwr == SDHCI_POWER_180)
				vol &= ~ESDHC_VOL_SEL;
			else
				vol |= ESDHC_VOL_SEL;
		} else
			return;
#endif
	}

	sdhci_clrsetbits(host, 0xff, val, reg);
}

/*
 * For Abort or Suspend after Stop at Block Gap, ignore the ADMA
 * error(IRQSTAT[ADMAE]) if both Transfer Complete(IRQSTAT[TC])
 * and Block Gap Event(IRQSTAT[BGE]) are also set.
 * For Continue, apply soft reset for data(SYSCTL[RSTD]);
 * and re-issue the entire read transaction from beginning.
 */
static void esdhci_of_adma_workaround(struct sdhci_host *host, u32 intmask)
{
	u32 tmp;
	bool applicable;
	dma_addr_t dmastart;
	dma_addr_t dmanow;

	tmp = esdhc_readl(host, SDHCI_SLOT_INT_STATUS);
	tmp = (tmp & SDHCI_VENDOR_VER_MASK) >> SDHCI_VENDOR_VER_SHIFT;

	applicable = (intmask & SDHCI_INT_DATA_END) &&
		(intmask & SDHCI_INT_BLK_GAP) &&
		(tmp == VENDOR_V_23);
	if (applicable) {

		sdhci_reset(host, SDHCI_RESET_DATA);
		host->data->error = 0;
		dmastart = sg_dma_address(host->data->sg);
		dmanow = dmastart + host->data->bytes_xfered;
		/*
		 * Force update to the next DMA block boundary.
		 */
		dmanow = (dmanow & ~(SDHCI_DEFAULT_BOUNDARY_SIZE - 1)) +
			SDHCI_DEFAULT_BOUNDARY_SIZE;
		host->data->bytes_xfered = dmanow - dmastart;
		esdhc_writel(host, dmanow, SDHCI_DMA_ADDRESS);

		return;
	}

#if defined CONFIG_PPC_OF
	/*
	 * Check for A-004388: eSDHC DMA might not stop if error
	 * occurs on system transaction
	 * Impact list:
	 * T4240-4160-R1.0 B4860-4420-R1.0-R2.0 P1010-1014-R1.0
	 * P3041-R1.0-R2.0-R1.1 P2041-2040-R1.0-R1.1-R2.0
	 * P5020-5010-R2.0-R1.0 P5040-5021-R2.0-R2.1
	 */
	if (!(((SVR_SOC_VER(svr) == SVR_T4240) && (SVR_REV(svr) == 0x10)) ||
		((SVR_SOC_VER(svr) == SVR_T4160) && (SVR_REV(svr) == 0x10)) ||
		((SVR_SOC_VER(svr) == SVR_T4080) && (SVR_REV(svr) == 0x10)) ||
		((SVR_SOC_VER(svr) == SVR_B4420) && (SVR_REV(svr) == 0x10)) ||
		((SVR_SOC_VER(svr) == SVR_B4420) && (SVR_REV(svr) == 0x20)) ||
		((SVR_SOC_VER(svr) == SVR_B4860) && (SVR_REV(svr) == 0x10)) ||
		((SVR_SOC_VER(svr) == SVR_B4860) && (SVR_REV(svr) == 0x20)) ||
		((SVR_SOC_VER(svr) == SVR_P1010) && (SVR_REV(svr) == 0x10)) ||
		((SVR_SOC_VER(svr) == SVR_P1014) && (SVR_REV(svr) == 0x10)) ||
		((SVR_SOC_VER(svr) == SVR_P3041) && (SVR_REV(svr) <= 0x20)) ||
		((SVR_SOC_VER(svr) == SVR_P2041) && (SVR_REV(svr) <= 0x20)) ||
		((SVR_SOC_VER(svr) == SVR_P2040) && (SVR_REV(svr) <= 0x20)) ||
		((SVR_SOC_VER(svr) == SVR_P5020) && (SVR_REV(svr) <= 0x20)) ||
		((SVR_SOC_VER(svr) == SVR_P5010) && (SVR_REV(svr) <= 0x20)) ||
		((SVR_SOC_VER(svr) == SVR_P5021) && (SVR_REV(svr) <= 0x21)) ||
		((SVR_SOC_VER(svr) == SVR_P5040) && (SVR_REV(svr) <= 0x21))))
		return;
#endif

	sdhci_reset(host, SDHCI_RESET_DATA);

	if (host->flags & SDHCI_USE_ADMA) {
		u32 mod, i, offset;
		u8 *desc;
		dma_addr_t addr;
		struct scatterlist *sg;
		__le32 *dataddr;
		__le32 *cmdlen;

		/*
		 * If block count was enabled, in case read transfer there
		 * is no data was corrupted
		 */
		mod = esdhc_readl(host, SDHCI_TRANSFER_MODE);
		if ((mod & SDHCI_TRNS_BLK_CNT_EN) &&
				(host->data->flags & MMC_DATA_READ))
			host->data->error = 0;

		BUG_ON(!host->data);
		desc = host->adma_desc;
		for_each_sg(host->data->sg, sg, host->sg_count, i) {
			addr = sg_dma_address(sg);
			offset = (4 - (addr & 0x3)) & 0x3;
			if (offset)
				desc += 8;
			desc += 8;
		}

		/*
		 * Add an extra zero descriptor next to the
		 * terminating descriptor.
		 */
		desc += 8;
		WARN_ON((desc - host->adma_desc) > (128 * 2 + 1) * 4);

		dataddr = (__le32 __force *)(desc + 4);
		cmdlen = (__le32 __force *)desc;

		cmdlen[0] = cpu_to_le32(0);
		dataddr[0] = cpu_to_le32(0);
	}

	if ((host->flags & SDHCI_USE_SDMA) &&
			(host->data->flags & MMC_DATA_READ))
		host->data->error = 0;

	return;
}

static int esdhc_of_enable_dma(struct sdhci_host *host)
{
	esdhc_writel(host, esdhc_readl(host, ESDHC_DMA_SYSCTL)
			| ESDHC_DMA_SNOOP, ESDHC_DMA_SYSCTL);
	return 0;
}

static unsigned int esdhc_of_get_max_clock(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);

	return pltfm_host->clock;
}

static unsigned int esdhc_of_get_min_clock(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);

	return pltfm_host->clock / 256 / 16;
}

static void esdhc_of_set_clock(struct sdhci_host *host, unsigned int clock)
{
	/* Workaround to reduce the clock frequency for p1010 esdhc */
	if (of_find_compatible_node(NULL, NULL, "fsl,p1010-esdhc")) {
		if (clock > 20000000)
			clock -= 5000000;
		if (clock > 40000000)
			clock -= 5000000;
	}

	/* Set the clock */
	esdhc_set_clock(host, clock, host->max_clk);
}

#ifdef CONFIG_PM
static u32 esdhc_proctl;
static void esdhc_of_suspend(struct sdhci_host *host)
{
	esdhc_proctl = esdhc_readl(host, SDHCI_HOST_CONTROL);
}

static void esdhc_of_resume(struct sdhci_host *host)
{
	esdhc_writel(host, esdhc_proctl, SDHCI_HOST_CONTROL);
}
#endif

static void esdhc_of_platform_reset_enter(struct sdhci_host *host, u8 mask)
{
	if ((host->quirks2 & SDHCI_QUIRK2_DISABLE_CLOCK_BEFORE_RESET) &&
	    (mask & SDHCI_RESET_ALL)) {
		u16 clk;

		clk = esdhc_readw(host, SDHCI_CLOCK_CONTROL);
		clk &= ~ESDHC_CLOCK_CRDEN;
		esdhc_writew(host, clk, SDHCI_CLOCK_CONTROL);
	}
}

static void esdhc_of_platform_reset_exit(struct sdhci_host *host, u8 mask)
{
	if ((host->quirks2 & SDHCI_QUIRK2_DISABLE_CLOCK_BEFORE_RESET) &&
	    (mask & SDHCI_RESET_ALL)) {
		u16 clk;

		clk = esdhc_readw(host, SDHCI_CLOCK_CONTROL);
		clk |= ESDHC_CLOCK_CRDEN;
		esdhc_writew(host, clk, SDHCI_CLOCK_CONTROL);
	}
}

static void esdhc_of_platform_init(struct sdhci_host *host)
{
	u32 vvn;

#if defined CONFIG_PPC_OF
	svr =  mfspr(SPRN_SVR);
#endif
	vvn = esdhc_readl(host, SDHCI_SLOT_INT_STATUS);
	vvn = (vvn & SDHCI_VENDOR_VER_MASK) >> SDHCI_VENDOR_VER_SHIFT;
	if (vvn == VENDOR_V_22)
		host->quirks2 |= SDHCI_QUIRK2_HOST_NO_CMD23;

	if (vvn > VENDOR_V_22)
		host->quirks &= ~SDHCI_QUIRK_NO_BUSY_IRQ;

#if defined CONFIG_PPC_OF
	/*
	 * Check for A-005055: A glitch is generated on the card clock
	 * due to software reset or a clock change
	 * Impact list:
	 * T4240-4160-R1.0 B4860-4420-R1.0-R2.0 P3041-R1.0-R1.1-R2.0
	 * P2041-2040-R1.0-R1.1-R2.0 P1010-1014-R1.0 P5020-5010-R1.0-R2.0
	 * P5040-5021-R1.0-R2.0-R2.1
	 */
	if (((SVR_SOC_VER(svr) == SVR_T4240) && (SVR_REV(svr) == 0x10)) ||
	    ((SVR_SOC_VER(svr) == SVR_T4240) && (SVR_REV(svr) == 0x20)) ||
	    ((SVR_SOC_VER(svr) == SVR_T4160) && (SVR_REV(svr) == 0x10)) ||
	    ((SVR_SOC_VER(svr) == SVR_T4160) && (SVR_REV(svr) == 0x20)) ||
	    ((SVR_SOC_VER(svr) == SVR_T4080) && (SVR_REV(svr) == 0x10)) ||
	    ((SVR_SOC_VER(svr) == SVR_T4080) && (SVR_REV(svr) == 0x20)) ||
	    ((SVR_SOC_VER(svr) == SVR_B4860) && (SVR_REV(svr) == 0x10)) ||
	    ((SVR_SOC_VER(svr) == SVR_B4860) && (SVR_REV(svr) == 0x20)) ||
	    ((SVR_SOC_VER(svr) == SVR_B4420) && (SVR_REV(svr) == 0x10)) ||
	    ((SVR_SOC_VER(svr) == SVR_B4420) && (SVR_REV(svr) == 0x20)) ||
	    ((SVR_SOC_VER(svr) == SVR_P5040) && (SVR_REV(svr) <= 0x21)) ||
	    ((SVR_SOC_VER(svr) == SVR_P5020) && (SVR_REV(svr) <= 0x20)) ||
	    ((SVR_SOC_VER(svr) == SVR_P5021) && (SVR_REV(svr) <= 0x21)) ||
	    ((SVR_SOC_VER(svr) == SVR_P5010) && (SVR_REV(svr) <= 0x20)) ||
	    ((SVR_SOC_VER(svr) == SVR_P3041) && (SVR_REV(svr) <= 0x20)) ||
	    ((SVR_SOC_VER(svr) == SVR_P2041) && (SVR_REV(svr) <= 0x20)) ||
	    ((SVR_SOC_VER(svr) == SVR_P2040) && (SVR_REV(svr) <= 0x20)) ||
	    ((SVR_SOC_VER(svr) == SVR_P1014) && (SVR_REV(svr) == 0x10)) ||
	    ((SVR_SOC_VER(svr) == SVR_P1010) && (SVR_REV(svr) == 0x10)))
		host->quirks2 |= SDHCI_QUIRK2_DISABLE_CLOCK_BEFORE_RESET;
#endif
}

/* Return: 1 - the card is present; 0 - card is absent */
static int esdhc_of_get_cd(struct sdhci_host *host)
{
	u32 present;
	u32 sysctl;

	if (host->flags & SDHCI_DEVICE_DEAD)
		return 0;
	if (host->quirks2 & SDHCI_QUIRK2_FORCE_CMD13_DETECT_CARD)
		return -ENOSYS;

	sysctl = sdhci_32bs_readl(host, SDHCI_CLOCK_CONTROL);

	/* Enable the controller clock to update the present state */
	sdhci_32bs_writel(host, sysctl | SDHCI_CLOCK_INT_EN,
			SDHCI_CLOCK_CONTROL);

	/* Detect the card present or absent */
	present = sdhci_32bs_readl(host, SDHCI_PRESENT_STATE);
	present &= (SDHCI_CARD_PRESENT | SDHCI_CARD_CDPL);

	/* Resave the previous to System control register */
	sdhci_32bs_writel(host, sysctl, SDHCI_CLOCK_CONTROL);

	return !!present;
}

static void esdhc_get_pltfm_irq(struct sdhci_host *host, u32 *irq)
{
	*irq |= ESDHC_INT_DMA_ERROR;
}

static void esdhc_pltfm_irq_handler(struct sdhci_host *host, u32 intmask)
{
	if (intmask & (ESDHC_INT_DMA_ERROR | SDHCI_INT_ADMA_ERROR)) {
		host->data->error = -EIO;
		pr_err("%s: ADMA error\n", mmc_hostname(host->mmc));
		sdhci_show_adma_error(host);
		esdhci_of_adma_workaround(host, intmask);
	}
}

static int esdhc_pltfm_bus_width(struct sdhci_host *host, int width)
{
	u32 ctrl;

	switch (width) {
	case MMC_BUS_WIDTH_8:
		ctrl = ESDHC_CTRL_8BITBUS;
		break;

	case MMC_BUS_WIDTH_4:
		ctrl = ESDHC_CTRL_4BITBUS;
		break;

	default:
		ctrl = 0;
		break;
	}

	sdhci_clrsetbits(host, ESDHC_CTRL_BUSWIDTH_MASK, ctrl,
			 SDHCI_HOST_CONTROL);

	return 0;
}

static void esdhc_clock_control(struct sdhci_host *host, bool enable)
{
	u32 value;
	u32 time_out;

	value = sdhci_readl(host, ESDHC_SYSTEM_CONTROL);

	if (enable)
		value |= ESDHC_CLOCK_CRDEN;
	else
		value &= ~ESDHC_CLOCK_CRDEN;

	sdhci_writel(host, value, ESDHC_SYSTEM_CONTROL);

	time_out = 20;
	value = ESDHC_CLK_STABLE;
	while (!(sdhci_readl(host, ESDHCI_PRESENT_STATE) & value)) {
		if (time_out == 0) {
			pr_err("%s: Internal clock never stabilised.\n",
				mmc_hostname(host->mmc));
			break;
		}
		time_out--;
		mdelay(1);
	}
}

static int esdhc_set_uhs_signaling(struct sdhci_host *host, unsigned int uhs)
{
	u16 ctrl_2;
	u32 time_out;
	u32 value;

	ctrl_2 = sdhci_readw(host, SDHCI_HOST_CONTROL2);
	/* Select Bus Speed Mode for host */
	ctrl_2 &= ~SDHCI_CTRL_UHS_MASK;
	if ((uhs == MMC_TIMING_MMC_HS200) ||
		(uhs == MMC_TIMING_UHS_SDR104))
		ctrl_2 |= SDHCI_CTRL_UHS_SDR104;
	else if (uhs == MMC_TIMING_UHS_SDR12)
		ctrl_2 |= SDHCI_CTRL_UHS_SDR12;
	else if (uhs == MMC_TIMING_UHS_SDR25)
		ctrl_2 |= SDHCI_CTRL_UHS_SDR25;
	else if (uhs == MMC_TIMING_UHS_SDR50)
		ctrl_2 |= SDHCI_CTRL_UHS_SDR50;
	else if (uhs == MMC_TIMING_UHS_DDR50)
		ctrl_2 |= SDHCI_CTRL_UHS_DDR50;

	if (uhs == MMC_TIMING_UHS_DDR50) {
		esdhc_clock_control(host, false);
		sdhci_writew(host, ctrl_2, SDHCI_HOST_CONTROL2);
		value = sdhci_readl(host, ESDHC_CLOCK_CONTROL);
		value |= (ESDHC_CLKLPBK_EXTPIN | ESDHC_CMDCLK_SHIFTED);
		sdhci_writel(host, value, ESDHC_CLOCK_CONTROL);
		esdhc_clock_control(host, true);

		esdhc_clock_control(host, false);
		value = sdhci_readl(host, ESDHC_DMA_SYSCTL);
		value |= ESDHC_FLUSH_ASYNC_FIFO;
		sdhci_writel(host, value, ESDHC_DMA_SYSCTL);
		/* Wait max 20 ms */
		time_out = 20;
		value = ESDHC_FLUSH_ASYNC_FIFO;
		while (sdhci_readl(host, ESDHC_DMA_SYSCTL) & value) {
			if (time_out == 0) {
				pr_err("%s: FAF bit is auto cleaned failed.\n",
					mmc_hostname(host->mmc));

				break;
			}
			time_out--;
			mdelay(1);
		}
		esdhc_clock_control(host, true);
	} else
		sdhci_writew(host, ctrl_2, SDHCI_HOST_CONTROL2);
	return 0;
}

static const struct sdhci_ops sdhci_esdhc_ops = {
	.read_l = esdhc_readl,
	.read_w = esdhc_readw,
	.read_b = esdhc_readb,
	.write_l = esdhc_writel,
	.write_w = esdhc_writew,
	.write_b = esdhc_writeb,
	.set_clock = esdhc_of_set_clock,
	.enable_dma = esdhc_of_enable_dma,
	.get_max_clock = esdhc_of_get_max_clock,
	.get_min_clock = esdhc_of_get_min_clock,
	.get_platform_irq = esdhc_get_pltfm_irq,
	.handle_platform_irq = esdhc_pltfm_irq_handler,
	.platform_reset_enter = esdhc_of_platform_reset_enter,
	.platform_reset_exit = esdhc_of_platform_reset_exit,
	.platform_init = esdhc_of_platform_init,
	.get_cd = esdhc_of_get_cd,
#ifdef CONFIG_PM
	.platform_suspend = esdhc_of_suspend,
	.platform_resume = esdhc_of_resume,
#endif
	.adma_workaround = esdhci_of_adma_workaround,
	.platform_bus_width = esdhc_pltfm_bus_width,
	.set_uhs_signaling = esdhc_set_uhs_signaling,
};

static const struct sdhci_pltfm_data sdhci_esdhc_pdata = {
	/*
	 * card detection could be handled via GPIO
	 * eSDHC cannot support End Attribute in NOP ADMA descriptor
	 */
	.quirks = ESDHC_DEFAULT_QUIRKS | SDHCI_QUIRK_BROKEN_CARD_DETECTION
		| SDHCI_QUIRK_NO_CARD_NO_RESET
		| SDHCI_QUIRK_NO_ENDATTR_IN_NOPDESC,
	.ops = &sdhci_esdhc_ops,
};

static void esdhc_get_property(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct sdhci_host *host = platform_get_drvdata(pdev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	const __be32 *value;
	int size;

	sdhci_get_of_property(pdev);

	/* call to generic mmc_of_parse to support additional capabilities */
	mmc_of_parse(host->mmc);
	mmc_of_parse_voltage(np, &host->ocr_mask);

	value = of_get_property(np, "adapter-type", &size);
	if (value && size == sizeof(*value) && *value)
		adapter_type = be32_to_cpup(value);

	/* If getting a peripheral-frequency, use it instead */
	value = of_get_property(np, "peripheral-frequency", &size);
	if (value && size == sizeof(*value) && *value) {
		pltfm_host->clock = be32_to_cpup(value);
		peripheral_clk_available = true;
	} else
		peripheral_clk_available = false;

	if (of_device_is_compatible(np, "fsl,p2020-esdhc")) {
		/*
		 * Freescale messed up with P2020 as it has a non-standard
		 * host control register
		 */
		host->quirks2 |= SDHCI_QUIRK2_BROKEN_HOST_CONTROL;
	}

	if (of_device_is_compatible(np, "fsl,ls1021a-esdhc"))
		host->quirks |= SDHCI_QUIRK_BROKEN_TIMEOUT_VAL;

	if (!pltfm_host->clock) {
		pltfm_host->clk = devm_clk_get(&pdev->dev, NULL);
		pltfm_host->clock = clk_get_rate(pltfm_host->clk);
		clk_prepare_enable(pltfm_host->clk);
	}
}

static int sdhci_esdhc_probe(struct platform_device *pdev)
{
	struct sdhci_host *host;
	int ret;
	u32 value;

	host = sdhci_pltfm_init(pdev, &sdhci_esdhc_pdata, 0);
	if (IS_ERR(host))
		return PTR_ERR(host);

	esdhc_get_property(pdev);

	/* Select peripheral clock as the eSDHC clock */
	if (peripheral_clk_available) {
		esdhc_clock_control(host, false);
		value = sdhci_readl(host, ESDHC_DMA_SYSCTL);
		value |= ESDHC_USE_PERICLK;
		sdhci_writel(host, value, ESDHC_DMA_SYSCTL);
		esdhc_clock_control(host, true);
	}

	ret = sdhci_add_host(host);
	if (ret)
		sdhci_pltfm_free(pdev);

	return ret;
}

static int sdhci_esdhc_remove(struct platform_device *pdev)
{
	return sdhci_pltfm_unregister(pdev);
}

static const struct of_device_id sdhci_esdhc_of_match[] = {
	{ .compatible = "fsl,mpc8379-esdhc" },
	{ .compatible = "fsl,mpc8536-esdhc" },
	{ .compatible = "fsl,esdhc" },
	{ }
};
MODULE_DEVICE_TABLE(of, sdhci_esdhc_of_match);

static struct platform_driver sdhci_esdhc_driver = {
	.driver = {
		.name = "sdhci-esdhc",
		.owner = THIS_MODULE,
		.of_match_table = sdhci_esdhc_of_match,
		.pm = SDHCI_PLTFM_PMOPS,
	},
	.probe = sdhci_esdhc_probe,
	.remove = sdhci_esdhc_remove,
};

module_platform_driver(sdhci_esdhc_driver);

MODULE_DESCRIPTION("SDHCI OF driver for Freescale MPC eSDHC");
MODULE_AUTHOR("Xiaobo Xie <X.Xie@freescale.com>, "
	      "Anton Vorontsov <avorontsov@ru.mvista.com>");
MODULE_LICENSE("GPL v2");
