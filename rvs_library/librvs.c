/*=========================================================================
 * RapiTime    : a tool for Measurement-Based Execution Time Analysis
 * Module      : rvs_library for PPC Linux
 * File        : librvs.c
 * Description :
 * Kernel and user space execution time tracing for Linux.
 *
 * Copyright (c) 2016 Rapita Systems Ltd.               All rights reserved
 *=========================================================================
*/

#ifndef CONFIG_PPC
#error "CONFIG_PPC is not set. This code only supports PPC."
#endif

#include <stdint.h>
#include <rvs.h>
#include "librvs.h"

#include "ppc_linux.h"

#ifndef NULL
#define NULL ((void*)0)
#endif

#define RVS_UENTRY_SIZE       8     /* must be power of 2 */
#define PAGE_SIZE             4096  /* must match system page size */
#define USER_BUFFER_SIZE      (1 << 25) /* 32M entries */
#define SMALL_USER_BUFFER_SIZE (1 << 14) /* 16K entries (must be less than USER_BUFFER_SIZE) */
#define KERNEL_BUFFER_SIZE    (128 * 1024) /* 128K entries */
#define MERGED_BUFFER_SIZE    (USER_BUFFER_SIZE + KERNEL_BUFFER_SIZE)


struct rvs_uentry {
   unsigned id;
   unsigned tstamp;
};

static int32_t rvs_device_fd = -1;
static int32_t rvs_trace_fd = -1;
static uint32_t segfault_handler_installed = 0;
static struct rvs_uentry kernel_buffer[KERNEL_BUFFER_SIZE];
static struct rvs_uentry merged_buffer[MERGED_BUFFER_SIZE];
static uint8_t unaligned_user_start[RVS_UENTRY_SIZE * USER_BUFFER_SIZE];
static struct rvs_uentry * merged_pos = NULL;
static uint32_t kernel_loaded = 0, kernel_read = 0;
static unsigned * trigger_page = NULL;

/* Initial value of this pointer is set so that there is
 * somewhere valid for ipoints to go, if the user calls RVS_Ipoint before
 * calling RVS_Init. These ipoints are lost. */
unsigned * rvs_user_trace_write_pointer = (unsigned *) &merged_buffer[0];


/* reset the buffer pointer to the beginning of the user buffer */
static void reset_buffer_pointer (void)
{
   intptr_t p = (intptr_t) &unaligned_user_start[0];

   /* Align to beginning of next page */
   p |= (PAGE_SIZE - 1);
   p ++;
   /* Offset by -4, because of stwu instruction */
   p -= 4;
   rvs_user_trace_write_pointer = (unsigned *) p;
}

/* get trace data from kernel (while merging) */
static void download_kernel_trace (void)
{
   int32_t           r;

   if (kernel_loaded > kernel_read) {
      /* buffer not consumed, don't download again yet */
      return;
   }

   r = ppc_ioctl (rvs_device_fd, RVS_DISABLE, 0);
   if (r < 0) {
      ppc_fatal_error("rvslib: failed to disable kernel tracing before read");
   }

   r = ppc_read (rvs_device_fd, kernel_buffer, KERNEL_BUFFER_SIZE * sizeof(struct rvs_uentry));
   if (r < 0) {
      ppc_fatal_error ("rvslib: download_kernel_trace: read error");
   }
   if ((r % sizeof(struct rvs_uentry)) != 0) {
      ppc_fatal_error ("rvslib: unexpected kernel behaviour (1)");
   }
   if (r > (KERNEL_BUFFER_SIZE * sizeof(struct rvs_uentry))) {
      ppc_fatal_error ("rvslib: unexpected kernel behaviour (1)");
   }
   kernel_loaded = r / sizeof(struct rvs_uentry);
   kernel_read = 0;

   r = ppc_ioctl (rvs_device_fd, RVS_ENABLE, 0);
   if (r < 0) {
      ppc_fatal_error("rvslib: failed to re-enable kernel tracing");
   }
}

/* write trace buffer to disk (while merging) */
static void flush_merged_buffer (int force)
{
   int32_t total = 0;
   int32_t todo = 0;

   if (force || (merged_pos == &merged_buffer[MERGED_BUFFER_SIZE])) {
      todo = (merged_pos - merged_buffer) * sizeof (struct rvs_uentry);
      if (todo) {
         total = ppc_write (rvs_trace_fd, merged_buffer, todo);
         if (total != todo) {
            ppc_fatal_error ("rvslib: unable to write all trace elements to disk");
         }
      } 
      merged_pos = &merged_buffer[0];
   }
}

