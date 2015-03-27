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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <compat.h>
#include <linux/kernel.h>
#include <linux/gfp.h>
#include <linux/slab.h>
#include <linux/of_platform.h>
#include <linux/fsl_qman.h>

#include "ncsw_ext.h"
#include "fm_ext.h"
#include "fm_port_ext.h"
#include "fm_pcd_ext.h"
#include "dpaa_capwap_domain.h"
#include "dpaa_capwap_fq.h"

#define GET_UPPER_TUNNEL_ID(tunnel_id) \
	(tunnel_id + capwap_domain->max_num_of_tunnels)

#define DTLS_ENCAP_OPTION_W_B   (1 << 1) /* 0x02 */
#define DTLS_ENCAP_OPTION_E_I   (1 << 0) /* 0x01 */

#define DTLS_DECAP_OPTION_NO_ARS (0 << 6)
#define DTLS_DECAP_OPTION_32_ENTRY_ARS (1 << 6)
#define DTLS_DECAP_OPTION_64_ENTRY_ARS (3 << 6)

struct capwap_alg_suite capwap_algs[] = CAPWAP_ALGS;

static int set_outbound_pcd(struct dpaa_capwap_domain *capwap_domain)
{
	t_FmPcdCcNodeParams *cc_node_param;
	t_FmPcdCcTreeParams *cc_tree_param;
	t_FmPcdNetEnvParams *net_env_params;
	t_FmPortPcdParams *pcd_param;
	t_FmPortPcdCcParams cc_param;
	struct t_Port *out_op_port;
	int i = 0;
	int err = 0;

	out_op_port = &capwap_domain->out_op_port;
	/* Network Environment initialization */
	net_env_params = kzalloc(sizeof(t_FmPcdNetEnvParams), GFP_KERNEL);
	if (!net_env_params)
		return -ENOMEM;
	net_env_params->numOfDistinctionUnits = 0;
	out_op_port->fmPcdInfo.h_NetEnv =
		FM_PCD_NetEnvCharacteristicsSet(capwap_domain->h_fm_pcd,
					net_env_params);
	if (!out_op_port->fmPcdInfo.h_NetEnv) {
		pr_err("FM_PCD_NetEnvCharacteristicsSet error\n");
		kfree(net_env_params);
		return -EINVAL;
	}
	kfree(net_env_params);

	/* Nodes & Tree */
	cc_node_param = kzalloc(sizeof(t_FmPcdCcNodeParams), GFP_KERNEL);
	if (!cc_node_param)
		return -ENOMEM;
	cc_node_param->extractCcParams.type  = e_FM_PCD_EXTRACT_NON_HDR;
	cc_node_param->extractCcParams.extractNonHdr.src =
					e_FM_PCD_EXTRACT_FROM_FLOW_ID;
	cc_node_param->extractCcParams.extractNonHdr.action =
					e_FM_PCD_ACTION_INDEXED_LOOKUP;
	cc_node_param->extractCcParams.extractNonHdr.offset = 0;
	cc_node_param->extractCcParams.extractNonHdr.size = 2;

	cc_node_param->keysParams.numOfKeys =
			capwap_domain->out_op_port.numOfTxQs;
	cc_node_param->extractCcParams.extractNonHdr.icIndxMask =
		(uint16_t)((cc_node_param->keysParams.numOfKeys - 1) << 4);
	cc_node_param->keysParams.keySize = 2;
	cc_node_param->keysParams.maxNumOfKeys =
				cc_node_param->keysParams.numOfKeys;
	cc_node_param->keysParams.statisticsMode =
				e_FM_PCD_CC_STATS_MODE_BYTE_AND_FRAME;

	for (i = 0; i < cc_node_param->keysParams.numOfKeys; i++) {
		cc_node_param->keysParams.keyParams[i].ccNextEngineParams.
						nextEngine = e_FM_PCD_DONE;
		cc_node_param->keysParams.keyParams[i].ccNextEngineParams.
			params.enqueueParams.action = e_FM_PCD_DROP_FRAME;
		cc_node_param->keysParams.keyParams[i].ccNextEngineParams.
			statisticsEn = TRUE;
	}

	out_op_port->fmPcdInfo.h_CcNodes[0] =
			FM_PCD_MatchTableSet(capwap_domain->h_fm_pcd,
					cc_node_param);
	if (!out_op_port->fmPcdInfo.h_CcNodes[0]) {
		pr_err("FM_PCD_MatchTableSet failed\n");
		kfree(cc_node_param);
		return -EBUSY;
	}
	out_op_port->fmPcdInfo.h_CcNodesOrder[out_op_port->fmPcdInfo
		.numOfCcNodes++] = out_op_port->fmPcdInfo.h_CcNodes[0];
	kfree(cc_node_param);

	/* define a tree with 1 group of size 1 only,
	 * i.e. all traffic goes to this node
	 */
	cc_tree_param = kzalloc(sizeof(t_FmPcdCcTreeParams), GFP_KERNEL);
	if (!cc_tree_param)
		return -ENOMEM;
	cc_tree_param->numOfGrps = 1;
	cc_tree_param->h_NetEnv = out_op_port->fmPcdInfo.h_NetEnv;

	cc_tree_param->ccGrpParams[0].numOfDistinctionUnits = 0;
	cc_tree_param->ccGrpParams[0].nextEnginePerEntriesInGrp[0]
		.nextEngine = e_FM_PCD_CC;
	cc_tree_param->ccGrpParams[0].nextEnginePerEntriesInGrp[0]
		.params.ccParams.h_CcNode = out_op_port->fmPcdInfo.h_CcNodes[0];

	/* Build tree */
	out_op_port->fmPcdInfo.h_CcTree =
		FM_PCD_CcRootBuild(capwap_domain->h_fm_pcd, cc_tree_param);
	if (!out_op_port->fmPcdInfo.h_CcTree) {
		pr_err("FM_PCD_CcRootBuild failed\n");
		kfree(cc_tree_param);
		return -EBUSY;
	}
	kfree(cc_tree_param);

	/* bind port to PCD properties */
	/* initialize PCD parameters */
	pcd_param = kzalloc(sizeof(t_FmPortPcdParams), GFP_KERNEL);
	if (!pcd_param)
		return -ENOMEM;
	pcd_param->h_NetEnv = out_op_port->fmPcdInfo.h_NetEnv;
	pcd_param->pcdSupport = e_FM_PORT_PCD_SUPPORT_CC_ONLY;
	pcd_param->p_CcParams = &cc_param;

	/* initialize coarse classification parameters */
	memset(&cc_param, 0, sizeof(t_FmPortPcdCcParams));
	cc_param.h_CcTree = out_op_port->fmPcdInfo.h_CcTree;

	FM_PORT_Disable(capwap_domain->h_op_port);
	err = FM_PORT_SetPCD(capwap_domain->h_op_port, pcd_param);
	FM_PORT_Enable(capwap_domain->h_op_port);
	kfree(pcd_param);

	capwap_domain->h_flow_id_table = out_op_port->fmPcdInfo.h_CcNodes[0];

	return err;
}

