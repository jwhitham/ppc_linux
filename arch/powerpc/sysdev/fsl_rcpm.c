/*
 * RCPM(Run Control/Power Management) support
 *
 * Copyright 2012 Freescale Semiconductor Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/suspend.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/of_platform.h>
#include <linux/export.h>

#include <asm/io.h>
#include <asm/cacheflush.h>
#include <asm/fsl_guts.h>

static struct ccsr_rcpm __iomem *rcpm_regs;

static int rcpm_suspend_enter(suspend_state_t state)
{
	int ret = 0;

	switch (state) {
	case PM_SUSPEND_STANDBY:

		flush_dcache_L1();
		flush_backside_L2_cache();

		setbits32(&rcpm_regs->powmgtcsr, RCPM_POWMGTCSR_SLP);
		/* At this point, the device is in sleep mode. */

		/* Upon resume, wait for SLP bit to be clear. */
		ret = spin_event_timeout(
		  (in_be32(&rcpm_regs->powmgtcsr) & RCPM_POWMGTCSR_SLP) == 0,
		  10000, 10);
		if (!ret) {
			pr_err("%s: timeout waiting for SLP bit "
				"to be cleared\n", __func__);
			ret = -EINVAL;
		}
		break;

	default:
		ret = -EINVAL;

	}
	return ret;
}

static int rcpm_suspend_valid(suspend_state_t state)
{
	if (state == PM_SUSPEND_STANDBY)
		return 1;
	else
		return 0;
}

static const struct platform_suspend_ops rcpm_suspend_ops = {
	.valid = rcpm_suspend_valid,
	.enter = rcpm_suspend_enter,
};

static int rcpm_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;

	rcpm_regs = of_iomap(np, 0);
	if (!rcpm_regs)
		return -ENOMEM;

	suspend_set_ops(&rcpm_suspend_ops);

	dev_info(&pdev->dev, "Freescale RCPM driver\n");
	return 0;
}

static const struct of_device_id rcpm_ids[] = {
	{ .compatible = "fsl,qoriq-rcpm-1.0", },
	{ },
};

static struct platform_driver rcpm_driver = {
	.driver = {
		.name = "fsl-rcpm",
		.owner = THIS_MODULE,
		.of_match_table = rcpm_ids,
	},
	.probe = rcpm_probe,
};

static int __init rcpm_init(void)
{
	return platform_driver_register(&rcpm_driver);
}

device_initcall(rcpm_init);
