/*
 * Copyright 2013 Freescale Semiconductor, Inc.
 *
 * CPU Frequency Scaling driver for Freescale QorIQ SoCs.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define pr_fmt(fmt)	KBUILD_MODNAME ": " fmt

#include <linux/clk.h>
#include <linux/cpufreq.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <linux/cpu.h>

/**
 * struct cpu_data - per CPU data struct
 * @clk: the clk of CPU
 * @pclk: the parent clock of cpu
 * @table: frequency table
 */
struct cpu_data {
	struct clk *clk;
	struct clk **pclk;
	struct cpufreq_frequency_table *table;
};

/**
 * struct soc_data - SoC specific data
 * @freq_mask: mask the disallowed frequencies
 * @flag: unique flags
 */
struct soc_data {
	u32 freq_mask[4];
	u32 flag;
};

#define FREQ_MASK	1
/* see hardware specification for the allowed frqeuencies */
static const struct soc_data sdata[] = {
	{ /* used by p2041 and p3041 */
		.freq_mask = {0x8, 0x8, 0x2, 0x2},
		.flag = FREQ_MASK,
	},
	{ /* used by p5020 */
		.freq_mask = {0x8, 0x2},
		.flag = FREQ_MASK,
	},
	{ /* used by p4080, p5040 */
		.freq_mask = {0},
		.flag = 0,
	},
};

/*
 * the minimum allowed core frequency, in Hz
 * for chassis v1.0, >= platform frequency
 * for chassis v2.0, >= platform frequency / 2
 */
static u32 min_cpufreq;
static const u32 *fmask;

/* serialize frequency changes  */
static DEFINE_MUTEX(cpufreq_lock);
static DEFINE_PER_CPU(struct cpu_data *, cpu_data);

static unsigned int qoriq_cpufreq_get_speed(unsigned int cpu)
{
	struct cpu_data *data = per_cpu(cpu_data, cpu);

	return clk_get_rate(data->clk) / 1000;
}

/* reduce the duplicated frequencies in frequency table */
static void freq_table_redup(struct cpufreq_frequency_table *freq_table,
		int count)
{
	int i, j;

	for (i = 1; i < count; i++) {
		for (j = 0; j < i; j++) {
			if (freq_table[j].frequency == CPUFREQ_ENTRY_INVALID ||
					freq_table[j].frequency !=
					freq_table[i].frequency)
				continue;

			freq_table[i].frequency = CPUFREQ_ENTRY_INVALID;
			break;
		}
	}
}

/* sort the frequencies in frequency table in descenting order */
static void freq_table_sort(struct cpufreq_frequency_table *freq_table,
		int count)
{
	int i, j, ind;
	unsigned int freq, max_freq;
	struct cpufreq_frequency_table table;
	for (i = 0; i < count - 1; i++) {
		max_freq = freq_table[i].frequency;
		ind = i;
		for (j = i + 1; j < count; j++) {
			freq = freq_table[j].frequency;
			if (freq == CPUFREQ_ENTRY_INVALID ||
					freq <= max_freq)
				continue;
			ind = j;
			max_freq = freq;
		}

		if (ind != i) {
			/* exchange the frequencies */
			table.driver_data = freq_table[i].driver_data;
			table.frequency = freq_table[i].frequency;
			freq_table[i].driver_data = freq_table[ind].driver_data;
			freq_table[i].frequency = freq_table[ind].frequency;
			freq_table[ind].driver_data = table.driver_data;
			freq_table[ind].frequency = table.frequency;
		}
	}
}

#if defined(CONFIG_ARM)
static int get_cpu_physical_id(int cpu)
{
	return topology_core_id(cpu);
}
#else
static int get_cpu_physical_id(int cpu)
{
	return get_hard_smp_processor_id(cpu);
}
#endif

static u32 get_bus_freq(void)
{
	struct device_node *soc;
	u32 sysfreq;

	soc = of_find_node_by_type(NULL, "soc");
	if (!soc)
		return 0;

	if (of_property_read_u32(soc, "bus-frequency", &sysfreq))
		sysfreq = 0;

	of_node_put(soc);

	return sysfreq;
}