static int calc_key_size(bool ipv6,
		uint32_t valid_key_fields,
		uint8_t *key_size)
{
	uint32_t i, field_mask = 0;

	for (i = 0; i < sizeof(uint32_t) * BITS_PER_BYTE; i++) {
		field_mask = (uint32_t)(1 << i);
		switch (valid_key_fields & field_mask) {
		case DPAA_CAPWAP_DOMAIN_KEY_FIELD_SIP:
		case DPAA_CAPWAP_DOMAIN_KEY_FIELD_DIP:
			if (ipv6)
				*key_size += NET_HEADER_FIELD_IPv6_ADDR_SIZE;
			else
				*key_size += NET_HEADER_FIELD_IPv4_ADDR_SIZE;
			break;
		case DPAA_CAPWAP_DOMAIN_KEY_FIELD_PROTO:/* same size for ipv6 */
			*key_size += NET_HEADER_FIELD_IPv4_PROTO_SIZE;
			break;
		case DPAA_CAPWAP_DOMAIN_KEY_FIELD_SPORT:
		case DPAA_CAPWAP_DOMAIN_KEY_FIELD_DPORT:
			*key_size += NET_HEADER_FIELD_UDP_PORT_SIZE;
			break;
		case DPAA_CAPWAP_DOMAIN_KEY_FIELD_PREAMBLE:
			*key_size += 1;
			break;
		case DPAA_CAPWAP_DOMAIN_KEY_FIELD_DTLS_TYPE:
			*key_size += 1;
			break;
		default:
			break;
		}
	}
	return 0;
}

static const struct of_device_id dpa_capwap_match[] = {
	{
		.compatible	= "fsl,dpa-ethernet-shared"
	},
	{}
};

static struct net_device *get_net_dev(void)
{
	struct device_node *capwap_eth_node;
	struct platform_device *capwap_eth_dev;
	struct net_device *net_dev;

	capwap_eth_node = of_find_matching_node(NULL, &dpa_capwap_match[0]);
	if (!capwap_eth_node) {
		pr_err("Couln't find the device_node CAPWAP Ethernet, check the device tree\n");
		return NULL;
	}

	capwap_eth_dev = of_find_device_by_node(capwap_eth_node);
	if (!capwap_eth_dev) {
		pr_err("CAPWAP Ethernet of_device null\n");
		return NULL;
	}

	net_dev = dev_get_drvdata(&capwap_eth_dev->dev);

	return net_dev;

}

struct dpaa_capwap_domain *dpaa_capwap_domain_config(
		struct dpaa_capwap_domain_params *new_capwap_domain)
{
	struct dpaa_capwap_domain *capwap_domain = NULL;
	int ret = 0;

	pr_info("dpaa_capwap_domain_config\n");

	capwap_domain = kzalloc(sizeof(struct dpaa_capwap_domain), GFP_KERNEL);
	if (!capwap_domain) {
		pr_err("no memory for DPAA CAPWAP DOMAIN\n");
		return NULL;
	}

	capwap_domain->h_fm_pcd = new_capwap_domain->h_fm_pcd;
	capwap_domain->max_num_of_tunnels =
		new_capwap_domain->max_num_of_tunnels;
	capwap_domain->support_ipv6 = new_capwap_domain->support_ipv6;

	capwap_domain->post_dec_op_port.fm_id =
		new_capwap_domain->inbound_op.fm_id;
	capwap_domain->post_dec_op_port.port_id =
		new_capwap_domain->inbound_op.port_id;
	capwap_domain->out_op_port.fm_id =
		new_capwap_domain->outbound_op.fm_id;
	capwap_domain->out_op_port.port_id =
		new_capwap_domain->outbound_op.port_id;
	capwap_domain->h_op_port = new_capwap_domain->outbound_op.port_handle;
	capwap_domain->h_em_table =
		new_capwap_domain->inbound_pre_params.h_Table;
	capwap_domain->key_fields =
		new_capwap_domain->inbound_pre_params.key_fields;
	capwap_domain->mask_fields =
		new_capwap_domain->inbound_pre_params.mask_fields;

	calc_key_size(capwap_domain->support_ipv6,
			capwap_domain->key_fields, &capwap_domain->key_size);
	if (capwap_domain->key_size > TABLE_KEY_MAX_SIZE) {
		kfree(capwap_domain);
		pr_err("hash key size exceeded %d bytes\n", TABLE_KEY_MAX_SIZE);
		return NULL;
	}

	INIT_LIST_HEAD(&capwap_domain->in_tunnel_list);
	INIT_LIST_HEAD(&capwap_domain->out_tunnel_list);

	capwap_domain->post_dec_op_port.numOfTxQs  =
		capwap_domain->max_num_of_tunnels;
	capwap_domain->out_op_port.numOfTxQs  =
		capwap_domain->max_num_of_tunnels*2;

	ret = get_sec_info(&capwap_domain->secinfo);
	if (ret)
		return NULL;

	pr_info("Capwap-Domain configuration done\n");

	return capwap_domain;
}

static void dump_fq_ids(const struct dpaa_capwap_domain *domain)
{
	pr_info("***********************outbound***********************\n");
	pr_info("DTLS-Control:  Core--(0x%x)-->OP--(0x%x)-->Sec--(0x%x)-->OP--(0x%x)-->Tx\n",
			domain->fqs->outbound_core_tx_fqs.fqid_base + 0,
			domain->fqs->outbound_op_tx_fqs.fqid_base + 0,
			domain->fqs->outbound_sec_to_op_fqs.fqid_base + 0,
			domain->fqs->outbound_op_tx_fqs.fqid_base + 4 + 0);
	pr_info("DTLS-Data:     Core--(0x%x)-->OP--(0x%x)-->Sec--(0x%x)-->OP--(0x%x)-->Tx\n",
			domain->fqs->outbound_core_tx_fqs.fqid_base + 1,
			domain->fqs->outbound_op_tx_fqs.fqid_base + 1,
			domain->fqs->outbound_sec_to_op_fqs.fqid_base + 1,
			domain->fqs->outbound_op_tx_fqs.fqid_base + 4 + 1);
	pr_info("N-DTLS-Control:Core--(0x%x)-->OP--(0x%x)-->OP--(0x%x)-->Tx\n",
			domain->fqs->outbound_core_tx_fqs.fqid_base + 2,
			domain->fqs->outbound_op_tx_fqs.fqid_base + 2,
			domain->fqs->outbound_op_tx_fqs.fqid_base + 4 + 2);
	pr_info("N-DTLS-Data:   Core--(0x%x)-->OP--(0x%x)-->OP--(0x%x)-->Tx\n",
			domain->fqs->outbound_core_tx_fqs.fqid_base + 3,
			domain->fqs->outbound_op_tx_fqs.fqid_base + 3,
			domain->fqs->outbound_op_tx_fqs.fqid_base + 4 + 3);
	pr_info("***********************inbound***********************\n");
	pr_info("DTLS-Control:  Rx--(0x%x)-->Sec--(0x%x)-->OP--(0x%x)-->Core\n",
			domain->fqs->inbound_eth_rx_fqs.fqid_base + 0,
			domain->fqs->inbound_sec_to_op_fqs.fqid_base + 0,
			domain->fqs->inbound_core_rx_fqs.fqid_base + 0);
	pr_info("DTLS-Data:     Rx--(0x%x)-->Sec--(0x%x)-->OP--(0x%x)-->Core\n",
			domain->fqs->inbound_eth_rx_fqs.fqid_base + 1,
			domain->fqs->inbound_sec_to_op_fqs.fqid_base + 1,
			domain->fqs->inbound_core_rx_fqs.fqid_base + 1);
	pr_info("N-DTLS-Control:Rx--(0x%x)-->OP--(0x%x)-->Core\n",
			domain->fqs->inbound_eth_rx_fqs.fqid_base + 2,
			domain->fqs->inbound_core_rx_fqs.fqid_base + 2);
	pr_info("N-DTLS-Data:   Rx--(0x%x)-->OP--(0x%x)-->Core\n",
			domain->fqs->inbound_eth_rx_fqs.fqid_base + 3,
			domain->fqs->inbound_core_rx_fqs.fqid_base + 3);
	pr_info("N-CAPWAP:      0x%x\n",
			domain->fqs->inbound_eth_rx_fqs.fqid_base + 4);
}

