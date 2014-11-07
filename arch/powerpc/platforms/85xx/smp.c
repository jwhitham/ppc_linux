/*
 * Author: Andy Fleming <afleming@freescale.com>
 * 	   Kumar Gala <galak@kernel.crashing.org>
 *
 * Copyright 2006-2008, 2011-2012 Freescale Semiconductor Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/kexec.h>
#include <linux/highmem.h>
#include <linux/cpu.h>

#include <asm/machdep.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/mpic.h>
#include <asm/cacheflush.h>
#include <asm/dbell.h>
#include <asm/fsl_guts.h>
#include <asm/cputhreads.h>
#include <asm/fsl_pm.h>
#include <asm/cacheflush.h>

#include <sysdev/fsl_soc.h>
#include <sysdev/mpic.h>
#include "smp.h"

struct epapr_spin_table {
	u32	addr_h;
	u32	addr_l;
	u32	r3_h;
	u32	r3_l;
	u32	reserved;
	u32	pir;
};

static void __iomem *guts_regs;
static u64 timebase;
static int tb_req;
static int tb_valid;
static u32 cur_booting_core;

/* specify the cpu PM state when cpu dies, PH15/NAP is the default */
int qoriq_cpu_die_state = E500_PM_PH15;

#ifdef CONFIG_PPC_E500MC
/* get a physical mask of online cores and booting core */
static inline u32 get_phy_cpu_mask(void)
{
	u32 mask;
	int cpu;

	if (smt_capable()) {
		/* two threads in one core share one time base */
		mask = 1 << cpu_core_index_of_thread(cur_booting_core);
		for_each_online_cpu(cpu)
			mask |= 1 << cpu_core_index_of_thread(
					get_hard_smp_processor_id(cpu));
	} else {
		mask = 1 << cur_booting_core;
		for_each_online_cpu(cpu)
			mask |= 1 << get_hard_smp_processor_id(cpu);
	}

	return mask;
}

static void mpc85xx_timebase_freeze(int freeze)
{
	qoriq_pm_ops->freeze_time_base(freeze);
}
#else
static void __cpuinit mpc85xx_timebase_freeze(int freeze)
{
	struct ccsr_guts __iomem *guts = guts_regs;
	u32 mask;

	mask = CCSR_GUTS_DEVDISR_TB0 | CCSR_GUTS_DEVDISR_TB1;
	if (freeze)
		setbits32(&guts->devdisr, mask);
	else
		clrbits32(&guts->devdisr, mask);

	/* read back to push the previous write */
	in_be32(&guts->devdisr);
}
#endif

static void __cpuinit mpc85xx_give_timebase(void)
{
	unsigned long flags;

	/* only do time base sync when system is running */
	if (system_state == SYSTEM_BOOTING)
		return;
	/*
	 * If the booting thread is not the first thread of the core,
	 * skip time base sync.
	 */
	if (smt_capable() &&
		cur_booting_core != cpu_first_thread_sibling(cur_booting_core))
		return;

	local_irq_save(flags);

	while (!tb_req)
		barrier();
	tb_req = 0;

	mpc85xx_timebase_freeze(1);
#ifdef CONFIG_PPC64
	/*
	 * e5500/e6500 have a workaround for erratum A-006958 in place
	 * that will reread the timebase until TBL is non-zero.
	 * That would be a bad thing when the timebase is frozen.
	 *
	 * Thus, we read it manually, and instead of checking that
	 * TBL is non-zero, we ensure that TB does not change.  We don't
	 * do that for the main mftb implementation, because it requires
	 * a scratch register
	 */
	{
		u64 prev;

		asm volatile("mfspr %0, %1" : "=r" (timebase) :
			     "i" (SPRN_TBRL));

		do {
			prev = timebase;
			asm volatile("mfspr %0, %1" : "=r" (timebase) :
				     "i" (SPRN_TBRL));
		} while (prev != timebase);
	}
#else
	timebase = get_tb();
#endif
	mb();
	tb_valid = 1;

	while (tb_valid)
		barrier();

	mpc85xx_timebase_freeze(0);

	local_irq_restore(flags);
}

static void __cpuinit mpc85xx_take_timebase(void)
{
	unsigned long flags;

	if (system_state == SYSTEM_BOOTING)
		return;

	if (smt_capable() &&
		cur_booting_core != cpu_first_thread_sibling(cur_booting_core))
		return;

	local_irq_save(flags);

	tb_req = 1;
	while (!tb_valid)
		barrier();

	set_tb(timebase >> 32, timebase & 0xffffffff);
	isync();
	tb_valid = 0;

	local_irq_restore(flags);
}

