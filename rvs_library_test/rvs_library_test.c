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
#define INNER_LOOP_CYCLES 100000
#define OUTER_LOOP_CYCLES 10000

void run_loop (unsigned count);

static inline uint32_t rvs_get_cycles(void)
{
   uint32_t l1;
   asm volatile("mfspr %0, 526" : "=r" (l1));
   return l1;
}

typedef struct s_delta_data {
   unsigned index;
   unsigned delta;
   unsigned pollution_count;
} t_delta_data;

static t_delta_data delta_list[OUTER_LOOP_CYCLES];

void test_ipoint_1 (int v);
void test_ipoint_2 (void);
void test_ipoint_3 (void);

static int cmp_delta (const void *p1, const void *p2)
{
   t_delta_data * d1 = (t_delta_data *) p1;
   t_delta_data * d2 = (t_delta_data *) p2;

   if (d1->delta == d2->delta) {
      return d1->index - d2->index;
   } else {
      return d2->delta - d1->delta;
   }
}


int main (void)
{
   FILE *         fd;
   unsigned       i;
   uint32_t       tstamp = 0;
   uint32_t       id = 0;
   unsigned       timer_events = 0;
   unsigned       sys_events = 0;
   unsigned       irq_events = 0;
   unsigned       sched_events = 0;
   unsigned       mathemu_events = 0;
   uint32_t       old_tstamp = 0;
   uint64_t       offset = 0;
   uint64_t       fixed_tstamp = 0;
   uint64_t       enter_kernel_tstamp = 0;
   uint64_t       start_tstamp = 0;
   uint64_t       total_kernel_time = 0;
   uint64_t       min_loop_time = ~0;
   uint64_t       max_loop_time = 0;
   unsigned       interrupted_count = 0;
   unsigned       uninterrupted_count = 0;
   unsigned       pollution_count = 0;
   unsigned       kernel_depth = 0;
   unsigned       hwm = 0;
   unsigned       check = 0;
   unsigned       begin_write = 0;
   unsigned       end_write = 0;

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
   printf ("ipoints written: %u\n", check);

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
   printf ("disk writes: %u\n", begin_write);

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

   printf ("Calling RVS_Init() - run with big buffer now\n");
   fflush (stdout);
   run_loop (10); /* warm up icache */
   RVS_Init();

   for (i = 1; i <= OUTER_LOOP_CYCLES; i++) {
      run_loop (INNER_LOOP_CYCLES);
   }
   check = OUTER_LOOP_CYCLES;

   RVS_Output();

   printf ("Examining results\n");
   fflush (stdout);

   begin_write = end_write = 0;
   if ((fd = fopen (TRACE_NAME, "rb")) == NULL) {
      perror ("reading " TRACE_NAME);
      return 1;
   }
   pollution_count = 9999;

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
         case RVS_TIMER_ENTRY:
         case RVS_IRQ_ENTRY:
         case RVS_SYS_ENTRY:
         case RVS_PFAULT_ENTRY:
         case RVS_MATHEMU_ENTRY:
            kernel_depth ++;
            if (kernel_depth == 1) {
               enter_kernel_tstamp = fixed_tstamp;
            }
            if (kernel_depth > 1000) {
               fprintf (stderr, "Too many nested kernel entries: last is 0x%x\n", (unsigned) id);
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
         case RVS_MATHEMU_EXIT:
            if (kernel_depth <= 0) {
               kernel_depth = 0;
            } else {
               kernel_depth --;
               if (kernel_depth == 0) {
                  total_kernel_time += fixed_tstamp - enter_kernel_tstamp;
               }
               switch (id) {
                  case RVS_TIMER_EXIT:
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
                  case RVS_MATHEMU_EXIT:
                     mathemu_events ++;
                     break;
               }
            }
            break;
         case 10:
            timer_events = sys_events = irq_events = sched_events = 0;
            mathemu_events = 0;
            total_kernel_time = 0;
            start_tstamp = fixed_tstamp;
            break;
         case 11:
            if ((timer_events + sys_events + irq_events + sched_events + mathemu_events) != 0) {
               /* loop was interrupted by at least one thing */
               interrupted_count ++;
               pollution_count ++;
            } else {
               /* loop should have got 100% of CPU */
               uint64_t delta = fixed_tstamp - start_tstamp;

               if (delta < min_loop_time) {
                  min_loop_time = delta;
               }
               if (delta > max_loop_time) {
                  max_loop_time = delta;
               }
               if (uninterrupted_count >= OUTER_LOOP_CYCLES) {
                  fputs ("delta_list overflow\n", stderr);
                  return 1;
               }
               delta_list[uninterrupted_count].delta = (unsigned) delta;
               delta_list[uninterrupted_count].index = uninterrupted_count;
               delta_list[uninterrupted_count].pollution_count = pollution_count;
               uninterrupted_count ++;
               pollution_count = 0;
            }
            check --;
            break;
         default:
            fprintf (stderr, "Invalid ipoint id %u (0x%x)\n", (unsigned) id, (unsigned) id);
            return 1;
      }
   }
   fclose (fd);
   if ((begin_write != 1) || (end_write != 1)) {
      fprintf (stderr, "Unexpected RVS_BEGIN_WRITE/RVS_END_WRITE count in short trace\n");
      return 1;
   }
   if (check != 0) {
      fputs ("Unexpected number of test ipoints in trace\n", stderr);
      return 1;
   }
   printf ("Maximum kernel depth: %u\n", (unsigned) hwm);
   printf ("Uninterrupted loops: %u\n", uninterrupted_count);
   printf ("Interrupted loops: %u (%1.1f%%)\n", interrupted_count,
      100.0 * ((double) interrupted_count / (double) (uninterrupted_count + interrupted_count)));
   printf ("Clock cycles per loop cycle: %1.0f\n", (double) (min_loop_time / INNER_LOOP_CYCLES));
   printf ("Minimum uninterrupted exec time: %1.0f clock cycles\n", (double) min_loop_time);
   printf ("Maximum uninterrupted exec time: %1.0f clock cycles\n", (double) max_loop_time);
   printf ("Span: %1.0f clock cycles\n", (double) (max_loop_time - min_loop_time));

   if (max_loop_time > (min_loop_time + 100)) {
      /* typical span would be about 40 clock cycles */
      fprintf (stderr, "Unexpectedly large min/max span: some kernel event is not accounted for\n");
      return 1;
   }
   qsort (delta_list, uninterrupted_count, sizeof (t_delta_data), cmp_delta);

   for (i = 0; (i < uninterrupted_count) && (i < 10); i++) {
      printf (" time %8u @ %08x pol %u\n",
         delta_list[i].delta, delta_list[i].index, delta_list[i].pollution_count);
   }

   {
      unsigned outputs = 0;
      unsigned same_count = 1;
      unsigned same_value = delta_list[0].delta;
      delta_list[uninterrupted_count].delta = 0;
      for (i = 1; (i <= uninterrupted_count) && (outputs < 10); i++) {
         if (delta_list[i].delta == same_value) {
            same_count ++;
         } else {
            printf (" time %8u appears %u times\n", same_value, same_count);
            same_count = 1;
            same_value = delta_list[i].delta;
            outputs ++;
         }
      }
   }
   printf ("Test passed\n");
   return 0;
}

