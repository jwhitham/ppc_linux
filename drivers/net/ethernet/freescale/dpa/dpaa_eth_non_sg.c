/*
 * Copyright 2008-2013 Freescale Semiconductor Inc.
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

#define pr_fmt(fmt) \
	KBUILD_MODNAME ": %s:%hu:%s() " fmt, \
	KBUILD_BASENAME".c", __LINE__, __func__

#include <linux/init.h>
#include <linux/of_platform.h>
#include "dpaa_eth.h"
#include "dpaa_eth_common.h"
#include "dpaa_1588.h"

#ifndef CONFIG_FSL_DPAA_ETH_SG_SUPPORT

/* Maximum frame size on Tx for which skb copying is preferrable to
 * creating a S/G frame
 */
#define DPA_SKB_COPY_MAX_SIZE	256

/* S/G table requires at least 256 bytes */
#define sgt_buffer_size(priv) \
	dpa_get_buffer_size(&priv->buf_layout[TX], 256)

extern struct dpaa_eth_hooks_s dpaa_eth_hooks;
uint32_t default_buf_size;

/* Allocate 8 socket buffers.
 * These buffers are counted for a particular CPU.
 */
static void dpa_bp_add_8(const struct dpa_bp *dpa_bp, unsigned int cpu)
{
	struct bm_buffer bmb[8];
	struct sk_buff **skbh;
	dma_addr_t addr;
	int i;
	struct sk_buff *skb;
	int *count_ptr;

	count_ptr = per_cpu_ptr(dpa_bp->percpu_count, cpu);

	for (i = 0; i < 8; i++) {
		/* The buffers tend to be aligned all to the same cache
		 * index.  A standard dequeue operation pulls in 15 packets.
		 * This means that when it stashes, it evicts half of the
		 * packets it's stashing. In order to prevent that, we pad
		 * by a variable number of cache lines, to reduce collisions.
		 * We always pad by at least 1 cache line, because we want
		 * a little extra room at the beginning for IPSec and to
		 * accommodate NET_IP_ALIGN.
		 */
		int pad = (i + 1) * L1_CACHE_BYTES;

		skb = dev_alloc_skb(dpa_bp->size + pad);
		if (unlikely(!skb)) {
			pr_err("dev_alloc_skb() failed\n");
			bm_buffer_set64(&bmb[i], 0);
			break;
		}

		skbh = (struct sk_buff **)(skb->head + pad);
		*skbh = skb;

		/* Here we need to map only for device write (DMA_FROM_DEVICE),
		 * but on Tx recycling we may also get buffers in the pool that
		 * are mapped bidirectionally.
		 * Use DMA_BIDIRECTIONAL here as well to avoid any
		 * inconsistencies when unmapping.
		 */
		addr = dma_map_single(dpa_bp->dev, skb->head + pad,
				dpa_bp->size, DMA_BIDIRECTIONAL);
		if (unlikely(dma_mapping_error(dpa_bp->dev, addr))) {
			dev_err(dpa_bp->dev, "DMA mapping failed");
			break;
		}

		bm_buffer_set64(&bmb[i], addr);
	}

	/* Avoid releasing a completely null buffer; bman_release() requires
	 * at least one buf.
	 */
	if (likely(i)) {
		/* Release the buffers. In case bman is busy, keep trying
		 * until successful. bman_release() is guaranteed to succeed
		 * in a reasonable amount of time
		 */
		while (bman_release(dpa_bp->pool, bmb, i, 0))
			cpu_relax();

		*count_ptr += i;
	}
}

void dpa_bp_default_buf_size_update(uint32_t size)
{
	if (size > default_buf_size)
		default_buf_size = size;
}

uint32_t dpa_bp_default_buf_size_get(void)
{
	return default_buf_size;
}

int dpa_bp_priv_seed(struct dpa_bp *dpa_bp)
{
	int i;
	dpa_bp->size = default_buf_size;

	/* Give each cpu an allotment of "count" buffers */
	for_each_possible_cpu(i) {
		int j;

		for (j = 0; j < dpa_bp->target_count; j += 8)
			dpa_bp_add_8(dpa_bp, i);
	}
	return 0;
}

void dpa_bp_priv_non_sg_seed(struct dpa_bp *dpa_bp)
{
	static bool default_pool_seeded;

	if (default_pool_seeded)
		return;

	default_pool_seeded = true;

	dpa_bp_priv_seed(dpa_bp);
}