#ifdef CONFIG_HOTPLUG_CPU
#ifdef CONFIG_PPC_E500MC
static inline bool is_core_down(unsigned int thread)
{
	cpumask_t thd_mask;

	if (!smt_capable())
		return true;

	cpumask_shift_left(&thd_mask, &threads_core_mask,
			cpu_core_index_of_thread(thread) * threads_per_core);

	return !cpumask_intersects(&thd_mask, cpu_online_mask);
}

static void __cpuinit smp_85xx_mach_cpu_die(void)
{
	unsigned int cpu = smp_processor_id();

	hard_irq_disable();
	idle_task_exit();

	if (qoriq_pm_ops->irq_mask)
		qoriq_pm_ops->irq_mask(cpu);

	mtspr(SPRN_TCR, 0);
	mtspr(SPRN_TSR, mfspr(SPRN_TSR));

	generic_set_cpu_dead(cpu);

	if (cur_cpu_spec && cur_cpu_spec->cpu_flush_caches)
		cur_cpu_spec->cpu_flush_caches();

	if (qoriq_pm_ops->cpu_enter_state)
		qoriq_pm_ops->cpu_enter_state(cpu, qoriq_cpu_die_state);
}

#else
static void smp_85xx_mach_cpu_die(void)
{
	unsigned int cpu = smp_processor_id();
	u32 tmp;

	local_irq_disable();
	idle_task_exit();
	generic_set_cpu_dead(cpu);
	mb();

	mtspr(SPRN_TCR, 0);

	if (cur_cpu_spec && cur_cpu_spec->cpu_flush_caches)
		cur_cpu_spec->cpu_flush_caches();

	tmp = (mfspr(SPRN_HID0) & ~(HID0_DOZE|HID0_SLEEP)) | HID0_NAP;
	mtspr(SPRN_HID0, tmp);
	isync();

	/* Enter NAP mode. */
	tmp = mfmsr();
	tmp |= MSR_WE;
	mb();
	mtmsr(tmp);
	isync();

	while (1)
		;
}
#endif /* CONFIG_PPC_E500MC */
#endif

#if defined(CONFIG_PPC_E500MC) && defined(CONFIG_HOTPLUG_CPU)
static int cluster_offline(unsigned int cpu)
{
	unsigned long flags;
	int i;
	const struct cpumask *mask;
	static void __iomem *cluster_l2_base;
	int ret = 0;

	mask = cpu_cluster_mask(cpu);
	cluster_l2_base = get_cpu_l2_base(cpu);

	/* Wait until all CPU has entered wait state. */
	for_each_cpu(i, mask) {
		if (!spin_event_timeout(
				qoriq_pm_ops->cpu_ready(i, E500_PM_PW10),
				10000, 100)) {
			pr_err("%s: cpu enter wait state timeout.\n",
					__func__);
			ret = -ETIMEDOUT;
			goto out;
		}
	}

	/* Flush, invalidate L2 cache */
	local_irq_save(flags);
	cluster_flush_invalidate_L2_cache(cluster_l2_base);

	/* Let all cores of the same cluster enter PH20 state */
	for_each_cpu(i, mask) {
		qoriq_pm_ops->cpu_enter_state(i, E500_PM_PH20);
	}

	/* Wait until all cores has entered PH20 state */
	for_each_cpu(i, mask) {
		if (!spin_event_timeout(
				qoriq_pm_ops->cpu_ready(i, E500_PM_PH20),
				10000, 100)) {
			pr_err("%s: core enter PH20 timeout\n", __func__);
			ret = -ETIMEDOUT;
			goto irq_restore_out;
		}
	}

	/* Disable L2 cache */
	cluster_disable_L2_cache(cluster_l2_base);

	/* Cluster enter PCL10 stste */
	qoriq_pm_ops->cluster_enter_state(cpu, E500_PM_PCL10);

	if (!spin_event_timeout(qoriq_pm_ops->cpu_ready(cpu, E500_PM_PCL10),
				10000, 100)) {
		/* If entering PCL10 failed. Enable L2 cache back */
		cluster_invalidate_enable_L2(cluster_l2_base);
		pr_err("%s: cluster enter PCL10 timeout\n", __func__);
		ret = -ETIMEDOUT;
	}

irq_restore_out:
	local_irq_restore(flags);
out:
	iounmap(cluster_l2_base);
	return ret;
}

