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

#define PAGE_SIZE             4096  /* must match system page size */
#define USER_BUFFER_SIZE      (1 << 25) /* 32M entries */
#define SMALL_USER_BUFFER_SIZE (1 << 14) /* 16K entries (must be less than USER_BUFFER_SIZE) */
#define KERNEL_BUFFER_SIZE    (128 * 1024) /* 128K entries */
#define OUTPUT_BUFFER_SIZE    (16 * 1024) /* 16K entries: the most we write to disk at a time */

#define NUM_TRIGGER_PAGES     3
#define FORCE_FLUSH_TRIGGER_A 0
#define FORCE_FLUSH_TRIGGER_B 1
#define END_BUF_TRIGGER       2 

static int32_t rvs_device_fd = -1;
static int32_t rvs_trace_fd = -1;
static uint32_t segfault_handler_installed = 0;
static struct rvs_uentry kernel_buffer[KERNEL_BUFFER_SIZE];
static struct rvs_uentry output_buffer[OUTPUT_BUFFER_SIZE];
static uint8_t unaligned_user_start[RVS_UENTRY_SIZE * USER_BUFFER_SIZE];
static unsigned * trigger_page[NUM_TRIGGER_PAGES] = {NULL, NULL, NULL};

/* Initial value of this pointer is set so that there is
 * somewhere valid for ipoints to go, if the user calls RVS_Ipoint before
 * calling RVS_Init. These ipoints are lost. */
struct rvs_uentry_opaque * rvs_user_trace_write_pointer =
    (struct rvs_uentry_opaque *) &output_buffer[0];


/* reset the buffer pointer to the beginning of the user buffer */
static void reset_buffer_pointer (void)
{
   intptr_t p = (intptr_t) &unaligned_user_start[0];

   /* Align to beginning of next page */
   p |= (PAGE_SIZE - 1);
   p ++;
   /* Offset by -4, because of stwu instruction */
   p -= 4;
   rvs_user_trace_write_pointer = (struct rvs_uentry_opaque *) p;
}

/* check write pointer is appropriately positioned and aligned */
static void check_write_pos (intptr_t pos)
{
   intptr_t left = (intptr_t) &unaligned_user_start[0];
   intptr_t right = (intptr_t) &unaligned_user_start[sizeof (unaligned_user_start)];

   if ((pos % RVS_UENTRY_SIZE) != 0) {
      ppc_fatal_error ("rvslib: write pointer is not correctly aligned");
   }
   if (pos < left) {
      ppc_fatal_error ("rvslib: write pointer is below the user buffer");
   }
   if (pos >= right) {
      ppc_fatal_error ("rvslib: write pointer is above the user buffer");
   }
}

/* uninstall FORCE_FLUSH_TRIGGER pages A and B */
static void uninstall_trigger_AB (void)
{
   if (trigger_page[FORCE_FLUSH_TRIGGER_A]) {
      if (ppc_mprotect_rdwr (trigger_page[FORCE_FLUSH_TRIGGER_A], PAGE_SIZE) != 0) {
         goto fail;
      }
      trigger_page[FORCE_FLUSH_TRIGGER_A] = NULL;
   }
   if (trigger_page[FORCE_FLUSH_TRIGGER_B]) {
      if (ppc_mprotect_rdwr (trigger_page[FORCE_FLUSH_TRIGGER_B], PAGE_SIZE) != 0) {
         goto fail;
      }
      trigger_page[FORCE_FLUSH_TRIGGER_B] = NULL;
   }
   return;
fail:
   ppc_fatal_error ("unable to uninstall force flush trigger");
}

