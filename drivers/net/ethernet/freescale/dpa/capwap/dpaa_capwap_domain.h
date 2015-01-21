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

#ifndef __DPAA_CAPWAP_DOMAIN_H
#define __DPAA_CAPWAP_DOMAIN_H

#include "fm_port_ext.h"
#include "fm_pcd_ext.h"
#include "dpaa_capwap_domain_ext.h"
#include "dpaa_capwap_desc.h"

#define OUTER_HEADER_MAX_SIZE 100
#define DTLS_KEY_MAX_SIZE 256
#define TABLE_KEY_MAX_SIZE FM_PCD_MAX_SIZE_OF_KEY

#define UDP_HDR_SIZE 8

enum e_PortType {
	e_CAPWAP_DOM_PORT_RXTX = 0,
	e_CAPWAP_DOM_PORT_SEC_DEC,
	e_CAPWAP_DOM_PORT_SEC_ENC,
	e_CAPWAP_DOM_PORT_OP_POST_DEC,
	e_CAPWAP_DOM_PORT_OP_OUT
};

struct qman_fq_chain {
	struct qman_fq *fq;
	struct list_head list;
};

struct dpaa_capwap_tunnel {
	enum dpaa_capwap_domain_direction tunnel_dir;
	uint32_t tunnel_id;
	uint16_t key_index;
	t_Handle dpaa_capwap_domain;
	t_Handle sec_desc;

	struct cipher_params cipher_data;
	struct auth_params auth_data;

	/* Tx internal info */
	t_Handle h_hm_capwap;
	t_Handle h_ccNode;
	t_Handle h_hm_l2;
	t_Handle h_hm_l3;
	t_Handle h_hm_l4;
	t_Handle h_capwap_frag;
	t_Handle h_capwap_manip;

	/* Rx Pre SEC internal info */
	uint8_t *p_key;
	uint8_t *p_mask;
	t_Handle h_hm_till_manip;

	struct list_head node;
	struct list_head fq_chain_head;
};

struct t_FmPcdInfo {
	t_Handle h_NetEnv;
	t_Handle h_CcTree;
	uint8_t numOfCcNodes;
	t_Handle h_CcNodes[5];
	t_Handle h_CcNodesOrder[5];
};

struct t_Port {
	enum e_PortType type;
	t_Handle h_DpaPort;
	t_Handle h_Domain;
	uint32_t rxPcdQsBase;
	uint32_t numOfTxQs;
	struct t_FmPcdInfo fmPcdInfo;
	uint8_t	fm_id;
	uint8_t	port_id;
	uint16_t tx_ch;
};

struct dpaa_capwap_domain {
	struct t_Port rx_tx_port;
	struct t_Port post_dec_op_port;
	struct t_Port out_op_port;

	t_Handle h_fm_pcd;

	uint32_t max_num_of_tunnels;
	bool support_ipv6;

	/* Tx internal info */
	t_Handle h_op_port;
	t_Handle h_flow_id_table;

	/* Rx Pre SEC internal info */
	uint8_t key_size;
	uint32_t key_fields;
	uint32_t mask_fields;
	t_Handle h_em_table;

	struct list_head in_tunnel_list;
	struct list_head out_tunnel_list;
	struct dpaa_capwap_sec_info secinfo;
	struct dpaa_capwap_domain_fqs *fqs;
	struct net_device *net_dev; /* Device for CAPWAP Ethernet */
	uint8_t bpid;
	size_t bp_size;
};

static inline struct dpaa_capwap_tunnel *dequeue_tunnel_obj(
		struct list_head *p_list)
{
	struct dpaa_capwap_tunnel *p_tunnel = NULL;
	struct list_head *p_next;

	if (!list_empty(p_list)) {
		p_next = p_list->next;
		p_tunnel = list_entry(p_next, struct dpaa_capwap_tunnel, node);
		list_del(p_next);
	}

	return p_tunnel;
}

static inline void enqueue_tunnel_obj(struct list_head *p_List,
				struct dpaa_capwap_tunnel *p_Tunnel)
{
	list_add_tail(&p_Tunnel->node, p_List);
}

int add_in_tunnel(struct dpaa_capwap_domain *capwap_domain,
		struct dpaa_capwap_tunnel *p_tunnel,
		struct dpaa_capwap_domain_tunnel_in_params *in_tunnel_params);

int add_out_tunnel(struct dpaa_capwap_domain *capwap_domain,
		struct dpaa_capwap_tunnel *p_tunnel,
		struct dpaa_capwap_domain_tunnel_out_params *out_tunnel_params);

struct dpaa_capwap_domain_fqs *get_domain_fqs(void);

int op_init(struct t_Port *port, struct net_device *net_dev);
int capwap_fq_pre_init(struct dpaa_capwap_domain *capwap_domain);
int capwap_tunnel_drv_init(struct dpaa_capwap_domain *domain);
void get_capwap_bp(struct net_device *net_dev,
		struct dpaa_capwap_domain *capwap_domain);
int capwap_br_init(struct dpaa_capwap_domain *domain);
uint16_t get_flow_index(bool is_dtls, bool is_control_tunnel);
int capwap_kernel_rx_ctl(struct capwap_domain_kernel_rx_ctl *rx_ctl);
struct dpaa_capwap_domain *dpaa_capwap_domain_config(
		struct dpaa_capwap_domain_params *new_capwap_domain);
int dpaa_capwap_domain_init(struct dpaa_capwap_domain *capwap_domain);
int dpaa_capwap_domain_remove_tunnel(struct dpaa_capwap_tunnel *p_tunnel);

#endif /* __DPAA_CAPWAP_DOMAIN_H */
