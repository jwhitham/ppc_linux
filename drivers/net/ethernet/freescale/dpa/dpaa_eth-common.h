/*
 * Copyright 2008-2012 Freescale Semiconductor Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *	 notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *	 notice, this list of conditions and the following disclaimer in the
 *	 documentation and/or other materials provided with the distribution.
 *     * Neither the name of Freescale Semiconductor nor the
 *	 names of its contributors may be used to endorse or promote products
 *	 derived from this software without specific prior written permission.
 *
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation, either version 2 of that License or (at your option) any
 * later version.
 *
 * THIS SOFTWARE IS PROVIDED BY Freescale Semiconductor ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL Freescale Semiconductor BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __DPA_COMMON_H
#define __DPA_COMMON_H

#include <linux/kernel.h>	/* pr_*() */
#include <linux/device.h>	/* dev_*() */
#include <linux/smp.h>		/* smp_processor_id() */

#define __hot

/*
 * TODO Remove these altogether. They have been removed form kernel 3.8
 * and are still here for compatibility while we're rebasing the code.
 */
#ifndef __devinit
#define __devinit
#endif
#ifndef __devexit_p
#define __devexit_p
#endif
#ifndef __devinitdata
#define __devinitdata
#endif
#ifndef __devinitconst
#define __devinitconst
#endif
#ifndef __devexit
#define __devexit
#endif

/* Simple enum of FQ types - used for array indexing */
enum {RX, TX};

/* More detailed FQ types - used for fine-grained WQ assignments */
enum dpa_fq_type {
	FQ_TYPE_RX_DEFAULT = 1, /* Rx Default FQs */
	FQ_TYPE_RX_ERROR,       /* Rx Error FQs */
	FQ_TYPE_RX_PCD,         /* User-defined PCDs */
	FQ_TYPE_TX,             /* "Real" Tx FQs */
	FQ_TYPE_TX_CONFIRM,     /* Tx Confirmation FQs (actually Rx FQs) */
	FQ_TYPE_TX_ERROR,       /* Tx Error FQs (these are actually Rx FQs) */
#ifdef CONFIG_DPA_TX_RECYCLE
	FQ_TYPE_TX_RECYCLE,	/* Tx FQs for recycleable frames only */
#endif
};


#define DPA_PARSE_RESULTS_SIZE sizeof(t_FmPrsResult)
#define DPA_HASH_RESULTS_SIZE 16

#define DPA_TX_PRIV_DATA_SIZE	16

#define dpaa_eth_init_port(type, port, param, errq_id, defq_id, priv_size, \
			   has_timer) \
{ \
	param.errq = errq_id; \
	param.defq = defq_id; \
	param.priv_data_size = priv_size; \
	param.parse_results = true; \
	param.hash_results = true; \
	param.frag_enable = false; \
	param.time_stamp = has_timer; \
	fm_set_##type##_port_params(port, &param); \
}

#endif	/* __DPA_COMMON_H */
