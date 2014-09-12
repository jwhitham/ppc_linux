/* Copyright (c) 2014 Freescale Semiconductor, Inc.
 * All rights reserved.
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

#ifndef __DPAA_CAPWAP_DOMAIN_EXT_H
#define __DPAA_CAPWAP_DOMAIN_EXT_H

#include "error_ext.h"
#include "std_ext.h"

#include "fm_pcd_ext.h"

/* Use source-address in key */
#define DPAA_CAPWAP_DOMAIN_KEY_FIELD_SIP	0x00000001
/* Use destination-address in key */
#define DPAA_CAPWAP_DOMAIN_KEY_FIELD_DIP	0x00000002
/* Use protocol field in key */
#define DPAA_CAPWAP_DOMAIN_KEY_FIELD_PROTO	0x00000004
/* Use UDP source-port in key */
#define DPAA_CAPWAP_DOMAIN_KEY_FIELD_SPORT	0x00000008
/* Use UDP destination-port in key */
#define DPAA_CAPWAP_DOMAIN_KEY_FIELD_DPORT	0x00000010
/* Use CAPWAP-Preamble in key (first BYTE);
 * NOTE: This field MUST be in the key in order
 * to distinguish between DTLS and non-DTLS tunnels
 */
#define DPAA_CAPWAP_DOMAIN_KEY_FIELD_PREAMBLE	0x00000020
/* Use DTLS type in key (first BYTE) */
#define DPAA_CAPWAP_DOMAIN_KEY_FIELD_DTLS_TYPE	0x00000040

#define DPAA_CAPWAP_DOMAIN_MAX_NUM_OF_TUNNELS	(FM_PCD_MAX_NUM_OF_FLOWS/2)

 /* A structure for inbound-pre parameters */
struct dpaa_capwap_domain_inbound_pre_params {
	/* Flags indicating key components;
	 * (use DPAA_CAPWAP_DOMAIN_KEY_FIELD_xxx macros to configure)
	 */
	uint32_t key_fields;
	/* Flags indicating mask components;
	 * (use DPAA_CAPWAP_DOMAIN_KEY_FIELD_xxx macros to configure)
	 */
	uint32_t mask_fields;
	/* Handle to a table */
	t_Handle h_Table;
};

struct capwap_op_port {
	uint8_t fm_id;
	uint8_t port_id;
	t_Handle port_handle;
};

/* A structure for defining DPAA-CAPWAP-Domain initialization parameters */
struct dpaa_capwap_domain_params {
	struct dpaa_capwap_domain_inbound_pre_params inbound_pre_params;
	void *h_fm_pcd;		/* A handle to the FM-PCD module */
	bool support_ipv6;	/* TODO */
	uint32_t max_num_of_tunnels;/* Maximal number of active Tunnels */

	struct capwap_op_port outbound_op;
	struct capwap_op_port inbound_op;

	void *id;	/*Output Params, the pointer of CAPWAP_DOMAIN */
};

 /* DPAA CAPWAP Domain Direction */
enum dpaa_capwap_domain_direction {
	e_DPAA_CAPWAP_DOMAIN_DIR_INVALID = 0,    /* Invalid direction */
	e_DPAA_CAPWAP_DOMAIN_DIR_INBOUND,        /* Inbound direction */
	e_DPAA_CAPWAP_DOMAIN_DIR_OUTBOUND        /* Outbound direction */
};

 /* A structure for defining IP address */
struct dpaa_capwap_domain_ip_address {
	bool ipv6;	/* TRUE for ipv6 format */

	union {
		/* IPv4 address format */
		uint32_t ipv4_addr;
		/* IPv6 address format */
		uint8_t ipv6_addr[NET_HEADER_FIELD_IPv6_ADDR_SIZE];
	} u;
};

/* A structure for defining IP mask */
struct dpaa_capwap_domain_ip_mask {
	bool ipv6;       /* TRUE for ipv6 format */

	union {
		uint32_t ipv4_mask;   /* IPv4 mask format */
		uint8_t ipv6_mask[NET_HEADER_FIELD_IPv6_ADDR_SIZE];
		/* IPv6 mask format */
	} u;
};

/* DTLS Anti-Replay-Size Options */
enum dtls_ars {
	e_DTLS_ARS_0 , /* No anti-replay window       */
	e_DTLS_ARS_32, /* 32-entry anti-replay window */
	e_DTLS_ARS_64  /* 64-entry anti-replay window */
};

