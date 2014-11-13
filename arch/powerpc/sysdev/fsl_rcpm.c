/*
 * RCPM(Run Control/Power Management) support
 *
 * Copyright 2012-2014 Freescale Semiconductor Inc.
 *
 * Author: Chenhui Zhao <chenhui.zhao@freescale.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/of_address.h>
#include <linux/export.h>

#include <asm/io.h>
#include <asm/fsl_guts.h>
#include <asm/cputhreads.h>
#include <asm/fsl_pm.h>
#include <asm/machdep.h>
#include <asm/smp.h>

#include <platforms/85xx/smp.h>

const struct fsl_pm_ops *qoriq_pm_ops;

static struct ccsr_rcpm_v1 __iomem *rcpm_v1_regs;
static struct ccsr_rcpm_v2 __iomem *rcpm_v2_regs;
static unsigned long rcpm_version;

static void rcpm_v1_irq_mask(int cpu)
{
	int hw_cpu = get_hard_smp_processor_id(cpu);
	unsigned int mask = 1 << hw_cpu;

	setbits32(&rcpm_v1_regs->cpmimr, mask);
	setbits32(&rcpm_v1_regs->cpmcimr, mask);
	setbits32(&rcpm_v1_regs->cpmmcmr, mask);
	setbits32(&rcpm_v1_regs->cpmnmimr, mask);
}

static void rcpm_v1_irq_unmask(int cpu)
{
	int hw_cpu = get_hard_smp_processor_id(cpu);
	unsigned int mask = 1 << hw_cpu;

	clrbits32(&rcpm_v1_regs->cpmimr, mask);
	clrbits32(&rcpm_v1_regs->cpmcimr, mask);
	clrbits32(&rcpm_v1_regs->cpmmcmr, mask);
	clrbits32(&rcpm_v1_regs->cpmnmimr, mask);
}

static void rcpm_v1_set_ip_power(int enable, u32 mask)
{
	if (enable)
		setbits32(&rcpm_v1_regs->ippdexpcr, mask);
	else
		clrbits32(&rcpm_v1_regs->ippdexpcr, mask);
}

static void rcpm_v1_cpu_enter_state(int cpu, int state)
{
	unsigned int hw_cpu = get_hard_smp_processor_id(cpu);
	unsigned int mask = 1 << hw_cpu;

	switch (state) {
	case E500_PM_PH10:
		setbits32(&rcpm_v1_regs->cdozcr, mask);
		break;
	case E500_PM_PH15:
		setbits32(&rcpm_v1_regs->cnapcr, mask);
		break;
	default:
		pr_err("%s: Unknown cpu PM state (%d)\n", __func__, state);
		break;
	}
}

static void rcpm_v1_cpu_exit_state(int cpu, int state)
{
	unsigned int hw_cpu = get_hard_smp_processor_id(cpu);
	unsigned int mask = 1 << hw_cpu;

	switch (state) {
	case E500_PM_PH10:
		clrbits32(&rcpm_v1_regs->cdozcr, mask);
		break;
	case E500_PM_PH15:
		clrbits32(&rcpm_v1_regs->cnapcr, mask);
		break;
	default:
		pr_err("%s: Unknown cpu PM state (%d)\n", __func__, state);
		break;
	}
}

static int rcpm_v1_plat_enter_state(int state)
{
	u32 *pmcsr_reg = &rcpm_v1_regs->powmgtcsr;
	int ret = 0;
	int result;

	switch (state) {
	case PLAT_PM_SLEEP:
		setbits32(pmcsr_reg, RCPM_POWMGTCSR_SLP);

		/* At this point, the device is in sleep mode. */

		/* Upon resume, wait for RCPM_POWMGTCSR_SLP bit to be clear. */
		result = spin_event_timeout(
		  !(in_be32(pmcsr_reg) & RCPM_POWMGTCSR_SLP), 10000, 10);
		if (!result) {
			pr_err("%s: timeout waiting for SLP bit to be cleared\n",
			       __func__);
			ret = -ETIMEDOUT;
		}
		break;
	default:
		pr_err("%s: Unknown platform PM state (%d)\n",
		       __func__, state);
		ret = -EINVAL;
	}

	return ret;
}

static void rcpm_common_freeze_time_base(u32 *tben_reg, int freeze)
{
	static u32 mask;

	if (freeze) {
		mask = in_be32(tben_reg);
		clrbits32(tben_reg, mask);
	} else {
		setbits32(tben_reg, mask);
	}

	/* read back to push the previous write */
	in_be32(tben_reg);
}

static void rcpm_v1_freeze_time_base(int freeze)
{
	rcpm_common_freeze_time_base(&rcpm_v1_regs->ctbenr, freeze);
}

static void rcpm_v2_freeze_time_base(int freeze)
{
	rcpm_common_freeze_time_base(&rcpm_v2_regs->pctbenr, freeze);
}