static struct device_node *cpu_to_clk_node(int cpu)
{
	struct device_node *np, *clk_np;

	if (!cpu_present(cpu))
		return NULL;

	np = of_get_cpu_node(cpu, NULL);
	if (!np)
		return NULL;

	clk_np = of_parse_phandle(np, "clocks", 0);
	if (!clk_np)
		return NULL;

	of_node_put(np);

	return clk_np;
}

/* traverse cpu nodes to get cpu mask of sharing clock wire */
static void set_affected_cpus(struct cpufreq_policy *policy)
{
	struct device_node *np, *clk_np;
	struct cpumask *dstp = policy->cpus;
	int i;

	np = cpu_to_clk_node(policy->cpu);
	if (!np)
		return;

	for_each_present_cpu(i) {
		clk_np = cpu_to_clk_node(i);
		if (!clk_np)
			continue;

		if (clk_np == np)
			cpumask_set_cpu(i, dstp);

		of_node_put(clk_np);
	}
	of_node_put(np);
}

static int qoriq_cpufreq_cpu_init(struct cpufreq_policy *policy)
{
	struct device_node *np, *pnode;
	int i, count, ret;
	u32 freq, mask;
	struct clk *clk;
	struct cpufreq_frequency_table *table;
	struct cpu_data *data;
	unsigned int cpu = policy->cpu;
	u64 u64temp;

	np = of_get_cpu_node(cpu, NULL);
	if (!np)
		return -ENODEV;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data) {
		pr_err("%s: no memory\n", __func__);
		goto err_np;
	}

	data->clk = of_clk_get(np, 0);
	if (IS_ERR(data->clk)) {
		pr_err("%s: no clock information\n", __func__);
		goto err_nomem2;
	}

	pnode = of_parse_phandle(np, "clocks", 0);
	if (!pnode) {
		pr_err("%s: could not get clock information\n", __func__);
		goto err_nomem2;
	}

	count = of_property_count_strings(pnode, "clock-names");
	data->pclk = kcalloc(count, sizeof(struct clk *), GFP_KERNEL);
	if (!data->pclk) {
		pr_err("%s: no memory\n", __func__);
		goto err_node;
	}

	table = kcalloc(count + 1, sizeof(*table), GFP_KERNEL);
	if (!table) {
		pr_err("%s: no memory\n", __func__);
		goto err_pclk;
	}

	if (fmask)
		mask = fmask[get_cpu_physical_id(cpu)];
	else
		mask = 0x0;

	for (i = 0; i < count; i++) {
		clk = of_clk_get(pnode, i);
		data->pclk[i] = clk;
		freq = clk_get_rate(clk);
		/*
		 * the clock is valid if its frequency is not masked
		 * and large than minimum allowed frequency.
		 */
		if (freq < min_cpufreq || (mask & (1 << i)))
			table[i].frequency = CPUFREQ_ENTRY_INVALID;
		else
			table[i].frequency = freq / 1000;
		table[i].driver_data = i;
	}
	freq_table_redup(table, count);
	freq_table_sort(table, count);
	table[i].frequency = CPUFREQ_TABLE_END;

	/* set the min and max frequency properly */
	ret = cpufreq_frequency_table_cpuinfo(policy, table);
	if (ret) {
		pr_err("invalid frequency table: %d\n", ret);
		goto err_nomem1;
	}

	data->table = table;
	per_cpu(cpu_data, cpu) = data;

	/* update ->cpus if we have cluster, no harm if not */
	set_affected_cpus(policy);
	for_each_cpu(i, policy->cpus)
		per_cpu(cpu_data, i) = data;

	/* Minimum transition latency is 12 platform clocks */
	u64temp = 12ULL * NSEC_PER_SEC;
	do_div(u64temp, get_bus_freq());
	policy->cpuinfo.transition_latency = u64temp + 1;
	policy->cur = qoriq_cpufreq_get_speed(policy->cpu);

	cpufreq_frequency_table_get_attr(table, cpu);
	of_node_put(np);
	of_node_put(pnode);

	return 0;

