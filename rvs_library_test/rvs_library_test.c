#include <stdio.h>
#include <stdint.h>
#include <sys/time.h>
#include <stdlib.h>
#include "librvs.h"

/* Markers to indicate time spent writing trace data */
#define RVS_BEGIN_WRITE		0xfffffff1
#define RVS_END_WRITE		0xfffffff0

/* Context switch markers in the trace. */
#define RVS_SWITCH_FROM		0xffffffff
#define RVS_SWITCH_TO		0xfffffffe

int main (void)
{
   unsigned       i, j;
   struct timeval t0;
   struct timeval t1;
   FILE *         fd;
   uint32_t       tstamp = 0;
   uint32_t       id = 0;
   uint32_t       kernel_entries = 0;
   uint32_t       old_tstamp = 0;
   uint64_t       offset = 0;
   uint64_t       fixed_tstamp = 0;
   uint64_t       enter_kernel_tstamp = 0;
   uint64_t       start_tstamp = 0;
   uint64_t       total_time = 0;
   uint64_t       total_kernel_time = 0;
   uint64_t       expected_time = 0;
   double         deinstrument = 0.0;

   printf ("Calling RVS_Init()\n");
   RVS_Init();

   printf ("Starting test\n");
   fflush (stdout);
   gettimeofday (&t0, NULL);
   do {
      for (i = 0; i < 10; i++) {
         RVS_Ipoint (10);
         /* busy wait for 100e6 clock cycles */
         for (j = 0; j < 50000000; j++) {
            asm ("nop");
         }
         RVS_Ipoint (11);
         expected_time = j * 2;
      }
      gettimeofday (&t1, NULL);
   } while (t1.tv_sec < (t0.tv_sec + 2));

   RVS_Output();
   printf ("Examining results\n");
   fd = fopen ("trace.bin", "rb");
   if (!fd) {
      printf ("Failed, trace.bin not found\n");
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
         case RVS_END_WRITE:
            printf ("Unexpected RVS_BEGIN_WRITE/RVS_END_WRITE markers in short trace\n");
            return 1;
         case RVS_SWITCH_FROM:
            enter_kernel_tstamp = fixed_tstamp;
            kernel_entries ++;
            break;
         case RVS_SWITCH_TO:
            printf ("kernel entry: %u cycles\n", (unsigned) (fixed_tstamp - enter_kernel_tstamp));
            total_kernel_time += fixed_tstamp - enter_kernel_tstamp;
            break;
         case 10:
            kernel_entries = 0;
            total_kernel_time = 0;
            start_tstamp = fixed_tstamp;
            break;
         case 11:
            if (kernel_entries != 0) {
               printf ("%u kernel entries, ", kernel_entries);

               total_time = fixed_tstamp - start_tstamp;
               printf ("%1.0f clock cycles, ", (double) total_time);

               if (total_time < expected_time) {
                  printf ("Failed, total time was less than %1.0f\n", (double) expected_time);
                  return 1;
               }

               total_time -= total_kernel_time;
               printf ("minus %1.0f in kernel, ", (double) total_kernel_time);

               if (total_time < expected_time) {
                  printf ("Failed, total time was less than %1.0f\n", (double) expected_time);
                  return 1;
               }

               deinstrument = (double) (total_time - expected_time) / (double) kernel_entries;
               printf ("kernel entry overhead seems to be %1.0f\n", deinstrument);
            }
            break;
         default:
            printf ("Unexpected ipoint id %u\n", id);
            return 1;
      }
   }
   fclose (fd);
   printf ("Test passed\n");
   return 0;
}