/* Add buffers/(skbuffs) for Rx processing whenever bpool count falls below
 * REFILL_THRESHOLD.
 */
int dpaa_eth_refill_bpools(struct dpa_bp* dpa_bp)
{
	int *countptr = __this_cpu_ptr(dpa_bp->percpu_count);
	int count = *countptr;
	/* this function is called in softirq context;
	 * no need to protect smp_processor_id() on RT kernel
	 */
	unsigned int cpu = smp_processor_id();

	if (unlikely(count < CONFIG_FSL_DPAA_ETH_REFILL_THRESHOLD)) {
		int i;

		for (i = count; i < CONFIG_FSL_DPAA_ETH_MAX_BUF_COUNT; i += 8)
			dpa_bp_add_8(dpa_bp, cpu);
	}

	return 0;
}

/* Cleanup function for outgoing frame descriptors that were built on Tx path,
 * either contiguous frames or scatter/gather ones with a single data buffer.
 * Skb freeing is not handled here.
 *
 * This function may be called on error paths in the Tx function, so guard
 * against cases when not all fd relevant fields were filled in.
 *
 * Return the skb backpointer, since for S/G frames the buffer containing it
 * gets freed here.
 */
struct sk_buff *_dpa_cleanup_tx_fd(const struct dpa_priv_s *priv,
			       const struct qm_fd *fd)
{
	dma_addr_t addr = qm_fd_addr(fd);
	dma_addr_t sg_addr;
	void *vaddr;
	struct dpa_bp *bp = priv->dpa_bp;
	struct sk_buff **skbh;
	struct sk_buff *skb = NULL;

	BUG_ON(!fd);

	if (unlikely(!addr))
		return skb;
	vaddr = phys_to_virt(addr);
	skbh = (struct sk_buff **)vaddr;

	if (fd->format == qm_fd_contig) {
		/* For contiguous frames, just unmap data buffer;
		 * mapping direction depends on whether the frame was
		 * meant to be recycled or not
		 */
		if (fd->cmd & FM_FD_CMD_FCO)
			dma_unmap_single(bp->dev, addr, bp->size,
					 DMA_BIDIRECTIONAL);
		else
			dma_unmap_single(bp->dev, addr, bp->size,
					 DMA_TO_DEVICE);
		/* Retrieve the skb backpointer */
		skb = *skbh;
	} else {
		/* For s/g, we need to unmap both the SGT buffer and the
		 * data buffer, and also free the SGT buffer
		 */
		struct qm_sg_entry *sg_entry;

		/* Unmap first buffer (contains S/G table) */
		dma_unmap_single(bp->dev, addr, sgt_buffer_size(priv),
				 DMA_TO_DEVICE);

		/* Unmap data buffer */
		sg_entry = (struct qm_sg_entry *)(vaddr + fd->offset);
		sg_addr = qm_sg_addr(sg_entry);
		if (likely(sg_addr))
			dma_unmap_single(bp->dev, sg_addr, bp->size,
					 DMA_TO_DEVICE);
		/* Retrieve the skb backpointer */
		skb = *skbh;

	}
/* on some error paths this might not be necessary: */
#ifdef CONFIG_FSL_DPAA_1588
	if (priv->tsu && priv->tsu->valid && priv->tsu->hwts_tx_en_ioctl)
		dpa_ptp_store_txstamp(priv, skb, (void *)skbh);
#endif
#ifdef CONFIG_FSL_DPAA_TS
	if (unlikely(priv->ts_tx_en &&
			skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP)) {
		struct skb_shared_hwtstamps shhwtstamps;

		if (!dpa_get_ts(priv, TX, &shhwtstamps, (void *)skbh))
			skb_tstamp_tx(skb, &shhwtstamps);
	}
#endif /* CONFIG_FSL_DPAA_TS */

	/* Free first buffer (which was allocated on Tx) containing the
	 * skb backpointer and hardware timestamp information
	 */
	if (fd->format != qm_fd_contig)
		kfree(vaddr);

	return skb;
}

/* When we put the buffer into the pool, we purposefully added
 * some padding to the address so that the buffers wouldn't all
 * be page-aligned. But the skb has been reset to a default state,
 * so it is pointing up to DPAA_ETH_MAX_PAD - L1_CACHE_BYTES bytes
 * before the actual data. We subtract skb->head from the fd addr,
 * and then mask off the translated part to get the actual distance.
 */