static int cluster_online(unsigned int cpu)
{
	unsigned long flags;
	int i;
	const struct cpumask *mask;
	static void __iomem *cluster_l2_base;
	int ret = 0;

	mask = cpu_cluster_mask(cpu);
	cluster_l2_base = get_cpu_l2_base(cpu);

	local_irq_save(flags);

	qoriq_pm_ops->cluster_exit_state(cpu, E500_PM_PCL10);

	/* Wait until cluster exit PCL10 state */
	if (!spin_event_timeout(
				!qoriq_pm_ops->cpu_ready(cpu, E500_PM_PCL10),
				10000, 100)) {
		pr_err("%s: cluster exit PCL10 timeout\n", __func__);
		ret = -ETIMEDOUT;
		goto out;
	}

	/* Invalidate and enable L2 cache */
	cluster_invalidate_enable_L2(cluster_l2_base);

	/* Let all cores of a cluster exit PH20 state */
	for_each_cpu(i, mask) {
		qoriq_pm_ops->cpu_exit_state(i, E500_PM_PH20);
	}

	/* Wait until all cores of a cluster exit PH20 state */
	for_each_cpu(i, mask) {
		if (!spin_event_timeout(
				!qoriq_pm_ops->cpu_ready(i, E500_PM_PH20),
				10000, 100)) {
			pr_err("%s: core exit PH20 timeout\n", __func__);
			ret = -ETIMEDOUT;
			break;
		}
	}

out:
	local_irq_restore(flags);
	iounmap(cluster_l2_base);
	return ret;
}

void platform_cpu_die(unsigned int cpu)
{
	int i;
	const struct cpumask *cluster_mask;

	if (PVR_VER(cur_cpu_spec->pvr_value) == PVR_VER_E6500) {
		cluster_mask = cpu_cluster_mask(cpu);
		for_each_cpu(i, cluster_mask) {
			if (cpu_online(i))
				return;
		}

		cluster_offline(cpu);
	}
}
#endif

static struct device_node *cpu_to_l2cache(int cpu)
{
	struct device_node *np;
	struct device_node *cache;

	if (!cpu_present(cpu))
		return NULL;

	np = of_get_cpu_node(cpu, NULL);
	if (np == NULL)
		return NULL;

	cache = of_find_next_cache_node(np);

	of_node_put(np);

	return cache;
}

DEFINE_PER_CPU(cpumask_t, cpu_cluster_map);
EXPORT_PER_CPU_SYMBOL(cpu_cluster_map);

static void init_cpu_cluster_map(void)
{
	struct device_node *l2_cache, *np;
	int cpu, i;

	for_each_cpu(cpu, cpu_online_mask) {
		l2_cache = cpu_to_l2cache(cpu);
		if (!l2_cache)
			continue;

		for_each_cpu(i, cpu_online_mask) {
			np = cpu_to_l2cache(i);
			if (!np)
				continue;
			if (np == l2_cache) {
				cpumask_set_cpu(cpu, cpu_cluster_mask(i));
				cpumask_set_cpu(i, cpu_cluster_mask(cpu));
			}
			of_node_put(np);
		}
		of_node_put(l2_cache);
	}
}

static void smp_85xx_bringup_done(void)
{
	init_cpu_cluster_map();
}

static inline void flush_spin_table(void *spin_table)
{
	flush_dcache_range((ulong)spin_table,
		(ulong)spin_table + sizeof(struct epapr_spin_table));
}

static inline u32 read_spin_table_addr_l(void *spin_table)
{
	flush_dcache_range((ulong)spin_table,
		(ulong)spin_table + sizeof(struct epapr_spin_table));
	return in_be32(&((struct epapr_spin_table *)spin_table)->addr_l);
}

