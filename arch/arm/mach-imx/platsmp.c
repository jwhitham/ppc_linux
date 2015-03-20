/*
 * Copyright 2011 Freescale Semiconductor, Inc.
 * Copyright 2011 Linaro Ltd.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/init.h>
#include <linux/smp.h>
#include <asm/cacheflush.h>
#include <asm/page.h>
#include <asm/smp_scu.h>
#include <asm/mach/map.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/delay.h>
#include <asm/smp_plat.h>

#include "common.h"
#include "hardware.h"

#define SCU_STANDBY_ENABLE	(1 << 5)

#define	SCFG_CORE0_SFT_RST	0x130
#define	SCFG_REVCR		0x200
#define	SCFG_CORESRENCR		0x204
#define	SCFG_SPARECR4		0x50C

#define	DCFG_CCSR_BRR		0x0E4
#define	DCFG_CCSR_SCRATCHRW1	0x200

#define	DCSR_RCPM2_DEBUG1	0x400
#define	DCSR_RCPM2_DEBUG2	0x414

#define	CCSR_TWAITSR0		0x04C

#define	STRIDE_4B		4

u32 g_diag_reg;
static void __iomem *scu_base;
static void __iomem *dcfg_base;
static void __iomem *scfg_base;
static void __iomem *dcsr_rcpm2_base;
static void __iomem *rcpm_base;
static u32 secondary_pre_boot_entry;

static struct map_desc scu_io_desc __initdata = {
	/* .virtual and .pfn are run-time assigned */
	.length		= SZ_4K,
	.type		= MT_DEVICE,
};

void __init imx_scu_map_io(void)
{
	unsigned long base;

	/* Get SCU base */
	asm("mrc p15, 4, %0, c15, c0, 0" : "=r" (base));

	scu_io_desc.virtual = IMX_IO_P2V(base);
	scu_io_desc.pfn = __phys_to_pfn(base);
	iotable_init(&scu_io_desc, 1);

	scu_base = IMX_IO_ADDRESS(base);
}

void imx_scu_standby_enable(void)
{
	u32 val = readl_relaxed(scu_base);

	val |= SCU_STANDBY_ENABLE;
	writel_relaxed(val, scu_base);
}

static int imx_boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	imx_set_cpu_jump(cpu, v7_secondary_startup);
	imx_enable_cpu(cpu, true);
	return 0;
}

/*
 * Initialise the CPU possible map early - this describes the CPUs
 * which may be present or become present in the system.
 */
static void __init imx_smp_init_cpus(void)
{
	int i, ncores;

	ncores = scu_get_core_count(scu_base);

	for (i = ncores; i < NR_CPUS; i++)
		set_cpu_possible(i, false);
}

void imx_smp_prepare(void)
{
	scu_enable(scu_base);
}

static void __init imx_smp_prepare_cpus(unsigned int max_cpus)
{
	imx_smp_prepare();

	/*
	 * The diagnostic register holds the errata bits.  Mostly bootloader
	 * does not bring up secondary cores, so that when errata bits are set
	 * in bootloader, they are set only for boot cpu.  But on a SMP
	 * configuration, it should be equally done on every single core.
	 * Read the register from boot cpu here, and will replicate it into
	 * secondary cores when booting them.
	 */
	asm("mrc p15, 0, %0, c15, c0, 1" : "=r" (g_diag_reg) : : "cc");
	__cpuc_flush_dcache_area(&g_diag_reg, sizeof(g_diag_reg));
	outer_clean_range(__pa(&g_diag_reg), __pa(&g_diag_reg + 1));
}

struct smp_operations  imx_smp_ops __initdata = {
	.smp_init_cpus		= imx_smp_init_cpus,
	.smp_prepare_cpus	= imx_smp_prepare_cpus,
	.smp_boot_secondary	= imx_boot_secondary,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_die		= imx_cpu_die,
	.cpu_kill		= imx_cpu_kill,
#endif
};