/* download kernel trace, do merge, and flush trace to disk */
static void merge_buffers_now (unsigned prepend_oi_signal, unsigned reenable)
{
   unsigned start_tstamp = 0;
   struct rvs_uentry * user_start = NULL;
   struct rvs_uentry * user_ptr = NULL;
   struct rvs_uentry * user_end = NULL;
   struct rvs_uentry * kernel_start = NULL;
   struct rvs_uentry * kernel_ptr = NULL;
   struct rvs_uentry * kernel_end = NULL;
   struct rvs_uentry * output_start = NULL;
   struct rvs_uentry * output_ptr = NULL;
   struct rvs_uentry * output_end = NULL;
   int r, todo;

   /* uninstall special trigger pages if present */
   uninstall_trigger_AB ();

   /* kernel: stop writing! */
   r = ppc_ioctl (rvs_device_fd, RVS_DISABLE, 0);
   if (r < 0) {
      ppc_fatal_error("rvslib: failed to disable kernel tracing before write to disk");
   }
   start_tstamp = rvs_get_cycles();

   /* download new data from kernel - we read the whole kernel buffer in one go */
   r = ppc_read (rvs_device_fd, kernel_buffer, KERNEL_BUFFER_SIZE * sizeof(struct rvs_uentry));
   if (r < 0) {
      ppc_fatal_error ("rvslib: download_kernel_trace: read error (check 'dmesg' output)");
   }
   if ((r % sizeof (struct rvs_uentry)) != 0) {
      ppc_fatal_error ("rvslib: unexpected kernel behaviour (1)");
   }
   if (r > (KERNEL_BUFFER_SIZE * sizeof(struct rvs_uentry))) {
      ppc_fatal_error ("rvslib: unexpected kernel behaviour (2)");
   }
   kernel_start = &kernel_buffer[0];
   kernel_end = &kernel_buffer[r / sizeof (struct rvs_uentry)];

   /* output buffer */
   output_start = &output_buffer[0];
   output_end = &output_buffer[OUTPUT_BUFFER_SIZE];

   /* find start/end of user buffer */
   user_end =  /* offset by +4 because of stwu instruction: */
      (struct rvs_uentry *) (((intptr_t) rvs_user_trace_write_pointer) + 4);
   reset_buffer_pointer ();
   user_start =
      (struct rvs_uentry *) (((intptr_t) rvs_user_trace_write_pointer) + 4);
   check_write_pos ((intptr_t) user_start);
   check_write_pos ((intptr_t) user_end);

   /* merge loop */
   kernel_ptr = kernel_start;
   user_ptr = user_start;

   /* output loop (repeatedly output up to 16K trace records) */
   do {

      /* merge loops: */
      for (output_ptr = output_start;
            (output_ptr < output_end)
            && (user_ptr < user_end)
            && (kernel_ptr < kernel_end); output_ptr ++) {

         if (rvs_time_before (user_ptr->tstamp, kernel_ptr->tstamp)) {
            /* merge from user */
            output_ptr->id = user_ptr->id;
            output_ptr->tstamp = user_ptr->tstamp;
            user_ptr ++;
         } else {
            /* merge from kernel */
            output_ptr->id = kernel_ptr->id;
            output_ptr->tstamp = kernel_ptr->tstamp;
            kernel_ptr ++;
         }
      }
      /* additional data from kernel? */
      for (; (output_ptr < output_end) && (kernel_ptr < kernel_end); output_ptr ++) {
         output_ptr->id = kernel_ptr->id;
         output_ptr->tstamp = kernel_ptr->tstamp;
         kernel_ptr ++;
      }
      /* additional data from user? */
      for (; (output_ptr < output_end) && (user_ptr < user_end); output_ptr ++) {
         output_ptr->id = user_ptr->id;
         output_ptr->tstamp = user_ptr->tstamp;
         user_ptr ++;
      }
      /* no data at all? then we are done */
      if (output_ptr == output_start) {
         break;
      }

      /* Write merged data to disk */
      todo = (output_ptr - output_start) * sizeof (struct rvs_uentry);
      r = ppc_write (rvs_trace_fd, output_start, todo);
      if (r != todo) {
         ppc_fatal_error ("rvslib: unable to write all trace elements to disk (write error)");
      }
   } while (1);

   /* ready for more trace data now */
   reset_buffer_pointer ();

   /* notify time spent writing and merging */
   output_ptr = output_start;
   if (prepend_oi_signal) {
      output_ptr->id = RVS_OI_SIGNAL;
      output_ptr->tstamp = start_tstamp;
      output_ptr++;
   }
   output_ptr->id = RVS_BEGIN_WRITE;
   output_ptr->tstamp = start_tstamp;
   output_ptr++;
   output_ptr->id = RVS_END_WRITE;
   output_ptr->tstamp = rvs_get_cycles();
   output_ptr++;

   /* write notification to disk */
   todo = (output_ptr - output_start) * sizeof (struct rvs_uentry);
   r = ppc_write (rvs_trace_fd, output_start, todo);
   if (r != todo) {
      ppc_fatal_error ("rvslib: unable to write begin/end write event to disk");
   }

   /* reset and (possibly) reenable */
   r = ppc_ioctl (rvs_device_fd, RVS_RESET, 0);
   if (r < 0) {
      ppc_fatal_error("rvslib: failed to reset module");
   }
   if (reenable) {
      r = ppc_ioctl (rvs_device_fd, RVS_ENABLE, 0);
      if (r < 0) {
         ppc_fatal_error("rvslib: failed to re-enable kernel tracing after write to disk");
      }
   }
}

