/*=========================================================================
 * RapiTime    : a tool for Measurement-Based Execution Time Analysis
 * Module      : rvs_library for PPC Linux
 * File        : rvs_library_test.c
 * Description :
 * Test librvs.a and rvs.ko API.
 *
 * Copyright (c) 2016 Rapita Systems Ltd.               All rights reserved
 *=========================================================================
*/
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/time.h>
#include <stdlib.h>
#include "librvs.h"

#define TRACE_NAME "trace.bin"

void loopN (unsigned count);

static inline uint32_t rvs_get_cycles(void)
{
   uint32_t l1;
   asm volatile("mfspr %0, 526" : "=r" (l1));
   return l1;
}

void test_ipoint_1 (int v);
void test_ipoint_2 (void);
void test_ipoint_3 (void);

int main (void)
{
   FILE *         fd;
   FILE *         fd3;
   FILE *         fd2;
   uint32_t       i;
   uint32_t       tstamp = 0;
   uint32_t       id = 0;
   uint32_t       timer_events = 0;
   uint32_t       sys_events = 0;
   uint32_t       irq_events = 0;
   uint32_t       sched_events = 0;
   uint32_t       old_tstamp = 0;
   uint64_t       offset = 0;
   uint64_t       fixed_tstamp = 0;
   uint64_t       enter_kernel_tstamp = 0;
   uint64_t       start_tstamp = 0;
   uint64_t       total_kernel_time = 0;
   uint64_t       last_kernel_exit = 0;
   uint64_t       min_loop_time = 0;
   uint32_t       kernel_depth = 0;
   uint32_t       hwm = 0;
   uint32_t       check = 0;
   uint32_t       begin_write = 0;
   uint32_t       end_write = 0;
   const uint32_t loop_cycles = 100000;

   printf ("Testing librvs.a and rvs.ko\n\n");
   fflush (stdout);

   printf ("Ipoints before first RVS_Init? (API misuse)\n");
   for (i = 0; i < 100; i++) {
      RVS_Ipoint (999); /* does not appear in trace */
   }
   printf ("Calling RVS_Init() - first time\n");
   RVS_Init();

   printf ("Repeated calls to RVS_Init()? (API misuse)\n");
   for (i = 0; i < 100; i++) {
      RVS_Init ();
      RVS_Ipoint (999); /* appears in output trace */
      check ++;
   }

   printf ("Calling RVS_Output() - first time\n");
   RVS_Output();

   printf ("Repeated calls to RVS_Output()? (API misuse)\n");
   for (i = 0; i < 100; i++) {
      RVS_Ipoint (999); /* does not appear in trace */
      RVS_Output ();
   }

   printf ("Testing trace\n");
   if ((fd = fopen (TRACE_NAME, "rb")) == NULL) {
      perror ("reading " TRACE_NAME);
      return 1;
   }
   while ((fread (&id, 4, 1, fd) == 1) && (fread (&tstamp, 4, 1, fd) == 1)) {
      if (id == 999) {
         check --;
      }
   }
   fclose (fd);

   if (check != 0) {
      fputs ("Unexpected number of '999' test ipoints in trace\n", stderr);
      return 1;
   }

   printf ("Calling RVS_Init_Ex() - reinitialise with tiny buffer\n");
   RVS_Init_Ex (TRACE_NAME, RVS_SMALL_BUFFER);

   /* Fill tiny buffer repeatedly, causing flushes to disk */
   for (i = 0; i < 100000; i ++) {
      test_ipoint_1 (1999);
      test_ipoint_2 ();
      test_ipoint_3 ();
      RVS_Ipoint (1999);
      check += 4;
   }
   RVS_Output ();
   printf ("%u ipoints written\n", check);

   if ((fd = fopen (TRACE_NAME, "rb")) == NULL) {
      perror ("reading " TRACE_NAME);
      return 1;
   }
   while ((fread (&id, 4, 1, fd) == 1) && (fread (&tstamp, 4, 1, fd) == 1)) {
      if (id == 1999) {
         check --;
      } else if (id < 1000000) {
         fputs ("Unexpected low-value ipoint in trace\n", stderr);
         return 1;
      } else if (id == RVS_BEGIN_WRITE) {
         begin_write ++;
      } else if (id == RVS_END_WRITE) {
         end_write ++;
      }
   }
   fclose (fd);
   printf ("%u disk writes\n", begin_write);

   if (begin_write != end_write) {
      fputs ("begin_write and end_write don't match\n", stderr);
      return 1;
   }
   /* Write 400000 elements to the trace. With a buffer holding
    * at most 16K elements, the expected result is at least 24
    * disk writes: more are possible. */

   if (begin_write < 24) {
      fputs ("unexpectedly small number of disk writes\n", stderr);
      return 1;
   }
   if (begin_write > 100) {
      fputs ("unexpectedly large number of disk writes\n", stderr);
      return 1;
   }
   if (check != 0) {
      fputs ("Unexpected number of '1999' test ipoints in trace\n", stderr);
      return 1;
   }

   printf ("Calling RVS_Init() - run with big buffer now:\n");
   RVS_Init();
   fflush (stdout);

   for (i = 0; i < 10000; i++) {
      RVS_Ipoint (10);
      loopN (loop_cycles);
      RVS_Ipoint (11);
   }

   RVS_Output();

   printf ("Examining results\n");

   begin_write = end_write = 0;
   if ((fd3 = fopen ("trace.txt", "wt")) == NULL) {
      perror ("creating trace.txt");
      return 1;
   }
   if ((fd2 = fopen ("results.txt", "wt")) == NULL) {
      perror ("creating results.txt");
      return 1;
   }
   if ((fd = fopen (TRACE_NAME, "rb")) == NULL) {
      perror ("reading " TRACE_NAME);
      return 1;
   }

   while ((fread (&id, 4, 1, fd) == 1) && (fread (&tstamp, 4, 1, fd) == 1)) {
      if (tstamp < old_tstamp) {
         offset += 1ULL << 32ULL;
      }
      fixed_tstamp = tstamp + offset;
      old_tstamp = tstamp;

      switch (id) {
         case RVS_BEGIN_WRITE:
            begin_write ++;
            break;
         case RVS_END_WRITE:
            end_write ++;
            break;
         case RVS_SWITCH_FROM:
            if ((kernel_depth == 0) && last_kernel_exit) {
               /* Special case. The scheduler is invoked after the interrupt
                * has been serviced, before returning to user code. This is done
                * from code in arch/powerpc/kernel/entry_32.S:
                * ret_from_except -> do_work -> do_resched -> recheck -> schedule
                *
                * Ignore the time between the end of the interrupt and the start
                * of the scheduler.
                */
               total_kernel_time += fixed_tstamp - last_kernel_exit;
            }
            /* fall through */
         case RVS_TIMER_ENTRY:
         case RVS_IRQ_ENTRY:
         case RVS_SYS_ENTRY:
         case RVS_PFAULT_ENTRY:
            last_kernel_exit = 0;
            kernel_depth ++;
            if (kernel_depth == 1) {
               enter_kernel_tstamp = fixed_tstamp;
            }
            if (kernel_depth > 100) {
               printf ("Too many nested kernel entries\n");
               return 1;
            }
            if (kernel_depth > hwm) {
               hwm = kernel_depth;
            }
            break;
         case RVS_IRQ_EXIT:
         case RVS_TIMER_EXIT:
         case RVS_SYS_EXIT:
         case RVS_SWITCH_TO:
         case RVS_PFAULT_EXIT:
            last_kernel_exit = 0;
            if (kernel_depth <= 0) {
               kernel_depth = 0;
            } else {
               kernel_depth --;
               if (kernel_depth == 0) {
                  total_kernel_time += fixed_tstamp - enter_kernel_tstamp;
               }
               switch (id) {
                  case RVS_TIMER_EXIT:
                     last_kernel_exit = fixed_tstamp;
                     timer_events ++;
                     break;
                  case RVS_SYS_EXIT:
                     sys_events ++;
                     break;
                  case RVS_IRQ_EXIT:
                     irq_events ++;
                     break;
                  case RVS_SWITCH_TO:
                     sched_events ++;
                     break;
               }
            }
            break;
         case 10:
            last_kernel_exit = 0;
            timer_events = sys_events = irq_events = sched_events = 0;
            total_kernel_time = 0;
            start_tstamp = fixed_tstamp;
            break;
         case 11:
            last_kernel_exit = 0;
            fprintf (fd2, "%1.0f %1.0f %u %u %u %u\n",
               (double) (fixed_tstamp - start_tstamp),
               (double) ((fixed_tstamp - start_tstamp) - total_kernel_time),
               (unsigned) timer_events, (unsigned) sys_events, (unsigned) irq_events, (unsigned) sched_events);
            if ((timer_events + sys_events + irq_events + sched_events) == 0) {
               if ((!min_loop_time) || ((fixed_tstamp - start_tstamp) < min_loop_time)) {
                  min_loop_time = (fixed_tstamp - start_tstamp);
                  printf ("Uninterrupted loop execution time is %1.0f\n",
                          (double) min_loop_time);
               }
            } else {
               if (min_loop_time) {
                  printf ("Loop %1.0f to %1.0f is: %1.0f wall-clock, %1.0f execution, "
                        "%1.0f extra from %u timer events, %u sys events, %u irq events, %u sched_events\n",
                     (double) start_tstamp,
                     (double) fixed_tstamp,
                     (double) (fixed_tstamp - start_tstamp),
                     (double) ((fixed_tstamp - start_tstamp) - total_kernel_time),
                     (double) ((fixed_tstamp - start_tstamp) - total_kernel_time - min_loop_time),
                     (unsigned) timer_events, (unsigned) sys_events, (unsigned) irq_events, (unsigned) sched_events);
               }
            }
            break;
         default:
            printf ("Invalid ipoint id %u\n", (unsigned) id);
            return 1;
      }
      fprintf (fd3, "%08x %14.0f %14.0f\n", (unsigned) id, (double) fixed_tstamp,
               (double) ((fixed_tstamp - start_tstamp) - total_kernel_time));
   }
   fclose (fd);
   fclose (fd2);
   fclose (fd3);
   if ((begin_write != 1) || (end_write != 1)) {
      printf ("Unexpected RVS_BEGIN_WRITE/RVS_END_WRITE count in short trace\n");
      return 1;
   }
   printf ("Maximum kernel depth: %u\n", (unsigned) hwm);
   printf ("Test passed\n");
   return 0;
}

