/*
 *  RVS Tracing API
 *
 *  Author: Jack Whitham (jwhitham@rapitasystems.com)
 *
 *  These are the low-level components of the RVS tracing API,
 *  implemented in arch/powerpc/kernel/
 *
 *  Copyright (C) 2016 Rapita Systems Ltd.
 *
 */
#ifndef _LINUX_TRACE_RVS_H
#define _LINUX_TRACE_RVS_H

struct rvs_entry {
   unsigned id, tstamp;
};

void trace_rvs_reset (void);
void trace_rvs_start (void);
void trace_rvs_stop (void);
void trace_rvs_ipoint (unsigned id);
ssize_t trace_rvs_download (struct rvs_entry __user *target_p, size_t target_size);

#endif

