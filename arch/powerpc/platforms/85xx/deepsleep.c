/*
 * Support deep sleep feature for T104x
 *
 * Copyright 2014 Freescale Semiconductor Inc.
 *
 * Author: Chenhui Zhao <chenhui.zhao@freescale.com>
 *
 * This program is free software; you can redistribute	it and/or modify it
 * under  the terms of	the GNU General	 Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/kernel.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <sysdev/fsl_soc.h>
#include <asm/machdep.h>
#include <asm/fsl_pm.h>

#define SIZE_1MB	0x100000
#define SIZE_2MB	0x200000

#define CPC_CPCHDBCR0		0x10f00
#define CPC_CPCHDBCR0_SPEC_DIS	0x08000000

#define CCSR_SCFG_DPSLPCR	0xfc000
#define CCSR_SCFG_DPSLPCR_WDRR_EN	0x1
#define CCSR_SCFG_SPARECR2	0xfc504
#define CCSR_SCFG_SPARECR3	0xfc508

#define CCSR_GPIO1_GPDIR	0x130000
#define CCSR_GPIO1_GPODR	0x130004
#define CCSR_GPIO1_GPDAT	0x130008
#define CCSR_GPIO1_GPDIR_29	0x4

#define RCPM_BLOCK_OFFSET	0x00022000
#define EPU_BLOCK_OFFSET	0x00000000
#define NPC_BLOCK_OFFSET	0x00001000

#define CSTTACR0		0xb00
#define CG1CR0			0x31c

/* 128 bytes buffer for restoring data broke by DDR training initialization */
#define DDR_BUF_SIZE	128
static u8 ddr_buff[DDR_BUF_SIZE] __aligned(64);

static void *dcsr_base, *ccsr_base, *pld_base;
static int pld_flag;

int fsl_dp_iomap(void)
{
	struct device_node *np;
	int ret = 0;
	phys_addr_t ccsr_phy_addr, dcsr_phy_addr;

	ccsr_phy_addr = get_immrbase();
	if (ccsr_phy_addr == -1) {
		pr_err("%s: Can't get the address of CCSR\n", __func__);
		ret = -EINVAL;
		goto ccsr_err;
	}
	ccsr_base = ioremap(ccsr_phy_addr, SIZE_2MB);
	if (!ccsr_base) {
		ret = -ENOMEM;
		goto ccsr_err;
	}

	dcsr_phy_addr = get_dcsrbase();
	if (dcsr_phy_addr == -1) {
		pr_err("%s: Can't get the address of DCSR\n", __func__);
		ret = -EINVAL;
		goto dcsr_err;
	}
	dcsr_base = ioremap(dcsr_phy_addr, SIZE_1MB);
	if (!dcsr_base) {
		ret = -ENOMEM;
		goto dcsr_err;
	}

	np = of_find_compatible_node(NULL, NULL, "fsl,tetra-fpga");
	if (np) {
		pld_flag = T1040QDS_TETRA_FLAG;
	} else {
		np = of_find_compatible_node(NULL, NULL, "fsl,t104xrdb-cpld");
		if (np) {
			pld_flag = T104xRDB_CPLD_FLAG;
		} else {
			pr_err("%s: Can't find the FPGA/CPLD node\n",
					__func__);
			ret = -EINVAL;
			goto pld_err;
		}
	}
	pld_base = of_iomap(np, 0);
	of_node_put(np);

	return 0;

pld_err:
	iounmap(dcsr_base);
dcsr_err:
	iounmap(ccsr_base);
ccsr_err:
	ccsr_base = NULL;
	dcsr_base = NULL;
	pld_base = NULL;
	return ret;
}

void fsl_dp_iounmap(void)
{
	if (dcsr_base) {
		iounmap(dcsr_base);
		dcsr_base = NULL;
	}

	if (ccsr_base) {
		iounmap(ccsr_base);
		ccsr_base = NULL;
	}

	if (pld_base) {
		iounmap(pld_base);
		pld_base = NULL;
	}
}

static void fsl_dp_ddr_save(void *ccsr_base)
{
	u32 ddr_buff_addr;

	/*
	 * DDR training initialization will break 128 bytes at the beginning
	 * of DDR, therefore, save them so that the bootloader will restore
	 * them. Assume that DDR is mapped to the address space started with
	 * CONFIG_PAGE_OFFSET.
	 */
	memcpy(ddr_buff, (void *)CONFIG_PAGE_OFFSET, DDR_BUF_SIZE);

	/* assume ddr_buff is in the physical address space of 4GB */
	ddr_buff_addr = (u32)(__pa(ddr_buff) & 0xffffffff);

	/*
	 * the bootloader will restore the first 128 bytes of DDR from
	 * the location indicated by the register SPARECR3
	 */
	out_be32(ccsr_base + CCSR_SCFG_SPARECR3, ddr_buff_addr);
}

static void fsl_dp_set_resume_pointer(void *ccsr_base)
{
	u32 resume_addr;

	/* the bootloader will finally jump to this address to return kernel */
#ifdef CONFIG_PPC32
	resume_addr = (u32)(__pa(fsl_booke_deep_sleep_resume));
#else
	resume_addr = (u32)(__pa(*(u64 *)fsl_booke_deep_sleep_resume)
			    & 0xffffffff);
#endif

	/* use the register SPARECR2 to save the resume address */
	out_be32(ccsr_base + CCSR_SCFG_SPARECR2, resume_addr);

}

int fsl_enter_epu_deepsleep(void)
{
	fsl_dp_ddr_save(ccsr_base);

	fsl_dp_set_resume_pointer(ccsr_base);

	/*  enable Warm Device Reset request. */
	setbits32(ccsr_base + CCSR_SCFG_DPSLPCR, CCSR_SCFG_DPSLPCR_WDRR_EN);

	/* set GPIO1_29 as an output pin (not open-drain), and output 0 */
	clrbits32(ccsr_base + CCSR_GPIO1_GPDAT, CCSR_GPIO1_GPDIR_29);
	clrbits32(ccsr_base + CCSR_GPIO1_GPODR, CCSR_GPIO1_GPDIR_29);
	setbits32(ccsr_base + CCSR_GPIO1_GPDIR, CCSR_GPIO1_GPDIR_29);

	/*
	 * Disable CPC speculation to avoid deep sleep hang, especially
	 * in secure boot mode. This bit will be cleared automatically
	 * when resuming from deep sleep.
	 */
	setbits32(ccsr_base + CPC_CPCHDBCR0, CPC_CPCHDBCR0_SPEC_DIS);

	fsl_epu_setup_default(dcsr_base + EPU_BLOCK_OFFSET);
	fsl_npc_setup_default(dcsr_base + NPC_BLOCK_OFFSET);
	out_be32(dcsr_base + RCPM_BLOCK_OFFSET + CSTTACR0, 0x00001001);
	out_be32(dcsr_base + RCPM_BLOCK_OFFSET + CG1CR0, 0x00000001);

	fsl_dp_enter_low(ccsr_base, dcsr_base, pld_base, pld_flag);

	/* disable Warm Device Reset request */
	clrbits32(ccsr_base + CCSR_SCFG_DPSLPCR, CCSR_SCFG_DPSLPCR_WDRR_EN);

	fsl_epu_clean_default(dcsr_base + EPU_BLOCK_OFFSET);

	return 0;
}
