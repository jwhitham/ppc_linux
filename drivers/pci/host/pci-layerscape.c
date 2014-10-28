/*
 * PCIe host controller driver for Freescale Layerscape SoCs
 *
 * Copyright (C) 2014 Freescale Semiconductor.
 *
  * Author: Minghuan Lian <Minghuan.Lian@freescale.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of_pci.h>
#include <linux/of_platform.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/resource.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/bitrev.h>

#include "pcie-designware.h"

/* PEX1/2 Misc Ports Status Register */
#define SCFG_PEXMSCPORTSR(pex_idx)	(0x94 + (pex_idx) * 4)
#define LTSSM_STATE_SHIFT	20
#define LTSSM_STATE_MASK	0x3f
#define LTSSM_PCIE_L0		0x11 /* L0 state */

/* SCFG MSI register */
#define SCFG_SPIMSICR		0x40
#define SCFG_SPIMSICLRCR	0x90

#define MSI_LS1021A_ADDR		0x1570040
#define MSI_LS1021A_DATA(pex_idx)	(0xb3 + pex_idx)

/* Symbol Timer Register and Filter Mask Register 1 */
#define PCIE_STRFMR1 0x71c

struct ls_pcie {
	struct list_head node;
	struct device *dev;
	struct pci_bus *bus;
	void __iomem *dbi;
	struct regmap *scfg;
	struct pcie_port pp;
	int index;
	int msi_irq;
};

#define to_ls_pcie(x)	container_of(x, struct ls_pcie, pp)

static int ls_pcie_link_up(struct pcie_port *pp)
{
	u32 state;
	struct ls_pcie *pcie = to_ls_pcie(pp);

	regmap_read(pcie->scfg, SCFG_PEXMSCPORTSR(pcie->index), &state);
	state = (state >> LTSSM_STATE_SHIFT) & LTSSM_STATE_MASK;

	if (state < LTSSM_PCIE_L0)
		return 0;

	return 1;
}

static u32 ls_pcie_get_msi_addr(struct pcie_port *pp)
{
	return MSI_LS1021A_ADDR;
}

static u32 ls_pcie_get_msi_data(struct pcie_port *pp, int pos)
{
	struct ls_pcie *pcie = to_ls_pcie(pp);

	return MSI_LS1021A_DATA(pcie->index);
}

static irqreturn_t ls_pcie_msi_irq_handler(int irq, void *data)
{
	struct pcie_port *pp = data;
	struct ls_pcie *pcie = to_ls_pcie(pp);
	unsigned int msi_irq;

	/* clear the interrupt */
	regmap_write(pcie->scfg, SCFG_SPIMSICLRCR,
		     MSI_LS1021A_DATA(pcie->index));

	msi_irq = irq_find_mapping(pp->irq_domain, 0);
	if (!msi_irq) {
		/*
		 * that's weird who triggered this?
		 * just clear it
		 */
		dev_err(pcie->dev, "unexpected MSI\n");
		return IRQ_NONE;
	}

	generic_handle_irq(msi_irq);
	return IRQ_HANDLED;
}

static void ls_pcie_msi_clear_irq(struct pcie_port *pp, int irq)
{
}

static void ls_pcie_msi_set_irq(struct pcie_port *pp, int irq)
{
}

static void ls1021a_pcie_msi_fixup(struct pcie_port *pp)
{
	int i;

	/*
	 * LS1021A has only one MSI interrupt
	 * Set all msi interrupts as used except the first one
	 */
	for (i = 1; i < MAX_MSI_IRQS; i++)
		set_bit(i, pp->msi_irq_in_use);
}

static void ls_pcie_host_init(struct pcie_port *pp)
{
	struct ls_pcie *pcie = to_ls_pcie(pp);
	int count = 0;
	u32 val;

	dw_pcie_setup_rc(pp);

	while (!ls_pcie_link_up(pp)) {
		usleep_range(100, 1000);
		count++;
		if (count >= 200) {
			dev_err(pp->dev, "phy link never came up\n");
			return;
		}
	}

	if (of_device_is_compatible(pcie->dev->of_node, "fsl,ls1021a-pcie")) {
		/*
		 * LS1021A Workaround for internal TKT228622
		 * to fix the INTx hang issue
		 */
		val = ioread32(pcie->dbi + PCIE_STRFMR1);
		val &= 0xffff;
		iowrite32(val, pcie->dbi + PCIE_STRFMR1);

		ls1021a_pcie_msi_fixup(pp);
	}
}

