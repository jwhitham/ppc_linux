/*=========================================================================
 * RapiTime    : a tool for Measurement-Based Execution Time Analysis
 * Module      : rvs_library for PPC Linux
 * File        : librvs.h
 * Description :
 * Kernel and user space execution time tracing for Linux.
 *
 * Copyright (c) 2016 Rapita Systems Ltd.               All rights reserved
 *=========================================================================
*/
#ifndef __LIBRVS_H__
#define __LIBRVS_H__


#ifdef __cplusplus
extern "C" {
#endif

#ifndef RVS_TIMER_ENTRY
/* Context switch markers in the trace. */
#define RVS_MATHEMU_ENTRY  0xfffffffd     /* mathemu entry */
#define RVS_MATHEMU_EXIT   0xfffffffc     /* mathemu exit */
#define RVS_PFAULT_ENTRY   0xfffffffb     /* page fault entry */
#define RVS_PFAULT_EXIT    0xfffffffa     /* page fault exit */
#define RVS_TIMER_ENTRY    0xfffffff9     /* timer interrupt entry */
#define RVS_TIMER_EXIT     0xfffffff8     /* timer interrupt exit */
#define RVS_SYS_ENTRY      0xfffffff7     /* syscall entry */
#define RVS_SYS_EXIT       0xfffffff6     /* syscall exit */
#define RVS_IRQ_ENTRY      0xfffffff5     /* interrupt entry */
#define RVS_IRQ_EXIT       0xfffffff4     /* interrupt exit */
#define RVS_SWITCH_FROM    0xfffffff3     /* task suspended */
#define RVS_SWITCH_TO      0xfffffff2     /* task resumed */
#define RVS_BEGIN_WRITE    0xfffffff1     /* userspace: librvs began writing trace to disk */
#define RVS_END_WRITE      0xfffffff0     /* userspace: librvs finished writing trace to disk */
#endif

/* Flags for RVS_Init_Ex */
#define RVS_SMALL_BUFFER   1  /* use smaller buffers and dump more frequently */


extern unsigned * rvs_user_trace_write_pointer;

void RVS_Init (void);
void RVS_Init_Ex (const char * trace_file_name, unsigned flags);
void RVS_Output (void);

static inline void RVS_Ipoint (unsigned id)
{
   unsigned tmp1;
   asm volatile (
      "stwu %3,4(%1)\n"    /* store id (may write to trigger page) */
      "mfspr %0,526\n"     /* get timestamp */
      "stwu %0,4(%1)\n"    /* store timestamp */
      : "=&r"(tmp1), "=&b"(rvs_user_trace_write_pointer)
      : "1"(rvs_user_trace_write_pointer), "r"(id));
}


#ifdef __cplusplus
};
#endif

#endif /* __LIBRVS_H__ */
