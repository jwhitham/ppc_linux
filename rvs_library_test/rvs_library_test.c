#include <stdio.h>
#include <stdint.h>
#include <sys/time.h>
#include <stdlib.h>
#include "librvs.h"

int main (void)
{
   unsigned  i, j;
   struct timeval t0;
   struct timeval t1;

   RVS_Init();

   gettimeofday (&t0, NULL);
   do {
      for (i = 10; i < 20; i += 2) {
         RVS_Ipoint (i);
         for (j = 0; j < 1000000; j++) { /* busy wait for 2000000 clock cycles */
            asm ("nop");
         }
         RVS_Ipoint (i + 1);
      }
      gettimeofday (&t1, NULL);
   } while (t1.tv_sec < (t0.tv_sec + 2));

   RVS_Output();
   return 0;
}

