#include <sys/syscall.h>
#include <unistd.h>
#include <stdint.h>
#include <fcntl.h>
#include "librvs.h"


const unsigned LOOP_SIZE = 10000;
const unsigned IPOINT_COUNT = 4;

int nolib_syscall (int nr, ...);

void testing (double * ptr);

void exit (int rc)
{
   nolib_syscall (__NR_exit, rc, 0, 0);
   while (1) {}
}

void print (const char * text)
{
   unsigned sz;

   for (sz = 0; text[sz] != '\0'; sz++) {}

   nolib_syscall (__NR_write, STDOUT_FILENO, text, sz);
}

void printdec (unsigned x)
{
   char ch;
   if (x >= 10) {
      printdec (x / 10);
   }
   ch = '0' + (x % 10);
   nolib_syscall (__NR_write, STDOUT_FILENO, &ch, 1);
}

int _start (void)
{
   unsigned       i;
   unsigned       kernel_flag, bytes;
   unsigned       sd_count = 0;
   unsigned       pf_count = 0;
   unsigned       tstamp = 0;
   unsigned       id = 0;
   uint64_t       offset = 0;
   unsigned       old_tstamp = 0;
   unsigned       total_time[IPOINT_COUNT];
   unsigned       min_time[IPOINT_COUNT];
   unsigned       max_time[IPOINT_COUNT];
   unsigned       count[IPOINT_COUNT];
   uint64_t       fixed_tstamp = 0;
   uint64_t       prev_tstamp = 0;
   double         buffer[3];
   int            fd;
   char *         ptr;

   print ("Calling RVS_Init()\n");
   RVS_Init();

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

   RVS_Output();

   fd = nolib_syscall (__NR_open, "trace.bin", O_RDONLY, 0);
   if (fd < 0) {
      print ("unable to read trace.bin\n");
      exit (1);
   }
   bytes = kernel_flag = 0;

   while ((4 == nolib_syscall (__NR_read, fd, &id, 4))
   && (4 == nolib_syscall (__NR_read, fd, &tstamp, 4))) {
      if (tstamp < old_tstamp) {
         offset += 1ULL << 32ULL;
      }
      fixed_tstamp = tstamp + offset;
      old_tstamp = tstamp;
      bytes += 8;

      switch (id) {
         case RVS_MATHEMU_ENTRY:
         case RVS_MATHEMU_EXIT:
            print ("Unexpected MATHEMU ipoint: FP instruction was emulated.\n");
            exit (1);
         case RVS_BEGIN_WRITE:
         case RVS_END_WRITE:
            print ("Unexpected RVS_BEGIN_WRITE/RVS_END_WRITE markers in short trace\n");
            exit (1);
         case RVS_PFAULT_ENTRY:
         case RVS_PFAULT_EXIT:
            if (!kernel_flag) {
               pf_count ++;
            }
            kernel_flag = 1;
            break;
         case RVS_TIMER_ENTRY:
         case RVS_IRQ_ENTRY:
         case RVS_SYS_ENTRY:
         case RVS_IRQ_EXIT:
         case RVS_TIMER_EXIT:
         case RVS_SYS_EXIT:
         case RVS_SWITCH_FROM:
         case RVS_SWITCH_TO:
            if (!kernel_flag) {
               print ("kernel event: ");
               printdec (id);
               print ("\n");
            }
            kernel_flag = 1;
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
         default:
            print ("Invalid ipoint id ");
            printdec (id);
            print ("\n");
            exit (1);
      }
   }
   nolib_syscall (__NR_close, fd, 0, 0);
   printdec (pf_count);
   print (" page faults\n");

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

   exit (0);
   return 0;
}