int dpaa_capwap_domain_init(struct dpaa_capwap_domain *capwap_domain)
{
	struct dpaa_capwap_tunnel *p_tunnel;
	int err = 0;
	uint32_t i;
	struct net_device *net_dev = NULL;

	if (!capwap_domain) {
		pr_err("failed for %s\n", __func__);
		return -EINVAL;
	}

	for (i = 0; i < capwap_domain->max_num_of_tunnels; i++) {
		p_tunnel =
			kzalloc(sizeof(struct dpaa_capwap_tunnel), GFP_KERNEL);
		if (!p_tunnel)
			goto no_memory;

		p_tunnel->auth_data.auth_key = kzalloc(DTLS_KEY_MAX_SIZE,
				GFP_KERNEL | GFP_DMA);
		if (!p_tunnel->auth_data.auth_key)
			goto no_memory;

		p_tunnel->auth_data.split_key = kzalloc(DTLS_KEY_MAX_SIZE,
				GFP_KERNEL | GFP_DMA);
		if (!p_tunnel->auth_data.split_key)
			goto no_memory;

		p_tunnel->cipher_data.cipher_key = kzalloc(DTLS_KEY_MAX_SIZE,
				GFP_KERNEL | GFP_DMA);
		if (!p_tunnel->cipher_data.cipher_key)
			goto no_memory;

		p_tunnel->p_key = kzalloc(capwap_domain->key_size, GFP_KERNEL);
		if (!p_tunnel->p_key)
			goto no_memory;

		p_tunnel->p_mask = kzalloc(capwap_domain->key_size, GFP_KERNEL);
		if (!p_tunnel->p_mask)
			goto no_memory;

		p_tunnel->tunnel_dir = e_DPAA_CAPWAP_DOMAIN_DIR_INBOUND;
		p_tunnel->dpaa_capwap_domain = capwap_domain;
		p_tunnel->tunnel_id = i;
		INIT_LIST_HEAD(&p_tunnel->fq_chain_head);
		enqueue_tunnel_obj(&capwap_domain->in_tunnel_list, p_tunnel);

		p_tunnel =
			kzalloc(sizeof(struct dpaa_capwap_tunnel), GFP_KERNEL);
		if (!p_tunnel)
			goto no_memory;

		p_tunnel->auth_data.auth_key = kzalloc(DTLS_KEY_MAX_SIZE,
				GFP_KERNEL | GFP_DMA);
		if (!p_tunnel->auth_data.auth_key)
			goto no_memory;

		p_tunnel->auth_data.split_key = kzalloc(DTLS_KEY_MAX_SIZE,
				GFP_KERNEL | GFP_DMA);
		if (!p_tunnel->auth_data.split_key)
			goto no_memory;

		p_tunnel->cipher_data.cipher_key = kzalloc(DTLS_KEY_MAX_SIZE,
				GFP_KERNEL | GFP_DMA);
		if (!p_tunnel->cipher_data.cipher_key)
			goto no_memory;

		p_tunnel->tunnel_dir = e_DPAA_CAPWAP_DOMAIN_DIR_OUTBOUND;
		p_tunnel->dpaa_capwap_domain = capwap_domain;
		p_tunnel->tunnel_id = i;
		INIT_LIST_HEAD(&p_tunnel->fq_chain_head);
		enqueue_tunnel_obj(&capwap_domain->out_tunnel_list, p_tunnel);
	}

	err = set_outbound_pcd(capwap_domain);
	if (err) {
		pr_err("set_outbound_pcd error:%d\n", err);
		return err;
	}

	net_dev = get_net_dev();
	if (net_dev == NULL) {
		pr_err("No CAPWAP Ethernet Device\n");
		return -ENODEV;
	}

	capwap_domain->net_dev = net_dev;
	get_capwap_bp(net_dev, capwap_domain);

	err = op_init(&capwap_domain->post_dec_op_port, net_dev);
	if (err) {
		pr_err("outbound OP init failed\n");
		return err;
	}
	err = op_init(&capwap_domain->out_op_port, net_dev);
	if (err) {
		pr_err("inbound OP init failed\n");
		return err;
	}

	capwap_domain->fqs = get_domain_fqs();
	if (capwap_domain->fqs == NULL) {
		pr_err("Alloc fqs for capwap domain failed\n");
		return err;
	}
	err = capwap_fq_pre_init(capwap_domain);
	if (err) {
		pr_err("pre-init fq for capwap domain failed\n");
		return err;
	}
	dump_fq_ids(capwap_domain);

	err = capwap_tunnel_drv_init(capwap_domain);
	if (err) {
		pr_err("Capwap Tunnel Driver init failed\n");
		return err;
	}

	err = capwap_br_init(capwap_domain);
	if (err) {
		pr_err("Capwap Bridge Driver init failed\n");
		return err;
	}
	return 0;

no_memory:
	if (p_tunnel) {
		kfree(p_tunnel->auth_data.auth_key);
		kfree(p_tunnel->auth_data.split_key);
		kfree(p_tunnel->cipher_data.cipher_key);
		kfree(p_tunnel->p_key);
		kfree(p_tunnel->p_mask);
		kfree(p_tunnel);
	}
	p_tunnel = dequeue_tunnel_obj(&capwap_domain->in_tunnel_list);
	while (p_tunnel) {
		kfree(p_tunnel->auth_data.auth_key);
		kfree(p_tunnel->auth_data.split_key);
		kfree(p_tunnel->cipher_data.cipher_key);
		kfree(p_tunnel->p_key);
		kfree(p_tunnel->p_mask);
		kfree(p_tunnel);
		p_tunnel = dequeue_tunnel_obj(&capwap_domain->in_tunnel_list);
	}
	p_tunnel = dequeue_tunnel_obj(&capwap_domain->out_tunnel_list);
	while (p_tunnel) {
		kfree(p_tunnel->auth_data.auth_key);
		kfree(p_tunnel->auth_data.split_key);
		kfree(p_tunnel->cipher_data.cipher_key);
		kfree(p_tunnel);
		p_tunnel = dequeue_tunnel_obj(&capwap_domain->in_tunnel_list);
	}
	pr_err("no memory for malloc in %s\n", __func__);
	return -ENOMEM;

}

uint16_t get_flow_index(bool is_dtls, bool is_control_tunnel)
{
	if (is_dtls && is_control_tunnel)
		return 0;
	else if (is_dtls && !is_control_tunnel)
		return 1;
	else if (!is_dtls && is_control_tunnel)
		return 2;
	else
		return 3;
}

int add_in_tunnel(struct dpaa_capwap_domain *capwap_domain,
		struct dpaa_capwap_tunnel *p_tunnel,
		struct dpaa_capwap_domain_tunnel_in_params *in_tunnel_params)
{
	t_FmPcdManipParams fm_pcd_manip_params;
	t_FmPcdCcKeyParams key_params;
	uint8_t match_key_size = 0;
	int err = 0;
	struct auth_params *auth;
	struct cipher_params *cipher;
	struct dtls_block_decap_pdb *pdb;
	struct dtls_decap_descriptor_t *preheader_initdesc = NULL;
	struct qman_fq *fq = NULL;
	uint16_t desc_len;
	unsigned char *buff_start = NULL;
	u64 context_a = 0;
	uint32_t context_b = 0;
	uint16_t channel;
	uint16_t flow_index;
	dma_addr_t dma_addr = 0;
	struct qman_fq_chain *fq_node;

