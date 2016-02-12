/*
 *  RVS Tracing API
 *
 *  Author: Jack Whitham (jwhitham@rapitasystems.com)
 *
 *  These are the low-level components of the RVS tracing API.
 *  The trace buffer is stored in the kernel's .bss section.
 *
 *  Copyright (C) 2016 Rapita Systems Ltd.
 *
 */
#include <linux/string.h>
#include <linux/irqflags.h>
#include <linux/uaccess.h>
#include <linux/trace_rvs.h>


/* The total capacity of the kernel trace buffer: */
#define NUM_ENTRIES		(16 * 1024 * 1024 / 8) /* 16 megabytes */

/* The minimum number of trace entries needed to trigger the
 * "imminent overflow" condition - where we send a signal to
 * the process to tell it to flush the buffer: */
#define OVERFLOW_POINT	((NUM_ENTRIES * 25) / 100)

/* The processor ID register for the process we are tracing: */
unsigned trace_rvs_pir = ~0;

/* Trace data storage: */
struct rvs_entry trace_rvs_buffer[NUM_ENTRIES];
struct rvs_entry *trace_rvs_write_p = (void *) 0;
struct rvs_entry *trace_rvs_end_p = (void *) 0;



void trace_rvs_reset (void)
{
	unsigned long flags;
	local_irq_save (flags);
	trace_rvs_pir = ~0;
	trace_rvs_write_p = &trace_rvs_buffer[0];
	trace_rvs_end_p = &trace_rvs_buffer[NUM_ENTRIES];
	asm volatile ("msync");
	local_irq_restore (flags);
}

void trace_rvs_start (void)
{
	unsigned pir;
	unsigned long flags;
	local_irq_save (flags);
	asm volatile ("mfspr %0, 286\n" : "=r"(pir));
	trace_rvs_pir = pir;
	asm volatile ("msync");
	local_irq_restore (flags);
}

void trace_rvs_stop (void)
{
	unsigned long flags;
	local_irq_save (flags);
	trace_rvs_pir = ~0;
	asm volatile ("msync");
	local_irq_restore (flags);
}

void trace_rvs_ipoint_asm (unsigned id); /* defined in head_fsl_booke.S */

int trace_rvs_ipoint (unsigned id)
{
	unsigned long flags;
	const struct rvs_entry *overflow_p = &trace_rvs_buffer[OVERFLOW_POINT];
	int overflow;

	local_irq_save (flags);
	trace_rvs_ipoint_asm (id);
	overflow = (trace_rvs_write_p > overflow_p);
	local_irq_restore (flags);
	return overflow;
}

ssize_t trace_rvs_download (struct rvs_entry __user *target_p, size_t target_size)
{
	unsigned long flags;
	size_t available_size = 0;
	ssize_t rc = 0;
	struct rvs_entry *trace_rvs_begin_p = &trace_rvs_buffer[0];

	local_irq_save (flags);
	available_size = trace_rvs_write_p - trace_rvs_begin_p;
	available_size *= sizeof (struct rvs_entry);
	/* Be sure that no other code is going to write to the trace: */
	trace_rvs_pir = ~0;
	asm volatile ("msync");
	local_irq_restore (flags);

	if (target_size < (NUM_ENTRIES * sizeof (struct rvs_entry))) {
		/* Can't download everything in one go: bail out without further action */
		rc = -EINVAL;
		goto restore;
	}
	if (available_size >= ((NUM_ENTRIES - 1) * sizeof (struct rvs_entry))) {
		/* Buffer full condition detected */
		printk(KERN_ERR "rvs: kernel trace buffer is full!\n");
		rc = -ENOSPC;
		goto reset;
	}
	if (available_size == 0) {
		/* Buffer empty condition detected */
		rc = 0;
		goto reset;
	}
	if (copy_to_user (target_p, trace_rvs_begin_p, available_size)) {
		/* copy failed */
		rc = -EFAULT;
		goto restore;
	}
	rc = available_size;

reset:
	local_irq_save (flags);
	trace_rvs_write_p = trace_rvs_begin_p;
	local_irq_restore (flags);
restore:
	return rc;
}

EXPORT_SYMBOL(trace_rvs_ipoint);
EXPORT_SYMBOL(trace_rvs_download);
EXPORT_SYMBOL(trace_rvs_start);
EXPORT_SYMBOL(trace_rvs_stop);
EXPORT_SYMBOL(trace_rvs_reset);


