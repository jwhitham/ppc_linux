 /* Copyright 2014 Freescale Semiconductor Inc.
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

#include <linux/init.h>
#include <linux/if_vlan.h>
#include <linux/fs.h>
#include <linux/fsl_bman.h>

#include "fsl_capwap_br.h"
#include "dpaa_eth_common.h"
#include "dpaa_capwap.h"
#include "dpaa_capwap_domain.h"

#ifdef CAPWAP_HEADER_MANIP
static const char capwap_hdr[] = {
	0x00, 0x28, 0x43, 0x10, 0x00, 0x01, 0x00, 0x00, 0x08, 0xfc,
	0x6e, 0x64, 0xe5, 0xd1, 0x56, 0xee, 0xc1, 0x00, 0x00, 0x00
};
#define CAPWAP_HEADER_LENGTH 20
#endif

#define MANIP_EXTRA_SPACE 64

#define ETHERNET_HEADER_LENGTH 14

#define DPA_NAPI_WEIGHT		64

struct fslbr_if_stats {
	uint32_t if_rx;
	uint32_t if_tx;
	uint32_t br_tx[2];
	uint32_t br_tx_err[2];
	uint32_t br_no_buffer_err[2];
};

struct fslbr_if {
	struct net_device *dev;
	struct list_head list;
	int ifindex;
	struct net_device *capwap_net_dev;
	struct qman_fq *br_to_dpaa_fq;
	struct fslbr_if_stats if_stats;
};

struct tunnel_stats {
	uint32_t tunnel_rx;
	uint32_t tunnel_rx_err;
	uint32_t tunnel_rx_drop;
	uint32_t tunnel_upload;
	uint32_t tunnel_upload_err;
};

static uint32_t stat_buffer_alloc;
static uint32_t stat_buffer_free;

#define DATA_DTLS_TUNNEL 0
#define DATA_N_DTLS_TUNNEL 1
static struct tunnel_stats fsl_tunnel_stats[2];

static LIST_HEAD(fslbr_iflist);
static int fslbr_if_count;
static struct dpaa_capwap_domain *capwap_domain;
static struct dpa_bp *br_dpa_bp;
static int encrypt_status; /* 0: non-dtls encrypt, 1: dtls encrypt */

#ifdef CONFIG_FSL_CAPWAP_BRIDGE_ZMC
static struct sk_buff *alloc_bman_skb(void *bp, unsigned int length);
static void free_bman_skb(struct sk_buff *skb);
#endif

static void capwap_napi_enable(struct dpa_priv_s *priv)
{
	struct dpa_percpu_priv_s *percpu_priv;
	int i, j;

	for_each_possible_cpu(i) {
		percpu_priv = per_cpu_ptr(priv->percpu_priv, i);

		for (j = 0; j < qman_portal_max; j++)
			napi_enable(&percpu_priv->np[j].napi);
	}
}

void capwap_napi_disable(struct dpa_priv_s *priv)
{
	struct dpa_percpu_priv_s *percpu_priv;
	int i, j;

	for_each_possible_cpu(i) {
		percpu_priv = per_cpu_ptr(priv->percpu_priv, i);

		for (j = 0; j < qman_portal_max; j++)
			napi_disable(&percpu_priv->np[j].napi);
	}
}

static int capwap_napi_add(struct net_device *net_dev)
{
	struct dpa_priv_s *priv = netdev_priv(net_dev);
	struct dpa_percpu_priv_s *percpu_priv;
	int i, cpu;

	for_each_possible_cpu(cpu) {
		percpu_priv = per_cpu_ptr(priv->percpu_priv, cpu);

		percpu_priv->np = kzalloc(
			qman_portal_max * sizeof(struct dpa_napi_portal),
			GFP_KERNEL);

		if (unlikely(percpu_priv->np == NULL)) {
			netdev_err(net_dev, "kzalloc() failed\n");
			return -ENOMEM;
		}

		for (i = 0; i < qman_portal_max; i++)
			netif_napi_add(net_dev, &percpu_priv->np[i].napi,
					dpaa_eth_poll, DPA_NAPI_WEIGHT);
	}

	return 0;
}