static int dpa_process_one(struct dpa_percpu_priv_s *percpu_priv,
		struct sk_buff *skb, struct dpa_bp *bp, const struct qm_fd *fd)
{
	dma_addr_t fd_addr = qm_fd_addr(fd);
	unsigned long skb_addr = virt_to_phys(skb->head);
	u32 pad = fd_addr - skb_addr;
	unsigned int data_start;
	int *countptr = __this_cpu_ptr(bp->percpu_count);

	(*countptr)--;

	/* The skb is currently pointed at head + headroom. The packet
	 * starts at skb->head + pad + fd offset.
	 */
	data_start = pad + dpa_fd_offset(fd) - skb_headroom(skb);
	skb_put(skb, dpa_fd_length(fd) + data_start);
	skb_pull(skb, data_start);

	return 0;
}

void __hot _dpa_rx(struct net_device *net_dev,
		const struct dpa_priv_s *priv,
		struct dpa_percpu_priv_s *percpu_priv,
		const struct qm_fd *fd,
		u32 fqid)
{
	struct dpa_bp *dpa_bp;
	struct sk_buff *skb;
	struct sk_buff **skbh;
	dma_addr_t addr = qm_fd_addr(fd);
	u32 fd_status = fd->status;
	unsigned int skb_len;
	fm_prs_result_t *parse_result;
	int use_gro = net_dev->features & NETIF_F_GRO;

	skbh = (struct sk_buff **)phys_to_virt(addr);

	if (unlikely(fd_status & FM_FD_STAT_ERRORS) != 0) {
		if (netif_msg_hw(priv) && net_ratelimit())
			netdev_warn(net_dev, "FD status = 0x%08x\n",
					fd->status & FM_FD_STAT_ERRORS);

		percpu_priv->stats.rx_errors++;

		goto _return_dpa_fd_release;
	}

	if (unlikely(fd->format != qm_fd_contig)) {
		percpu_priv->stats.rx_dropped++;
		if (netif_msg_rx_status(priv) && net_ratelimit())
			netdev_warn(net_dev, "Dropping a SG frame\n");
		goto _return_dpa_fd_release;
	}

	dpa_bp = dpa_bpid2pool(fd->bpid);

	dma_unmap_single(dpa_bp->dev, addr, dpa_bp->size, DMA_BIDIRECTIONAL);
	/* Execute the Rx processing hook, if it exists. */
	if (dpaa_eth_hooks.rx_default && dpaa_eth_hooks.rx_default((void *)fd,
		net_dev, fqid) == DPAA_ETH_STOLEN)
		/* won't count the rx bytes in */
		goto skb_stolen;

	skb = *skbh;
	prefetch(skb);

	/* Fill the SKB */
	dpa_process_one(percpu_priv, skb, dpa_bp, fd);

	prefetch(skb_shinfo(skb));

#ifdef CONFIG_FSL_DPAA_1588
	if (priv->tsu && priv->tsu->valid && priv->tsu->hwts_rx_en_ioctl)
		dpa_ptp_store_rxstamp(priv, skb, (void *)skbh);
#endif

	skb->protocol = eth_type_trans(skb, net_dev);

	if (unlikely(dpa_check_rx_mtu(skb, net_dev->mtu))) {
		percpu_priv->stats.rx_dropped++;
		goto drop_large_frame;
	}


	skb_len = skb->len;

	/* Validate the skb csum and figure out whether GRO is appropriate */
	parse_result = (fm_prs_result_t *)((u8 *)skbh + DPA_RX_PRIV_DATA_SIZE);
	_dpa_process_parse_results(parse_result, fd, skb, &use_gro);

#ifdef CONFIG_FSL_DPAA_TS
	if (priv->ts_rx_en)
		dpa_get_ts(priv, RX, skb_hwtstamps(skb), (void *)skbh);
#endif /* CONFIG_FSL_DPAA_TS */

	if (use_gro) {
		gro_result_t gro_result;

		gro_result = napi_gro_receive(&percpu_priv->napi, skb);
		/* If frame is dropped by the stack, rx_dropped counter is
		 * incremented automatically, so no need for us to update it
		 */
		if (unlikely(gro_result == GRO_DROP))
			goto packet_dropped;
	} else if (unlikely(netif_receive_skb(skb) == NET_RX_DROP))
		goto packet_dropped;

	percpu_priv->stats.rx_packets++;
	percpu_priv->stats.rx_bytes += skb_len;

packet_dropped:
skb_stolen:
	return;

drop_large_frame:
	dev_kfree_skb(skb);
	return;

_return_dpa_fd_release:
	dpa_fd_release(net_dev, fd);
}

