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
#endif	/* __KERNEL__ */
#endif  /* __PPC_FSL_PM_H */