static inline struct fslbr_if *distribute_to_eth(const struct ethhdr *eth)
{
	struct fslbr_if *fslbr_dev;

	list_for_each_entry(fslbr_dev, &fslbr_iflist, list) {
		if (ether_addr_equal(fslbr_dev->dev->dev_addr, eth->h_source))
			return fslbr_dev;
	}
	return NULL;
}

static enum qman_cb_dqrr_result __hot
capwap_dpaa_to_br(const struct qm_fd *fd, struct qman_fq *fq,
		struct net_device *net_dev, int tunnel_id)
{
	struct dpa_priv_s		*priv;
	struct dpa_bp *dpa_bp;
	struct sk_buff *skb;
	struct qm_sg_entry *sgt;
	int i, ret;
	struct net_device *to_dev = NULL;
	void *new_buf;
	ssize_t fd_off = dpa_fd_offset(fd);
	struct ethhdr *eth = NULL;
	struct fslbr_if *fslbr_dev;
	dma_addr_t addr;

	priv = netdev_priv(net_dev);

	dpa_bp = dpa_bpid2pool(fd->bpid);
	BUG_ON(!dpa_bp);

	if (unlikely(fd->status & FM_FD_STAT_RX_ERRORS) != 0) {
		if (netif_msg_hw(priv) && net_ratelimit())
			netdev_warn(net_dev, "FD status = 0x%08x\n",
					fd->status & FM_FD_STAT_RX_ERRORS);

		fsl_tunnel_stats[tunnel_id].tunnel_rx_err++;

		goto out;
	}

	fsl_tunnel_stats[tunnel_id].tunnel_rx++;

	if (fd->format == qm_fd_contig) {
		addr = qm_fd_addr(fd);
		new_buf = phys_to_virt(addr);
		eth = new_buf + fd_off;
	} else if (fd->format == qm_fd_sg) {
			addr = qm_fd_addr(fd);
			sgt = phys_to_virt(addr) + dpa_fd_offset(fd);
			addr = qm_sg_addr(&sgt[0]) + sgt[0].offset;
			eth = phys_to_virt(addr);
	}

	if (eth) {
		fslbr_dev = distribute_to_eth(eth);
		if (fslbr_dev)
			to_dev = fslbr_dev->dev;
	}

	if (to_dev == NULL) {
		ret = upload_data_packets(fq->fqid, fd, net_dev);
		if (ret) {
			fsl_tunnel_stats[tunnel_id].tunnel_upload_err++;
			goto out;
		}
		fsl_tunnel_stats[tunnel_id].tunnel_upload++;
		return qman_cb_dqrr_consume;
	}

#ifdef CONFIG_FSL_CAPWAP_BRIDGE_ZMC
	/* Just use zero-copy for contig frames */
	if (fd->format == qm_fd_contig) {
		addr = qm_fd_addr(fd);
		new_buf = phys_to_virt(addr);
		skb = build_skb(new_buf, DPA_SKB_SIZE(dpa_bp->size));
		if (skb) { /* zero copy */
			skb_reserve(skb, fd_off);
			skb_put(skb, dpa_fd_length(fd));
			skb->hw_skb_state |= IS_HW_SKB;
			skb->hw_skb_state |= HW_SKB_SW_FREE;
			skb->hw_skb_priv = dpa_bp;
			skb->free_hw_skb = free_bman_skb;
			goto skb_copied;
		}
	}
#endif

	skb = netdev_alloc_skb(net_dev,
			 priv->tx_headroom + dpa_fd_length(fd));
	if (unlikely(skb == NULL)) {
		if (netif_msg_rx_err(priv) && net_ratelimit())
			netdev_err(net_dev, "Could not alloc skb\n");

		fsl_tunnel_stats[tunnel_id].tunnel_rx_err++;
		goto out;
	}

	skb_reserve(skb, priv->tx_headroom);