static int smp_85xx_kick_cpu(int nr)
{
	unsigned long flags;
	const u64 *cpu_rel_addr;
	__iomem struct epapr_spin_table *spin_table;
	struct device_node *np;
	int hw_cpu = get_hard_smp_processor_id(nr);
	int ioremappable;
	int ret = 0;

	WARN_ON(nr < 0 || nr >= NR_CPUS);
	WARN_ON(hw_cpu < 0 || hw_cpu >= NR_CPUS);

	pr_debug("smp_85xx_kick_cpu: kick CPU #%d\n", nr);

#ifdef CONFIG_PPC64
	/* If the cpu we're kicking is a thread, kick it and return */
	if (smt_capable() && (cpu_thread_in_core(nr) != 0)) {
		/*
		 * Since Thread 1 can not start Thread 0 in the same core,
		 * Thread 0 of each core must run first before starting
		 * Thread 1.
		 */
		if (cpu_online(cpu_first_thread_sibling(nr))) {

			local_irq_save(flags);
#ifdef CONFIG_PPC_E500MC
			if (system_state == SYSTEM_RUNNING) {
				/*
				 * In cpu hotplug case, Thread 1 of Core0
				 * must start by Thread0 of Core0 calling
				 * fsl_enable_threads().
				 */
				if (nr == 1) {
					if (get_cpu() == boot_cpuid)
						fsl_enable_threads(&ret);
					else
						work_on_cpu(boot_cpuid,
						    fsl_enable_threads, NULL);
				}

				/*
				 * Thread 1 of other cores can be waken up from
				 * low-power state when Thread 0 is online,
				 * or restart by Thread 0 after core reset.
				 */
				if (!strcmp(cur_cpu_spec->cpu_name, "e6500"))
					arch_send_call_function_single_ipi(nr);

				if (qoriq_pm_ops->irq_unmask)
					qoriq_pm_ops->irq_unmask(nr);
			}
#endif
			smp_generic_kick_cpu(nr);

			generic_set_cpu_up(nr);
			cur_booting_core = hw_cpu;

			local_irq_restore(flags);

			return 0;
		} else {
			pr_err("%s: Can not start CPU #%d. Start CPU #%d first.\n",
				__func__, nr, cpu_first_thread_sibling(nr));
			return -ENOENT;
		}
	}

#ifdef CONFIG_HOTPLUG_CPU
	/* Starting Thread 0 will reset core, so put both threads down first */
	if (smt_capable() && system_state == SYSTEM_RUNNING &&
			cpu_thread_in_core(nr) == 0 && !is_core_down(nr)) {
			pr_err("%s: Can not start CPU #%d. Put CPU #%d down first.",
				__func__, nr, cpu_last_thread_sibling(nr));
			return -ENOENT;
	}
#endif
#endif

#if defined(CONFIG_PPC_E500MC) && defined(CONFIG_HOTPLUG_CPU)
	/* If cluster is in PCL10, exit PCL10 first */
	if (system_state == SYSTEM_RUNNING &&
			(PVR_VER(cur_cpu_spec->pvr_value) == PVR_VER_E6500)) {
		if (qoriq_pm_ops->cpu_ready(nr, E500_PM_PCL10))
			cluster_online(nr);
	}
#endif

	np = of_get_cpu_node(nr, NULL);
	cpu_rel_addr = of_get_property(np, "cpu-release-addr", NULL);

	if (cpu_rel_addr == NULL) {
		printk(KERN_ERR "No cpu-release-addr for cpu %d\n", nr);
		return -ENOENT;
	}

	/*
	 * A secondary core could be in a spinloop in the bootpage
	 * (0xfffff000), somewhere in highmem, or somewhere in lowmem.
	 * The bootpage and highmem can be accessed via ioremap(), but
	 * we need to directly access the spinloop if its in lowmem.
	 */
	ioremappable = *cpu_rel_addr > virt_to_phys(high_memory);

	/* Map the spin table */
	if (ioremappable)
		spin_table = ioremap_prot(*cpu_rel_addr,
			sizeof(struct epapr_spin_table), _PAGE_COHERENT);
	else
		spin_table = phys_to_virt(*cpu_rel_addr);

	local_irq_save(flags);

	if (system_state == SYSTEM_RUNNING) {
		/*
		 * To keep it compatible with old boot program which uses
		 * cache-inhibit spin table, we need to flush the cache
		 * before accessing spin table to invalidate any staled data.
		 * We also need to flush the cache after writing to spin
		 * table to push data out.
		 */
		flush_spin_table(spin_table);
		out_be32(&spin_table->addr_l, 0);
		flush_spin_table(spin_table);

#ifdef CONFIG_PPC_E500MC
		/*
		 * For e6500-based platform, wake up core from
		 * low-power state before reset core.
		 */
		if (!strcmp(cur_cpu_spec->cpu_name, "e6500"))
			arch_send_call_function_single_ipi(nr);

		/*
		 * Errata-A-006568. If SOC-rcpm is V1, we need enable
		 * cpu first, T4240rev2 and later Soc has been fixed.
		 * But before this errata is still needed.
		 */
		if (get_rcpm_version() == RCPM_V1 &&
		    qoriq_pm_ops->cpu_exit_state)
			qoriq_pm_ops->cpu_exit_state(nr, qoriq_cpu_die_state);
#endif

		/*
		 * We don't set the BPTR register here since it already points
		 * to the boot page properly.
		 */
		mpic_reset_core(nr);

		/*
		 * wait until core is ready...
		 * We need to invalidate the stale data, in case the boot
		 * loader uses a cache-inhibited spin table.
		 */
		if (!spin_event_timeout(
				read_spin_table_addr_l(spin_table) == 1,
				10000, 100)) {
			pr_err("%s: timeout waiting for core %d to reset\n",
							__func__, hw_cpu);
			ret = -ENOENT;
			goto out;
		}

		/*  clear the acknowledge status */
		__secondary_hold_acknowledge = -1;

#ifdef CONFIG_PPC_E500MC
		if (qoriq_pm_ops->irq_unmask)
			qoriq_pm_ops->irq_unmask(nr);
#endif
	}
	flush_spin_table(spin_table);
	out_be32(&spin_table->pir, hw_cpu);
#ifdef CONFIG_PPC32
	out_be32(&spin_table->addr_l, __pa(__early_start));
#else
	out_be32(&spin_table->addr_h,
		__pa(*(u64 *)generic_secondary_smp_init) >> 32);
	out_be32(&spin_table->addr_l,
		__pa(*(u64 *)generic_secondary_smp_init) & 0xffffffff);
#endif
	flush_spin_table(spin_table);

#ifdef CONFIG_PPC32
	/* Wait a bit for the CPU to ack. */
	if (!spin_event_timeout(__secondary_hold_acknowledge == hw_cpu,
					10000, 100)) {
		pr_err("%s: timeout waiting for core %d to ack\n",
						__func__, hw_cpu);
		ret = -ENOENT;
		goto out;
	}
#else
	smp_generic_kick_cpu(nr);
#endif
	/* Corresponding to generic_set_cpu_dead() */
	generic_set_cpu_up(nr);
	cur_booting_core = hw_cpu;

out:
	local_irq_restore(flags);

	if (ioremappable)
		iounmap(spin_table);

	return ret;
}