/* signal handler: invoked when the RVS_Ipoint function attempts to write
 * to the trigger page at the end of the user trace buffer. Returns non-zero if
 * the instruction should be skipped. */
int rvs_segfault_signal (unsigned * trigger_pc, unsigned * gregs, unsigned * addr)
{
   const unsigned mask = 0xfc00ffffU;
   const unsigned stwu = 0x95490004U & mask;    /* stwu r10, 4(r9) */
   const unsigned mfspr = 0x7d0e82a6U & mask;   /* mfspr r8, 526 */
   unsigned i, triggered_by = ~0;

   /* reinstall old signal handler while we are here */
   if (segfault_handler_installed) {
      ppc_restore_signal_handler ();
      segfault_handler_installed = 0;
   }

   /* check that this segfault is in the ipoint routine (possibly inlined)
    * by inspecting the machine code: 

         180051c:       95 49 00 04     stwu    r10,4(r9) <-- may write to trigger page
         1800520:       7d 0e 82 a6     mfspr   r8,526
         1800524:       95 09 00 04     stwu    r8,4(r9) <-- may also write to trigger page
                                                            in special circumstances 

      (addresses and register numbers will be different)
   */

   if (rvs_device_fd < 0) {
      /* RVS device not open, so whatever is causing this segfault, it's 
       * not the trace code. */

   } else if ((trigger_pc[0] & mask) != stwu) {
      /* PC does not point at an stwu operation, so this can't be trace code. */

   } else if (((trigger_pc[-1] & mask) == mfspr)
   && ((trigger_pc[-2] & mask) == stwu)) {
      /* PC points at the *second* stwu. Happens in the rare circumstance
       * that the "overflow imminent" signal arrives between the two "stwu"
       * instructions, so the first succeeds and the second fails. */
      intptr_t fault_address = (intptr_t) addr;
      unsigned * page = (unsigned *) (fault_address & ~(PAGE_SIZE - 1));
      unsigned ptr_reg = (trigger_pc[0] >> 16) & 0x1f;
      intptr_t pos = gregs[ptr_reg] + 4;
      unsigned data_reg = (trigger_pc[0] >> 21) & 0x1f;

      if (fault_address != pos) {
         ppc_fatal_error ("rvslib: faulting instruction behaved unexpectedly");
      }
      check_write_pos (pos + 4);

      /* Which trigger page actually caused this? */
      for (i = 0; i < NUM_TRIGGER_PAGES; i++) {
         if (trigger_page[i] && (page == trigger_page[i])) {
            triggered_by = i;
            break;
         }
      }
      if (triggered_by >= NUM_TRIGGER_PAGES) {
         ppc_fatal_error ("rvslib: segfault pointer is not within any trigger page (2nd stwu)");
      }
      if (triggered_by == END_BUF_TRIGGER) {
         ppc_fatal_error ("rvslib: unexpectedly reached end of buffer on 2nd stwu");
      }

      /* uninstall trigger, repeat store, reinstall trigger */
      if (ppc_mprotect_rdwr (page, PAGE_SIZE) != 0) {
         ppc_fatal_error ("rvslib: unable to uninstall force flush trigger for 2nd stwu");
      }

      (* ((unsigned *) pos)) = gregs[data_reg];
      gregs[ptr_reg] = pos;

      if (ppc_mprotect_read (page, PAGE_SIZE) != 0) {
         ppc_fatal_error ("rvslib: unable to reinstall force flush trigger for 2nd stwu");
      }

      /* reinstall segfault handler */
      if (!segfault_handler_installed) {
         segfault_handler_installed = 1;
         ppc_install_signal_handler (SIG_RVS_IMMINENT_OVERFLOW);
      }
      /* skip this instruction */
      return 1;

   } else if (((trigger_pc[1] & mask) == mfspr)
   && ((trigger_pc[2] & mask) == stwu)) {
      /* PC points at the first stwu instruction */
      unsigned ptr_reg = (trigger_pc[0] >> 16) & 0x1f;
      unsigned * ptr_val = (unsigned *) ((intptr_t) gregs[ptr_reg]);
      intptr_t pos = ((intptr_t) ptr_val) + 4;
      intptr_t fault_address = (intptr_t) addr;
      unsigned * page = (unsigned *) (pos & ~(PAGE_SIZE - 1));

      /* Debug error conditions caused by SIGSEGV being generated
       * by inconsistent addresses */
      if (fault_address != pos) {
         ppc_fatal_error ("rvslib: faulting instruction behaved unexpectedly");
      }

      /* Error handler for segfaults not corresponding to a trigger page */
      check_write_pos (pos);

      /* Which trigger page actually caused this? */
      for (i = 0; i < NUM_TRIGGER_PAGES; i++) {
         if (trigger_page[i] && (page == trigger_page[i])) {
            triggered_by = i;
            break;
         }
      }
      if (triggered_by >= NUM_TRIGGER_PAGES) {
         ppc_fatal_error ("rvslib: segfault pointer is not within any trigger page");
      }

      /* Overflow imminent condition: uninstall the trigger now */
      uninstall_trigger_AB ();

      /* write trace buffer to disk */
      rvs_user_trace_write_pointer = (struct rvs_uentry_opaque *) ptr_val;
      check_write_pos (4 + ((intptr_t) rvs_user_trace_write_pointer));

      merge_buffers_now (triggered_by != END_BUF_TRIGGER, 1);

      /* continue at the beginning of the buffer: we rerun the store instruction
       * with ptr_val reset to beginning of buffer. */
      gregs[ptr_reg] = (intptr_t) rvs_user_trace_write_pointer;
      check_write_pos (4 + ((intptr_t) rvs_user_trace_write_pointer));

      /* reinstall segfault handler */
      if (!segfault_handler_installed) {
         segfault_handler_installed = 1;
         ppc_install_signal_handler (SIG_RVS_IMMINENT_OVERFLOW);
      }
      return 0;
   } else {
      /* stwu operation isn't related to trace code (not next to mfspr) */
   }

   /* Some other segfault - what caused it? The following
    * label may be used to set a breakpoint. */
   asm volatile ("rvs_unhandled_segfault: nop\n");
   return 0;
}