	if (fd->format == qm_fd_sg) {
			addr = qm_fd_addr(fd);
			sgt = phys_to_virt(addr) + dpa_fd_offset(fd);

			for (i = 0; i < DPA_SGT_MAX_ENTRIES; i++) {
				BUG_ON(sgt[i].extension);

				/* copy from sgt[i] */
				memcpy(skb_put(skb, sgt[i].length),
					phys_to_virt(qm_sg_addr(&sgt[i]) +
						sgt[i].offset),
					sgt[i].length);
				if (sgt[i].final)
					break;
			}
		goto skb_copied;
	}

	/* otherwise fd->format == qm_fd_contig */
	/* Fill the SKB */
	memcpy(skb_put(skb, dpa_fd_length(fd)),
		phys_to_virt(qm_fd_addr(fd)) +
		dpa_fd_offset(fd), dpa_fd_length(fd));

skb_copied:
#ifdef CAPWAP_HEADER_MANIP
	/* Remove CAPWAP header */
	skb_pull(skb, CAPWAP_HEADER_LENGTH);
#endif
	skb_reset_mac_header(skb);

	if (to_dev) {
		/* Dropped when frames are larger than MTU */
		if (skb->len > (to_dev->mtu + ETHERNET_HEADER_LENGTH)) {
			dev_kfree_skb_any(skb);
			fsl_tunnel_stats[tunnel_id].tunnel_rx_drop++;
			goto out;
		}
		skb->dev = to_dev;
#ifdef CONFIG_FSL_CAPWAP_BRIDGE_ZMC
		if (skb->hw_skb_state & IS_HW_SKB) {
			dev_queue_xmit(skb);
			fslbr_dev->if_stats.if_tx++;
			return qman_cb_dqrr_consume;
		}
#endif
		dev_queue_xmit(skb);
		fslbr_dev->if_stats.if_tx++;
	}

out:
	dpa_fd_release(net_dev, fd);

	return qman_cb_dqrr_consume;
}

enum qman_cb_dqrr_result __hot
capwap_data_dtls_rx_dqrr(struct qman_portal *portal, struct qman_fq *fq,
		const struct qm_dqrr_entry *dq)
{
	struct net_device *net_dev;
	struct dpa_priv_s *priv;
	struct dpa_percpu_priv_s *percpu_priv;
	const struct qm_fd *fd = &dq->fd;

	net_dev = ((struct dpa_fq *)fq)->net_dev;
	priv = netdev_priv(net_dev);

	percpu_priv = __this_cpu_ptr(priv->percpu_priv);

	if (unlikely(dpaa_eth_napi_schedule(percpu_priv, portal)))
		return qman_cb_dqrr_stop;

	return capwap_dpaa_to_br(fd, fq, net_dev, DATA_DTLS_TUNNEL);
}

enum qman_cb_dqrr_result __hot
capwap_data_n_dtls_rx_dqrr(struct qman_portal		*portal,
			struct qman_fq			*fq,
			const struct qm_dqrr_entry	*dq)
{
	struct net_device *net_dev;
	struct dpa_priv_s *priv;
	struct dpa_percpu_priv_s *percpu_priv;
	const struct qm_fd *fd = &dq->fd;

	net_dev = ((struct dpa_fq *)fq)->net_dev;
	priv = netdev_priv(net_dev);

	percpu_priv = __this_cpu_ptr(priv->percpu_priv);

	if (unlikely(dpaa_eth_napi_schedule(percpu_priv, portal)))
		return qman_cb_dqrr_stop;

	return capwap_dpaa_to_br(fd, fq, net_dev, DATA_N_DTLS_TUNNEL);
}

static int __hot capwap_br_to_dpaa(struct sk_buff *skb,
		struct fslbr_if *fslbr_dev)
{
	struct dpa_bp *dpa_bp;
	struct bm_buffer bmb;
	struct dpa_percpu_priv_s *percpu_priv;
	struct dpa_priv_s *priv;
	struct qm_fd fd;
	int queue_mapping;
	int err;
	void *dpa_bp_vaddr;
	int i;
	struct qman_fq *fq_base, *fq;
#ifdef CONFIG_FSL_CAPWAP_BRIDGE_ZMC
	dma_addr_t addr;
#endif
	struct net_device *net_dev = fslbr_dev->capwap_net_dev;
	int tunnel_id = encrypt_status ? DATA_DTLS_TUNNEL : DATA_N_DTLS_TUNNEL;

#ifdef CAPWAP_HEADER_MANIP
	skb_push(skb, skb->mac_len + CAPWAP_HEADER_LENGTH);
	memcpy(skb->data, capwap_hdr, CAPWAP_HEADER_LENGTH);
#else
	skb_push(skb, skb->mac_len);
#endif

