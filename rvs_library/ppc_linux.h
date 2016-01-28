/*=========================================================================
 * RapiTime    : a tool for Measurement-Based Execution Time Analysis
 * Module      : rvs_library for PPC Linux
 * File        : ppc_linux.h
 * Description :
 * Kernel interface for librvs providing simple C library functionality
 * and signal handling.
 *
 * Copyright (c) 2016 Rapita Systems Ltd.               All rights reserved
 *=========================================================================
*/
#ifndef PPC_LINUX_H
#define PPC_LINUX_H

void ppc_fatal_error (const char * s);
int ppc_ioctl (int fd, int ioctl_nr, void * data);
int ppc_read (int fd, void * data, unsigned size);
int ppc_write (int fd, const void * data, unsigned size);
int ppc_open_rdwr (const char * filename);
int ppc_creat (const char * filename);
int ppc_close (int fd);
int ppc_mprotect_read (void * base, unsigned size);
int ppc_mprotect_rdwr (void * base, unsigned size);
void ppc_exit (int rc);
void ppc_restore_signal_handler (void);
void ppc_install_signal_handler (void);

#endif

