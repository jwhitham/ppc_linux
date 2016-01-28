/*=========================================================================
 * RapiTime    : a tool for Measurement-Based Execution Time Analysis
 * Module      : rvs_library for PPC Linux
 * File        : ppc_linux.c
 * Description :
 * Kernel interface for librvs providing simple C library functionality
 * and signal handling.
 *
 * Copyright (c) 2016 Rapita Systems Ltd.               All rights reserved
 *=========================================================================
*/

#include "ppc_linux.h"

#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <ucontext.h>

#define PC_REG_OFFSET 32 /* 32nd register is program counter */

/* Linux kernel sigaction structure as used by __NR_sigaction syscall;
 * things in the system headers called "struct sigaction" are usually
 * subtly different, and translated to the kernel form within the C
 * library.
 */

#define SA_MASK_SIZE 8

typedef struct kernel_sigaction {
   void * func;
   uint32_t sa_flags;
   uint8_t sa_mask[SA_MASK_SIZE];
} _kernel_sigaction;

static struct kernel_sigaction old_signal_handler;




/* ppc_syscall implemented in ppc_syscall.S */
int ppc_syscall (int nr, ...);

/* rvs_segfault_signal implemented in librvs.c */
void rvs_segfault_signal (unsigned * trigger_pc, unsigned * gregs, unsigned * addr);

/* error reporting function */
void ppc_fatal_error (const char * s)
{
   unsigned sz;

   for (sz = 0; s[sz] != '\0'; sz++) {}

   ppc_write (STDERR_FILENO, s, sz);
   ppc_write (STDERR_FILENO, "\n", 1);
   ppc_syscall (__NR_exit, 1, 0, 0);
}

/* minimalist interface to system calls: */
int ppc_ioctl (int fd, int ioctl_nr, void * data)
{
   return ppc_syscall (__NR_ioctl, fd, ioctl_nr, data);
}

int ppc_read (int fd, void * data, unsigned size)
{
   return ppc_syscall (__NR_read, fd, data, size);
}

int ppc_write (int fd, const void * data, unsigned size)
{
   return ppc_syscall (__NR_write, fd, data, size);
}

int ppc_open_rdwr (const char * filename)
{
   return ppc_syscall (__NR_open, filename, O_RDWR, 0);
}

int ppc_creat (const char * filename)
{
   return ppc_syscall (__NR_creat, filename, 0664, 0);
}

int ppc_close (int fd)
{
   return ppc_syscall (__NR_close, fd, 0, 0);
}

int ppc_mprotect_read (void * base, unsigned size)
{
   return ppc_syscall (__NR_mprotect, base, size, PROT_READ);
}

int ppc_mprotect_rdwr (void * base, unsigned size)
{
   return ppc_syscall (__NR_mprotect, base, size, PROT_READ | PROT_WRITE);
}

void ppc_exit (int rc)
{
   ppc_syscall (__NR_exit, rc, 0, 0);
}

/* call segfault handler in librvs.c with correct information from kernel */
static void handle_segfault (int n, siginfo_t * si, void * _uc)
{
   ucontext_t * uc = (ucontext_t *) _uc;
   unsigned * gregs = (unsigned *) uc->uc_mcontext.uc_regs->gregs;
   unsigned * trigger_pc = (unsigned *) ((intptr_t) gregs[PC_REG_OFFSET]);

   rvs_segfault_signal (trigger_pc, gregs, (unsigned *) si->si_addr);
}

/* signal handler management */
void ppc_restore_signal_handler (void)
{
   if (ppc_syscall (__NR_rt_sigaction, SIGSEGV, &old_signal_handler, NULL, SA_MASK_SIZE) != 0) {
      ppc_fatal_error ("sigaction (SIGSEGV) reinstall of old handler failed");
   }
}

void ppc_install_signal_handler (void)
{
   struct kernel_sigaction sa;
   unsigned i;

   /* zero the sigaction struct */
   for (i = 0; i < sizeof (sa); i++) {
      ((char *) &sa)[i] = 0;
   }
   sa.func = handle_segfault;
   sa.sa_flags = SA_SIGINFO;

   if (ppc_syscall (__NR_rt_sigaction, SIGSEGV, &sa, &old_signal_handler, SA_MASK_SIZE) != 0) {
      ppc_fatal_error ("RVS_Init: sigaction (SIGSEGV) install failed");
   }
}