static int skb_to_sg_fd(struct dpa_priv_s *priv,
		struct sk_buff *skb, struct qm_fd *fd)
{
	struct dpa_bp *dpa_bp = priv->dpa_bp;
	void *vaddr;
	dma_addr_t paddr;
	struct sk_buff **skbh;
	struct qm_sg_entry *sg_entry;
	struct net_device *net_dev = priv->net_dev;
	int err;

	/* Allocate the first buffer in the FD (used for storing S/G table) */
	vaddr = kmalloc(sgt_buffer_size(priv), GFP_ATOMIC);
	if (unlikely(vaddr == NULL)) {
		if (netif_msg_tx_err(priv) && net_ratelimit())
			netdev_err(net_dev, "Memory allocation failed\n");
		return -ENOMEM;
	}
	/* Store skb backpointer at the beginning of the buffer */
	skbh = (struct sk_buff **)vaddr;
	*skbh = skb;

	/* Fill in FD */
	fd->format = qm_fd_sg;
	fd->offset = priv->tx_headroom;
	fd->length20 = skb->len;

	/* Enable hardware checksum computation */
	err = dpa_enable_tx_csum(priv, skb, fd,
		(char *)vaddr + DPA_TX_PRIV_DATA_SIZE);
	if (unlikely(err < 0)) {
		if (netif_msg_tx_err(priv) && net_ratelimit())
			netdev_err(net_dev, "HW csum error: %d\n", err);
		kfree(vaddr);
		return err;
	}

	/* Map the buffer and store its address in the FD */
	paddr = dma_map_single(dpa_bp->dev, vaddr, sgt_buffer_size(priv),
			       DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(dpa_bp->dev, paddr))) {
		if (netif_msg_tx_err(priv) && net_ratelimit())
			netdev_err(net_dev, "DMA mapping failed\n");
		kfree(vaddr);
		return -EINVAL;
	}

	fd->addr_hi = upper_32_bits(paddr);
	fd->addr_lo = lower_32_bits(paddr);

	/* Fill in S/G entry */
	sg_entry = (struct qm_sg_entry *)(vaddr + fd->offset);

	sg_entry->extension = 0;
	sg_entry->final = 1;
	sg_entry->length = skb->len;
	/* Put the same offset in the data buffer as in the SGT (first) buffer.
	 * This is the format for S/G frames generated by FMan; the manual is
	 * not clear if same is required of Tx S/G frames, but since we know
	 * for sure we have at least tx_headroom bytes of skb headroom,
	 * lets not take any chances.
	 */
	sg_entry->offset = priv->tx_headroom;

	paddr = dma_map_single(dpa_bp->dev, skb->data - sg_entry->offset,
			       dpa_bp->size, DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(dpa_bp->dev, paddr))) {
		if (netif_msg_tx_err(priv) && net_ratelimit())
			netdev_err(net_dev, "DMA mapping failed\n");
		return -EINVAL;
	}
	sg_entry->addr_hi = upper_32_bits(paddr);
	sg_entry->addr_lo = lower_32_bits(paddr);

#ifdef CONFIG_FSL_DPAA_TS
	if (unlikely(priv->ts_tx_en &&
			skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP)) {
		skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;
	}
#endif /* CONFIG_FSL_DPAA_TS */

	return 0;
}

static int skb_to_contig_fd(struct dpa_priv_s *priv,
		struct dpa_percpu_priv_s *percpu_priv,
		struct sk_buff *skb, struct qm_fd *fd)
{
	struct sk_buff **skbh;
	dma_addr_t addr;
	struct dpa_bp *dpa_bp = priv->dpa_bp;
	struct net_device *net_dev = priv->net_dev;
	enum dma_data_direction dma_dir = DMA_TO_DEVICE;
	bool can_recycle = false;
	int offset, extra_offset;
	int err;
	int *countptr = __this_cpu_ptr(dpa_bp->percpu_count);

