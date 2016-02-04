/*
 *  RVS Tracing API
 *
 *  Author: Jack Whitham (jwhitham@rapitasystems.com)
 *
 *  RVS trace point module. Implements /dev/rvs, and enables
 *  access to the trace buffer implemented in arch/powerpc/kernel/
 *
 *  Copyright (C) 2016 Rapita Systems Ltd.
 *
 */
#ifndef __RVS_H__
#define __RVS_H__

#include <linux/ioctl.h>
#include <linux/types.h>

/* API Version. */
#define RVS_API_VERSION		3

/* ioctl()'s supported */
#define RVSIO	0xaf

/* ioctl()'s */
#define RVS_GET_VERSION		_IOW(RVSIO, 0x00, __u32)
#define RVS_RESET		_IO(RVSIO, 0x01)

#define RVS_ENABLE		_IO(RVSIO, 0x04)
#define RVS_DISABLE		_IO(RVSIO, 0x05)

/* signal sent to user process if kernel buffer is about to overflow */
#define SIG_RVS_IMMINENT_OVERFLOW	50

/* Device name. */
#define RVS_FILE_NAME		"/dev/rvs"

static inline __s32 rvs_time_before(__u32 a, __u32 b)
{
   return (__s32)a - (__s32)b < 0;
}

#ifdef CONFIG_PPC
static inline __u32 rvs_get_cycles(void)
{
   unsigned l1;
   asm volatile("mfspr %0, 526" : "=r" (l1));
   return l1;
}
#else
#error "This module only supports the PowerPC platform"
#endif /* PPC */
#endif /* !__RVS_H__ */

