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

/* Context switch markers in the trace. */
#define RVS_ENTRY_MASK     0xffffff80     /* anything matching this mask is a kernel event, except: */
#define RVS_BEGIN_WRITE    0xfffffff1     /* userspace: librvs began writing trace to disk */
#define RVS_END_WRITE      0xfffffff0     /* userspace: librvs finished writing trace to disk */
#define RVS_OI_SIGNAL      0xffffffec     /* userspace: overflow imminent signal sent by kernel */
#define RVS_ENTRY_COUNT    0x80           /* maximum of 0x80 event codes */


/* Flags for RVS_Init_Ex */
#define RVS_SMALL_BUFFER   1  /* use smaller buffers and dump more frequently */


typedef struct rvs_uentry_opaque {
	short dummy;
} rvs_uentry_opaque;

extern struct rvs_uentry_opaque * rvs_user_trace_write_pointer;

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