struct smp_ops_t smp_85xx_ops = {
	.kick_cpu = smp_85xx_kick_cpu,
	.cpu_bootable = smp_generic_cpu_bootable,
	.bringup_done = smp_85xx_bringup_done,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_disable	= generic_cpu_disable,
	.cpu_die	= generic_cpu_die,
#endif
};

#ifdef CONFIG_KEXEC
atomic_t kexec_down_cpus = ATOMIC_INIT(0);

void mpc85xx_smp_kexec_cpu_down(int crash_shutdown, int secondary)
{
	local_irq_disable();

	if (secondary) {
		atomic_inc(&kexec_down_cpus);
		/* loop forever */
		while (1);
	}
}

static void mpc85xx_smp_kexec_down(void *arg)
{
	if (ppc_md.kexec_cpu_down)
		ppc_md.kexec_cpu_down(0,1);
}

static void map_and_flush(unsigned long paddr)
{
	struct page *page = pfn_to_page(paddr >> PAGE_SHIFT);
	unsigned long kaddr  = (unsigned long)kmap(page);

	flush_dcache_range(kaddr, kaddr + PAGE_SIZE);
	kunmap(page);
}

/**
 * Before we reset the other cores, we need to flush relevant cache
 * out to memory so we don't get anything corrupted, some of these flushes
 * are performed out of an overabundance of caution as interrupts are not
 * disabled yet and we can switch cores
 */