	priv = netdev_priv(net_dev);
	percpu_priv = __this_cpu_ptr(priv->percpu_priv);

	memset(&fd, 0, sizeof(fd));
	fd.format = qm_fd_contig;

	queue_mapping = smp_processor_id();

	dpa_bp = priv->dpa_bp;

#ifdef CONFIG_FSL_CAPWAP_BRIDGE_ZMC
	if (skb->hw_skb_state & IS_HW_SKB) {
		fd.bpid = dpa_bp->bpid;
		fd.length20 = skb_headlen(skb);
		addr = virt_to_phys(skb->head);
		qm_fd_addr_set64(&fd, addr);
		fd.offset = skb_headroom(skb);
		goto skb_copied;
	}
#endif

	err = bman_acquire(dpa_bp->pool, &bmb, 1, 0);
	if (unlikely(err <= 0)) {
		fslbr_dev->if_stats.br_no_buffer_err[tunnel_id]++;
		if (err == 0)
			err = -ENOMEM;
		goto buf_acquire_failed;
	}
	fd.bpid = dpa_bp->bpid;

	fd.length20 = skb_headlen(skb);
	qm_fd_addr_set64(&fd, bm_buffer_get64(&bmb));
	fd.offset = priv->tx_headroom + MANIP_EXTRA_SPACE;

	dpa_bp_vaddr = phys_to_virt(bm_buf_addr(&bmb));

	/* Copy the packet payload */
	skb_copy_from_linear_data(skb,
			  dpa_bp_vaddr + dpa_fd_offset(&fd),
			  dpa_fd_length(&fd));

#ifdef CONFIG_FSL_CAPWAP_BRIDGE_ZMC
skb_copied:
#endif
	fq_base = (struct qman_fq *)capwap_domain->fqs->
					outbound_core_tx_fqs.fq_base;
	if (encrypt_status)
		fq = &fq_base[1];
	else
		fq = &fq_base[3];

	for (i = 0; i < 100000; i++) {
		err = qman_enqueue(fq, &fd, 0);
		if (err != -EBUSY)
			break;
	}
	if (unlikely(err < 0)) {
		/* TODO differentiate b/w -EBUSY (EQCR full) and other codes? */
		fslbr_dev->if_stats.br_tx_err[tunnel_id]++;
		pr_err("fslbr: fsl bridge transmit to dpaa error\n");
		return err;
	} else
		fslbr_dev->if_stats.br_tx[tunnel_id]++;

	return 0;

buf_acquire_failed:
	/* We're done with the skb */
	return -ENOMEM;
}

rx_handler_result_t fslbr_handle_frame(struct sk_buff **pskb)
{
	struct sk_buff *skb = *pskb;
	struct fslbr_if *fslbr_dev;
	int ret;

	if (unlikely(skb->pkt_type == PACKET_LOOPBACK))
		return RX_HANDLER_PASS;

	if (!is_valid_ether_addr(eth_hdr(skb)->h_source))
		goto drop;

	skb = skb_share_check(skb, GFP_ATOMIC);
	if (!skb)
		return RX_HANDLER_CONSUMED;

	fslbr_dev =
		(struct fslbr_if *)rcu_dereference(skb->dev->rx_handler_data);
	fslbr_dev->if_stats.if_rx++;

	if (skb_is_nonlinear(skb)) {
		pr_warn("CAPWAP Bridge does't support nonlinear skb");
		goto drop;
	}

	ret = capwap_br_to_dpaa(skb, fslbr_dev);
	if (ret)
		goto drop;
#ifdef CONFIG_FSL_CAPWAP_BRIDGE_ZMC
	/* Set use_dpaa_bp_state to free skb without free data memory region*/
	if (skb->hw_skb_state & IS_HW_SKB)
		skb->hw_skb_state &= ~HW_SKB_SW_FREE;
#endif
drop:
	kfree_skb(skb);

	return RX_HANDLER_CONSUMED;
}

