/*=========================================================================
 * RapiTime    : a tool for Measurement-Based Execution Time Analysis
 * Module      : none
 * File        : mprot_expt.c
 * Description :
 * Experiment: use of memory protection to detect the end of the trace
 * buffer and trigger writing to disk. PPC Linux version. Same principle
 * possible on other architectures with modification of inline assembly
 * and machine code checks.
 *
 * Copyright (c) 2016 Rapita Systems Ltd.               All rights reserved
 *=========================================================================
*/

#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <ucontext.h>

#define ELTS_IN_BUFFER 0x10000


/****************************************/

struct rvs_uentry {
   unsigned id; 
   unsigned tstamp;
};

extern struct rvs_uentry * rvs_user_trace_write_pointer;

static inline void RVS_Ipoint (unsigned id) 
{
/* The ipoint code as written in C: */
/*
   struct rvs_uentry * u = rvs_user_trace_write_pointer;
   
   u->id = id; 
   asm volatile("mfspr %0, 526" : "=r" (u->tstamp));
   u++;
   rvs_user_trace_write_pointer = u;
*/
   unsigned tmp1;

/* The ipoint code as written in PPC assembly: */
   asm volatile (
      "stw %2,0(%1)\n"    /* store id (may write to trigger page) */
      "mfspr %0, 526\n"   /* get timestamp */
      "stw %0,4(%1)\n"    /* store timestamp */
      "addi %1,%1,8\n"    /* increment write pointer */
      : "=r"(tmp1), "=&b"(rvs_user_trace_write_pointer)
      : "r"(id), "1"(rvs_user_trace_write_pointer));

}

struct rvs_uentry * rvs_user_trace_write_pointer;
static struct sigaction old_signal_handler;
static void dump_the_buffer (void);

static void segfault (int n, siginfo_t * si, void * _uc)
{
   ucontext_t * uc = (ucontext_t *) _uc;
   unsigned * trigger_pc = (unsigned *) ((intptr_t) uc->uc_mcontext.uc_regs->gregs[32]);

   /* check that this segfault is in the ipoint routine (possibly inlined)
    * by inspecting the machine code: 
         100004a4: 91 49 00 00  stw     r10,0(r9) <-- may write to trigger page
         100004a8: 7c ee 82 a6  mfspr   r7,526
         100004ac: 90 e9 00 04  stw     r7,4(r9)
         100004b0: 39 29 00 08  addi    r9,r9,8
      (addresses and register numbers will be different)
   */

   unsigned store_id = trigger_pc[0];
   unsigned get_timestamp = trigger_pc[1];
   unsigned store_timestamp = trigger_pc[2];
   unsigned increment_pointer = trigger_pc[3];

   if (((get_timestamp & 0xffff) == 0x82a6)
   && ((get_timestamp >> 26) == 0x1f)
   && ((store_id & 0xffff) == 0x0000) 
   && ((store_id >> 26) == 0x24)
   && ((store_timestamp & 0xffff) == 0x0004) 
   && ((store_timestamp >> 26) == 0x24)
   && ((increment_pointer & 0xffff) == 0x0008)
   && ((increment_pointer >> 26) == 0x0e)) {

      /* This segfault was generated as expected */
      unsigned ptr_reg = (store_id >> 16) & 0x1f;
      struct rvs_uentry * ptr_val =
         (struct rvs_uentry *) ((intptr_t) uc->uc_mcontext.uc_regs->gregs[ptr_reg]);

      /* write trace buffer to disk */
      rvs_user_trace_write_pointer = ptr_val;
      dump_the_buffer ();

      /* continue at the beginning of the buffer: we rerun the store instruction
       * with ptr_val reset to beginning of buffer. */
      uc->uc_mcontext.uc_regs->gregs[ptr_reg] = (intptr_t) rvs_user_trace_write_pointer;
      return;
   }

   /* some other segfault: reinstall old signal handler */
   if (sigaction (SIGSEGV, &old_signal_handler, NULL) != 0) {
      perror ("sigaction (SIGSEGV) reinstall of old handler failed");
      exit (1);
   }

   /* return to code: segfault will happen again immediately */
}

/****************************************/

static char megabuffer[ELTS_IN_BUFFER * 8];
static unsigned check = 0;
unsigned * boom = NULL;


void reset_the_buffer (void)
{
   unsigned p = (intptr_t) &megabuffer[0];

   /* align to 8 byte boundary so that an rvs_uentry will be on exactly 1 page */
   p = (p | 7) + 1;
   rvs_user_trace_write_pointer = (struct rvs_uentry *) p;
}

static void dump_the_buffer (void)
{
   struct rvs_uentry * scan;
   struct rvs_uentry * end_buffer;
   unsigned i = 0;
   unsigned now = 0;
   static unsigned max = 0;
   static unsigned prev = 0;
   
   end_buffer = rvs_user_trace_write_pointer;
   reset_the_buffer ();
   scan = rvs_user_trace_write_pointer;

   while (scan != end_buffer) {
      check++;
      if (check != scan->id) {
         fprintf (stderr, "incorrect ipoint id %u expected %u index %u\n",
               scan->id, check, i);
         exit (1);
      }
      now = scan->tstamp;
      if (prev && ((now - prev) > max)) {
         max = now - prev;
         printf ("%u\n", max);
      }
      prev = now;
      i++;
      scan++;
   }
}


int main (void)
{
   long page_size = sysconf (_SC_PAGESIZE);
   intptr_t page_pos;
   struct sigaction sa;
   unsigned x = 0;
   unsigned i = 0;

   printf ("page size = %ld\n", page_size);
   reset_the_buffer ();

   printf ("buffer start = %p\n", rvs_user_trace_write_pointer);

   page_pos = (intptr_t) &megabuffer[sizeof (megabuffer)];
   printf ("buffer end = %p\n", (void *) page_pos);
   page_pos -= page_size;
   page_pos &= ~ (page_size - 1);

   printf ("buffer full trigger page = %p\n", (void *) page_pos);

   if (mprotect ((void *) page_pos, page_size, PROT_READ) != 0) {
      perror ("mprotect");
      exit (1);
   }

   sa.sa_sigaction = segfault;
   sa.sa_flags = SA_SIGINFO;
   sigemptyset (&sa.sa_mask);

   if (sigaction (SIGSEGV, &sa, &old_signal_handler) != 0) {
      perror ("sigaction (SIGSEGV) install failed");
      exit (1);
   }

   for (i = 0; i < (ELTS_IN_BUFFER * 2); i++) {
      x++;
      RVS_Ipoint (x);
   }
   x++;
   RVS_Ipoint (x);
   x++;
   RVS_Ipoint (x);
   for (i = 0; i < ELTS_IN_BUFFER; i++) {
      x++;
      RVS_Ipoint (x);
   }
   dump_the_buffer ();
   for (i = 0; i < ELTS_IN_BUFFER; i++) {
      x++;
      RVS_Ipoint (x);
   }
   dump_the_buffer ();
   dump_the_buffer ();


   fputs ("everything is ok, testing a REAL segfault now:\n", stderr);

   *boom = 1;

   return 1;
}