err_nomem1:
	kfree(table);
err_pclk:
	kfree(data->pclk);
err_node:
	of_node_put(pnode);
err_nomem2:
	per_cpu(cpu_data, cpu) = NULL;
	kfree(data);
err_np:
	of_node_put(np);

	return -ENODEV;
}

static int __exit qoriq_cpufreq_cpu_exit(struct cpufreq_policy *policy)
{
	struct cpu_data *data = per_cpu(cpu_data, policy->cpu);

	cpufreq_frequency_table_put_attr(policy->cpu);
	kfree(data->pclk);
	kfree(data->table);
	kfree(data);

	return 0;
}

static int qoriq_cpufreq_verify(struct cpufreq_policy *policy)
{
	struct cpufreq_frequency_table *table =
		per_cpu(cpu_data, policy->cpu)->table;

	return cpufreq_frequency_table_verify(policy, table);
}

#if (defined(CONFIG_PPC) && defined(CONFIG_HOTPLUG_CPU))
/*
 * t4240 specific data struct used by a workaround for errata:
 * A-008083: Dynamic frequency switch (DFS) can hang SoC
 * when changing frequency of a cluster with active cores
 * or snoop transactions.
 *
 * Basically, the workaround is putting a cluster to PCL10 status
 * before changing its frequency.
 */
struct t4240_dfs {
	struct clk *parent;
	struct clk *child;
	unsigned int cpu;
	spinlock_t lock;
} t4dfs;

static int t4240_workaround;

static void t4240_work_fn(struct work_struct *unused)
{
	struct cpumask mask;
	int i;
	struct t4240_dfs dfs;

	spin_lock(&t4dfs.lock);
	dfs.cpu = t4dfs.cpu;
	dfs.parent = t4dfs.parent;
	dfs.child = t4dfs.child;
	spin_unlock(&t4dfs.lock);

	/* save the cpu mask */
	cpumask_copy(&mask, cpu_core_mask(dfs.cpu));

	/*
	 * shut down all the CPUs in this cluster.
	 * this cluster will enter PCL10 automatically.
	 */
	for_each_cpu(i, &mask)
		cpu_down(i);

	/* switch CPU frequency safely */
	clk_set_parent(dfs.child, dfs.parent);

	/* bring CPUs back */
	for_each_cpu(i, &mask)
		cpu_up(i);
}

/*
 * CPUFreq framework requires CPU must be online when
 * changing its frequency, while this workaround requires
 * CPU must be offline. So, use a workqueue here to fulfill
 * both requirements.
 */
static DECLARE_WORK(t4240_dfs_work, t4240_work_fn);

#endif
static int qoriq_cpufreq_target(struct cpufreq_policy *policy,
		unsigned int target_freq, unsigned int relation)
{
	struct cpufreq_freqs freqs;
	unsigned int new;
	struct clk *parent;
	int ret;
	struct cpu_data *data = per_cpu(cpu_data, policy->cpu);
	int workaround = 0;
#if (defined(CONFIG_PPC) && defined(CONFIG_HOTPLUG_CPU))

	/*
	 * workaround should be applied on 2 conditions:
	 * 1. on t4240 platform
	 * 2. the cluster CPU belongs to is not same as
	 *	 the cluster boot CPU belongs to.
	 */
	if (t4240_workaround &&
			(cpumask_equal(cpu_core_mask(boot_cpuid),
				cpu_core_mask(policy->cpu)) == 0))
		workaround = 1;
#endif

	cpufreq_frequency_table_target(policy, data->table,
			target_freq, relation, &new);

	if (policy->cur == data->table[new].frequency)
		return 0;

	freqs.old = policy->cur;
	freqs.new = data->table[new].frequency;

	mutex_lock(&cpufreq_lock);
	cpufreq_notify_transition(policy, &freqs, CPUFREQ_PRECHANGE);

	parent = data->pclk[data->table[new].driver_data];

#if (defined(CONFIG_PPC) && defined(CONFIG_HOTPLUG_CPU))
	if (workaround == 1) {
		freqs.new = freqs.old;
		ret = -1;
	}
#endif

	if (workaround == 0) {
		ret = clk_set_parent(data->clk, parent);
		if (ret)
			ret = -1;
	}

	cpufreq_notify_transition(policy, &freqs, CPUFREQ_POSTCHANGE);
	mutex_unlock(&cpufreq_lock);

#if (defined(CONFIG_PPC) && defined(CONFIG_HOTPLUG_CPU))
	if (workaround == 1) {
		spin_lock(&t4dfs.lock);
		t4dfs.parent = parent;
		t4dfs.child = data->clk;
		t4dfs.cpu = policy->cpu;
		spin_unlock(&t4dfs.lock);
		schedule_work(&t4240_dfs_work);
	}
#endif
	return ret;
}

