#ifndef POWERPC_85XX_SMP_H_
#define POWERPC_85XX_SMP_H_ 1

#include <linux/init.h>

#ifdef CONFIG_SMP
#define THREAD_IN_CLUSTER	8

DECLARE_PER_CPU(cpumask_t, cpu_cluster_map);
static inline struct cpumask *cpu_cluster_mask(int cpu)
{
	return &per_cpu(cpu_cluster_map, cpu);
}

void __init mpc85xx_smp_init(void);
#else
static inline void mpc85xx_smp_init(void)
{
	/* Nothing to do */
}
#endif

extern long fsl_enable_threads(void *);

#endif /* not POWERPC_85XX_SMP_H_ */