static void rcpm_v2_irq_mask(int cpu)
{
	int hw_cpu = get_hard_smp_processor_id(cpu);
	unsigned int mask = 1 << hw_cpu;

	if (strcmp(cur_cpu_spec->cpu_name, "e6500"))
		setbits32(&rcpm_v2_regs->tpmimr0, mask);

	setbits32(&rcpm_v2_regs->tpmcimr0, mask);
	setbits32(&rcpm_v2_regs->tpmmcmr0, mask);
	setbits32(&rcpm_v2_regs->tpmnmimr0, mask);
}

static void rcpm_v2_irq_unmask(int cpu)
{
	int hw_cpu = get_hard_smp_processor_id(cpu);
	unsigned int mask = 1 << hw_cpu;

	if (strcmp(cur_cpu_spec->cpu_name, "e6500"))
		clrbits32(&rcpm_v2_regs->tpmimr0, mask);

	clrbits32(&rcpm_v2_regs->tpmcimr0, mask);
	clrbits32(&rcpm_v2_regs->tpmmcmr0, mask);
	clrbits32(&rcpm_v2_regs->tpmnmimr0, mask);
}

static void rcpm_v2_set_ip_power(int enable, u32 mask)
{
	if (enable)
		/* enable power of IP blocks in deep sleep mode */
		setbits32(&rcpm_v2_regs->ippdexpcr[0], mask);
	else
		clrbits32(&rcpm_v2_regs->ippdexpcr[0], mask);
}

static void rcpm_v2_cpu_enter_state(int cpu, int state)
{
	unsigned int hw_cpu = get_hard_smp_processor_id(cpu);
	u32 mask = 1 << cpu_core_index_of_thread(hw_cpu);

	switch (state) {
	case E500_PM_PH10:
		/* one bit corresponds to one thread for PH10 of 6500 */
		setbits32(&rcpm_v2_regs->tph10setr0, 1 << hw_cpu);
		break;
#ifdef CONFIG_PPC_BOOK3E_64
	case E500_PM_PW10:
		/* one bit corresponds to one thread for PW10 of 6500 */
		book3e_die();
		break;
#endif
	case E500_PM_PH15:
		setbits32(&rcpm_v2_regs->pcph15setr, mask);
		break;
	case E500_PM_PH20:
		setbits32(&rcpm_v2_regs->pcph20setr, mask);
		break;
	case E500_PM_PH30:
		setbits32(&rcpm_v2_regs->pcph30setr, mask);
		break;
	default:
		pr_err("%s: Unknown cpu PM state (%d)\n", __func__, state);
	}
}

static void rcpm_v2_cpu_exit_state(int cpu, int state)
{
	unsigned int hw_cpu = get_hard_smp_processor_id(cpu);
	u32 mask = 1 << cpu_core_index_of_thread(hw_cpu);

	/*
	 * The true value read from these registers only means
	 * there is a pending request.
	 */
	switch (state) {
	case E500_PM_PH10:
		out_be32(&rcpm_v2_regs->tph10clrr0, 1 << hw_cpu);
		break;
	case E500_PM_PH15:
		out_be32(&rcpm_v2_regs->pcph15clrr, mask);
		break;
	case E500_PM_PH20:
		out_be32(&rcpm_v2_regs->pcph20clrr, mask);
		break;
	case E500_PM_PH30:
		out_be32(&rcpm_v2_regs->pcph30clrr, mask);
		break;
	default:
		pr_err("%s: Unknown cpu PM state (%d)\n", __func__, state);
	}
}

#ifdef CONFIG_HOTPLUG_CPU
static void rcpm_v2_cluster_enter_state(int cpu, int state)
{
	int hw_cpu;
	u32 cluster_mask;

	hw_cpu = get_hard_smp_processor_id(cpu);
	cluster_mask = 1 << (hw_cpu / THREAD_IN_CLUSTER);

	switch (state) {
	case E500_PM_PCL10:
		/* one bit corresponds to one cluster */
		out_be32(&rcpm_v2_regs->clpcl10setr, cluster_mask);

		break;
	default:
		pr_err("%s: Unknown cluster PM state (%d)\n", __func__, state);
	}
}

static void rcpm_v2_cluster_exit_state(int cpu, int state)
{
	int hw_cpu;
	u32 cluster_mask;

	hw_cpu = get_hard_smp_processor_id(cpu);
	cluster_mask = 1 << (hw_cpu / THREAD_IN_CLUSTER);

	switch (state) {
	case E500_PM_PCL10:
		/* one bit corresponds to one cluster */
		out_be32(&rcpm_v2_regs->clpcl10clrr, cluster_mask);
		break;
	default:
		pr_err("%s: Unknown cluster PM state (%d)\n", __func__, state);
	}
}
#endif


static int rcpm_v2_plat_enter_state(int state)
{
	u32 *pmcsr_reg = &rcpm_v2_regs->powmgtcsr;
	int ret = 0;
	int result;

	switch (state) {
	case PLAT_PM_LPM20:
		/* clear previous LPM20 status */
		setbits32(pmcsr_reg, RCPM_POWMGTCSR_P_LPM20_ST);
		/* enter LPM20 status */
		setbits32(pmcsr_reg, RCPM_POWMGTCSR_LPM20_RQ);

		/* At this point, the device is in LPM20 status. */

		/* resume ... */
		result = spin_event_timeout(
		  !(in_be32(pmcsr_reg) & RCPM_POWMGTCSR_LPM20_ST), 10000, 10);
		if (!result) {
			pr_err("%s: timeout waiting for LPM20 bit to be cleared\n",
			       __func__);
			ret = -ETIMEDOUT;
		}
		break;
	default:
		pr_err("%s: Unknown platform PM state (%d)\n",
		       __func__, state);
		ret = -EINVAL;
	}

	return ret;
}

