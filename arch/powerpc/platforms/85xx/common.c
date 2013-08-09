/*
 * Routines common to most mpc85xx-based boards.
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/of_platform.h>

#include <asm/time.h>

#include <sysdev/cpm2_pic.h>

#include "mpc85xx.h"

#define MAX_BIT				64

#define ALTIVEC_COUNT_OFFSET		16
#define ALTIVEC_IDLE_COUNT_MASK		0x003f0000

/*
 * FIXME - We don't know the AltiVec application scenarios.
 */
#define ALTIVEC_IDLE_TIME	1000 /* 1ms */

static struct of_device_id __initdata mpc85xx_common_ids[] = {
	{ .type = "soc", },
	{ .compatible = "soc", },
	{ .compatible = "simple-bus", },
	{ .name = "cpm", },
	{ .name = "localbus", },
	{ .compatible = "gianfar", },
	{ .compatible = "fsl,qe", },
	{ .compatible = "fsl,cpm2", },
	{ .compatible = "fsl,srio", },
	/* So that the DMA channel nodes can be probed individually: */
	{ .compatible = "fsl,eloplus-dma", },
	/* For the PMC driver */
	{ .compatible = "fsl,mpc8548-guts", },
	/* Probably unnecessary? */
	{ .compatible = "gpio-leds", },
	/* For all PCI controllers */
	{ .compatible = "fsl,mpc8540-pci", },
	{ .compatible = "fsl,mpc8548-pcie", },
	{ .compatible = "fsl,p1022-pcie", },
	{ .compatible = "fsl,p1010-pcie", },
	{ .compatible = "fsl,p1023-pcie", },
	{ .compatible = "fsl,p4080-pcie", },
	{ .compatible = "fsl,qoriq-pcie-v2.4", },
	{ .compatible = "fsl,qoriq-pcie-v2.3", },
	{ .compatible = "fsl,qoriq-pcie-v2.2", },
	/* For the FMan driver */
	{ .compatible = "fsl,dpaa", },
	{},
};

int __init mpc85xx_common_publish_devices(void)
{
	return of_platform_bus_probe(NULL, mpc85xx_common_ids, NULL);
}
#ifdef CONFIG_CPM2
static void cpm2_cascade(unsigned int irq, struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);
	int cascade_irq;

	while ((cascade_irq = cpm2_get_irq()) >= 0)
		generic_handle_irq(cascade_irq);

	chip->irq_eoi(&desc->irq_data);
}


void __init mpc85xx_cpm2_pic_init(void)
{
	struct device_node *np;
	int irq;

	/* Setup CPM2 PIC */
	np = of_find_compatible_node(NULL, NULL, "fsl,cpm2-pic");
	if (np == NULL) {
		printk(KERN_ERR "PIC init: can not find fsl,cpm2-pic node\n");
		return;
	}
	irq = irq_of_parse_and_map(np, 0);
	if (irq == NO_IRQ) {
		of_node_put(np);
		printk(KERN_ERR "PIC init: got no IRQ for cpm cascade\n");
		return;
	}

	cpm2_pic_init(np);
	of_node_put(np);
	irq_set_chained_handler(irq, cpm2_cascade);
}
#endif

static unsigned int get_idle_ticks_bit(unsigned int us)
{
	unsigned int cycle;

	/*
	 * The time control by TB turn over bit, so we need
	 * to be divided by 2.
	 */
	cycle = (us / 2) * tb_ticks_per_usec;

	return ilog2(cycle) + 1;
}

static void setup_altivec_idle(void *unused)
{
	u32 altivec_idle, pvr, bit;

	pvr = mfspr(SPRN_PVR);

	/* AltiVec idle feature only exists for E6500 */
	if (PVR_VER(pvr) != PVR_VER_E6500)
		return;

	/* Fix erratum, e6500 rev1 not support altivec idle */
	if (PVR_REV(pvr) < 0x20)
		return;

	/* Enable Altivec Idle */
	altivec_idle = mfspr(SPRN_PWRMGTCR0);
	altivec_idle |= PWRMGTCR0_ALTIVEC_IDLE;

	/* Set Automatic AltiVec Idle Count */
	/* clear count */
	altivec_idle &= ~ALTIVEC_IDLE_COUNT_MASK;

	/* set count */
	bit = get_idle_ticks_bit(ALTIVEC_IDLE_TIME);
	altivec_idle |= ((MAX_BIT - bit) << ALTIVEC_COUNT_OFFSET);

	mtspr(SPRN_PWRMGTCR0, altivec_idle);
}

static int __init setup_idle_hw_governor(void)
{
	on_each_cpu(setup_altivec_idle, NULL, 1);

	return 0;
}
late_initcall(setup_idle_hw_governor);