	/* We are guaranteed that we have at least tx_headroom bytes.
	 * Buffers we allocated are padded to improve cache usage. In order
	 * to increase buffer re-use, we aim to keep any such buffers the
	 * same. This means the address passed to the FM should be
	 * tx_headroom bytes before the data for forwarded frames.
	 *
	 * However, offer some flexibility in fd layout, to allow originating
	 * (termination) buffers to be also recycled when possible.
	 *
	 * First, see if the conditions needed to recycle the skb are met:
	 * - skb not cloned, not shared
	 * - buffer size is large enough to accomodate a maximum size Rx frame
	 * - buffer size does not exceed the maximum size allowed in the pool
	 *   (to avoid unbounded increase of buffer size in certain forwarding
	 *   conditions)
	 * - buffer address is 16 byte aligned, as per DPAARM
	 * - there's enough room in the buffer pool
	 */
	if (likely(skb_is_recycleable(skb, dpa_bp->size) &&
		   (skb_end_pointer(skb) - skb->head <=
			DPA_RECYCLE_MAX_SIZE) &&
		   (*countptr < dpa_bp->target_count))) {
		/* Compute the minimum necessary fd offset */
		offset = dpa_bp->size - skb->len - skb_tailroom(skb);

		/* And make sure the offset is no lower than the offset
		 * required by FMan
		 */
		offset = max_t(int, offset, priv->tx_headroom);

		/* We also need to align the buffer address to 16, such that
		 * Fman will be able to reuse it on Rx.
		 * Since the buffer going to FMan starts at (skb->data - offset)
		 * this is what we'll try to align. We already know that
		 * headroom is at least tx_headroom bytes long, but with
		 * the extra offset needed for alignment we may go beyond
		 * the beginning of the buffer.
		 *
		 * Also need to check that we don't go beyond the maximum
		 * offset that can be set for a contiguous FD.
		 */
		extra_offset = (unsigned long)(skb->data - offset) & 0xF;
		if (likely((offset + extra_offset) <= skb_headroom(skb) &&
			   (offset + extra_offset) <= DPA_MAX_FD_OFFSET)) {
			/* We're good to go for recycling*/
			offset += extra_offset;
			can_recycle = true;
		}
	}

#ifdef CONFIG_FSL_DPAA_TS
	if (unlikely(priv->ts_tx_en &&
			skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP)) {
		/* we need the fd back to get the timestamp */
		can_recycle = false;
		skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;
	}
#endif /* CONFIG_FSL_DPAA_TS */

	if (likely(can_recycle)) {
		/* Buffer will get recycled, setup fd accordingly */
		fd->cmd |= FM_FD_CMD_FCO;
		fd->bpid = dpa_bp->bpid;
		/* Since the buffer will get back to the Bman pool
		 * and be re-used on Rx, map it for both read and write
		 */
		dma_dir = DMA_BIDIRECTIONAL;
	} else {
		/* No recycling here, so we don't care about address alignment.
		 * Just use the smallest offset required by FMan
		 */
		offset = priv->tx_headroom;
	}

	skbh = (struct sk_buff **)(skb->data - offset);
	*skbh = skb;


	/* Enable L3/L4 hardware checksum computation.
	 *
	 * We must do this before dma_map_single(), because we may
	 * need to write into the skb.
	 */
	err = dpa_enable_tx_csum(priv, skb, fd,
				 ((char *)skbh) + DPA_TX_PRIV_DATA_SIZE);
	if (unlikely(err < 0)) {
		if (netif_msg_tx_err(priv) && net_ratelimit())
			netdev_err(net_dev, "HW csum error: %d\n", err);
		return err;
	}

	fd->format = qm_fd_contig;
	fd->length20 = skb->len;
	fd->offset = offset;

	addr = dma_map_single(dpa_bp->dev, skbh, dpa_bp->size, dma_dir);
	if (unlikely(dma_mapping_error(dpa_bp->dev, addr))) {
		if (netif_msg_tx_err(priv)  && net_ratelimit())
			netdev_err(net_dev, "dma_map_single() failed\n");
		return -EINVAL;
	}

	fd->addr_hi = upper_32_bits(addr);
	fd->addr_lo = lower_32_bits(addr);

