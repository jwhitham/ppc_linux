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
#include <unistd.h>
#include <time.h>
#include "librvs.h"

#define TRACE_NAME "trace.bin"
#define OUTER_LOOP_CYCLES 5000

#define RVS_TLB_EVENT      0xffffffef
#define RVS_SYS_EVENT      0xffffff8c

#define RVS_FPU_EVENT      0xffffff87     /* illegal instruction -> FPU emulation */
#define RVS_ALIGN_EVENT    0xffffff86     /* alignment error */


void run_spe_one (unsigned count);
void run_spe_two (unsigned count);
void run_loop (unsigned count);
void run_major_test_loop (unsigned count);
/* void spe_one (unsigned count);
void spe_two (unsigned count); */

static int detail = 0;

static inline uint32_t rvs_get_cycles(void)
{
   uint32_t l1;
   asm volatile("mfspr %0, 526" : "=r" (l1));
   return l1;
}

void explosion (unsigned r3, unsigned r4, unsigned r5)
{
   fprintf (stderr, 
      "\nloop test failed, r3 = %08x, r4 = %08x, r5 = %08x\n", r3, r4, r5);
   exit (1);
}



typedef struct s_delta_data {
   unsigned index;
   unsigned delta;
   unsigned pollution_count;
} t_delta_data;

static t_delta_data delta_list[OUTER_LOOP_CYCLES + 1];

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

static int cmp_index (const void *p1, const void *p2)
{
   t_delta_data * d1 = (t_delta_data *) p1;
   t_delta_data * d2 = (t_delta_data *) p2;

   return d1->index - d2->index;
}

static int do_measurement (const char * label, unsigned pad,
   void (* do_run_loop) (unsigned count), unsigned inner_loop_cycles);
static int do_test_kernel_flush (int tlb);

static void bad_build_id ()
{
   fputs ("\nIncorrect RVS_Build_Id sequence\n", stderr);
   exit (1);
}

