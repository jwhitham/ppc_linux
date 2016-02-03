/*=========================================================================
 * RapiTime    : a tool for Measurement-Based Execution Time Analysis
 * Module      : rvs_library for PPC Linux
 * File        : rvs_loop_test.c
 * Description :
 * Test timing capture with some simple loops: this test was originally
 * created to search for cases where the kernel is doing something that
 * takes a long time *and* we're not aware of it.
 *
 * Copyright (c) 2016 Rapita Systems Ltd.               All rights reserved
 *=========================================================================
*/
#include <unistd.h>
#include <stdint.h>
#include <fcntl.h>
#include "librvs.h"
#include "ppc_linux.h"


const unsigned LOOP_SIZE = 10000;
const unsigned IPOINT_COUNT = 4;

void testing (double * ptr);

void print (const char * text)
{
   unsigned sz;

   for (sz = 0; text[sz] != '\0'; sz++) {}

   ppc_write (STDOUT_FILENO, text, sz);
}

void printdec (unsigned x)
{
   char ch;
   if (x >= 10) {
      printdec (x / 10);
   }
   ch = '0' + (x % 10);
   ppc_write (STDOUT_FILENO, &ch, 1);
}

int main (void)
{
   unsigned       i;
   unsigned       kernel_flag, bytes;
   unsigned       sd_count = 0;
   unsigned       tstamp = 0;
   unsigned       four = 0;
   unsigned       five = 0;
   unsigned       six = 0;
   unsigned       id = 0;
   uint64_t       offset = 0;
   unsigned       old_tstamp = 0;
   unsigned       write_count = 0;
   unsigned       total_time[IPOINT_COUNT];
   unsigned       min_time[IPOINT_COUNT];
   unsigned       max_time[IPOINT_COUNT];
   unsigned       count[IPOINT_COUNT];
   uint64_t       fixed_tstamp = 0;
   uint64_t       prev_tstamp = 0;
   double         buffer[3];
   unsigned       event_counter[RVS_ENTRY_COUNT];
   int            fd;
   char *         ptr;

   /* Test: ipoints before RVS_Init are ignored */
   RVS_Ipoint (1);
   RVS_Ipoint (2);
   RVS_Ipoint (3);

   print ("Calling RVS_Init()\n");
   RVS_Init ();
   RVS_Ipoint (4);

   /* Test: calling RVS_Init() twice is ignored */
   RVS_Init ();
   RVS_Ipoint (5);
   RVS_Init ();

   for (i = 0; i < IPOINT_COUNT; i++) {
      total_time[i] = max_time[i] = count[i] = 0;
      min_time[i] = ~0;
   }

   print ("Starting test:\n");

   // Alt 0: nop
   buffer[0] = 1.0;
   RVS_Ipoint (0);
   for (i = 0; i < LOOP_SIZE; i++) {
      asm volatile ("nop");
      RVS_Ipoint (1000);
   }
   // Alt 1: misaligned pointers
   ptr = ((char *) buffer) + 1;
   ((double *) ptr)[0] = 1.0;
   RVS_Ipoint (0);
   for (i = 0; i < LOOP_SIZE; i++) {
      testing ((double *) ptr);
      RVS_Ipoint (1001);
   }
   // Alt 2: aligned pointers
   ptr = ((char *) buffer) + 0;
   ((double *) ptr)[0] = 1.0;
   RVS_Ipoint (0);
   for (i = 0; i < LOOP_SIZE; i++) {
      testing ((double *) ptr);
      RVS_Ipoint (1002);
   }
   // Alt 3: direct operation on double
   buffer[0] = 1.0;
   RVS_Ipoint (0);
   for (i = 0; i < LOOP_SIZE; i++) {
      buffer[0] += 0.5;
      buffer[0] *= 0.5;
      RVS_Ipoint (1003);
   }
   RVS_Ipoint (0);
   RVS_Ipoint (6);

   RVS_Output();
   /* Test: multiple calls to RVS_Output/ipoints after RVS_Ipoint */
   RVS_Ipoint (1);
   RVS_Output();
   RVS_Output();
   RVS_Ipoint (2);
   RVS_Ipoint (3);

   fd = ppc_open_rdwr ("trace.bin");
   if (fd < 0) {
      print ("unable to read trace.bin\n");
      ppc_exit (1);
   }
   bytes = kernel_flag = 0;
   for (i = 0; i < RVS_ENTRY_COUNT; i++) {
      event_counter[i] = 0;
   }

   while ((4 == ppc_read (fd, &id, 4))
   && (4 == ppc_read (fd, &tstamp, 4))) {
      if (tstamp < old_tstamp) {
         offset += 1ULL << 32ULL;
      }
      fixed_tstamp = tstamp + offset;
      old_tstamp = tstamp;
      bytes += 8;

      switch (id) {
         case RVS_BEGIN_WRITE:
            if (write_count != 0) {
               print ("Unexpected RVS_BEGIN_WRITE/RVS_END_WRITE markers in short trace\n");
               ppc_exit (1);
            }
            write_count ++;
            break;
         case RVS_END_WRITE:
            break;
         case 0:
            /* loop to loop transition */
            kernel_flag = 1;
            break;
         case 1000:
         case 1001:
         case 1002:
         case 1003:
            if (!kernel_flag) {
               uint64_t delta = fixed_tstamp - prev_tstamp;

               sd_count ++;
               id -= 1000;
               if (delta > max_time[id]) {
                  max_time[id] = delta;
               }
               if (delta < min_time[id]) {
                  min_time[id] = delta;
               }
               total_time[id] += delta;
               count[id] ++;
            }
            kernel_flag = 0;
            prev_tstamp = fixed_tstamp;
            break;
         case 4:
            four ++;
            break;
         case 5:
            five ++;
            break;
         case 6:
            six ++;
            break;
         default:
            if ((id & RVS_ENTRY_MASK) != RVS_ENTRY_MASK) {
               print ("Invalid ipoint id ");
               printdec (id);
               print ("\n");
               ppc_exit (1);
               return 1;
            }
            event_counter[id & (RVS_ENTRY_COUNT - 1)] ++;
            kernel_flag = 1;
            break;
      }
   }
   ppc_close (fd);

   for (i = 0; i < IPOINT_COUNT; i++) {
      print ("Alt");
      printdec (i);
      print (": min ET is ");
      printdec (min_time[i]);
      print (" max ET is ");
      printdec (max_time[i]);
      print (" mean ET is ");
      printdec (total_time[i] / count[i]);
      print ("\n");
   }
   for (i = 0; i < RVS_ENTRY_COUNT; i++) {
      if (event_counter[i]) {
         print ("  interruption type ");
         printdec (i);
         print (" count ");
         printdec (event_counter[i]);
         print ("\n");
      }
   }

   if ((four != 1) || (five != 1) || (six != 1)) {
      /* This would indicate bad behaviour after RVS_Init/RVS_Output */
      print ("Invalid count for special ipoints 4, 5, 6\n");
      ppc_exit (1);
   }

   print ("Test ok\n");
   ppc_exit (0);
   return 0;
}

int _start (void)
{
   return main ();
}