/* do merge and flush trace to disk */
static void merge_buffers_now (void)
{
   struct rvs_uentry * user_start;
   struct rvs_uentry * user_end;
   uint32_t user_loaded = 0, user_read = 0;
   struct rvs_uentry write_event[2];

   /* Notify start of write operation */
   write_event[0].id = RVS_BEGIN_WRITE;
   write_event[0].tstamp = rvs_get_cycles ();
   write_event[1].id = RVS_END_WRITE;

   /* find start/end of user buffer */
   user_end =  /* offset by +4 because of stwu instruction: */
      (struct rvs_uentry *) (rvs_user_trace_write_pointer + 1);
   reset_buffer_pointer ();
   user_start =
      (struct rvs_uentry *) (rvs_user_trace_write_pointer + 1);

   /* Merge user and kernel buffers */
   merged_pos = &merged_buffer[0];
   user_loaded = user_end - user_start;
   user_read = 0;
   flush_merged_buffer (1);
   download_kernel_trace ();

   /* While both buffers contain data */
   while ((kernel_read < kernel_loaded) && (user_read < user_loaded)) {
      if (rvs_time_before (kernel_buffer[kernel_read].tstamp, user_start[user_read].tstamp)) {
         (* merged_pos) = kernel_buffer[kernel_read];
         kernel_read++;
         merged_pos++;
         flush_merged_buffer (0);
         download_kernel_trace ();
      } else {
         (* merged_pos) = user_start[user_read];
         user_read++;
         merged_pos++;
      }
   }

   /* One buffer is empty now */
   while (kernel_read < kernel_loaded) {
      (* merged_pos) = kernel_buffer[kernel_read];
      kernel_read++;
      merged_pos++;
   }

   while (user_read < user_loaded) {
      (* merged_pos) = user_start[user_read];
      user_read++;
      merged_pos++;
   }

   flush_merged_buffer (1);

   /* Ready for more trace data now */
   reset_buffer_pointer ();

   /* notify time spent writing and merging */
   write_event[1].tstamp = rvs_get_cycles ();
   if (sizeof (write_event) != ppc_write (rvs_trace_fd, write_event, sizeof (write_event))) {
      ppc_fatal_error ("rvslib: unable to write begin/end write event to disk");
   }
}

/* signal handler: invoked when the RVS_Ipoint function attempts to write
 * to the trigger page at the end of the user trace buffer */
void rvs_segfault_signal (unsigned * trigger_pc, unsigned * gregs, unsigned * addr)
{
   /* check that this segfault is in the ipoint routine (possibly inlined)
    * by inspecting the machine code: 

         180051c:       95 49 00 04     stwu    r10,4(r9) <-- may write to trigger page
         1800520:       7d 0e 82 a6     mfspr   r8,526
         1800524:       95 09 00 04     stwu    r8,4(r9)

      (addresses and register numbers will be different)
   */

   unsigned store_id = trigger_pc[0];
   unsigned get_timestamp = trigger_pc[1];
   unsigned store_timestamp = trigger_pc[2];

   if (((get_timestamp & 0xffff) == 0x82a6)
   && ((get_timestamp >> 26) == 0x1f)
   && ((store_id & 0xffff) == 0x0004) 
   && ((store_id >> 26) == 0x25)
   && ((store_timestamp & 0xffff) == 0x0004) 
   && ((store_timestamp >> 26) == 0x25)
   && (rvs_device_fd >= 0)) {

      /* This segfault was generated by the expected instruction: */
      unsigned ptr_reg = (store_id >> 16) & 0x1f;
      unsigned * ptr_val = (unsigned *) ((intptr_t) gregs[ptr_reg]);
      intptr_t pos = (intptr_t) (ptr_val + 1);
      intptr_t fault_address = (intptr_t) addr;

      /* Debug error conditions caused by SIGSEGV being generated
       * by inconsistent addresses */

      if (fault_address != pos) {
         if (fault_address == 0) {
            /* "Thus, if a general register is required by the instruction, you could use
             * either the "r" or "b" constraint. The "b" constraint is of interest, 
             * because many instructions use the designation of register 0 specially –-
             * a designation of register 0 does not mean that r0 is used, but instead a
             * literal value of 0."
             * ... If you get here, you may have an "stw rX, 0(0)".
             */
            ppc_fatal_error ("fault_address seems to be 0");
         }
         ppc_fatal_error ("faulting instruction behaved unexpectedly");
      }
      if ((ptr_val + 1) != trigger_page) {
         intptr_t left = (intptr_t) &unaligned_user_start[0];
         intptr_t right = (intptr_t) &unaligned_user_start[sizeof (unaligned_user_start)];

         if (pos < left) {
            ppc_fatal_error ("segfault pointer is below the user buffer");
         }
         if (pos >= right) {
            ppc_fatal_error ("segfault pointer is above the user buffer");
         }
         if (pos & (RVS_UENTRY_SIZE - 1)) {
            ppc_fatal_error ("segfault pointer is not correctly aligned");
         }
         ppc_fatal_error ("segfault pointer is not equal to the start of the trigger page");
      }

      /* write trace buffer to disk */
      rvs_user_trace_write_pointer = ptr_val;
      merge_buffers_now ();

      /* continue at the beginning of the buffer: we rerun the store instruction
       * with ptr_val reset to beginning of buffer. */
      gregs[ptr_reg] = (intptr_t) rvs_user_trace_write_pointer;
      return;
   }

   /* some other segfault: reinstall old signal handler */
   ppc_restore_signal_handler ();

   /* return to code: segfault will happen again immediately */
}