	flow_index = get_flow_index(in_tunnel_params->dtls,
			in_tunnel_params->is_control);

	/* Configure of the DTLS decryption parameters */
	if (in_tunnel_params->dtls) {
		preheader_initdesc =
			kzalloc(sizeof(struct dtls_decap_descriptor_t),
				       GFP_KERNEL);
		if (preheader_initdesc == NULL) {
			pr_err("error: %s: No More Buffers left for Descriptor\n",
					__func__);
			return -ENOMEM;
		}

		desc_len = (sizeof(struct dtls_decap_descriptor_t) -
				sizeof(struct preheader_t)) / sizeof(uint32_t);

		buff_start = (unsigned char *)preheader_initdesc +
			sizeof(struct preheader_t);

		pdb = &preheader_initdesc->pdb;

		if (in_tunnel_params->dtls_params.wbIv)
			pdb->options |= DTLS_ENCAP_OPTION_W_B;

		switch (in_tunnel_params->dtls_params.arw) {
		case e_DTLS_ARS_32:
			pdb->options |= DTLS_DECAP_OPTION_32_ENTRY_ARS;
			break;
		case e_DTLS_ARS_64:
			pdb->options |= DTLS_DECAP_OPTION_64_ENTRY_ARS;
			break;
		default:
			break;
		}

		pdb->epoch = in_tunnel_params->dtls_params.epoch;
		memcpy(pdb->seq_num, &in_tunnel_params->dtls_params.seq_num, 6);
		memcpy(pdb->iv, in_tunnel_params->dtls_params.p_Iv, 16);

		auth = &p_tunnel->auth_data;
		cipher = &p_tunnel->cipher_data;

		if ((in_tunnel_params->dtls_params.cipher_key_len / 8) >
			       DTLS_KEY_MAX_SIZE) {
			pr_err("key size exceeded %d bytes", DTLS_KEY_MAX_SIZE);
			kfree(preheader_initdesc);
			return -EINVAL;
		}

		auth->auth_key_len =
			in_tunnel_params->dtls_params.auth_key_len / 8;
		memcpy(auth->auth_key, in_tunnel_params->dtls_params.auth_key,
				auth->auth_key_len);
		cipher->cipher_key_len =
			in_tunnel_params->dtls_params.cipher_key_len / 8;
		memcpy(cipher->cipher_key,
				in_tunnel_params->dtls_params.cipher_key,
			       cipher->cipher_key_len);
		auth->auth_type =
			capwap_algs[in_tunnel_params->dtls_params.alg_type]
			.auth_alg;
		cipher->cipher_type =
			capwap_algs[in_tunnel_params->dtls_params.alg_type]
			.cipher_alg;

		err = generate_split_key(auth, &capwap_domain->secinfo);
		if (err) {
			pr_err("error: %s: generate splitkey error\n",
				       __func__);
			kfree(preheader_initdesc);
			return err;
		}

		cnstr_shdsc_dtls_decap((uint32_t *) buff_start, &desc_len,
			cipher, auth, 4);

		preheader_initdesc->prehdr.lo.field.pool_id =
			capwap_domain->bpid;
		preheader_initdesc->prehdr.lo.field.pool_buffer_size =
			capwap_domain->bp_size;
		preheader_initdesc->prehdr.lo.field.offset = 1;
		preheader_initdesc->prehdr.hi.field.idlen = desc_len;

		p_tunnel->sec_desc = (t_Handle)preheader_initdesc;

		dma_addr = dma_map_single(capwap_domain->secinfo.jrdev,
				p_tunnel->sec_desc,
				sizeof(struct preheader_t) + desc_len * 4,
				DMA_TO_DEVICE);
		if (dma_mapping_error(capwap_domain->secinfo.jrdev, dma_addr)) {
			pr_err("Unable to DMA map the dtls decap-descriptor address\n");
			kfree(preheader_initdesc);
			return -ENOMEM;
		}

		/* Init FQ from Rx port to SEC */
		fq = (struct qman_fq *)
			capwap_domain->fqs->inbound_eth_rx_fqs.fq_base;
		fq[flow_index].fqid =
			capwap_domain->fqs->inbound_eth_rx_fqs.fqid_base +
			flow_index;
		channel = qm_channel_caam;
		context_a = (u64)dma_addr;
		context_b = capwap_domain->fqs->inbound_sec_to_op_fqs.fqid_base
			+ flow_index;
		err = capwap_fq_tx_init(&fq[flow_index], channel, context_a,
				context_b, 3);
		if (err)
			goto error;

		fq_node = kzalloc(sizeof(struct qman_fq_chain), GFP_KERNEL);
		if (fq_node == NULL) {
			err = -ENOMEM;
			goto error;
		}
		fq_node->fq = &fq[flow_index];
		list_add_tail(&fq_node->list, &p_tunnel->fq_chain_head);
	}

	/* Pre SEC Section */
	memset(&key_params, 0, sizeof(t_FmPcdCcKeyParams));
	key_params.p_Key  = p_tunnel->p_key;
	key_params.p_Mask = p_tunnel->p_mask;

	memset(key_params.p_Key, 0, capwap_domain->key_size);
	memset(key_params.p_Mask, 0xFF, capwap_domain->key_size);

	if (capwap_domain->key_fields & DPAA_CAPWAP_DOMAIN_KEY_FIELD_SIP) {
		memcpy(&key_params.p_Key[match_key_size],
			(in_tunnel_params->sip.ipv6) ?
			in_tunnel_params->sip.u.ipv6_addr :
			(uint8_t *)&in_tunnel_params->sip.u.ipv4_addr,
			(in_tunnel_params->sip.ipv6) ?
			NET_HEADER_FIELD_IPv6_ADDR_SIZE :
			NET_HEADER_FIELD_IPv4_ADDR_SIZE);

		memcpy(&key_params.p_Mask[match_key_size],
			(in_tunnel_params->sip_mask.ipv6) ?
			in_tunnel_params->sip_mask.u.ipv6_mask :
			(uint8_t *)&in_tunnel_params->sip_mask.u.ipv4_mask,
			(in_tunnel_params->sip_mask.ipv6) ?
			NET_HEADER_FIELD_IPv6_ADDR_SIZE :
			NET_HEADER_FIELD_IPv4_ADDR_SIZE);

		match_key_size += (in_tunnel_params->sip.ipv6) ?
			NET_HEADER_FIELD_IPv6_ADDR_SIZE :
			NET_HEADER_FIELD_IPv4_ADDR_SIZE;
	}
	if (capwap_domain->key_fields & DPAA_CAPWAP_DOMAIN_KEY_FIELD_DIP) {
		memcpy(&key_params.p_Key[match_key_size],
			(in_tunnel_params->dip.ipv6) ?
			in_tunnel_params->dip.u.ipv6_addr :
			(uint8_t *)&in_tunnel_params->dip.u.ipv4_addr,
			(in_tunnel_params->dip.ipv6) ?
			NET_HEADER_FIELD_IPv6_ADDR_SIZE :
			NET_HEADER_FIELD_IPv4_ADDR_SIZE);

		memcpy(&key_params.p_Mask[match_key_size],
			(in_tunnel_params->dip_mask.ipv6) ?
			in_tunnel_params->dip_mask.u.ipv6_mask :
			(uint8_t *)&in_tunnel_params->dip_mask.u.ipv4_mask,
			(in_tunnel_params->dip_mask.ipv6) ?
			NET_HEADER_FIELD_IPv6_ADDR_SIZE :
			NET_HEADER_FIELD_IPv4_ADDR_SIZE);

		match_key_size += (in_tunnel_params->dip.ipv6)
			? NET_HEADER_FIELD_IPv6_ADDR_SIZE :
			NET_HEADER_FIELD_IPv4_ADDR_SIZE;
	}