static long fslbr_add_del_if(void __user *arg, int isadd)
{
	struct net *net = &init_net;
	struct net_device *dev;
	struct fslbr_if *fslbr_dev;
	int ret;
	int ifindex;

	ret = copy_from_user(&ifindex, arg, sizeof(ifindex));
	if (ret)
		return ret;

	dev = __dev_get_by_index(net, ifindex);
	if (dev == NULL)
		return -EINVAL;

	if (isadd) {
		if (fslbr_if_count >= MAX_IF_COUNT)
			return -EINVAL;

		list_for_each_entry(fslbr_dev, &fslbr_iflist, list)
			if (fslbr_dev->ifindex == ifindex)
				return -EBUSY;

		fslbr_dev = kzalloc(sizeof(*fslbr_dev), GFP_KERNEL);
		if (!fslbr_dev) {
			pr_err("Failed to add fslbr if\n");
			return -ENOMEM;
		}

		fslbr_dev->dev = dev;
		fslbr_dev->ifindex = ifindex;
		fslbr_dev->capwap_net_dev = capwap_domain->net_dev;
		rtnl_lock();
		ret = netdev_rx_handler_register(dev,
				fslbr_handle_frame, fslbr_dev);
		rtnl_unlock();
		if (ret) {
			kfree(fslbr_dev);
			return ret;
		}
#ifdef CONFIG_FSL_CAPWAP_BRIDGE_ZMC
		dev->hw_skb_priv = br_dpa_bp;
		dev->alloc_hw_skb = alloc_bman_skb;
		dev->free_hw_skb = free_bman_skb;
#endif
		list_add_tail(&fslbr_dev->list, &fslbr_iflist);
		fslbr_if_count++;

		return 0;
	} else {
		list_for_each_entry(fslbr_dev, &fslbr_iflist, list) {
			if (fslbr_dev->dev == dev) {
				list_del(&fslbr_dev->list);
				kfree(fslbr_dev);
				fslbr_if_count--;
				rtnl_lock();
				netdev_rx_handler_unregister(dev);
				rtnl_unlock();
				return 0;
			}
		}
		return -EINVAL;
	}
}

static long fslbr_list(void __user *arg)
{
	/* iflist defines the data to be copied to userspace.
	 * The first "int" data is encryption status,
	 * the second "int" data is the count of interfaces in bridge
	 * the following data are the index list of interfaces in bridge
	 */
	int iflist[MAX_IF_COUNT + 2];
	struct fslbr_if *fslbr_dev;
	int i = 2;
	long ret = 0;

	iflist[0] = encrypt_status;
	iflist[1] = fslbr_if_count;

	list_for_each_entry(fslbr_dev, &fslbr_iflist, list) {
		iflist[i++] = fslbr_dev->ifindex;
	}
	ret = copy_to_user(arg, iflist, sizeof(iflist));
	return ret;
}

static long fslbr_ioctl(struct file *fp, unsigned int cmd, unsigned long arg)
{
	void __user *a = (void __user *)arg;
	int status;
	int ret;

	switch (cmd) {
	case FSLBR_IOCTL_IF_ADD:
		return fslbr_add_del_if(a, 1);
	case FSLBR_IOCTL_IF_DEL:
		return fslbr_add_del_if(a, 0);
	case FSLBR_IOCTL_IF_LIST:
		return fslbr_list(a);
	case FSLBR_IOCTL_SET_ENCRYPT:
		ret = copy_from_user(&status, a, sizeof(int));
		if (ret)
			return ret;
		if (!status || status == 1) {
			encrypt_status = status;
			return 0;
		} else
			return -EINVAL;
	}
	return -EINVAL;
}

#ifdef CONFIG_COMPAT
static long fslbr_ioctl_compat(struct file *fp, unsigned int cmd,
				unsigned long arg)
{
	switch (cmd) {
	default:
		return fslbr_ioctl(fp, cmd, arg);
	}
	return -EINVAL;
}
#endif

