#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/time.h>
#include <stdlib.h>
#include "librvs.h"


static inline uint32_t rvs_get_cycles(void)
{
   uint32_t l1;
   asm volatile("mfspr %0, 526" : "=r" (l1));
   return l1;
}

typedef struct overflow_s {
   uint32_t t1, t2;
} overflow_t;

int main (void)
{
   FILE *         fd;
   FILE *         fd3;
   uint32_t       i;
   uint32_t       tstamp = 0;
   uint32_t       id = 0;
   uint32_t       kernel_entries = 0;
   uint32_t       old_tstamp = 0;
   uint64_t       offset = 0;
   uint64_t       fixed_tstamp = 0;
   uint64_t       enter_kernel_tstamp = 0;
   uint64_t       start_tstamp = 0;
   uint64_t       total_kernel_time = 0;
   uint64_t       last_kernel_exit = 0;
   uint64_t       loop_time = 0;
   uint64_t       start_time = 0;
   const uint64_t expected_time = 2000 * 1000 * 1000;
   const uint32_t histogram_size = 100;
   const uint32_t overflow_size = 10000;
   uint32_t       histogram[histogram_size];
   overflow_t     overflow[overflow_size];
   uint32_t       overflow_count = 0;
   uint32_t       peak = 0;
   uint32_t       average_iteration_time = 1;
   uint32_t       iterations = 0;
   uint32_t       kernel_depth = 0;

   printf ("Calling RVS_Init()\n");
   RVS_Init();

   memset (histogram, 0, sizeof (histogram));
   printf ("Starting test\n");
   fflush (stdout);
   {
      uint32_t loop_start, loop_now, loop_last, delta;

      RVS_Ipoint (10);
      loop_start = rvs_get_cycles ();
      loop_last = loop_now = loop_start;
      do {
         loop_now = rvs_get_cycles ();
         delta = loop_now - loop_last;
         if (delta < histogram_size) {
            histogram[delta]++;
         } else if (overflow_count < overflow_size) {
            overflow[overflow_count].t1 = loop_last;
            overflow[overflow_count].t2 = loop_now;
            overflow_count++;
         }
         iterations ++;
         loop_last = loop_now;
      } while ((loop_now - loop_start) < (uint32_t) expected_time);
      RVS_Ipoint (11);
   }

   RVS_Output();

   printf ("Examining results\n");
   fd = fopen ("histogram.txt", "wt");
   if (!fd) {
      printf ("Failed, histogram.txt not created\n");
      return 1;
   }
   for (i = 0; i < histogram_size; i++) {
      fprintf (fd, "%u,%u\n", i, histogram[i]);
      if (histogram[i] > peak) {
         peak = histogram[i];
         average_iteration_time = i;
      }
   }
   fclose (fd);
   printf ("An uninterrupted loop iteration costs %u clock cycles\n",
           average_iteration_time);

   fd = fopen ("overflow.txt", "wt");
   if (!fd) {
      printf ("Failed, overflow.txt not created\n");
      return 1;
   }
   for (i = 0; i < overflow_count; i++) {
      fprintf (fd, "%u,%u,%u\n", i, overflow[i].t1, overflow[i].t2);
   }
   fclose (fd);
   printf ("There were %u interruptions\n", overflow_count);

   fd3 = fopen ("trace.txt", "wt");
   fd = fopen ("trace.bin", "rb");
   if ((!fd) || (!fd3)) {
      printf ("Failed, trace.bin not found\n");
      return 1;
   }
   while ((fread (&id, 4, 1, fd) == 1) && (fread (&tstamp, 4, 1, fd) == 1)) {
      if (tstamp < old_tstamp) {
         offset += 1ULL << 32ULL;
      }
      fixed_tstamp = tstamp + offset;
      old_tstamp = tstamp;
      if (start_time == 0) {
         start_time = tstamp;
      }
      fprintf (fd3, "%08x %10.6f\n", id, ((double) (tstamp - start_time) / 800e6));

      switch (id) {
         case RVS_BEGIN_WRITE:
         case RVS_END_WRITE:
            printf ("Unexpected RVS_BEGIN_WRITE/RVS_END_WRITE markers in short trace\n");
            return 1;
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
            last_kernel_exit = 0;
            kernel_depth ++;
            if (kernel_depth == 1) {
               enter_kernel_tstamp = fixed_tstamp;
               kernel_entries ++;
            }
            if (kernel_depth > 10) {
               printf ("Too many nested kernel entries\n");
               return 1;
            }
            break;
         case RVS_IRQ_EXIT:
         case RVS_TIMER_EXIT:
         case RVS_SYS_EXIT:
         case RVS_SWITCH_TO:
            last_kernel_exit = 0;
            if (kernel_depth <= 0) {
               kernel_depth = 0;
            } else {
               kernel_depth --;
               if (kernel_depth == 0) {
                  total_kernel_time += fixed_tstamp - enter_kernel_tstamp;
                  last_kernel_exit = fixed_tstamp;
               }
            }
            break;
         case 10:
            last_kernel_exit = 0;
            kernel_entries = 0;
            total_kernel_time = 0;
            start_tstamp = fixed_tstamp;
            break;
         case 11:
            last_kernel_exit = 0;
            printf ("loop: %u iterations, %1.0f wallclock cycles, %u switches, %1.0f ticks\n",
               iterations,
               (double) (fixed_tstamp - start_tstamp),
               kernel_entries,
               ((double) (fixed_tstamp - start_tstamp) / 3.2e6));
            loop_time = (uint64_t) iterations * (uint64_t) average_iteration_time;
            printf ("      %1.0f in other threads, %1.0f in user space, %1.0f unaccounted\n",
               (double) total_kernel_time,
               (double) loop_time,
               (double) (fixed_tstamp - start_tstamp) - 
                  (double) total_kernel_time - (double) loop_time);
            break;
         default:
            printf ("Invalid ipoint id %u\n", id);
            return 1;
      }
   }
   fclose (fd);
   fclose (fd3);
   printf ("Test passed\n");
   return 0;
}