	if (capwap_domain->key_fields & DPAA_CAPWAP_DOMAIN_KEY_FIELD_PROTO) {
		key_params.p_Key[match_key_size] = IPPROTO_UDP; /* UDP */
		match_key_size += NET_HEADER_FIELD_IPv4_PROTO_SIZE;
	}

	if (capwap_domain->key_fields & DPAA_CAPWAP_DOMAIN_KEY_FIELD_SPORT) {
		memcpy(&key_params.p_Key[match_key_size],
				&in_tunnel_params->src_port,
				NET_HEADER_FIELD_UDP_PORT_SIZE);
		match_key_size += NET_HEADER_FIELD_UDP_PORT_SIZE;
	}

	if (capwap_domain->key_fields & DPAA_CAPWAP_DOMAIN_KEY_FIELD_DPORT) {
		memcpy(&key_params.p_Key[match_key_size],
				&in_tunnel_params->dst_port,
				NET_HEADER_FIELD_UDP_PORT_SIZE);
		match_key_size += NET_HEADER_FIELD_UDP_PORT_SIZE;
	}

	if (capwap_domain->key_fields & DPAA_CAPWAP_DOMAIN_KEY_FIELD_PREAMBLE) {
		key_params.p_Key[match_key_size] = (in_tunnel_params->dtls) ?
						1 : 0; /* DTLS or not */
		match_key_size += 1;
	}

	if ((in_tunnel_params->dtls) && (capwap_domain->key_fields &
				DPAA_CAPWAP_DOMAIN_KEY_FIELD_DTLS_TYPE)) {
		key_params.p_Key[match_key_size] =
			in_tunnel_params->dtls_params.type;
		match_key_size += 1;
	}

	memset(&key_params.p_Mask[match_key_size], 0,
			capwap_domain->key_size - match_key_size);

	memset(&fm_pcd_manip_params, 0, sizeof(fm_pcd_manip_params));
	fm_pcd_manip_params.type = e_FM_PCD_MANIP_HDR;
	fm_pcd_manip_params.u.hdr.dontParseAfterManip = TRUE;
	fm_pcd_manip_params.u.hdr.rmv = TRUE;
	fm_pcd_manip_params.u.hdr.rmvParams.type = e_FM_PCD_MANIP_RMV_BY_HDR;
	fm_pcd_manip_params.u.hdr.rmvParams.u.byHdr.type =
		e_FM_PCD_MANIP_RMV_BY_HDR_FROM_START;
	fm_pcd_manip_params.u.hdr.rmvParams.u.byHdr.u.hdrInfo.hdr =
		(in_tunnel_params->dtls) ? HEADER_TYPE_CAPWAP_DTLS :
		HEADER_TYPE_CAPWAP;
	p_tunnel->h_hm_till_manip = FM_PCD_ManipNodeSet(capwap_domain->h_fm_pcd,
			&fm_pcd_manip_params);
	if (!p_tunnel->h_hm_till_manip) {
		pr_err("FM_PCD_ManipNodeSet failed");
		err = -EINVAL;
		goto error;
	}

	key_params.ccNextEngineParams.nextEngine = e_FM_PCD_DONE;
	key_params.ccNextEngineParams.params.enqueueParams.action =
		e_FM_PCD_ENQ_FRAME;
	key_params.ccNextEngineParams.statisticsEn = TRUE;
	key_params.ccNextEngineParams.params.enqueueParams.overrideFqid = TRUE;
	key_params.ccNextEngineParams.params.enqueueParams.newFqid =
		capwap_domain->fqs->inbound_eth_rx_fqs.fqid_base + flow_index;
	key_params.ccNextEngineParams.h_Manip = p_tunnel->h_hm_till_manip;

	p_tunnel->key_index = flow_index;

	err = FM_PCD_MatchTableAddKey(capwap_domain->h_em_table, flow_index,
			capwap_domain->key_size, &key_params);
	if (err != E_OK)
		goto error;

	return E_OK;
error:
	if (in_tunnel_params->dtls) {
		qman_destroy_fq(&fq[flow_index], 0);
		dma_unmap_single(capwap_domain->secinfo.jrdev,
				dma_addr,
				sizeof(struct preheader_t) + desc_len * 4,
				DMA_TO_DEVICE);
		kfree(preheader_initdesc);
	}
	return err;
}

static int remove_in_tunnel(struct dpaa_capwap_tunnel *p_tunnel)
{
	struct dpaa_capwap_domain *capwap_domain;
	struct qman_fq_chain *fq_node, *tmp;
	int err;

	capwap_domain = p_tunnel->dpaa_capwap_domain;
	/* Take care of ingress side  */
	/* First, remove the match-key for this flow */
	err = FM_PCD_MatchTableRemoveKey(capwap_domain->h_em_table,
					p_tunnel->key_index);
	if (err != E_OK)
		return err;

	if (p_tunnel->h_hm_till_manip) {
		FM_PCD_ManipNodeDelete(p_tunnel->h_hm_till_manip);
		p_tunnel->h_hm_till_manip = NULL;
	}

	list_for_each_entry_safe(fq_node, tmp, &p_tunnel->fq_chain_head, list) {
		teardown_fq(fq_node->fq);
		list_del(&fq_node->list);
	}

	return 0;
}

int add_out_tunnel(struct dpaa_capwap_domain *capwap_domain,
		struct dpaa_capwap_tunnel *p_tunnel,
		struct dpaa_capwap_domain_tunnel_out_params *out_tunnel_params)
{
	t_FmPcdCcNextEngineParams *fm_pcd_cc_next_engine_params = NULL;
	t_FmPcdManipParams *fm_pcd_manip_params = NULL;
	t_FmPcdCcNodeParams *cc_node_param = NULL;
	struct t_Port *out_op_port;
	uint32_t fqid;
	int err = 0;
	struct dtls_block_encap_pdb *pdb;
	struct auth_params *auth;
	struct cipher_params *cipher;
	struct dtls_encap_descriptor_t *preheader_initdesc = NULL;
	struct qman_fq *fq = NULL;
	uint16_t desc_len;
	unsigned char *buff_start = NULL;
	u64 context_a = 0;
	uint32_t context_b = 0;
	uint16_t channel;
	uint16_t flow_index;
	dma_addr_t dma_addr = 0;
	struct qman_fq_chain *fq_node;

	if (!capwap_domain || !p_tunnel)
		return -EINVAL;

	if (!out_tunnel_params->p_ether_header ||
			!out_tunnel_params->eth_header_size ||
			!out_tunnel_params->p_ip_header ||
			!out_tunnel_params->ip_header_size ||
			!out_tunnel_params->p_udp_header) {
		pr_err("must provide ETH+IP+UDP headers and sizes\n");
		return -EINVAL;
	}

	if (!out_tunnel_params->p_NextEngineParams) {
		pr_err("must provide next-engine-params\n");
		return -EINVAL;
	}

	if (out_tunnel_params->p_NextEngineParams->h_Manip) {
		pr_err("cannot provide next-engine-params with pointer to manipulation\n");
		return -EINVAL;
	}