static ssize_t fslbr_show_statistic(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	ssize_t bytes = 0;
	struct fslbr_if *fslbr_dev;
	int i;
	uint32_t br_tx[2] = {0, 0};
	uint32_t br_tx_err[2] = {0, 0};
	uint32_t br_no_buffer_err[2] = {0, 0};

	list_for_each_entry(fslbr_dev, &fslbr_iflist, list) {
		bytes += sprintf(buf + bytes, "Eth%d\tRx: %u\tTx: %u\n",
				fslbr_dev->ifindex,
				fslbr_dev->if_stats.if_rx,
				fslbr_dev->if_stats.if_tx);
		br_tx[0] += fslbr_dev->if_stats.br_tx[0];
		br_tx[1] += fslbr_dev->if_stats.br_tx[1];
		br_tx_err[0] += fslbr_dev->if_stats.br_tx_err[0];
		br_tx_err[1] += fslbr_dev->if_stats.br_tx_err[1];
		br_no_buffer_err[0] += fslbr_dev->if_stats.br_no_buffer_err[0];
		br_no_buffer_err[1] += fslbr_dev->if_stats.br_no_buffer_err[1];
	}

	for (i = 0; i < 2; i++) {
		if (i == DATA_DTLS_TUNNEL)
			bytes += sprintf(buf + bytes,
					"CAPWAP-DATA-DTLS-Tunnel:\n");
		else
			bytes += sprintf(buf + bytes,
					"CAPWAP-N-DATA-DTLS-Tunnel:\n");
		bytes += sprintf(buf + bytes, "\tRx: %u\n",
			fsl_tunnel_stats[i].tunnel_rx);
		bytes += sprintf(buf + bytes, "\tRx Error: %u\n",
			fsl_tunnel_stats[i].tunnel_rx_err);
		bytes += sprintf(buf + bytes, "\tRx Drop: %u\n",
			fsl_tunnel_stats[i].tunnel_rx_drop);
		bytes += sprintf(buf + bytes, "\tTx: %u\n", br_tx[i]);
		bytes += sprintf(buf + bytes, "\tTx Error: %u\n", br_tx_err[i]);
		bytes += sprintf(buf + bytes,
				"\tTx N-ZZM No Buffer Error: %u\n",
				br_no_buffer_err[i]);
		bytes += sprintf(buf + bytes, "\tTx Upload: %u\n",
			fsl_tunnel_stats[i].tunnel_upload);
		bytes += sprintf(buf + bytes, "\tTx Upload Error: %u\n",
			fsl_tunnel_stats[i].tunnel_upload_err);
	}

	bytes += sprintf(buf + bytes, "BMan Buffer alloced: %u\n",
			stat_buffer_alloc);
	bytes += sprintf(buf + bytes, "BMan Buffer freed: %u\n",
			stat_buffer_free);
	return bytes;
}

static DEVICE_ATTR(capwap_bridge, S_IRUGO, fslbr_show_statistic, NULL);

static const struct file_operations fslbr_fops = {
	.open		   = simple_open,
	.unlocked_ioctl	   = fslbr_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	   = fslbr_ioctl_compat,
#endif
};

static struct miscdevice fslbr_miscdev = {
	.name = "fsl-br",
	.fops = &fslbr_fops,
	.minor = MISC_DYNAMIC_MINOR,
};