static struct pcie_host_ops ls_pcie_host_ops = {
	.link_up = ls_pcie_link_up,
	.host_init = ls_pcie_host_init,
	.msi_set_irq = ls_pcie_msi_set_irq,
	.msi_clear_irq = ls_pcie_msi_clear_irq,
	.get_msi_addr = ls_pcie_get_msi_addr,
	.get_msi_data = ls_pcie_get_msi_data,
};

static int ls_add_pcie_port(struct ls_pcie *pcie)
{
	struct pcie_port *pp;
	int ret;

	if (!pcie)
		return -EINVAL;

	pp = &pcie->pp;
	pp->dev = pcie->dev;
	pp->dbi_base = pcie->dbi;
	pp->msi_irq = pcie->msi_irq;

	if (IS_ENABLED(CONFIG_PCI_MSI)) {
		ret = devm_request_irq(pp->dev, pp->msi_irq,
					ls_pcie_msi_irq_handler,
					IRQF_SHARED, "ls-pcie-msi", pp);
		if (ret) {
			dev_err(pp->dev, "failed to request msi irq\n");
			return ret;
		}
	}

	pp->root_bus_nr = -1;
	pp->ops = &ls_pcie_host_ops;

	ret = dw_pcie_host_init(pp);
	if (ret) {
		dev_err(pp->dev, "failed to initialize host\n");
		return ret;
	}

	return 0;
}

static int __init ls_pcie_probe(struct platform_device *pdev)
{
	struct ls_pcie *pcie;
	struct resource *dbi_base;
	u32 index[2];
	int ret;

	pcie = devm_kzalloc(&pdev->dev, sizeof(*pcie), GFP_KERNEL);
	if (!pcie)
		return -ENOMEM;

	pcie->dev = &pdev->dev;

	dbi_base = platform_get_resource_byname(pdev, IORESOURCE_MEM, "regs");
	if (!dbi_base) {
		dev_err(&pdev->dev, "missing *regs* space\n");
		return -ENODEV;
	}

	pcie->dbi = devm_ioremap_resource(&pdev->dev, dbi_base);
	if (IS_ERR(pcie->dbi))
		return PTR_ERR(pcie->dbi);

	pcie->scfg = syscon_regmap_lookup_by_phandle(pdev->dev.of_node,
						     "fsl,pcie-scfg");
	if (IS_ERR(pcie->scfg)) {
		dev_err(&pdev->dev, "No syscfg phandle specified\n");
		return PTR_ERR(pcie->scfg);
	}

	ret = of_property_read_u32_array(pdev->dev.of_node,
					 "fsl,pcie-scfg", index, 2);
	if (ret)
		return ret;
	pcie->index = index[1];

	pcie->msi_irq = platform_get_irq_byname(pdev, "msi");
	if (pcie->msi_irq < 0) {
		dev_err(&pdev->dev,
			"failed to get MSI IRQ: %d\n", pcie->msi_irq);
		return pcie->msi_irq;
	}

	ret = ls_add_pcie_port(pcie);
	if (ret < 0)
		return ret;

	platform_set_drvdata(pdev, pcie);

	return 0;
}

static const struct of_device_id ls_pcie_of_match[] = {
	{ .compatible = "fsl,ls1021a-pcie" },
	{ },
};
MODULE_DEVICE_TABLE(of, ls_pcie_of_match);

static struct platform_driver ls_pcie_driver = {
	.driver = {
		.name = "layerscape-pcie",
		.owner = THIS_MODULE,
		.of_match_table = ls_pcie_of_match,
	},
};

module_platform_driver_probe(ls_pcie_driver, ls_pcie_probe);

MODULE_AUTHOR("Minghuan Lian <Minghuan.Lian@freescale.com>");
MODULE_DESCRIPTION("Freescale Layerscape PCIe host controller driver");
MODULE_LICENSE("GPL v2");
