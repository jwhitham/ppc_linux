/*
 * Copyright 2014 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/clk.h>
#include <linux/mfd/syscon.h>
#include <linux/of_address.h>
#include <linux/slab.h>
#include <dt-bindings/clock/ls1021a-clock.h>

#include "clk.h"

static struct clk *clks[LS1021A_CLK_END];
static struct clk_onecell_data clk_data;

/* The DCFG registers are in big endian mode on LS1021A SoC */
static u8 ls1021a_clk_shift_be(u8 shift)
{
	u8 m, n;

	m = shift / 8;
	n = shift % 8;

	return (u8)((3 - m) * 8 + n);
}

static void __init ls1021a_clocks_init(struct device_node *np)
{
	static void __iomem *dcfg_base;
	int i;

#define DCFG_CCSR_DEVDISR1	(dcfg_base + 0x70)
#define DCFG_CCSR_DEVDISR2	(dcfg_base + 0x74)
#define DCFG_CCSR_DEVDISR3	(dcfg_base + 0x78)
#define DCFG_CCSR_DEVDISR4	(dcfg_base + 0x7c)
#define DCFG_CCSR_DEVDISR5	(dcfg_base + 0x80)
#define DCFG_CCSR_RSTRQMR1	(dcfg_base + 0xc0)

	if (dcfg_base) {
		pr_warn("LS1021A gate clk has already added.\n");
		return;
	}

	dcfg_base = of_iomap(np, 0);

	BUG_ON(!dcfg_base);

	clks[LS1021A_CLK_PBL_EN] = ls1021a_clk_gate("pbl_en", "dummy",
						DCFG_CCSR_DEVDISR1,
						ls1021a_clk_shift_be(0));
	clks[LS1021A_CLK_ESDHC_EN] = ls1021a_clk_gate("esdhc_en", "dummy",
						DCFG_CCSR_DEVDISR1,
						ls1021a_clk_shift_be(2));
	clks[LS1021A_CLK_DMA1_EN] = ls1021a_clk_gate("dma1_en", "dummy",
						DCFG_CCSR_DEVDISR1,
						ls1021a_clk_shift_be(8));
	clks[LS1021A_CLK_DMA2_EN] = ls1021a_clk_gate("dma2_en", "dummy",
						DCFG_CCSR_DEVDISR1,
						ls1021a_clk_shift_be(9));
	clks[LS1021A_CLK_USB3_PHY_EN] = ls1021a_clk_gate("usb3_phy_en", "dummy",
						DCFG_CCSR_DEVDISR1,
						ls1021a_clk_shift_be(12));
	clks[LS1021A_CLK_USB2_EN] = ls1021a_clk_gate("usb2_en", "dummy",
						DCFG_CCSR_DEVDISR1,
						ls1021a_clk_shift_be(13));
	clks[LS1021A_CLK_SATA_EN] = ls1021a_clk_gate("sata_en", "dummy",
						DCFG_CCSR_DEVDISR1,
						ls1021a_clk_shift_be(16));
	clks[LS1021A_CLK_USB3_EN] = ls1021a_clk_gate("usb3_en", "dummy",
						DCFG_CCSR_DEVDISR1,
						ls1021a_clk_shift_be(17));
	clks[LS1021A_CLK_SEC_EN] = ls1021a_clk_gate("sec_en", "dummy",
						DCFG_CCSR_DEVDISR1,
						ls1021a_clk_shift_be(22));
	clks[LS1021A_CLK_2D_ACE_EN] = ls1021a_clk_gate("2d_ace_en", "dummy",
						DCFG_CCSR_DEVDISR1,
						ls1021a_clk_shift_be(30));
	clks[LS1021A_CLK_QE_EN] = ls1021a_clk_gate("qe_en", "dummy",
						DCFG_CCSR_DEVDISR1,
						ls1021a_clk_shift_be(31));

	clks[LS1021A_CLK_ETSEC1_EN] = ls1021a_clk_gate("etsec1_en", "dummy",
						DCFG_CCSR_DEVDISR2,
						ls1021a_clk_shift_be(0));
	clks[LS1021A_CLK_ETSEC2_EN] = ls1021a_clk_gate("etsec2_en", "dummy",
						DCFG_CCSR_DEVDISR2,
						ls1021a_clk_shift_be(1));
	clks[LS1021A_CLK_ETSEC3_EN] = ls1021a_clk_gate("etsec3_en", "dummy",
						DCFG_CCSR_DEVDISR2,
						ls1021a_clk_shift_be(2));

	clks[LS1021A_CLK_PEX1_EN] = ls1021a_clk_gate("pex1_en", "dummy",
						DCFG_CCSR_DEVDISR3,
						ls1021a_clk_shift_be(0));
	clks[LS1021A_CLK_PEX2_EN] = ls1021a_clk_gate("pex2_en", "dummy",
						DCFG_CCSR_DEVDISR3,
						ls1021a_clk_shift_be(1));

	clks[LS1021A_CLK_DUART1_EN] = ls1021a_clk_gate("duart1_en", "dummy",
						DCFG_CCSR_DEVDISR4,
						ls1021a_clk_shift_be(2));
	clks[LS1021A_CLK_DUART2_EN] = ls1021a_clk_gate("duart2_en", "dummy",
						DCFG_CCSR_DEVDISR4,
						ls1021a_clk_shift_be(3));
	clks[LS1021A_CLK_QSPI_EN] = ls1021a_clk_gate("qspi_en", "dummy",
						DCFG_CCSR_DEVDISR4,
						ls1021a_clk_shift_be(4));

	clks[LS1021A_CLK_DDR_EN] = ls1021a_clk_gate("ddr_en", "dummy",
						DCFG_CCSR_DEVDISR5,
						ls1021a_clk_shift_be(0));
	clks[LS1021A_CLK_OCRAM1_EN] = ls1021a_clk_gate("ocram1_en", "dummy",
						DCFG_CCSR_DEVDISR5,
						ls1021a_clk_shift_be(4));
	clks[LS1021A_CLK_IFC_EN] = ls1021a_clk_gate("ifc_en", "dummy",
						DCFG_CCSR_DEVDISR5,
						ls1021a_clk_shift_be(8));
	clks[LS1021A_CLK_GPIO_EN] = ls1021a_clk_gate("gpio_en", "dummy",
						DCFG_CCSR_DEVDISR5,
						ls1021a_clk_shift_be(9));
	clks[LS1021A_CLK_DBG_EN] = ls1021a_clk_gate("dbg_en", "dummy",
						DCFG_CCSR_DEVDISR5,
						ls1021a_clk_shift_be(10));
	clks[LS1021A_CLK_FLEXCAN1_EN] = ls1021a_clk_gate("flexcan1_en", "dummy",
						DCFG_CCSR_DEVDISR5,
						ls1021a_clk_shift_be(12));
	clks[LS1021A_CLK_FLEXCAN234_EN] = ls1021a_clk_gate("flexcan234_en",
						"dummy", DCFG_CCSR_DEVDISR5,
						ls1021a_clk_shift_be(13));
	/* For flextimer 2/3/4/5/6/7/8 */
	clks[LS1021A_CLK_FLEXTIMER_EN] = ls1021a_clk_gate("flextimer_en",
						"dummy", DCFG_CCSR_DEVDISR5,
						ls1021a_clk_shift_be(14));
	clks[LS1021A_CLK_SECMON_EN] = ls1021a_clk_gate("secmon_en", "dummy",
						DCFG_CCSR_DEVDISR5,
						ls1021a_clk_shift_be(17));
	clks[LS1021A_CLK_WDOG_EN] = ls1021a_clk_gate("wdog_en", "dummy",
						DCFG_CCSR_RSTRQMR1,
						ls1021a_clk_shift_be(22));
	clks[LS1021A_CLK_WDOG12_EN] = ls1021a_clk_gate("wdog12_en", "wdog_en",
						DCFG_CCSR_DEVDISR5,
						ls1021a_clk_shift_be(21));
	clks[LS1021A_CLK_I2C23_EN] = ls1021a_clk_gate("i2c23_en", "dummy",
						DCFG_CCSR_DEVDISR5,
						ls1021a_clk_shift_be(22));
	/* For SAI 1/2/3/4 */
	clks[LS1021A_CLK_SAI_EN] = ls1021a_clk_gate("sai_en", "dummy",
						DCFG_CCSR_DEVDISR5,
						ls1021a_clk_shift_be(23));
	/* For lpuart 2/3/4/5/6  */
	clks[LS1021A_CLK_LPUART_EN] = ls1021a_clk_gate("lpuart_en", "dummy",
						DCFG_CCSR_DEVDISR5,
						ls1021a_clk_shift_be(24));
	clks[LS1021A_CLK_DSPI12_EN] = ls1021a_clk_gate("dspi12_en", "dummy",
						DCFG_CCSR_DEVDISR5,
						ls1021a_clk_shift_be(25));
	clks[LS1021A_CLK_ASRC_EN] = ls1021a_clk_gate("asrc_en", "dummy",
						DCFG_CCSR_DEVDISR5,
						ls1021a_clk_shift_be(26));
	clks[LS1021A_CLK_SPDIF_EN] = ls1021a_clk_gate("spdif_en", "dummy",
						DCFG_CCSR_DEVDISR5,
						ls1021a_clk_shift_be(27));
	clks[LS1021A_CLK_I2C1_EN] = ls1021a_clk_gate("i2c1_en", "dummy",
						DCFG_CCSR_DEVDISR5,
						ls1021a_clk_shift_be(29));
	clks[LS1021A_CLK_LPUART1_EN] = ls1021a_clk_gate("lpuart1_en", "dummy",
						DCFG_CCSR_DEVDISR5,
						ls1021a_clk_shift_be(30));
	clks[LS1021A_CLK_FLEXTIMER1_EN] = ls1021a_clk_gate("flextimer1_en",
						"dummy", DCFG_CCSR_DEVDISR5,
						ls1021a_clk_shift_be(31));

	for (i = 0; i < LS1021A_CLK_END; i++) {
		if (IS_ERR(clks[i])) {
			pr_err("LS1021A clk %d: register failed with %ld\n",
			       i, PTR_ERR(clks[i]));
			BUG();
		}
	}

	/* Add the clocks to provider list */
	clk_data.clks = clks;
	clk_data.clk_num = LS1021A_CLK_END;
	of_clk_add_provider(np, of_clk_src_onecell_get, &clk_data);
}
CLK_OF_DECLARE(ls1021a, "fsl,ls1021a-gate", ls1021a_clocks_init);
