#define _GNU_SOURCE
#define USE_KERNEL

#include <sys/syscall.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>



#include "librvs.h"
#include <rvs.h>

#ifdef CONFIG_X86
#define nolib_syscall syscall
#else
#ifdef CONFIG_ARM
#define nolib_syscall syscall
#else
#ifdef CONFIG_PPC
/* nolib_syscall implemented in ppc_syscall.s */
int32_t nolib_syscall (int32_t nr, ...);
#else
#error "This library only supports PPC, x86 and ARM platforms"
#endif /* PPC */
#endif /* ARM */
#endif /* X86 */


#define TRACE_FILE_NAME    "trace.bin"

#define USER_BUFFER_SIZE     (1 << 20) /* 1M entries */
#define KERNEL_BUFFER_SIZE   (1 << 16) /* 64K entries */
#define MERGED_BUFFER_SIZE   (USER_BUFFER_SIZE + KERNEL_BUFFER_SIZE)


/* User RVS interface. */
struct rvs_uentry {
   uint32_t id;
   uint32_t tstamp;
};

static int32_t rvs_device_fd = -1;
static int32_t rvs_trace_fd = -1;
static struct rvs_uentry user_buffer[USER_BUFFER_SIZE];
static struct rvs_uentry kernel_buffer[KERNEL_BUFFER_SIZE];
static struct rvs_uentry merged_buffer[MERGED_BUFFER_SIZE];
static struct rvs_uentry * user_pos;
static struct rvs_uentry * merged_pos;
static uint32_t kernel_loaded, kernel_read;
static uint32_t user_loaded, user_read;

static void merge_buffers_now (void);

static void fatal_error (const char * s)
{
   uint32_t sz;

   for (sz = 0; s[sz] != '\0'; sz++) {}

   nolib_syscall (__NR_write, STDERR_FILENO, s, sz);
   nolib_syscall (__NR_write, STDERR_FILENO, "\n", 1);
   nolib_syscall (__NR_exit, 0, 0, 0);
   while (1) {}
}

#ifdef CONFIG_PPC
void ppc_syscall_error (void)
{
   fatal_error ("ppc_syscall_error");
}
void ppc_exit (void)
{
   nolib_syscall (__NR_exit, 0, 0, 0);
}
#endif

void RVS_Init(void)
{
   int32_t r;

   rvs_device_fd = nolib_syscall (__NR_open, RVS_FILE_NAME, O_RDWR, 0);
   if (rvs_device_fd < 0) {
      fatal_error ("RVS_Init: could not open " RVS_FILE_NAME);
   }
   if ((USER_BUFFER_SIZE < 4) || (KERNEL_BUFFER_SIZE < 4)) {
      fatal_error ("buffers are too small");
   }

   r = nolib_syscall (__NR_ioctl, rvs_device_fd, RVS_RESET, 0);
   if (r < 0) {
      fatal_error("RVS_Init: failed to reset module");
   }
   r = nolib_syscall (__NR_ioctl, rvs_device_fd, RVS_ENABLE, 0);
   if (r < 0) {
      fatal_error("RVS_Init: failed to enable kernel tracing");
   }
   user_pos = &user_buffer[0];
   merged_pos = &merged_buffer[0];

   rvs_trace_fd = nolib_syscall (__NR_creat, TRACE_FILE_NAME, 0664, 0);
   if (rvs_trace_fd < 0) {
      fatal_error ("RVS_Init: could not create " TRACE_FILE_NAME);
   }
}

void RVS_Ipoint (unsigned id)
{
   if (!user_pos) {
      return;
   }

   user_pos->tstamp = rvs_get_cycles ();
   user_pos->id = id;

   if (user_pos == &user_buffer[USER_BUFFER_SIZE - 1]) {
      /* flush the trace */
      user_pos->id = RVS_BEGIN_WRITE;
      user_pos ++;
      merge_buffers_now ();
      user_pos->tstamp = rvs_get_cycles ();
      user_pos->id = RVS_END_WRITE;
      user_pos ++;
      user_pos->tstamp = rvs_get_cycles ();
      user_pos->id = id;
   }
   user_pos ++;
}

static void download_kernel_trace (void)
{
   int32_t           r;
   struct rvs_stats  s;

   if (kernel_loaded > kernel_read) {
      /* buffer not consumed, don't download again yet */
      return;
   }
   s.missed = 0;
   r = nolib_syscall (__NR_ioctl, rvs_device_fd, RVS_GET_STATS, &s);
   if (r < 0) {
      fatal_error ("rvslib: download_kernel_trace: ioctl RVS_GET_STATS error");
   }
   if (s.missed) {
      fatal_error ("rvslib: kernel reports some trace events were missed");
   }

   r = nolib_syscall (__NR_read, rvs_device_fd, kernel_buffer, KERNEL_BUFFER_SIZE * sizeof(struct rvs_uentry));
   if (r < 0) {
      fatal_error ("rvslib: download_kernel_trace: read error");
   }
   if ((r % sizeof(struct rvs_uentry)) != 0) {
      fatal_error ("rvslib: unexpected kernel behaviour (1)");
   }
   if (r > (KERNEL_BUFFER_SIZE * sizeof(struct rvs_uentry))) {
      fatal_error ("rvslib: unexpected kernel behaviour (12");
   }
   kernel_loaded = r / sizeof(struct rvs_uentry);
   kernel_read = 0;
}

static void flush_merged_buffer (int force)
{
   int32_t total = 0;
   int32_t todo = 0;

   if (force || (merged_pos == &merged_buffer[MERGED_BUFFER_SIZE])) {
      todo = (merged_pos - merged_buffer) * sizeof (struct rvs_uentry);
      if (todo) {
         total = nolib_syscall (__NR_write, rvs_trace_fd, merged_buffer, todo);
         if (total != todo) {
            fatal_error ("rvslib: unable to write all trace elements to disk");
         }
      } 
      merged_pos = &merged_buffer [0];
   }
}

static void merge_buffers_now (void)
{
   /* Merge user and kernel buffers */
   user_loaded = (user_pos - user_buffer);
   user_read = 0;
   flush_merged_buffer (1);
   download_kernel_trace ();

   /* While both buffers contain data */
   while ((kernel_read < kernel_loaded) && (user_read < user_loaded)) {
      if (rvs_time_before (kernel_buffer[kernel_read].tstamp, user_buffer[user_read].tstamp)) {
         (* merged_pos) = kernel_buffer[kernel_read];
         kernel_read++;
         merged_pos++;
         flush_merged_buffer (0);
         download_kernel_trace ();
      } else {
         (* merged_pos) = user_buffer[user_read];
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
      (* merged_pos) = user_buffer[user_read];
      user_read++;
      merged_pos++;
   }

   flush_merged_buffer (1);

   /* Ready for more trace data now */
   user_pos = &user_buffer[0];
}

void RVS_Output (void)
{
   int32_t r;

   if (rvs_device_fd >= 0) {
      r = nolib_syscall (__NR_ioctl, rvs_device_fd, RVS_DISABLE, 0);
      if (r < 0) {
         fatal_error ("RVS_Output: failed to disable kernel tracing");
      }
   }

   if (rvs_trace_fd >= 0) {
      merge_buffers_now ();
      nolib_syscall (__NR_close, rvs_trace_fd, 0, 0);
      rvs_trace_fd = -1;
      user_pos = NULL;
      merged_pos = NULL;
   }

   if (rvs_device_fd >= 0) {
      nolib_syscall (__NR_close, rvs_device_fd, 0, 0);
      rvs_device_fd = -1;
   }
}