int main (int argc, char ** argv)
{
   FILE *         fd;
   unsigned       i;
   uint32_t       tstamp = 0;
   uint32_t       id = 0;
   unsigned       check = 0;
   unsigned       begin_write = 0;
   unsigned       end_write = 0;
   unsigned       build_id_state = 0;

   printf ("Testing librvs.a and rvs.ko (with libc)\n\n");
   fflush (stdout);

   printf ("librvs.a API version: %d\n", RVS_Get_Version ());
   printf ("Loop test: ");
   fflush (stdout);
   run_major_test_loop (10);
   printf ("good\n");
   fflush (stdout);

   if ((argc > 1) && argv[1][0] == '-') {
      switch (argv[1][1]) {
         case 'm':
            printf ("\n\nmajor loop test (instrumentation):\n");
            fflush (stdout);
            RVS_Init();
            goto major_loop;
         case 'M':
            printf ("\n\nmajor loop test (no instrumentation):\n");
            fflush (stdout);
         major_loop:
            for (i = 0; i < 10000; i++) {
               printf ("\r%u", i);
               fflush (stdout);
               run_major_test_loop (1 << 24);
            }
            RVS_Output ();
            printf ("\nok\n");
            exit (0);
         case 'v':
            detail = 1;
            break;
         default:
            break;
      }
   }


   printf ("librvs.a/rvs.ko API test: ");
   fflush (stdout);

   /* Ipoints before first RVS_Init? (API misuse) */
   for (i = 0; i < 100; i++) {
      RVS_Ipoint (999); /* does not appear in trace */
   }
   /* Calling RVS_Init() - first time */
   RVS_Init();

   /* Repeated calls to RVS_Init()? (API misuse) */
   for (i = 0; i < 100; i++) {
      RVS_Init ();
      RVS_Ipoint (999); /* appears in output trace */
      check ++;
   }
   RVS_Build_Id ("AB");

   /* Calling RVS_Output() - first time */
   RVS_Output();

   /* Repeated calls to RVS_Output()? (API misuse) */
   for (i = 0; i < 100; i++) {
      RVS_Ipoint (999); /* does not appear in trace */
      RVS_Output ();
   }

   /* Testing trace */
   if ((fd = fopen (TRACE_NAME, "rb")) == NULL) {
      perror ("reading " TRACE_NAME);
      return 1;
   }
   while ((fread (&id, 4, 1, fd) == 1) && (fread (&tstamp, 4, 1, fd) == 1)) {
      switch (id) {
         case 999:
            check --;
            break;
         case 7:
            if (build_id_state != 0) { bad_build_id (); }
            build_id_state ++; break;
         case 5:
            if (build_id_state != 1) { bad_build_id (); }
            build_id_state ++; break;
         case 'A':
            if (build_id_state != 2) { bad_build_id (); }
            build_id_state ++; break;
         case 'B':
            if (build_id_state != 3) { bad_build_id (); }
            build_id_state ++; break;
         case 0:
            if (build_id_state != 4) { bad_build_id (); }
            build_id_state ++; break;
         default:
            break;
      }
   }
   fclose (fd);

   if (check != 0) {
      fputs ("\nUnexpected number of '999' test ipoints in trace\n", stderr);
      return 1;
   }
   if (build_id_state != 5) {
      bad_build_id ();
   }

   /* Calling RVS_Init_Ex() - reinitialise with tiny buffer */
   RVS_Init_Ex ("2" TRACE_NAME, RVS_SMALL_BUFFER);

   /* Fill tiny buffer repeatedly, causing flushes to disk */
   for (i = 0; i < 100000; i ++) {
      test_ipoint_1 (1999);
      test_ipoint_2 ();
      test_ipoint_3 ();
      RVS_Ipoint (1999);
      check += 4;
   }
   RVS_Output ();

   if ((fd = fopen ("2" TRACE_NAME, "rb")) == NULL) {
      perror ("reading 2" TRACE_NAME);
      return 1;
   }
   while ((fread (&id, 4, 1, fd) == 1) && (fread (&tstamp, 4, 1, fd) == 1)) {
      if (id == 1999) {
         check --;
      } else if (id < RVS_ENTRY_MASK) {
         fputs ("\nUnexpected low-value ipoint in trace\n", stderr);
         return 1;
      } else if (id == RVS_BEGIN_WRITE) {
         begin_write ++;
      } else if (id == RVS_END_WRITE) {
         end_write ++;
      }
   }
   fclose (fd);

   if (begin_write != end_write) {
      fprintf (stderr, "\nbegin_write (%u) and end_write (%u) don't match\n", begin_write, end_write);
      return 1;
   }
   /* Wrote 400000 elements to the trace. With a buffer holding
    * at most 16K elements, the expected result is at least 24
    * disk writes: more are possible. */

   if (begin_write < 24) {
      fputs ("\nunexpectedly small number of disk writes\n", stderr);
      return 1;
   }
   if (begin_write > 100) {
      fputs ("\nunexpectedly large number of disk writes\n", stderr);
      return 1;
   }
   if (check != 0) {
      fputs ("\nUnexpected number of '1999' test ipoints in trace\n", stderr);
      return 1;
   }

   /* Test of kernel buffer flushing feature */
   if (do_test_kernel_flush (0)) { /* with system calls */
      return 1;
   }
   if (do_test_kernel_flush (1)) { /* with TLB misses */
      return 1;
   }

   /* Test detection of:
    *   alignment error
    *   FPU floating-point operation (float/double)
    * All of these should cause exceptions detected by the kernel
    * and visible in the trace.
    */
   RVS_Init_Ex ("6" TRACE_NAME, 0);
   for (i = 0; i < 1000; i++) {
      static volatile unsigned scratch[32];
      static volatile float one = 1.0;
      static volatile double two = 2.0;

      RVS_Ipoint (10);
      /* unaligned access
       * A normal store operation does not require alignment,
       * but stmw insists on it, so we can use this to provoke
       * the exception. */
      asm volatile ("stmw %0, 3(%0)" : : "b"(scratch));
      RVS_Ipoint (11);
      /* FPU operation, single precision: should be emulated */
      one *= 1.234567f;
      RVS_Ipoint (12);
      /* FPU operation, double precision: should be emulated */
      two *= 1.234567;
   }
   RVS_Ipoint (10);
   RVS_Output ();

   if ((fd = fopen ("6" TRACE_NAME, "rb")) == NULL) {
      perror ("reading 6" TRACE_NAME);
      return 1;
   } else {
      unsigned event_counter[RVS_ENTRY_COUNT];
      unsigned prev_id = 0;
      unsigned prev_tstamp = 0;
      unsigned ok = 0;

      memset (event_counter, 0, sizeof (event_counter));

      while ((fread (&id, 4, 1, fd) == 1) && (fread (&tstamp, 4, 1, fd) == 1)) {
         if (id < RVS_ENTRY_MASK) {
            /* User ipoint */
            if (prev_id && (!ok)) {
               fprintf
                 (stderr,
                  "\nKernel did not detect the expected event "
                  "between ipoint %u and %u, time %u\n",
                  prev_id, id, tstamp - prev_tstamp);
               return 1;
            }
            ok = 0;
            prev_id = id;
            prev_tstamp = tstamp;
         } else {
            /* Kernel event */
            switch (prev_id) {
               case 10:
                  /* alignment error expected */
                  if (id == RVS_ALIGN_EVENT) { ok = 1; }
                  break;
               case 11:
               case 12:
                  /* FPU emulation expected */
                  if (id == RVS_FPU_EVENT) { ok = 1; }
                  break;
               default:
                  break;
            }
            event_counter[id & (RVS_ENTRY_COUNT - 1)] ++;
         }
      }
      fclose (fd);

      if (detail) {
         printf ("\n\nDetections:\n");
         for (i = 0; i < RVS_ENTRY_COUNT; i++) {
            if (event_counter[i]) {
               printf ("  entry type 0x%02x count %u\n", i, event_counter[i]);
            }
         }
      }
   }

   printf ("good\n");

   /* Measure overheads */
   printf ("per-ipoint overhead: ");
   fflush (stdout);
   RVS_Init_Ex ("5" TRACE_NAME, 0);
   for (i = 0; i < 10; i++) {
      RVS_Ipoint (10);
      RVS_Ipoint (10);
      RVS_Ipoint (10);
      RVS_Ipoint (10);
      RVS_Ipoint (10);
      RVS_Ipoint (10);
   }
   RVS_Output ();

   if ((fd = fopen ("5" TRACE_NAME, "rb")) == NULL) {
      perror ("reading 5" TRACE_NAME);
      return 1;
   }

   {
      unsigned old_tstamp = 0;
      unsigned delta, min_delta = ~0;
      while ((fread (&id, 4, 1, fd) == 1) && (fread (&tstamp, 4, 1, fd) == 1)) {
         if (id == 10) {
            if (old_tstamp) {
               delta = tstamp - old_tstamp;
               if (delta < min_delta) {
                  min_delta = delta;
               }
            }
            old_tstamp = tstamp;
         } else {
            old_tstamp = 0;
         }
      }
      printf ("%u clock cycles best case\n", min_delta);
      if (min_delta > 10) {
         /* The expected value is 4 or 5 clock cycles */
         fprintf (stderr, "\nThis ipoint overhead is unexpectedly large.\n");
         return 1;
      }
   }

   /* Loop testing: ensure that impact of kernel interference is eliminated
    * over long runs. */

   if (do_measurement ("Tiny loop (aligned)", 0, run_loop, 100000)) {
      return 1;
   }

   if (do_measurement ("Tiny loop (unaligned)", 1, run_loop, 100000)) {
      return 1;
   }

   /* Measure cost of SPE operations (expected to be small) */

   if (do_measurement ("SPE (single precision)", 0, run_spe_one, 1000)) {
      return 1;
   }

   if (do_measurement ("SPE (double precision)", 0, run_spe_two, 1000)) {
      return 1;
   }

   printf ("\n\nTests are ok. RVS kernel integration is working as expected.\n\n");

   return 0;
}


