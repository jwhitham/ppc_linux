#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/time.h>
#include <stdlib.h>
#include "librvs.h"


int main (void)
{
   unsigned       i;
   unsigned       kernel_flag, bytes;
   FILE *         fd;
   uint32_t       tstamp = 0;
   uint32_t       id = 0;
   uint64_t       offset = 0;
   uint32_t       old_tstamp = 0;
   uint64_t       fixed_tstamp = 0;
   uint64_t       prev_tstamp = 0;
   uint64_t       jump_tstamp = 0;

   printf ("Calling RVS_Init()\n");
   RVS_Init();

   printf ("Starting test:\n");
   fflush (stdout);

   for (i = 0; i < 1000000; i++) {
      RVS_Ipoint (9999);
   }

   RVS_Output();

   if ((fd = fopen ("trace.bin", "rb")) == NULL) {
      perror ("reading trace.bin");
      return 1;
   }
   bytes = kernel_flag = 0;

   while ((fread (&id, 4, 1, fd) == 1) && (fread (&tstamp, 4, 1, fd) == 1)) {
      if (tstamp < old_tstamp) {
         offset += 1ULL << 32ULL;
      }
      fixed_tstamp = tstamp + offset;
      old_tstamp = tstamp;
      bytes += 8;

      switch (id) {
         case RVS_BEGIN_WRITE:
         case RVS_END_WRITE:
            printf ("Unexpected RVS_BEGIN_WRITE/RVS_END_WRITE markers in short trace\n");
            return 1;
         case RVS_SWITCH_FROM:
         case RVS_TIMER_ENTRY:
         case RVS_IRQ_ENTRY:
         case RVS_SYS_ENTRY:
         case RVS_IRQ_EXIT:
         case RVS_TIMER_EXIT:
         case RVS_SYS_EXIT:
         case RVS_SWITCH_TO:
            if (!kernel_flag) {
               printf ("%1.0f: kernel\n", (double) fixed_tstamp);
            }
            kernel_flag = 1;
            break;
         case 9999:
            if (!kernel_flag) {
               uint64_t delta = fixed_tstamp - prev_tstamp;

               if (delta > 50) {
                  printf ("%1.0f: jump after %u bytes %1.0f cycles %1.0f delta\n",
                          (double) fixed_tstamp,
                          bytes, (double) (fixed_tstamp - jump_tstamp), (double) delta);
                  jump_tstamp = fixed_tstamp;
                  bytes = 0;
               }
            }
            kernel_flag = 0;
            prev_tstamp = fixed_tstamp;
            break;
         default:
            printf ("Invalid ipoint id %u\n", id);
            return 1;
      }
   }
   fclose (fd);
   return 0;
}

