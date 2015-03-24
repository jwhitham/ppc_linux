/*
 * Freescale General Power Management Implementation
 *
 * Copyright 2014 Freescale Semiconductor, Inc.
 * Author: Wang Dongsheng <dongsheng.wang@freescale.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include <linux/suspend.h>
#include <asm/fsl_pm.h>

static suspend_state_t pm_state;

void set_pm_suspend_state(suspend_state_t state)
{
	pm_state = state;
}
EXPORT_SYMBOL_GPL(set_pm_suspend_state);

suspend_state_t pm_suspend_state(void)
{
	return pm_state;
}
EXPORT_SYMBOL_GPL(pm_suspend_state);