	flow_index = get_flow_index(out_tunnel_params->dtls,
			out_tunnel_params->is_control);

	/* Post SEC Section */
	fm_pcd_manip_params = kzalloc(sizeof(t_FmPcdManipParams), GFP_KERNEL);
	if (fm_pcd_manip_params == NULL)
		return -ENOMEM;
	fm_pcd_manip_params->type = e_FM_PCD_MANIP_HDR;
	fm_pcd_manip_params->u.hdr.dontParseAfterManip = TRUE;
	fm_pcd_manip_params->u.hdr.insrt = TRUE;
	fm_pcd_manip_params->u.hdr.insrtParams.type =
		e_FM_PCD_MANIP_INSRT_GENERIC;
	fm_pcd_manip_params->u.hdr.insrtParams.u.generic.offset = 0;
	fm_pcd_manip_params->u.hdr.insrtParams.u.generic.size =
		out_tunnel_params->eth_header_size;
	fm_pcd_manip_params->u.hdr.insrtParams.u.generic.p_Data =
		out_tunnel_params->p_ether_header;
	p_tunnel->h_hm_l2 = FM_PCD_ManipNodeSet(capwap_domain->h_fm_pcd,
			fm_pcd_manip_params);
	if (!p_tunnel->h_hm_l2) {
		pr_err("FM_PCD_ManipNodeSet failed in add_out_tunnel: Hml2\n");
		err = -EINVAL;
		goto out;
	}

	memset(fm_pcd_manip_params, 0, sizeof(t_FmPcdManipParams));
	fm_pcd_manip_params->type = e_FM_PCD_MANIP_HDR;
	fm_pcd_manip_params->u.hdr.dontParseAfterManip = TRUE;
	fm_pcd_manip_params->u.hdr.insrt = TRUE;
	fm_pcd_manip_params->u.hdr.insrtParams.type =
		e_FM_PCD_MANIP_INSRT_BY_HDR;
	fm_pcd_manip_params->u.hdr.insrtParams.u.byHdr.type =
		e_FM_PCD_MANIP_INSRT_BY_HDR_IP;
	fm_pcd_manip_params->u.hdr.insrtParams.u.byHdr.u.ipParams.calcL4Checksum
		= TRUE;
	fm_pcd_manip_params->u.hdr.insrtParams.u.byHdr.u.ipParams.id =
		out_tunnel_params->initial_id;
	fm_pcd_manip_params->u.hdr.insrtParams.u.byHdr.u.ipParams.mappingMode =
		e_FM_PCD_MANIP_HDR_QOS_MAPPING_NONE;
	fm_pcd_manip_params->u.hdr.insrtParams.u.byHdr.u.ipParams.lastPidOffset
		= out_tunnel_params->last_pid_offset;
	fm_pcd_manip_params->u.hdr.insrtParams.u.byHdr.u.ipParams.insrt.size =
		out_tunnel_params->ip_header_size;
	fm_pcd_manip_params->u.hdr.insrtParams.u.byHdr.u.ipParams.insrt.p_Data =
		out_tunnel_params->p_ip_header;
	fm_pcd_manip_params->h_NextManip = p_tunnel->h_hm_l2;
	p_tunnel->h_hm_l3 = FM_PCD_ManipNodeSet(capwap_domain->h_fm_pcd,
			fm_pcd_manip_params);
	if (!p_tunnel->h_hm_l3) {
		pr_err("FM_PCD_ManipNodeSet failed in add_out_tunnel: Hml3\n");
		err = -EINVAL;
		goto out;
	}

	memset(fm_pcd_manip_params, 0, sizeof(t_FmPcdManipParams));
	fm_pcd_manip_params->type = e_FM_PCD_MANIP_HDR;
	fm_pcd_manip_params->u.hdr.dontParseAfterManip = TRUE;
	fm_pcd_manip_params->u.hdr.insrt = TRUE;
	fm_pcd_manip_params->u.hdr.insrtParams.type =
		e_FM_PCD_MANIP_INSRT_BY_HDR;
	fm_pcd_manip_params->u.hdr.insrtParams.u.byHdr.type =
		out_tunnel_params->udp_or_lite ?
		e_FM_PCD_MANIP_INSRT_BY_HDR_UDP_LITE :
		e_FM_PCD_MANIP_INSRT_BY_HDR_UDP;
	fm_pcd_manip_params->u.hdr.insrtParams.u.byHdr.u.insrt.p_Data =
		out_tunnel_params->p_udp_header;
	fm_pcd_manip_params->u.hdr.insrtParams.u.byHdr.u.insrt.size =
		UDP_HDR_SIZE;
	fm_pcd_manip_params->h_NextManip = p_tunnel->h_hm_l3;
	p_tunnel->h_hm_l4 = FM_PCD_ManipNodeSet(capwap_domain->h_fm_pcd,
			fm_pcd_manip_params);
	if (!p_tunnel->h_hm_l4) {
		pr_err("FM_PCD_ManipNodeSet failed in add_out_tunnel: Hml4\n");
		err = -EINVAL;
		goto out;
	}

	out_tunnel_params->p_NextEngineParams->h_Manip = p_tunnel->h_hm_l4;
	err = FM_PCD_MatchTableModifyNextEngine(capwap_domain->h_flow_id_table,
				(uint16_t)GET_UPPER_TUNNEL_ID(flow_index),
				out_tunnel_params->p_NextEngineParams);
	if (err != E_OK) {
		pr_err("FM_PCD_MatchTableModifyNextEngine failed in add_out_tunnel\n");
		err = -EINVAL;
		goto out;
	}

