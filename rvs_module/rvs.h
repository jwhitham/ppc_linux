#ifndef __RVS_H__
#define __RVS_H__

#include <linux/ioctl.h>
#include <linux/types.h>

/* API Version. */
#define RVS_API_VERSION		0

/* ioctl()'s supported */
#define RVSIO	0xaf

/* ioctl()'s */
#define RVS_GET_VERSION		_IOW(RVSIO, 0x00, __u32)
#define RVS_RESET		_IO(RVSIO, 0x01)
#define RVS_SET_BUFSHIFT	_IOR(RVSIO, 0x02, __u32)
#define RVS_GET_BUFSHIFT	_IOW(RVSIO, 0x03, __u32)

#define RVS_ENABLE		_IO(RVSIO, 0x04)
#define RVS_DISABLE		_IO(RVSIO, 0x05)

/* Tracing statistics. */
struct rvs_stats {
   __s32 missed;
   __s32 written;
   __s32 read;
};
#define RVS_GET_STATS		_IOW(RVSIO, 0x06, struct rvs_stats)

/* Device name. */
#define RVS_FILE_NAME		"/dev/rvs"

/* Context switch markers in the trace. */
#define RVS_TIMER_ENTRY    0xffffffff     /* timer interrupt entry */
#define RVS_TIMER_EXIT     0xfffffffe     /* timer interrupt exit */
#define RVS_SWITCH_FROM    0xfffffff3     /* task suspended */
#define RVS_SWITCH_TO      0xfffffff2     /* task resumed */

static inline __s32 rvs_time_before(__u32 a, __u32 b)
{
   return (__s32)a - (__s32)b < 0;
}

#ifdef CONFIG_X86
static inline __u32 rvs_get_cycles(void)
{
   __u32 high, low;

   asm volatile ("rdtsc" : "=a"(low), "=d"(high));
   return low;
}

static inline void rvs_init_arch(void)
{
}
#else
#ifdef CONFIG_ARM

static inline __u32 rvs_get_cycles(void)
{
   __u32 tstamp;

   asm volatile ("\tmrc p15, 0, %0, c9, c13, 0\t\n":"=r"(tstamp));
   return tstamp;
}

static inline void rvs_init_arch(void)
{
#ifndef LIBRVS
   printk(KERN_INFO "rvs: rvs_ini_arch. CPU : %d\n", smp_processor_id());
#endif

   /* Enable user mode access. */
   asm volatile ("\tmcr p15, 0, %0, c9, c14, 0\t\n"::"r"(1));

   /* Disable overflow notifications. */
   asm volatile ("\tmcr p15, 0, %0, c9, c14, 2\t\n"::"r"(0x8000000f));

   /* Reset all counters. */
   asm volatile ("\tmcr p15, 0, %0, c9, c12, 0\t\n"::"r"(0x17));

   /* Enable them. */
   asm volatile ("\tmcr p15, 0, %0, c9, c12, 1\t\n"::"r"(0x8000000f));

   /* Clear overflows (XXX shouldn't this come before the enable?). */
   asm volatile ("\tmcr p15, 0, %0, c9, c12, 3\t\n"::"r"(0x8000000f));
}
#else
#ifdef CONFIG_PPC
static inline __u32 rvs_get_cycles(void)
{
   unsigned l1;
   asm volatile("mfspr %0, 526" : "=r" (l1));
   return l1;
}

static inline void rvs_init_arch(void)
{
}
#else
#error "This module only supports PPC, x86 and ARM platforms"
#endif /* PPC */
#endif /* ARM */
#endif /* X86 */
#endif /* !__RVS_H__ */