int capwap_br_init(struct dpaa_capwap_domain *domain)
{
	int ret = 0, i;
	struct dpa_priv_s *priv;
	struct device *dev;
	struct net_device *dummy_dev;
	size_t alloc_size;
	struct net_device *p;
	struct dpa_fq *fq_base, *d_fq;
	struct dpa_percpu_priv_s *percpu_priv;

	fslbr_if_count = 0;
	encrypt_status = 1;
	stat_buffer_alloc = 0;
	stat_buffer_free = 0;
	memset(fsl_tunnel_stats, 0, sizeof(fsl_tunnel_stats));
	capwap_domain = domain;
	priv = netdev_priv(domain->net_dev);
	br_dpa_bp = priv->dpa_bp;

	ret = misc_register(&fslbr_miscdev);
	if (ret)
		pr_err("fslbr: failed to register misc device\n");
	dev = (&fslbr_miscdev)->this_device;
	if (device_create_file(dev, &dev_attr_capwap_bridge))
		dev_err(dev, "Error creating sysfs file\n");

	alloc_size = sizeof(struct net_device);
	/* ensure 32-byte alignment of private area */
	alloc_size = ALIGN(alloc_size, NETDEV_ALIGN);
	alloc_size += sizeof(struct dpa_priv_s);
	/* ensure 32-byte alignment of whole construct */
	alloc_size += NETDEV_ALIGN - 1;

	p = kzalloc(alloc_size, GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	dummy_dev = PTR_ALIGN(p, NETDEV_ALIGN);
	priv = netdev_priv(dummy_dev);
	init_dummy_netdev(dummy_dev);

	priv->percpu_priv = alloc_percpu(*priv->percpu_priv);

	if (priv->percpu_priv == NULL) {
		dev_err(dev, "alloc_percpu() failed\n");
		kfree(p);
		return -ENOMEM;
	}
	for_each_possible_cpu(i) {
		percpu_priv = per_cpu_ptr(priv->percpu_priv, i);
		memset(percpu_priv, 0, sizeof(*percpu_priv));
	}

	/* Initialize NAPI */
	ret = capwap_napi_add(dummy_dev);
	if (ret < 0) {
		dpa_private_napi_del(dummy_dev);
		free_percpu(priv->percpu_priv);
		kfree(p);
		return ret;
	}

	fq_base = (struct dpa_fq *)capwap_domain->fqs->inbound_core_rx_fqs
			.fq_base;
	d_fq = &fq_base[1];
	d_fq->net_dev = dummy_dev;
	d_fq = &fq_base[3];
	d_fq->net_dev = dummy_dev;

	capwap_napi_enable(priv);

	return ret;
}

void capwap_br_exit(void)
{
	struct device *dev;

	dev = (&fslbr_miscdev)->this_device;
	device_remove_file(dev, &dev_attr_capwap_bridge);
	misc_deregister(&fslbr_miscdev);
}

#ifdef CONFIG_FSL_CAPWAP_BRIDGE_ZMC
static struct sk_buff *alloc_bman_skb(void *bp, unsigned int length)
{
	struct dpa_bp *dpa_bp = (struct dpa_bp *)bp;
	void *new_buf;
	int err;
	struct bm_buffer bmb;
	struct sk_buff *skb;

	if (dpa_bp->size < length) {
		pr_warn("fslbr:bp size smaller than length\n");
		return NULL;
	}

	err = bman_acquire(dpa_bp->pool, &bmb, 1, 0);
	if (unlikely(err <= 0))
		return NULL;

	new_buf = phys_to_virt(bm_buf_addr(&bmb));
	skb = build_skb(new_buf, DPA_SKB_SIZE(dpa_bp->size));
	if (unlikely(!skb)) {
		while (unlikely(bman_release(dpa_bp->pool, &bmb, 1, 0)))
			cpu_relax();
			return NULL;
	}

	/* Set manip extra space for capwap tunnel */
	if (skb) {
		skb_reserve(skb, MANIP_EXTRA_SPACE);
		skb->hw_skb_state |= HW_SKB_SW_FREE;
	}
	stat_buffer_alloc++;
	return skb;

}

static void free_bman_skb(struct sk_buff *skb)
{
	struct dpa_bp *dpa_bp;
	struct bm_buffer bmb;
	dma_addr_t addr;

	addr = virt_to_phys(skb->head);
	bm_buffer_set64(&bmb, addr);
	if (skb->dev->hw_skb_priv) {
		dpa_bp = (struct dpa_bp *)skb->hw_skb_priv;
		while (bman_release(dpa_bp->pool, &bmb, 1, 0))
			cpu_relax();
	} else {
		if (br_dpa_bp) {
			while (bman_release(br_dpa_bp->pool, &bmb, 1, 0))
				cpu_relax();
		}
	}
	stat_buffer_free++;
}
#endif