/****************************************************************************/
/* Exported functions                                                       */
/****************************************************************************/

/* Send output to default trace file, "trace.bin",
 * truncating that file if it already exists. Call
 * RVS_Init (or RVS_Init_Ex) as soon as possible
 * after program startup. */
void RVS_Init(void)
{
   RVS_Init_Ex ("trace.bin", 0);
}

/* Send output to any trace file, truncating the file if
 * it already exists. Call RVS_Init_Ex as soon as possible
 * after program startup. */
void RVS_Init_Ex (const char * trace_file_name, unsigned flags)
{
   int32_t r, v;

   if (rvs_device_fd >= 0) {
      /* RVS_Init_Ex has been called twice. Be tolerant. */
      return;
   }

   /* Open and configure kernel driver */
   rvs_device_fd = ppc_open_rdwr (RVS_FILE_NAME);
   if (rvs_device_fd < 0) {
      ppc_fatal_error ("RVS_Init: could not open " RVS_FILE_NAME);
   }
   if ((USER_BUFFER_SIZE < 4) || (KERNEL_BUFFER_SIZE < 4)) {
      ppc_fatal_error ("buffers are too small");
   }
   v = 0;
   r = ppc_ioctl (rvs_device_fd, RVS_GET_VERSION, &v);
   if (r < 0) {
      ppc_fatal_error("RVS_Init: failed to get version");
   }
   if (v != RVS_API_VERSION) {
      ppc_fatal_error("RVS_Init: kernel module API version does not match librvs.a");
   }
   r = ppc_ioctl (rvs_device_fd, RVS_RESET, 0);
   if (r < 0) {
      ppc_fatal_error("RVS_Init: failed to reset module");
   }
   r = ppc_ioctl (rvs_device_fd, RVS_ENABLE, 0);
   if (r < 0) {
      ppc_fatal_error("RVS_Init: failed to enable kernel tracing");
   }

   /* Open trace output file (truncating it) */
   rvs_trace_fd = ppc_creat (trace_file_name);
   if (rvs_trace_fd < 0) {
      ppc_fatal_error ("RVS_Init: could not create trace output file");
   }

   /* Mark a trigger page at end of user trace buffer as not writable */
   {
      intptr_t p = (intptr_t) &unaligned_user_start[0];
      
      if (flags & RVS_SMALL_BUFFER) {
         p += SMALL_USER_BUFFER_SIZE * RVS_UENTRY_SIZE;
      } else {
         p += USER_BUFFER_SIZE * RVS_UENTRY_SIZE;
      }
      /* Why not just mark the final rvs_uentry as readonly? Because that would mean
       * marking the whole of its page, and other variables will be stored there.
       * Have to round down, and waste 1 - 2 pages of rvs_uentries. */
      p -= PAGE_SIZE;
      p &= ~ (PAGE_SIZE - 1);
      trigger_page = (unsigned *) p;

      if (ppc_mprotect_read (trigger_page, PAGE_SIZE) != 0) {
         ppc_fatal_error ("RVS_Init: mprotect user trace buffer failed");
      }
   }

   /* Install segfault handler to catch writes to the trigger page */
   if (!segfault_handler_installed) {
      ppc_install_signal_handler ();
      segfault_handler_installed = 1;
   }

   /* Reset trace buffers */
   reset_buffer_pointer ();
}

/* Force write to disk and close trace file */
void RVS_Output (void)
{
   int32_t r;

   if (rvs_device_fd < 0) {
      /* RVS_Output called twice, or RVS_Output 
       * called without first calling RVS_Init. Be tolerant. */
      return;
   }
      
   r = ppc_ioctl (rvs_device_fd, RVS_DISABLE, 0);
   if (r < 0) {
      ppc_fatal_error ("RVS_Output: failed to disable kernel tracing");
   }

   if (rvs_trace_fd >= 0) {
      merge_buffers_now ();
      ppc_close (rvs_trace_fd);
      rvs_trace_fd = -1;
   }
   if (trigger_page) {
      if (ppc_mprotect_rdwr (trigger_page, PAGE_SIZE) != 0) {
         ppc_fatal_error ("RVS_Output: mprotect user trace buffer failed");
      }
      trigger_page = NULL;
   }
   merged_pos = NULL;
   reset_buffer_pointer ();

   if (rvs_device_fd >= 0) {
      ppc_close (rvs_device_fd);
      rvs_device_fd = -1;
   }
}