/* signal handler: invoked when the kernel trace buffer is about to overflow */
void rvs_overflow_imminent_signal (void)
{
   /* We need to trigger a flush as soon as possible, but we can't flush right
    * now, because we may be in the middle of writing an ipoint! If we flush
    * now, then that ipoint will be lost or corrupt. Instead we will mark the
    * current page and next page as read-only, so the usual mechanism
    * is used to do the flush, when the user next writes an ipoint. */
   intptr_t p;

   if (rvs_device_fd < 0) {
      return; /* do nothing, not tracing */
   }
   if (trigger_page[FORCE_FLUSH_TRIGGER_A]) {
      return; /* do nothing, force flush pages already present */
   }

   p = (intptr_t) rvs_user_trace_write_pointer;
   p += 4; /* undo the -4 offset used for stwu instruction */
   p &= ~(PAGE_SIZE - 1);

   check_write_pos (p);
   
   p += PAGE_SIZE;
   if (p >= (intptr_t) trigger_page[END_BUF_TRIGGER]) {
      return; /* do nothing, about to trigger user flush anyway */
   }
   p -= PAGE_SIZE;

   /* setup trigger on this page and the one after it */
   trigger_page[FORCE_FLUSH_TRIGGER_A] = (unsigned *) p;
   p += PAGE_SIZE;
   trigger_page[FORCE_FLUSH_TRIGGER_B] = (unsigned *) p;

   if ((ppc_mprotect_read (trigger_page[FORCE_FLUSH_TRIGGER_A], PAGE_SIZE) != 0)
   || (ppc_mprotect_read (trigger_page[FORCE_FLUSH_TRIGGER_B], PAGE_SIZE) != 0)) {
      ppc_fatal_error ("rvslib: unable to install force flush trigger");
   }
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
   unsigned i;

   if (rvs_device_fd >= 0) {
      /* RVS_Init_Ex has been called twice. Be tolerant. */
      return;
   }
   for (i = 0; i < NUM_TRIGGER_PAGES; i++) {
      trigger_page[i] = NULL;
   }

   /* Open and configure kernel driver */
   rvs_device_fd = ppc_open_rdwr (RVS_FILE_NAME);
   if (rvs_device_fd < 0) {
      ppc_fatal_error ("RVS_Init: could not open " RVS_FILE_NAME);
   }
   if ((USER_BUFFER_SIZE < 4) || (KERNEL_BUFFER_SIZE < 4)) {
      ppc_fatal_error ("rvslib: buffers are too small");
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
      trigger_page[END_BUF_TRIGGER] = (unsigned *) p;

      if (ppc_mprotect_read (trigger_page[END_BUF_TRIGGER], PAGE_SIZE) != 0) {
         ppc_fatal_error ("RVS_Init: mprotect user trace buffer failed");
      }
   }

   /* Install segfault handler to catch writes to trigger pages */
   if (!segfault_handler_installed) {
      ppc_install_signal_handler (SIG_RVS_IMMINENT_OVERFLOW);
      segfault_handler_installed = 1;
   }

   /* Reset trace buffers */
   reset_buffer_pointer ();
}

