/*
 * Support Power Management feature
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
#include <linux/suspend.h>
#include <linux/of_platform.h>

#include <asm/fsl_pm.h>

#define FSL_SLEEP		0x1

/* specify the sleep state of the present platform */
unsigned int sleep_pm_state;
/* supported sleep modes by the present platform */
static unsigned int sleep_modes;

static int qoriq_suspend_enter(suspend_state_t state)
{
	int ret = 0;

	switch (state) {
	case PM_SUSPEND_STANDBY:

		if (cur_cpu_spec->cpu_flush_caches)
			cur_cpu_spec->cpu_flush_caches();

		ret = qoriq_pm_ops->plat_enter_state(sleep_pm_state);

		break;

	default:
		ret = -EINVAL;

	}

	return ret;
}

static int qoriq_suspend_valid(suspend_state_t state)
{
	if (state == PM_SUSPEND_STANDBY && (sleep_modes & FSL_SLEEP))
		return 1;

	return 0;
}

static const struct platform_suspend_ops qoriq_suspend_ops = {
	.valid = qoriq_suspend_valid,
	.enter = qoriq_suspend_enter,
};

static int __init qoriq_suspend_init(void)
{
	struct device_node *np;

	sleep_modes = FSL_SLEEP;
	sleep_pm_state = PLAT_PM_SLEEP;

	np = of_find_compatible_node(NULL, NULL, "fsl,qoriq-rcpm-2.0");
	if (np)
		sleep_pm_state = PLAT_PM_LPM20;

	suspend_set_ops(&qoriq_suspend_ops);

	return 0;
}
arch_initcall(qoriq_suspend_init);