static void mpc85xx_smp_flush_dcache_kexec(struct kimage *image)
{
	kimage_entry_t *ptr, entry;
	unsigned long paddr;
	int i;

	if (image->type == KEXEC_TYPE_DEFAULT) {
		/* normal kexec images are stored in temporary pages */
		for (ptr = &image->head; (entry = *ptr) && !(entry & IND_DONE);
		     ptr = (entry & IND_INDIRECTION) ?
				phys_to_virt(entry & PAGE_MASK) : ptr + 1) {
			if (!(entry & IND_DESTINATION)) {
				map_and_flush(entry);
			}
		}
		/* flush out last IND_DONE page */
		map_and_flush(entry);
	} else {
		/* crash type kexec images are copied to the crash region */
		for (i = 0; i < image->nr_segments; i++) {
			struct kexec_segment *seg = &image->segment[i];
			for (paddr = seg->mem; paddr < seg->mem + seg->memsz;
			     paddr += PAGE_SIZE) {
				map_and_flush(paddr);
			}
		}
	}

	/* also flush the kimage struct to be passed in as well */
	flush_dcache_range((unsigned long)image,
			   (unsigned long)image + sizeof(*image));
}

static void mpc85xx_smp_machine_kexec(struct kimage *image)
{
	int timeout = INT_MAX;
	int i, num_cpus = num_present_cpus();

	mpc85xx_smp_flush_dcache_kexec(image);

	if (image->type == KEXEC_TYPE_DEFAULT)
		smp_call_function(mpc85xx_smp_kexec_down, NULL, 0);

	while ( (atomic_read(&kexec_down_cpus) != (num_cpus - 1)) &&
		( timeout > 0 ) )
	{
		timeout--;
	}

	if ( !timeout )
		printk(KERN_ERR "Unable to bring down secondary cpu(s)");

	for_each_online_cpu(i)
	{
		if ( i == smp_processor_id() ) continue;
		mpic_reset_core(i);
	}

	default_machine_kexec(image);
}
#endif /* CONFIG_KEXEC */

static void smp_85xx_setup_cpu(int cpu_nr)
{
	if (smp_85xx_ops.probe == smp_mpic_probe)
		mpic_setup_this_cpu();

	if (cpu_has_feature(CPU_FTR_DBELL))
		doorbell_setup_this_cpu();
}

static const struct of_device_id mpc85xx_smp_guts_ids[] = {
	{ .compatible = "fsl,mpc8572-guts", },
	{ .compatible = "fsl,p1020-guts", },
	{ .compatible = "fsl,p1021-guts", },
	{ .compatible = "fsl,p1022-guts", },
	{ .compatible = "fsl,p1023-guts", },
	{ .compatible = "fsl,p2020-guts", },
	{ .compatible = "fsl,qoriq-rcpm-1.0", },
	{ .compatible = "fsl,qoriq-rcpm-2.0", },
	{ .compatible = "fsl,bsc9132-guts", },
	{},
};

void __init mpc85xx_smp_init(void)
{
	struct device_node *np;

	smp_85xx_ops.setup_cpu = smp_85xx_setup_cpu;

	np = of_find_node_by_type(NULL, "open-pic");
	if (np) {
		smp_85xx_ops.probe = smp_mpic_probe;
		smp_85xx_ops.message_pass = smp_mpic_message_pass;
	}

	if (cpu_has_feature(CPU_FTR_DBELL)) {
		/*
		 * If left NULL, .message_pass defaults to
		 * smp_muxed_ipi_message_pass
		 */
		smp_85xx_ops.message_pass = NULL;
		smp_85xx_ops.cause_ipi = doorbell_cause_ipi;
	}

#ifdef CONFIG_HOTPLUG_CPU
	ppc_md.cpu_die = generic_mach_cpu_die;
#endif

	np = of_find_matching_node(NULL, mpc85xx_smp_guts_ids);
	if (np) {
		guts_regs = of_iomap(np, 0);
		of_node_put(np);
		if (!guts_regs) {
			pr_err("%s: Could not map guts node address\n",
								__func__);
			return;
		}
		smp_85xx_ops.give_timebase = mpc85xx_give_timebase;
		smp_85xx_ops.take_timebase = mpc85xx_take_timebase;
#ifdef CONFIG_HOTPLUG_CPU
		ppc_md.cpu_die = smp_85xx_mach_cpu_die;
#endif
		/*
		 * For e6500-based platform, put thread into PW10 state gives
		 * it a chance to enter PW20 state when threads of the same
		 * core are in PW10 state.
		 */
		if (!strcmp(cur_cpu_spec->cpu_name, "e6500"))
			qoriq_cpu_die_state = E500_PM_PW10;
	}

	smp_ops = &smp_85xx_ops;

#ifdef CONFIG_KEXEC
	ppc_md.kexec_cpu_down = mpc85xx_smp_kexec_cpu_down;
	ppc_md.machine_kexec = mpc85xx_smp_machine_kexec;
#endif
}
