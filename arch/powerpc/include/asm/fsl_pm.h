/*
 * Support Power Management
 *
 * Copyright 2014 Freescale Semiconductor Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#ifndef __PPC_FSL_PM_H
#define __PPC_FSL_PM_H
#ifdef	__KERNEL__

#ifndef __ASSEMBLY__
#include <linux/suspend.h>

#define E500_PM_PH10	1
#define E500_PM_PH15	2
#define E500_PM_PH20	3
#define E500_PM_PH30	4
#define E500_PM_DOZE	E500_PM_PH10
#define E500_PM_NAP	E500_PM_PH15

#define PLAT_PM_SLEEP	20
#define PLAT_PM_LPM20	30

struct fsl_pm_ops {
	void (*irq_mask)(int cpu);
	void (*irq_unmask)(int cpu);
	void (*cpu_enter_state)(int cpu, int state);
	void (*cpu_exit_state)(int cpu, int state);
	int (*plat_enter_state)(int state);
	void (*freeze_time_base)(int freeze);
	void (*set_ip_power)(int enable, u32 mask);
};

extern const struct fsl_pm_ops *qoriq_pm_ops;

struct fsm_reg_vals;

extern void fsl_dp_fsm_setup(void __iomem *dcsr_base, struct fsm_reg_vals *val);
extern void fsl_dp_fsm_clean(void __iomem *dcsr_base, struct fsm_reg_vals *val);

extern int fsl_dp_iomap(void);
extern void fsl_dp_iounmap(void);

extern int fsl_enter_epu_deepsleep(void);
extern void fsl_dp_enter_low(void __iomem *ccsr_base, void __iomem *dcsr_base,
			     void __iomem *pld_base, int pld_flag);
extern void fsl_booke_deep_sleep_resume(void);

/*
 * RCPM definition
 */
#define RCPM_V1		1
#define RCPM_V2		2

unsigned long get_rcpm_version(void);

void set_pm_suspend_state(suspend_state_t state);
suspend_state_t pm_suspend_state(void);

#endif	/* __ASSEMBLY__ */

#define T1040QDS_TETRA_FLAG	1
#define T104xRDB_CPLD_FLAG	2

#endif	/* __KERNEL__ */
#endif  /* __PPC_FSL_PM_H */