static int ls1021a_secondary_iomap(void)
{
	struct device_node *np;
	int ret;

	np = of_find_compatible_node(NULL, NULL, "fsl,ls1021a-dcfg");
	if (!np) {
		pr_err("%s: failed to find dcfg node.\n", __func__);
		ret = -ENODEV;
		goto dcfg_err;
	}

	dcfg_base = of_iomap(np, 0);
	of_node_put(np);
	if (!dcfg_base) {
		pr_err("%s: failed to map dcfg.\n", __func__);
		ret = -ENOMEM;
		goto dcfg_err;
	}

	np = of_find_compatible_node(NULL, NULL, "fsl,ls1021a-scfg");
	if (!np) {
		pr_err("%s: failed to find scfg node.\n", __func__);
		ret = -ENODEV;
		goto scfg_err;
	}

	scfg_base = of_iomap(np, 0);
	of_node_put(np);
	if (!scfg_base) {
		pr_err("%s: failed to map scfg.\n", __func__);
		ret = -ENOMEM;
		goto scfg_err;
	}

	np = of_find_compatible_node(NULL, NULL, "fsl,ls1021a-dcsr-rcpm");
	if (!np) {
		pr_err("%s: failed to find dcsr node.\n", __func__);
		ret = -ENODEV;
		goto dcsr_err;
	}

	dcsr_rcpm2_base = of_iomap(np, 1);
	of_node_put(np);
	if (!dcsr_rcpm2_base) {
		pr_err("%s: failed to map dcsr.\n", __func__);
		ret = -ENOMEM;
		goto dcsr_err;
	}

	np = of_find_compatible_node(NULL, NULL, "fsl,qoriq-rcpm-2.1");
	if (!np) {
		pr_err("%s(): failed to find the RCPM node.\n", __func__);
		ret = -ENODEV;
		goto rcpm_err;
	}

	rcpm_base = of_iomap(np, 0);
	of_node_put(np);
	if (!rcpm_base) {
		pr_err("%s: failed to map rcpm.\n", __func__);
		ret = -ENOMEM;
		goto rcpm_err;
	}

	return 0;

rcpm_err:
	iounmap(dcsr_rcpm2_base);
dcsr_err:
	iounmap(scfg_base);
scfg_err:
	iounmap(dcfg_base);
dcfg_err:
	return ret;
}

u32 ls1_get_cpu_arg(int cpu)
{
	BUG_ON(!rcpm_base);

	cpu = cpu_logical_map(cpu);
	return ioread32be(rcpm_base + CCSR_TWAITSR0) & (1 << cpu);
}

void ls1021a_set_secondary_entry(void)
{
	unsigned long paddr;

	if (dcfg_base) {
		paddr = virt_to_phys(secondary_startup);
		writel_relaxed(cpu_to_be32(paddr),
				dcfg_base + DCFG_CCSR_SCRATCHRW1);
	}
}

static int ls1021a_reset_secondary(unsigned int cpu)
{
	u32 tmp;

	if (!scfg_base || !dcfg_base || !dcsr_rcpm2_base)
		return -ENOMEM;

	writel_relaxed(secondary_pre_boot_entry,
			dcfg_base + DCFG_CCSR_SCRATCHRW1);

	/* Apply LS1021A specific to write to the BE SCFG space */
	tmp = ioread32be(scfg_base + SCFG_REVCR);
	iowrite32be(0xffffffff, scfg_base + SCFG_REVCR);

	/* Soft reset secondary core */
	iowrite32be(0x80000000, scfg_base + SCFG_CORESRENCR);
	iowrite32be(0x80000000, scfg_base +
				SCFG_CORE0_SFT_RST + STRIDE_4B * cpu);
	mdelay(15);

	/* Release secondary core */
	iowrite32be(1 << cpu, dcfg_base + DCFG_CCSR_BRR);

	ls1021a_set_secondary_entry();

	/* Disable core soft reset register */
	iowrite32be(0x0, scfg_base + SCFG_CORESRENCR);

	/* Revert back to the default */
	iowrite32be(tmp, scfg_base + SCFG_REVCR);

	return 0;
}

static int ls1021a_boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	int ret = 0;

	if (system_state == SYSTEM_RUNNING)
		ret = ls1021a_reset_secondary(cpu);

	udelay(1);

	arch_send_wakeup_ipi_mask(cpumask_of(cpu));

	return ret;
}

static void __init ls1021a_smp_prepare_cpus(unsigned int max_cpus)
{
	ls1021a_secondary_iomap();

	secondary_pre_boot_entry = readl_relaxed(dcfg_base +
						DCFG_CCSR_SCRATCHRW1);

	ls1021a_set_secondary_entry();
}

struct smp_operations  ls1021a_smp_ops __initdata = {
	.smp_prepare_cpus	= ls1021a_smp_prepare_cpus,
	.smp_boot_secondary	= ls1021a_boot_secondary,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_die                = ls1021a_cpu_die,
	.cpu_kill               = ls1021a_cpu_kill,
#endif
};