bool rcpm_v2_cpu_ready(unsigned int cpu, int state)
{
	unsigned int hw_cpu;
	u32 mask;
	bool ret = false;

	hw_cpu = get_hard_smp_processor_id(cpu);

	switch (state) {
	case E500_PM_PH10:
		if (in_be32(&rcpm_v2_regs->tph10sr0) & (1 << hw_cpu))
			ret = true;
		break;
	case E500_PM_PW10:
		if (in_be32(&rcpm_v2_regs->twaitsr0) & (1 << hw_cpu))
			ret = true;
		break;
	case E500_PM_PH15:
		mask = 1 << cpu_core_index_of_thread(hw_cpu);

		if (in_be32(&rcpm_v2_regs->pcph15sr) & mask)
			ret = true;
		break;
	case E500_PM_PH20:
		mask = 1 << cpu_core_index_of_thread(hw_cpu);

		if (in_be32(&rcpm_v2_regs->pcph20sr) & mask)
			ret = true;
		break;
	case E500_PM_PW20:
		mask = 1 << cpu_core_index_of_thread(hw_cpu);

		if (in_be32(&rcpm_v2_regs->pcpw20sr) & mask)
			ret = true;
		break;
	case E500_PM_PH30:
		mask = 1 << cpu_core_index_of_thread(hw_cpu);

		if (in_be32(&rcpm_v2_regs->pcph30sr) & mask)
			ret = true;
		break;
#ifdef CONFIG_HOTPLUG_CPU
	case E500_PM_PCL10:
		/* PCL10 state is only supported on e6500 for now. */
		mask = 1 << (hw_cpu / THREAD_IN_CLUSTER);

		if (in_be32(&rcpm_v2_regs->clpcl10sr) & mask)
			ret = true;
		break;
#endif
	default:
		pr_err("%s: Unknown platform PM state (%d)\n",
				__func__, state);
		ret = false;

	}

	return ret;
}

static const struct fsl_pm_ops qoriq_rcpm_v1_ops = {
	.irq_mask = rcpm_v1_irq_mask,
	.irq_unmask = rcpm_v1_irq_unmask,
	.cpu_enter_state = rcpm_v1_cpu_enter_state,
	.cpu_exit_state = rcpm_v1_cpu_exit_state,
	.plat_enter_state = rcpm_v1_plat_enter_state,
	.set_ip_power = rcpm_v1_set_ip_power,
	.freeze_time_base = rcpm_v1_freeze_time_base,
};

static const struct fsl_pm_ops qoriq_rcpm_v2_ops = {
	.irq_mask = rcpm_v2_irq_mask,
	.irq_unmask = rcpm_v2_irq_unmask,
	.cpu_enter_state = rcpm_v2_cpu_enter_state,
	.cpu_exit_state = rcpm_v2_cpu_exit_state,
#ifdef CONFIG_HOTPLUG_CPU
	.cluster_enter_state = rcpm_v2_cluster_enter_state,
	.cluster_exit_state = rcpm_v2_cluster_exit_state,
#endif
	.plat_enter_state = rcpm_v2_plat_enter_state,
	.set_ip_power = rcpm_v2_set_ip_power,
	.freeze_time_base = rcpm_v2_freeze_time_base,
	.cpu_ready = rcpm_v2_cpu_ready,
};

static const struct of_device_id rcpm_matches[] = {
	{
		.compatible = "fsl,qoriq-rcpm-1.0",
		.data = (void *)RCPM_V1,
	},
	{
		.compatible = "fsl,qoriq-rcpm-2.0",
		.data = (void *)RCPM_V2,
	},
	{},
};

unsigned long get_rcpm_version(void)
{
	return rcpm_version;
}

int fsl_rcpm_init(void)
{
	struct device_node *np;
	const struct of_device_id *match;
	void __iomem *base;

	np = of_find_matching_node_and_match(NULL, rcpm_matches, &match);
	if (!np) {
		pr_err("%s: can't find the rcpm node.\n", __func__);
		return -EINVAL;
	}

	base = of_iomap(np, 0);
	of_node_put(np);
	if (!base)
		return -ENOMEM;

	rcpm_version = (unsigned long)match->data;

	switch (rcpm_version) {
	case RCPM_V1:
		rcpm_v1_regs = base;
		qoriq_pm_ops = &qoriq_rcpm_v1_ops;
		break;

	case RCPM_V2:
		rcpm_v2_regs = base;
		qoriq_pm_ops = &qoriq_rcpm_v2_ops;
		break;

	default:
		break;
	}

	return 0;
}

/* need to call this before SMP init */
early_initcall(fsl_rcpm_init);