	/* Configure of the DTLS encryption parameters */
	if (out_tunnel_params->dtls) {
		preheader_initdesc =
			kzalloc(sizeof(struct dtls_encap_descriptor_t),
					GFP_KERNEL);
		if (preheader_initdesc == NULL) {
			pr_err("error: %s: No More Buffers left for Descriptor\n",
					__func__);
			err = -ENOMEM;
			goto out;
		}

		desc_len = (sizeof(struct dtls_encap_descriptor_t) -
				sizeof(struct preheader_t)) / sizeof(uint32_t);

		buff_start = (unsigned char *)preheader_initdesc +
			sizeof(struct preheader_t);

		pdb = &preheader_initdesc->pdb;
		if (out_tunnel_params->dtls_params.wbIv)
			pdb->options |= DTLS_ENCAP_OPTION_W_B;
		pdb->epoch = out_tunnel_params->dtls_params.epoch;
		pdb->type      = out_tunnel_params->dtls_params.type;
		memcpy(pdb->version,
				&out_tunnel_params->dtls_params.version, 2);
		memcpy(pdb->seq_num,
				&out_tunnel_params->dtls_params.seq_num, 6);
		memcpy(pdb->iv, out_tunnel_params->dtls_params.p_Iv, 16);

		auth = &p_tunnel->auth_data;
		cipher = &p_tunnel->cipher_data;

		if ((out_tunnel_params->dtls_params.cipher_key_len / 8) >
				DTLS_KEY_MAX_SIZE) {
			pr_err("key size exceeded %d bytes", DTLS_KEY_MAX_SIZE);
			kfree(preheader_initdesc);
			err = -EINVAL;
			goto out;
		}

		auth->auth_key_len =
			out_tunnel_params->dtls_params.auth_key_len / 8;
		memcpy(auth->auth_key, out_tunnel_params->dtls_params.auth_key,
				auth->auth_key_len);
		cipher->cipher_key_len =
			out_tunnel_params->dtls_params.cipher_key_len / 8;
		memcpy(cipher->cipher_key,
				out_tunnel_params->dtls_params.cipher_key,
				cipher->cipher_key_len);
		auth->auth_type =
			capwap_algs[out_tunnel_params->dtls_params.alg_type].
			auth_alg;
		cipher->cipher_type =
			capwap_algs[out_tunnel_params->dtls_params.alg_type].
			cipher_alg;

		err = generate_split_key(auth, &capwap_domain->secinfo);
		if (err) {
			pr_err("error: %s: generate splitkey error\n",
					__func__);
			kfree(preheader_initdesc);
			goto out;
		}

		cnstr_shdsc_dtls_encap((uint32_t *) buff_start, &desc_len,
				cipher, auth, (uint32_t)4);

		preheader_initdesc->prehdr.lo.field.pool_id =
			capwap_domain->bpid;
		preheader_initdesc->prehdr.lo.field.pool_buffer_size =
			capwap_domain->bp_size;
		/* 64bytes offset in output fd*/
		preheader_initdesc->prehdr.lo.field.offset = 2;
		preheader_initdesc->prehdr.hi.field.idlen = desc_len;

		p_tunnel->sec_desc = (t_Handle)preheader_initdesc;

		dma_addr = dma_map_single(capwap_domain->secinfo.jrdev,
				p_tunnel->sec_desc,
				sizeof(struct preheader_t) + desc_len * 4,
				DMA_TO_DEVICE);
		if (dma_mapping_error(capwap_domain->secinfo.jrdev, dma_addr)) {
			pr_err("Unable to DMA map the dtls decap-descriptor address\n");
			kfree(preheader_initdesc);
			err = -ENOMEM;
			goto out;
		}

		/* Init FQ from OP port to SEC */
		fq = (struct qman_fq *)
			capwap_domain->fqs->outbound_op_tx_fqs.fq_base;
		fq[flow_index].fqid =
			capwap_domain->fqs->outbound_op_tx_fqs.fqid_base +
			flow_index;
		channel = qm_channel_caam;
		context_a = (u64)dma_addr;
		context_b =
			capwap_domain->fqs->outbound_sec_to_op_fqs.fqid_base +
			flow_index;
		err = capwap_fq_tx_init(&fq[flow_index], channel, context_a,
				context_b, 3);
		if (err)
			goto error_dma;

		fq_node = kzalloc(sizeof(struct qman_fq_chain), GFP_KERNEL);
		if (fq_node == NULL) {
			err = -ENOMEM;
			goto error_dma;
		}
		fq_node->fq = &fq[flow_index];
		list_add_tail(&fq_node->list, &p_tunnel->fq_chain_head);
	}

	/* Pre SEC Section
	 * 1. copy ToS
	 * 2. insert CAPWAP
	 * 3. CAPWAP-manip
	 * 4. fragmentation
	 */
	fm_pcd_cc_next_engine_params =
		kzalloc(sizeof(t_FmPcdCcNextEngineParams), GFP_KERNEL);
	if (fm_pcd_cc_next_engine_params == NULL) {
		err = -ENOMEM;
		goto error_dma;
	}

	fqid = capwap_domain->fqs->outbound_op_tx_fqs.fqid_base + flow_index;
	memset(fm_pcd_cc_next_engine_params, 0,
			sizeof(t_FmPcdCcNextEngineParams));
	fm_pcd_cc_next_engine_params->nextEngine = e_FM_PCD_DONE;
	fm_pcd_cc_next_engine_params->params.enqueueParams.action =
		e_FM_PCD_ENQ_FRAME;
	fm_pcd_cc_next_engine_params->params.enqueueParams.overrideFqid = TRUE;
	fm_pcd_cc_next_engine_params->params.enqueueParams.newFqid = fqid;

	if (out_tunnel_params->size_for_fragment) {
		memset(fm_pcd_manip_params, 0, sizeof(t_FmPcdManipParams));
		fm_pcd_manip_params->type = e_FM_PCD_MANIP_FRAG;
		fm_pcd_manip_params->u.frag.hdr = HEADER_TYPE_CAPWAP;
		fm_pcd_manip_params->u.frag.u.capwapFrag.sizeForFragmentation =
			out_tunnel_params->size_for_fragment;
		if (out_tunnel_params->frag_bp_enable) {
			fm_pcd_manip_params->u.frag.u.capwapFrag.sgBpidEn =
				TRUE;
			fm_pcd_manip_params->u.frag.u.capwapFrag.sgBpid =
				out_tunnel_params->frag_bp_id;
		}
		p_tunnel->h_capwap_frag =
			FM_PCD_ManipNodeSet(capwap_domain->h_fm_pcd,
					fm_pcd_manip_params);
		if (!p_tunnel->h_capwap_frag) {
			pr_err("FM_PCD_ManipNodeSet failed\n");
			err = -EINVAL;
			goto error_dma;
		}
	}

	memset(fm_pcd_manip_params, 0, sizeof(t_FmPcdManipParams));
	fm_pcd_manip_params->type = e_FM_PCD_MANIP_SPECIAL_OFFLOAD;
	fm_pcd_manip_params->u.specialOffload.type =
		e_FM_PCD_MANIP_SPECIAL_OFFLOAD_CAPWAP;
	fm_pcd_manip_params->u.specialOffload.u.capwap.qosSrc =
		e_FM_PCD_MANIP_HDR_QOS_SRC_NONE;
	fm_pcd_manip_params->u.specialOffload.u.capwap.dtls =
		out_tunnel_params->dtls;
	if (p_tunnel->h_capwap_frag)
		fm_pcd_manip_params->h_NextManip = p_tunnel->h_capwap_frag;
	p_tunnel->h_capwap_manip = FM_PCD_ManipNodeSet(capwap_domain->h_fm_pcd,
			fm_pcd_manip_params);
	if (!p_tunnel->h_capwap_manip) {
		pr_err("FM_PCD_ManipNodeSet failed\n");
		err = -EINVAL;
		goto error_dma;
	}
	fm_pcd_cc_next_engine_params->h_Manip = p_tunnel->h_capwap_manip;