struct dtls_sec_params {
	/* IV writeback (block cipher only):
	 *FALSE: IV field in PDB held constant
	 *TRUE:  IV field in PDB written back with last block of ciphertext
	 */
	bool wbIv;
	uint8_t type;	/* Record type */
	enum dtls_ars arw;	/* Anti replay window */
	uint16_t version;	/* Record version */
	uint16_t epoch;		/* Record epoch */
	uint64_t seq_num;	/* Initial sequence number */
	/* Initialization vector (16 bytes);
	 * Null pointer for using the internal random number generator
	 */
	uint8_t p_Iv[16];
	uint32_t alg_type;
	uint8_t *cipher_key;
	uint32_t cipher_key_len;
	uint8_t *auth_key;
	uint32_t auth_key_len;
};

 /* A structure for defining SA-Out parameters */
struct dpaa_capwap_domain_tunnel_out_params {
	uint32_t eth_header_size;	/* size of ETH header */
	uint8_t *p_ether_header;	/* ETH encapsulation header */
	uint32_t ip_header_size;	/* size of IP header */
	uint8_t *p_ip_header;	/* IP encapsulation header */
	/* offset of the last protocol-id field */
	uint32_t last_pid_offset;
	uint32_t initial_id;	/* initial ID value; will be incremented
				 *  for every frame
				 */
	uint8_t *p_udp_header;	/* UDP encapsulation header */
	bool udp_or_lite;		/* UDP or UDP-Lite header */
	uint32_t capwap_header_size;	/* size of CAPWAP header */
	uint8_t *p_capwap_header;	/* CAPWAP encapsulation header */
	struct t_FmPcdCcNextEngineParams *p_NextEngineParams; /* TODO */
	uint16_t size_for_fragment;	/* If not zero than fragmenation is
					 * required and will be build by
					 * the driver
					 */
	bool frag_bp_enable;	/* If set, Framentation will use a seperate
				 * Buffer Pool. otherwise, fragmentation will
				 * use the same buffer pool as input frame
				 */
	uint8_t	frag_bp_id;	/* Buffer Pool ID used by fragmentation */
	bool dtls;		/* DTLS tunnel */
	bool is_control;	/* true: control tunnel, false: data tunnel */

	struct dtls_sec_params dtls_params;

	void *capwap_domain_id;	/* The pointer of CAPWAP_DOMAIN */
	void *tunnel_id;	/* The pointer of CAPWAP_TUNNEL */
};

/*  A structure for defining tunnel-in parameters */
struct dpaa_capwap_domain_tunnel_in_params {
	struct dpaa_capwap_domain_ip_address sip;	/* Source IP address */
	struct dpaa_capwap_domain_ip_mask sip_mask;	/* Source IP mask */
	struct dpaa_capwap_domain_ip_address dip; /* Destination IP address */
	struct dpaa_capwap_domain_ip_mask dip_mask; /* Destination IP mask */
	uint16_t src_port;			/* Source UDP port */
	uint16_t dst_port;			/* Destination UDP port */

	bool dtls;				/* DTLS tunnel */
	bool is_control;			/* true: control tunnel,
						 * false: data tunnel
						 */

	struct dtls_sec_params dtls_params;

	void *capwap_domain_id;		/* The pointer of CAPWAP_DOMAIN */
	void *tunnel_id;		/* The pointer of CAPWAP_TUNNEL */
};

struct fqid_range {
	u32 fqid_base;
	u32 fq_count;
	void *fq_base; /* The base pointer for dpa_fq or qman_fq */
};

struct dpaa_capwap_domain_fqs {
	/* Inbound FQs */
	struct fqid_range inbound_eth_rx_fqs;
	struct fqid_range inbound_sec_to_op_fqs;
	struct fqid_range inbound_core_rx_fqs;
	/* Outbound FQs */
	struct fqid_range outbound_core_tx_fqs;
	struct fqid_range outbound_op_tx_fqs;
	struct fqid_range outbound_sec_to_op_fqs;
	u32 debug_fqid;
};

struct capwap_domain_kernel_rx_ctl {
	bool on;	/*true: turn on, false: turn off */
	bool is_control;/*true: control tunnel, false: data tunnel */
	bool is_dtls;	/*true: dtls tunnel, false: non-dtls-tunnel */
	u32 fqid;	/*fqid returned from kernel*/
	void *capwap_domain_id;	/* The pointer of CAPWAP_DOMAIN */
};

#endif /* __DPAA_CAPWAP_DOMAIN_EXT_H */