	return 0;
}

int __hot dpa_tx(struct sk_buff *skb, struct net_device *net_dev)
{
	struct dpa_priv_s	*priv;
	struct qm_fd		 fd;
	struct dpa_percpu_priv_s *percpu_priv;
	struct rtnl_link_stats64 *percpu_stats;
	int queue_mapping;
	int err;
	int *countptr;

	/* If there is a Tx hook, run it. */
	if (dpaa_eth_hooks.tx &&
		dpaa_eth_hooks.tx(skb, net_dev) == DPAA_ETH_STOLEN)
		/* won't update any Tx stats */
		goto done;

	priv = netdev_priv(net_dev);
	percpu_priv = __this_cpu_ptr(priv->percpu_priv);
	percpu_stats = &percpu_priv->stats;
	countptr = __this_cpu_ptr(priv->dpa_bp->percpu_count);

	clear_fd(&fd);
	queue_mapping = dpa_get_queue_mapping(skb);

	if (unlikely(skb_headroom(skb) < priv->tx_headroom)) {
		struct sk_buff *skb_new;

		skb_new = skb_realloc_headroom(skb, priv->tx_headroom);
		if (unlikely(!skb_new)) {
			percpu_stats->tx_errors++;
			kfree_skb(skb);
			goto done;
		}
		kfree_skb(skb);
		skb = skb_new;
	}

#ifdef CONFIG_FSL_DPAA_1588
	if (priv->tsu && priv->tsu->valid && priv->tsu->hwts_tx_en_ioctl)
		fd.cmd |= FM_FD_CMD_UPD;
#endif
#ifdef CONFIG_FSL_DPAA_TS
	if (unlikely(priv->ts_tx_en &&
			skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP))
		fd.cmd |= FM_FD_CMD_UPD;
#endif /* CONFIG_FSL_DPAA_TS */

	/* We have two paths here:
	 *
	 * 1.If the skb is cloned, create a S/G frame to avoid unsharing it.
	 * The S/G table will contain only one entry, pointing to our skb
	 * data buffer.
	 * The private data area containing the skb backpointer will reside
	 * inside the first buffer, such that it won't risk being overwritten
	 * in case a second skb pointing to the same data buffer is being
	 * processed concurently.
	 * No recycling is possible in this case, as the data buffer is shared.
	 *
	 * 2.If skb is not cloned, then the private area inside it can be
	 * safely used to store the skb backpointer. Simply create a contiguous
	 * fd in this case.
	 * Recycling can happen if the right conditions are met.
	 */
	if (skb_cloned(skb) && (skb->len > DPA_SKB_COPY_MAX_SIZE))
		err = skb_to_sg_fd(priv, skb, &fd);
	else {
		/* If cloned skb, but length is below DPA_SKB_COPY_MAX_SIZE,
		 * it's more efficient to unshare it and then use the new skb
		 */
		skb = skb_unshare(skb, GFP_ATOMIC);
		if (unlikely(!skb)) {
			percpu_stats->tx_errors++;
			goto done;
		}
		err = skb_to_contig_fd(priv, percpu_priv, skb, &fd);
	}
	if (unlikely(err < 0)) {
		percpu_stats->tx_errors++;
		goto fd_create_failed;
	}

	if (fd.cmd & FM_FD_CMD_FCO) {
		/* This skb is recycleable, and the fd generated from it
		 * has been filled in accordingly.
		 * NOTE: The recycling mechanism is fragile and dependant on
		 * upstream changes. It will be maintained for now, but plans
		 * are to remove it altoghether from the driver.
		 */
		skb_recycle(skb);
		skb = NULL;
		(*countptr)++;
		percpu_priv->tx_returned++;
	}

	if (unlikely(dpa_xmit(priv, percpu_stats, queue_mapping,
		&fd) < 0))
		goto xmit_failed;

	net_dev->trans_start = jiffies;
	goto done;

xmit_failed:
	if (fd.cmd & FM_FD_CMD_FCO) {
		(*countptr)--;
		percpu_priv->tx_returned--;
	}
fd_create_failed:
	_dpa_cleanup_tx_fd(priv, &fd);
	dev_kfree_skb(skb);

done:
	return NETDEV_TX_OK;
}

#endif /* CONFIG_FSL_DPAA_ETH_SG_SUPPORT */