/* In this function we ensure that impact of kernel interference is eliminated
 * over long runs. */

static int do_measurement (const char * label, unsigned pad,
      void (* do_run_loop) (unsigned count), unsigned inner_loop_cycles)
{
   FILE *         fd;
   unsigned       i;
   uint32_t       tstamp = 0;
   uint32_t       id = 0;
   unsigned       kernel_events = 0;
   uint32_t       old_tstamp = 0;
   uint64_t       offset = 0;
   uint64_t       fixed_tstamp = 0;
   uint64_t       start_tstamp = 0;
   unsigned       interrupted_count = 0;
   unsigned       uninterrupted_count = 0;
   unsigned       pollution_count = 0;
   unsigned       check = 0;
   unsigned       begin_write = 0;
   unsigned       end_write = 0;
   unsigned       event_counter[RVS_ENTRY_COUNT];
   unsigned       first_loop_time, max_loop_time, min_loop_time, max_expected_time;

   printf ("%s test: ", label);
   fflush (stdout);
   RVS_Init_Ex ("4" TRACE_NAME, 0);

   while (pad) {
      pad --;
      RVS_Ipoint (12);
   }

   for (i = 1; i <= OUTER_LOOP_CYCLES; i++) {
      do_run_loop (inner_loop_cycles);
   }
   check = OUTER_LOOP_CYCLES;

   RVS_Output();

   begin_write = end_write = 0;
   if ((fd = fopen ("4" TRACE_NAME, "rb")) == NULL) {
      perror ("reading 4" TRACE_NAME);
      return 1;
   }
   pollution_count = 9999;
   memset (event_counter, 0, sizeof (event_counter));

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
         case 10:
            kernel_events = 0;
            start_tstamp = fixed_tstamp;
            break;
         case 11:
            if (kernel_events) {
               /* loop was interrupted by at least one thing */
               interrupted_count ++;
               pollution_count ++;
            } else {
               /* loop should have got 100% of CPU */
               uint64_t delta = fixed_tstamp - start_tstamp;

               if (uninterrupted_count >= OUTER_LOOP_CYCLES) {
                  fputs ("delta_list overflow\n", stderr);
                  return 1;
               }
               delta_list[uninterrupted_count].delta = (unsigned) delta;
               delta_list[uninterrupted_count].index = uninterrupted_count + interrupted_count;
               delta_list[uninterrupted_count].pollution_count = pollution_count;
               uninterrupted_count ++;
               pollution_count = 0;
            }
            check --;
            break;
         case 12:
            /* padding element */
            break;
         default:
            if ((id & RVS_ENTRY_MASK) != RVS_ENTRY_MASK) {
               fprintf (stderr, "Invalid ipoint id %u (0x%x)\n", (unsigned) id, (unsigned) id);
               return 1;
            }
            event_counter[id & (RVS_ENTRY_COUNT - 1)] ++;
            kernel_events ++;
            break;
      }
   }
   fclose (fd);
   if ((begin_write != 1) || (end_write != 1)) {
      fprintf (stderr, "\nUnexpected RVS_BEGIN_WRITE/RVS_END_WRITE count in short trace\n");
      return 1;
   }
   if (check != 0) {
      fputs ("\nUnexpected number of test ipoints in trace\n", stderr);
      return 1;
   }
   if (detail) {
      printf ("\n\nuninterrupted_count = %u\n", uninterrupted_count);
      printf ("interrupted_count = %u\n", interrupted_count);
      for (i = 0; i < RVS_ENTRY_COUNT; i++) {
         if (event_counter[i]) {
            printf ("  entry type 0x%02x count %u\n", i, event_counter[i]);
         }
      }
   }
   if (uninterrupted_count < (OUTER_LOOP_CYCLES / 4)) {
      fputs ("\nNot enough uninterrupted loops\n", stderr);
      return 1;
   }

   delta_list[uninterrupted_count].delta = 0; /* sentinel after final element */
   delta_list[uninterrupted_count].index = ~0;

   /* sort delta list in descending order */
   qsort (&delta_list[1], uninterrupted_count - 1, sizeof (t_delta_data), cmp_delta);
   first_loop_time = delta_list[0].delta;
   max_loop_time = delta_list[1].delta;
   min_loop_time = delta_list[uninterrupted_count - 1].delta;
   max_expected_time = min_loop_time + 100;

   if (detail) {
      printf ("Clock cycles per loop cycle: %u\n", (min_loop_time / inner_loop_cycles));

      printf ("First uninterrupted loop exec time: %u clock cycles\n", first_loop_time);
      printf ("Minimum uninterrupted exec time: %u clock cycles\n", min_loop_time);
      printf ("Maximum uninterrupted exec time: %u clock cycles\n", max_loop_time);
      printf ("Maximum - minimum = %u clock cycles\n", (max_loop_time - min_loop_time));

      printf ("Greatest execution times (first appearance):\n");
      for (i = 0; (i < uninterrupted_count) && (i < 5); i++) {
         printf (" time %8u @ %6u 0x%06x pol %u\n",
            delta_list[i].delta,
            delta_list[i].index,
            delta_list[i].index,
            delta_list[i].pollution_count);
      }

      printf ("Greatest execution times (number of appearances):\n");
      {
         unsigned outputs = 0;
         unsigned same_count = 1;
         unsigned same_value = delta_list[0].delta;
         for (i = 1; (i <= uninterrupted_count) && (outputs < 5); i++) {
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

      if (max_loop_time < max_expected_time) {
         printf ("This result is within expected limits.\n");
      } else {
         unsigned outputs = 0;
         unsigned previous = 0;
         printf ("This result is NOT within expected limits; here are the big spikes:\n");

         /* sort delta list in index order */
         qsort (delta_list, uninterrupted_count, sizeof (t_delta_data), cmp_index);

         for (i = 0; (i < uninterrupted_count) && (outputs < 10); i++) {
            if (delta_list[i].delta >= max_expected_time) {
               printf (" time %8u @ %6u 0x%06x pol %u, gap from previous %u\n",
                  delta_list[i].delta,
                  delta_list[i].index,
                  delta_list[i].index,
                  delta_list[i].pollution_count,
                  delta_list[i].index - previous);
               previous = delta_list[i].index;
               outputs ++;
            }
         }
         fflush (stdout);
      }
      printf ("\n\n");
   }

   printf ("%1.1f%% interrupted, %u first\n"
           "    %u fastest, %u slowest, %u span, %u cycle: ",
      100.0 * ((double) interrupted_count / (double) (uninterrupted_count + interrupted_count)),
      first_loop_time,     /* first */
      min_loop_time,       /* fastest */
      max_loop_time,       /* slowest */
      max_loop_time - min_loop_time, /* span */
      min_loop_time / inner_loop_cycles); /* cost of loop cycle */


   if (max_loop_time < max_expected_time) {
      printf ("good\n");
   } else {
      printf ("no\n");
      fprintf (stderr, "\n%s: Unexpectedly large min/max span %u:\n"
			"  Some event is not accounted for, or perhaps the system is under load.\n"
			"  Stop other tasks and try again.\n",
			label, max_loop_time - min_loop_time);
      return 1;
   }
   return 0;
}


static int do_test_kernel_flush (int tlb)
{
   FILE *fd = NULL;
   unsigned i = 0;
   unsigned tmp_size = 0;
   unsigned page_size = 0;
   unsigned check = 0;
   unsigned oi_signal = 0;
   unsigned end_write = 0;
   unsigned id = 0;
   unsigned tstamp = 0;
   unsigned sys_count = 0;
   unsigned tlb_count = 0;
   unsigned others = 0;
   unsigned buffer_level = 0;
   unsigned expected_minimum = ~0;
   unsigned max_buffer_level = 0;
   const unsigned inner_loop_size = 10240;
   unsigned outer_loop_size = 10240;
   char *tmp = NULL;

   /* Test the "overflow imminent" mechanism, which is used
    * to flush the kernel buffer. In this function we will 
    * not generate many user ipoints. The traces will be mostly
    * full of kernel events.
    */

   RVS_Init_Ex ("3" TRACE_NAME, 0);

   /* Flushing may happen after each system call, no need to run the loop too often. */
   outer_loop_size = 250;

   /* !tlb => Provoked 250*10240 events from the kernel, creating at least 2500K
    * log events. The kernel buffer holds 2M elements, and overflow imminent is
    * signalled at 25% capacity, so we should expect 2500K / 500K = 5 overflow
    * signals, though more are possible. Expect at least 4. Typically about 9
    * are detected. */
   expected_minimum = 4;

   if (tlb) {
      page_size = sysconf (_SC_PAGESIZE);
      tmp_size = inner_loop_size * page_size;
      tmp = malloc (tmp_size);
      if (!tmp) {
         fputs ("\nUnable to allocate memory for do_test_kernel_flush\n", stderr);
         return 1;
      }
      /* Flushing mostly happens on timer interrupts. In this mode we're
       * not doing system calls, so we have to wait for the timer interrupt
       * before there is a flush. Consequently the buffer typically gets flushed
       * at a higher fill level - not 25%, more like 30%. We'll run
       * the loop more times to compensate. Expect about 7 overflows. */
      outer_loop_size += 125;
   }
   

   /* Fill kernel buffer */
   for (i = 0; i < outer_loop_size; i++) {
      unsigned j;
      for (j = 0; j < inner_loop_size; j++) {
         if (tlb) {
            tmp[j * page_size] = i;
         } else {
            (void) time (NULL); /* this is a system call */
         }
      }
      RVS_Ipoint (1979);
      check ++;
   }
   RVS_Output ();

   if (tmp) {
      free (tmp);
   }

   if ((fd = fopen ("3" TRACE_NAME, "rb")) == NULL) {
      perror ("reading 3" TRACE_NAME);
      return 1;
   }

   while ((fread (&id, 4, 1, fd) == 1) && (fread (&tstamp, 4, 1, fd) == 1)) {
      switch (id) {
         case 1979:
            check --;
            break;
         case RVS_END_WRITE:
            end_write ++;
            if (buffer_level > max_buffer_level) {
               max_buffer_level = buffer_level;
            }
            buffer_level = 0;
            break;
         case RVS_OI_SIGNAL:
            oi_signal ++;
            buffer_level ++;
            break;
         case RVS_TLB_EVENT:
            tlb_count ++;
            buffer_level ++;
            break;
         case RVS_SYS_EVENT:
            sys_count ++;
            buffer_level ++;
            break;
         default:
            others ++;
            buffer_level ++;
            if (id < RVS_ENTRY_MASK) {
               fputs ("\nUnexpected low-value ipoint in trace (2)\n", stderr);
               return 1;
            }
            break;
      }
   }
   fclose (fd);
   if (detail) {
      printf ("\nmeasuring do_test_kernel_flush (%u)\n"
         "  end_write = %u oi_signal = %u tlb_count = %u\n"
         "  sys_count = %u others = %u max_buffer_level = %u\n",
         tlb, end_write, oi_signal, tlb_count, sys_count,
         others, max_buffer_level);
   }

   if (oi_signal < expected_minimum) {
      fprintf (stderr, "\nunexpectedly small number of kernel overflow "
         "signals (%u < %u, TLB = %u)\n", oi_signal, expected_minimum, tlb);
      return 1;
   }
   if (end_write < oi_signal) {
      fprintf (stderr, "\ninconsistent oi_signal/end write (%u, %u)\n", oi_signal, end_write);
      return 1;
   }
   if (check != 0) {
      fputs ("\nUnexpected number of '1979' test ipoints in trace\n", stderr);
      return 1;
   }
   return 0;
}


