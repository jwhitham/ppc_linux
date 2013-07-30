/* Copyright 2008-2013 Freescale Semiconductor, Inc.
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

#ifndef __DPAA_ETH_COMMON_H
#define __DPAA_ETH_COMMON_H

#include <linux/etherdevice.h> /* struct net_device */
#include <linux/fsl_bman.h> /* struct bm_buffer */
#include <linux/of_platform.h> /* struct platform_device */
#include <linux/net_tstamp.h>	/* struct hwtstamp_config */

#define dpaa_eth_init_port(type, port, param, errq_id, defq_id, buf_layout,\
			   frag_enabled) \
{ \
	param.errq = errq_id; \
	param.defq = defq_id; \
	param.priv_data_size = buf_layout->priv_data_size; \
	param.parse_results = buf_layout->parse_results; \
	param.hash_results = buf_layout->hash_results; \
	param.frag_enable = frag_enabled; \
	param.time_stamp = buf_layout->time_stamp; \
	param.manip_extra_space = buf_layout->manip_extra_space; \
	param.data_align = buf_layout->data_align; \
	fm_set_##type##_port_params(port, &param); \
}

int dpa_netdev_init(struct device_node *dpa_node,
		struct net_device *net_dev,
		const uint8_t *mac_addr,
		uint16_t tx_timeout);
int __cold dpa_start(struct net_device *net_dev);
int __cold dpa_stop(struct net_device *net_dev);
void __cold dpa_timeout(struct net_device *net_dev);
struct rtnl_link_stats64 * __cold
dpa_get_stats64(struct net_device *net_dev,
		struct rtnl_link_stats64 *stats);
int dpa_change_mtu(struct net_device *net_dev, int new_mtu);
int dpa_ndo_init(struct net_device *net_dev);
int dpa_set_features(struct net_device *dev, netdev_features_t features);
netdev_features_t dpa_fix_features(struct net_device *dev,
		netdev_features_t features);
#if defined(CONFIG_FSL_DPAA_1588) || defined(CONFIG_FSL_DPAA_TS)
u64 dpa_get_timestamp_ns(const struct dpa_priv_s *priv,
			enum port_type rx_tx, const void *data);
#endif
#ifdef CONFIG_FSL_DPAA_TS
/* Updates the skb shared hw timestamp from the hardware timestamp */
int dpa_get_ts(const struct dpa_priv_s *priv, enum port_type rx_tx,
	struct skb_shared_hwtstamps *shhwtstamps, const void *data);
#endif /* CONFIG_FSL_DPAA_TS */
int dpa_ioctl(struct net_device *dev, struct ifreq *rq, int cmd);
int __cold dpa_remove(struct platform_device *of_dev);
struct mac_device * __cold __must_check
__attribute__((nonnull)) dpa_mac_probe(struct platform_device *_of_dev);
int dpa_set_mac_address(struct net_device *net_dev, void *addr);
void dpa_set_rx_mode(struct net_device *net_dev);
void dpa_set_buffers_layout(struct mac_device *mac_dev,
		struct dpa_buffer_layout_s *layout);
struct dpa_bp * __cold __must_check /* __attribute__((nonnull)) */
dpa_bp_probe(struct platform_device *_of_dev, size_t *count);
int dpa_bp_create(struct net_device *net_dev, struct dpa_bp *dpa_bp,
		size_t count);
int dpa_bp_shared_port_seed(struct dpa_bp *bp);
int __attribute__((nonnull))
dpa_bp_alloc(struct dpa_bp *dpa_bp);
void __cold __attribute__((nonnull))
dpa_bp_free(struct dpa_priv_s *priv, struct dpa_bp *dpa_bp);
struct dpa_bp *dpa_bpid2pool(int bpid);
void dpa_bpid2pool_map(int bpid, struct dpa_bp *dpa_bp);
bool dpa_bpid2pool_use(int bpid);
void dpa_bp_drain(struct dpa_bp *bp);
#ifdef CONFIG_FSL_DPAA_ETH_USE_NDO_SELECT_QUEUE
u16 dpa_select_queue(struct net_device *net_dev, struct sk_buff *skb);
#endif
struct dpa_fq *dpa_fq_alloc(struct device *dev,
				   const struct fqid_cell *fqids,
				   struct list_head *list,
				   enum dpa_fq_type fq_type);
int dpa_fq_probe_mac(struct device *dev, struct list_head *list,
		     struct fm_port_fqs *port_fqs,
		     bool tx_conf_fqs_per_core,
		     enum port_type ptype);
int dpa_get_channel(struct device *dev, struct device_node *dpa_node);
int dpaa_eth_add_channel(void *__arg);
int dpaa_eth_cgr_init(struct dpa_priv_s *priv);
void dpa_fq_setup(struct dpa_priv_s *priv, const dpa_fq_cbs_t *fq_cbs,
		struct fm_port *tx_port);
int dpa_fq_init(struct dpa_fq *dpa_fq, bool td_enable);
int __cold __attribute__((nonnull))
dpa_fq_free(struct device *dev, struct list_head *list);
void dpaa_eth_init_ports(struct mac_device *mac_dev,
		struct dpa_bp *bp, size_t count,
		struct fm_port_fqs *port_fqs,
		struct dpa_buffer_layout_s *buf_layout,
		struct device *dev);
void dpa_release_sgt(struct qm_sg_entry *sgt,
		struct bm_buffer *bmb);
void __attribute__((nonnull))
dpa_fd_release(const struct net_device *net_dev, const struct qm_fd *fd);
void count_ern(struct dpa_percpu_priv_s *percpu_priv,
		      const struct qm_mr_entry *msg);
int dpa_enable_tx_csum(struct dpa_priv_s *priv,
	struct sk_buff *skb, struct qm_fd *fd, char *parse_results);

#endif /* __DPAA_ETH_COMMON_H */
