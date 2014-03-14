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
#define FSL_DEEP_SLEEP		0x2

int (*fsl_enter_deepsleep)(void);

/* specify the sleep state of the present platform */
unsigned int sleep_pm_state;
/* supported sleep modes by the present platform */
static unsigned int sleep_modes;

void qoriq_enable_wakeup_source(struct device *dev, void *data)
{
	u32 value[2];
	u32 pw_mask;

	if (!device_may_wakeup(dev))
		return;

	if (of_property_read_u32_array(dev->of_node, "sleep", value, 2))
		return;

	/* get the second value, it is a mask */
	pw_mask = value[1];
	qoriq_pm_ops->set_ip_power(1, pw_mask);
}

static int qoriq_suspend_enter(suspend_state_t state)
{
	int ret = 0;
	int cpu;

	switch (state) {
	case PM_SUSPEND_STANDBY:

		if (cur_cpu_spec->cpu_flush_caches)
			cur_cpu_spec->cpu_flush_caches();

		ret = qoriq_pm_ops->plat_enter_state(sleep_pm_state);

		break;

	case PM_SUSPEND_MEM:

		cpu = smp_processor_id();
		qoriq_pm_ops->irq_mask(cpu);

		ret = fsl_enter_deepsleep();

		qoriq_pm_ops->irq_unmask(cpu);

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

	if (state == PM_SUSPEND_MEM && (sleep_modes & FSL_DEEP_SLEEP))
		return 1;

	return 0;
}

static int qoriq_suspend_begin(suspend_state_t state)
{
	dpm_for_each_dev(NULL, qoriq_enable_wakeup_source);

	if (state == PM_SUSPEND_MEM)
		return fsl_dp_iomap();

	return 0;
}

static void qoriq_suspend_end(void)
{
	fsl_dp_iounmap();
}

static const struct platform_suspend_ops qoriq_suspend_ops = {
	.valid = qoriq_suspend_valid,
	.enter = qoriq_suspend_enter,
	.begin = qoriq_suspend_begin,
	.end = qoriq_suspend_end,
};

static int __init qoriq_suspend_init(void)
{
	struct device_node *np;

	sleep_modes = FSL_SLEEP;
	sleep_pm_state = PLAT_PM_SLEEP;

	np = of_find_compatible_node(NULL, NULL, "fsl,qoriq-rcpm-2.0");
	if (np)
		sleep_pm_state = PLAT_PM_LPM20;

	np = of_find_compatible_node(NULL, NULL, "fsl,t1040-rcpm");
	if (np) {
		fsl_enter_deepsleep = fsl_enter_epu_deepsleep;
		sleep_modes |= FSL_DEEP_SLEEP;
	}

	suspend_set_ops(&qoriq_suspend_ops);

	return 0;
}
arch_initcall(qoriq_suspend_init);