/* Force write to disk and close trace file */
void RVS_Output (void)
{
   int32_t r;
   unsigned i;

   if (rvs_device_fd < 0) {
      /* RVS_Output called twice, or RVS_Output 
       * called without first calling RVS_Init. Be tolerant. */
      return;
   }
      
   if (segfault_handler_installed) {
      ppc_restore_signal_handler ();
      segfault_handler_installed = 0;
   }

   r = ppc_ioctl (rvs_device_fd, RVS_DISABLE, 0);
   if (r < 0) {
      ppc_fatal_error ("RVS_Output: failed to disable kernel tracing");
   }

   if (rvs_trace_fd >= 0) {
      merge_buffers_now (0, 0);
      ppc_close (rvs_trace_fd);
      rvs_trace_fd = -1;
   }
   for (i = 0; i < NUM_TRIGGER_PAGES; i++) {
      if (trigger_page[i]) {
         if (ppc_mprotect_rdwr (trigger_page[i], PAGE_SIZE) != 0) {
            ppc_fatal_error ("RVS_Output: unable to uninstall trigger");
         }
         trigger_page[i] = NULL;
      }
   }
   reset_buffer_pointer ();

   if (rvs_device_fd >= 0) {
      ppc_close (rvs_device_fd);
      rvs_device_fd = -1;
   }
}

/* Return the version of the librvs.a/rvs.ko API */
int RVS_Get_Version (void)
{
   return RVS_API_VERSION;
}

/* Set the build ID for the instrumented source code.
 * The build ID is written to the trace, so you should first call RVS_Init
 * or RVS_Init_Ex. Trace filters must be able to recognise build IDs. */
void RVS_Build_Id (const char * build_id)
{
   unsigned i;

   if (rvs_device_fd < 0) {
      ppc_fatal_error ("RVS_Build_Id called before RVS_Init");
   }
   merge_buffers_now (0, 0);

   /* 7,5 = build id */
   RVS_Ipoint (7);
   RVS_Ipoint (5);

   /* 0 terminated string = build ID */
   for (i = 0; build_id[i] != 0; i++) {
      RVS_Ipoint (build_id[i]);
   }
   RVS_Ipoint (0);

   merge_buffers_now (0, 1);
}

