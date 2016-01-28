#=========================================================================
# RapiTime    : a tool for Measurement-Based Execution Time Analysis
# Module      : rvs_library for PPC Linux
# File        : Makefile
# Description :
# build librvs.a from C source code and assembly code which was precompiled
# on the target board
#
# Copyright (c) 2016 Rapita Systems Ltd.               All rights reserved
#=========================================================================

all: librvs.a

MFLAGS=-mcpu=8548 -mhard-float \
   -mfloat-gprs=double -mspe -mabi=spe -mhard-float
CPPFLAGS += -I../rvs_module -I. -Ippc_headers
CFLAGS += -Wall -Werror -DLIBRVS -g -DCONFIG_PPC -O2 $(MFLAGS)
OBJS := librvs.o ppc_syscall.o ppc_linux.o

librvs.a: $(OBJS)
	# This Makefile rule runs on the host system
	$(AR) rcs $@ $(OBJS)

ppc_linux.s: ppc_linux.c ppc_linux.h
	# This Makefile rule should be run on the target board, because
	# that is where we find the header files that define the details
	# of the kernel interface for sigaction, etc.
	powerpc-linux-gnu-gcc -S ppc_linux.c -O2 -Wall -Werror

ppc_linux.o: ppc_linux.s
	# This Makefile rule runs on the host system
	$(CC) -o ppc_linux.o -c ppc_linux.s

.PHONY: clean
clean:
	rm -f $(OBJS) librvs.a