static struct freq_attr *qoriq_cpufreq_attr[] = {
	&cpufreq_freq_attr_scaling_available_freqs,
	NULL,
};

static struct cpufreq_driver qoriq_cpufreq_driver = {
	.name		= "qoriq_cpufreq",
	.flags		= CPUFREQ_CONST_LOOPS,
	.init		= qoriq_cpufreq_cpu_init,
	.exit		= __exit_p(qoriq_cpufreq_cpu_exit),
	.verify		= qoriq_cpufreq_verify,
	.target		= qoriq_cpufreq_target,
	.get		= qoriq_cpufreq_get_speed,
	.attr		= qoriq_cpufreq_attr,
};

static const struct of_device_id node_matches[] __initdata = {
	{ .compatible = "fsl,p2041-clockgen", .data = &sdata[0], },
	{ .compatible = "fsl,p3041-clockgen", .data = &sdata[0], },
	{ .compatible = "fsl,p5020-clockgen", .data = &sdata[1], },
	{ .compatible = "fsl,p4080-clockgen", .data = &sdata[2], },
	{ .compatible = "fsl,p5040-clockgen", .data = &sdata[2], },
	{ .compatible = "fsl,qoriq-clockgen-2.0", },
	{ .compatible = "fsl,ls1021a-clockgen", },
	{}
};

static int __init qoriq_cpufreq_init(void)
{
	int ret;
	struct device_node  *np;
	const struct of_device_id *match;
	const struct soc_data *data;

	np = of_find_matching_node(NULL, node_matches);
	if (!np)
		return -ENODEV;

	match = of_match_node(node_matches, np);
	data = match->data;
	if (data) {
		if (data->flag)
			fmask = data->freq_mask;
		min_cpufreq = get_bus_freq();
	} else {
		min_cpufreq = get_bus_freq() / 2;
	}

	of_node_put(np);

#ifdef CONFIG_PPC
	np = of_find_compatible_node(NULL, NULL, "fsl,t4240-clockgen");
	if (np) {
#ifndef CONFIG_HOTPLUG_CPU
		pr_info("HOTPLUG_CPU needs to be defined on T4240 platform\n");
		of_node_put(np);
		return -ENODEV;
#else
		t4240_workaround = 1;
		spin_lock_init(&t4dfs.lock);
		of_node_put(np);
#endif
	}
#endif

	ret = cpufreq_register_driver(&qoriq_cpufreq_driver);
	if (!ret)
		pr_info("Freescale PowerPC qoriq CPU frequency scaling driver\n");

	return ret;
}
module_init(qoriq_cpufreq_init);

static void __exit qoriq_cpufreq_exit(void)
{
	cpufreq_unregister_driver(&qoriq_cpufreq_driver);
}
module_exit(qoriq_cpufreq_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Tang Yuantian <Yuantian.Tang@freescale.com>");
MODULE_DESCRIPTION("cpufreq driver for Freescale e500mc series SoCs");