	if (out_tunnel_params->p_capwap_header) {
		/* Need to create cc-table with miss to overcome the HM->MANIP
		 * ilegal connection
		 */
		cc_node_param =
			kzalloc(sizeof(t_FmPcdCcNodeParams), GFP_KERNEL);
		if (cc_node_param == NULL) {
			err = -ENOMEM;
			goto error_dma;
		}

		out_op_port = &capwap_domain->out_op_port;
		cc_node_param->extractCcParams.type  = e_FM_PCD_EXTRACT_NON_HDR;
		cc_node_param->extractCcParams.extractNonHdr.src =
			e_FM_PCD_EXTRACT_FROM_FRAME_START;
		cc_node_param->extractCcParams.extractNonHdr.action =
			e_FM_PCD_ACTION_EXACT_MATCH;
		cc_node_param->extractCcParams.extractNonHdr.offset = 0;
		cc_node_param->extractCcParams.extractNonHdr.size = 1;
		cc_node_param->keysParams.numOfKeys = 1;
		cc_node_param->keysParams.keySize = 1;
		cc_node_param->keysParams.maxNumOfKeys = 0;

		cc_node_param->keysParams.keyParams[0].p_Key =
			&cc_node_param->keysParams.keySize;
		memcpy(&cc_node_param->keysParams.keyParams[0].
				ccNextEngineParams,
				fm_pcd_cc_next_engine_params,
				sizeof(t_FmPcdCcNextEngineParams));
		memcpy(&cc_node_param->keysParams.ccNextEngineParamsForMiss,
				fm_pcd_cc_next_engine_params,
				sizeof(t_FmPcdCcNextEngineParams));

		out_op_port->fmPcdInfo.h_CcNodes[out_op_port->
			fmPcdInfo.numOfCcNodes] =
			FM_PCD_MatchTableSet(capwap_domain->h_fm_pcd,
					cc_node_param);
		if (!out_op_port->fmPcdInfo.h_CcNodes[out_op_port->
				fmPcdInfo.numOfCcNodes]) {
			pr_err("FM_PCD_MatchTableSet failed\n");
			err = -EINVAL;
			goto error_dma;
		}
		p_tunnel->h_ccNode =
			out_op_port->fmPcdInfo.h_CcNodes[out_op_port->
			fmPcdInfo.numOfCcNodes];
		out_op_port->fmPcdInfo.h_CcNodesOrder[out_op_port->
			fmPcdInfo.numOfCcNodes] =
			out_op_port->fmPcdInfo.h_CcNodes[out_op_port->
			fmPcdInfo.numOfCcNodes];
		out_op_port->fmPcdInfo.numOfCcNodes++;

		memset(fm_pcd_manip_params, 0, sizeof(t_FmPcdManipParams));
		fm_pcd_manip_params->type = e_FM_PCD_MANIP_HDR;
		fm_pcd_manip_params->u.hdr.dontParseAfterManip = TRUE;
		fm_pcd_manip_params->u.hdr.insrt = TRUE;
		fm_pcd_manip_params->u.hdr.insrtParams.type =
			e_FM_PCD_MANIP_INSRT_BY_HDR;
		fm_pcd_manip_params->u.hdr.insrtParams.u.byHdr.type =
			e_FM_PCD_MANIP_INSRT_BY_HDR_CAPWAP;
		fm_pcd_manip_params->u.hdr.insrtParams.u.byHdr.u.insrt.p_Data =
			out_tunnel_params->p_capwap_header;
		fm_pcd_manip_params->u.hdr.insrtParams.u.byHdr.u.insrt.size =
			out_tunnel_params->capwap_header_size;
		p_tunnel->h_hm_capwap =
			FM_PCD_ManipNodeSet(capwap_domain->h_fm_pcd,
					fm_pcd_manip_params);
		if (!p_tunnel->h_hm_capwap) {
			pr_err("FM_PCD_ManipNodeSet failed\n");
			err = -EINVAL;
			goto error_dma;
		}

		memset(fm_pcd_cc_next_engine_params, 0,
				sizeof(t_FmPcdCcNextEngineParams));
		fm_pcd_cc_next_engine_params->nextEngine = e_FM_PCD_CC;
		fm_pcd_cc_next_engine_params->params.ccParams.h_CcNode =
			out_op_port->fmPcdInfo.h_CcNodes[out_op_port->
			fmPcdInfo.numOfCcNodes-1];
		fm_pcd_cc_next_engine_params->h_Manip = p_tunnel->h_hm_capwap;
	}

	err = FM_PCD_MatchTableModifyNextEngine(capwap_domain->h_flow_id_table,
					(uint16_t)flow_index,
					fm_pcd_cc_next_engine_params);
	if (err != E_OK)
		goto error_dma;

	p_tunnel->key_index = flow_index;
	err = 0;
	goto out;

error_dma:
	if (out_tunnel_params->dtls) {
		qman_destroy_fq(&fq[flow_index], 0);
		dma_unmap_single(capwap_domain->secinfo.jrdev,
				dma_addr,
				sizeof(struct preheader_t) + desc_len * 4,
				DMA_TO_DEVICE);
		kfree(preheader_initdesc);
	}
out:
	kfree(fm_pcd_manip_params);
	kfree(fm_pcd_cc_next_engine_params);
	kfree(cc_node_param);
	return err;
}

static int remove_out_tunnel(struct dpaa_capwap_tunnel *p_tunnel)
{
	struct dpaa_capwap_domain *capwap_domain;
	int err;
	t_FmPcdCcNextEngineParams cc_next_engine_params;
	struct t_Port *out_op_port;
	struct qman_fq_chain *fq_node, *tmp;

	capwap_domain = p_tunnel->dpaa_capwap_domain;
	memset(&cc_next_engine_params, 0, sizeof(t_FmPcdCcNextEngineParams));
	cc_next_engine_params.nextEngine = e_FM_PCD_DONE;
	cc_next_engine_params.params.enqueueParams.action = e_FM_PCD_DROP_FRAME;
	cc_next_engine_params.statisticsEn = TRUE;

	err = FM_PCD_MatchTableModifyNextEngine(capwap_domain->h_flow_id_table,
					(uint16_t)p_tunnel->key_index,
					&cc_next_engine_params);
	if (err != E_OK)
		return err;
	err = FM_PCD_MatchTableModifyNextEngine(capwap_domain->h_flow_id_table,
			(uint16_t)GET_UPPER_TUNNEL_ID(p_tunnel->key_index),
			&cc_next_engine_params);
	if (err != E_OK)
		return err;

	if (p_tunnel->h_hm_l4) {
		FM_PCD_ManipNodeDelete(p_tunnel->h_hm_l4);
		p_tunnel->h_hm_l4 = NULL;
	}
	if (p_tunnel->h_hm_l3) {
		FM_PCD_ManipNodeDelete(p_tunnel->h_hm_l3);
		p_tunnel->h_hm_l3 = NULL;
	}
	if (p_tunnel->h_hm_l2) {
		FM_PCD_ManipNodeDelete(p_tunnel->h_hm_l2);
		p_tunnel->h_hm_l2 = NULL;
	}
	if (p_tunnel->h_ccNode) {
		out_op_port = &capwap_domain->out_op_port;
		err = FM_PCD_MatchTableDelete(p_tunnel->h_ccNode);
		if (err != E_OK)
			return err;
		out_op_port->fmPcdInfo.numOfCcNodes--;
	}
	if (p_tunnel->h_capwap_manip) {
		FM_PCD_ManipNodeDelete(p_tunnel->h_capwap_manip);
		p_tunnel->h_capwap_manip = NULL;
	}
	if (p_tunnel->h_hm_capwap) {
		FM_PCD_ManipNodeDelete(p_tunnel->h_hm_capwap);
		p_tunnel->h_hm_capwap = NULL;
	}

	list_for_each_entry_safe(fq_node, tmp, &p_tunnel->fq_chain_head, list) {
		teardown_fq(fq_node->fq);
		list_del(&fq_node->list);
	}

	return 0;
}

int dpaa_capwap_domain_remove_tunnel(struct dpaa_capwap_tunnel *p_tunnel)
{
	struct dpaa_capwap_domain *capwap_domain;
	int err = 0;

	capwap_domain =
		(struct dpaa_capwap_domain *)p_tunnel->dpaa_capwap_domain;
	if (p_tunnel->tunnel_dir == e_DPAA_CAPWAP_DOMAIN_DIR_INBOUND)
		err = remove_in_tunnel(p_tunnel);
	else
		err = remove_out_tunnel(p_tunnel);

	if (err != E_OK)
		return err;
	if (p_tunnel->sec_desc)
		p_tunnel->sec_desc = NULL;

	if (p_tunnel->tunnel_dir == e_DPAA_CAPWAP_DOMAIN_DIR_INBOUND)
		enqueue_tunnel_obj(&capwap_domain->in_tunnel_list,
				p_tunnel);
	else
		enqueue_tunnel_obj(&capwap_domain->out_tunnel_list,
				p_tunnel);

	return E_OK;
}
