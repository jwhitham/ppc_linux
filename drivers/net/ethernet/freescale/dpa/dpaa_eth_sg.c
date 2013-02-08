/*
 * Copyright 2012 Freescale Semiconductor Inc.
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

#include <linux/init.h>
#include <linux/skbuff.h>
#include <linux/highmem.h>
#include <linux/fsl_bman.h>

#include "dpaa_eth.h"
#include "dpaa_1588.h"

#ifdef CONFIG_DPAA_ETH_SG_SUPPORT
#define DPA_SGT_MAX_ENTRIES 16 /* maximum number of entries in SG Table */

/*
 * It does not return a page as you get the page from the fd,
 * this is only for refcounting and DMA unmapping
 */
static inline void dpa_bp_removed_one_page(struct dpa_bp *dpa_bp,
					   dma_addr_t dma_addr)
{
	int *count_ptr;

	count_ptr = per_cpu_ptr(dpa_bp->percpu_count, smp_processor_id());
	(*count_ptr)--;

	dma_unmap_single(dpa_bp->dev, dma_addr, dpa_bp->size,
		DMA_BIDIRECTIONAL);
}

/* DMA map and add a page into the bpool */
static void dpa_bp_add_page(struct dpa_bp *dpa_bp, unsigned long vaddr)
{
	struct bm_buffer bmb;
	int *count_ptr;
	dma_addr_t addr;
	int offset;

	count_ptr = per_cpu_ptr(dpa_bp->percpu_count, smp_processor_id());

	/* Make sure we don't map beyond end of page */
	offset = vaddr & (PAGE_SIZE - 1);
	if (unlikely(dpa_bp->size + offset > PAGE_SIZE)) {
		free_page(vaddr);
		return;
	}
	addr = dma_map_single(dpa_bp->dev, (void *)vaddr, dpa_bp->size,
			      DMA_BIDIRECTIONAL);
	if (unlikely(dma_mapping_error(dpa_bp->dev, addr))) {
		dpaa_eth_err(dpa_bp->dev, "DMA mapping failed");
		return;
	}

	bm_buffer_set64(&bmb, addr);

	while (bman_release(dpa_bp->pool, &bmb, 1, 0))
		cpu_relax();

	(*count_ptr)++;
}

void dpa_bp_add_8_pages(struct dpa_bp *dpa_bp, int cpu_id)
{
	struct bm_buffer bmb[8];
	unsigned long new_page;
	int *count_ptr;
	dma_addr_t addr;
	int i;

	count_ptr = per_cpu_ptr(dpa_bp->percpu_count, cpu_id);

	for (i = 0; i < 8; i++) {
		new_page = __get_free_page(GFP_ATOMIC);
		if (unlikely(!new_page)) {
			dpaa_eth_err(dpa_bp->dev, "__get_free_page() failed\n");
			bm_buffer_set64(&bmb[i], 0);
			break;
		}

		addr = dma_map_single(dpa_bp->dev, (void *)new_page,
				dpa_bp->size, DMA_BIDIRECTIONAL);
		if (unlikely(dma_mapping_error(dpa_bp->dev, addr))) {
			dpaa_eth_err(dpa_bp->dev, "DMA mapping failed");
			free_page(new_page);
			break;
		}

		bm_buffer_set64(&bmb[i], addr);
	}

	/*
	 * Avoid releasing a completely null buffer; bman_release() requires
	 * at least one buffer.
	 */
	if (likely(i)) {
		/*
		 * Release the buffers. In case bman is busy, keep trying
		 * until successful. bman_release() is guaranteed to succeed
		 * in a reasonable amount of time
		 */
		while (bman_release(dpa_bp->pool, bmb, i, 0))
			cpu_relax();

		*count_ptr += i;
	}
}

void dpa_list_add_skb(struct dpa_percpu_priv_s *cpu_priv,
		      struct sk_buff *new_skb)
{
	struct sk_buff_head *list_ptr;

	if (cpu_priv->skb_count > DEFAULT_SKB_COUNT) {
		dev_kfree_skb(new_skb);
		return;
	}

	list_ptr = &cpu_priv->skb_list;
	skb_queue_head(list_ptr, new_skb);

	cpu_priv->skb_count += 1;
}

static struct sk_buff *dpa_list_get_skb(struct dpa_percpu_priv_s *cpu_priv)
{
	struct sk_buff_head *list_ptr;
	struct sk_buff *new_skb;

	list_ptr = &cpu_priv->skb_list;

	new_skb = skb_dequeue(list_ptr);
	if (new_skb)
		cpu_priv->skb_count -= 1;

	return new_skb;
}

void dpa_list_add_skbs(struct dpa_percpu_priv_s *cpu_priv, int count)
{
	struct sk_buff *new_skb;
	int i;

	for (i = 0; i < count; i++) {
		new_skb = dev_alloc_skb(DPA_BP_HEAD +
				dpa_get_rx_extra_headroom() +
				DPA_COPIED_HEADERS_SIZE);
		if (unlikely(!new_skb)) {
			pr_err("dev_alloc_skb() failed\n");
			break;
		}

		dpa_list_add_skb(cpu_priv, new_skb);
	}
}

void dpa_make_private_pool(struct dpa_bp *dpa_bp)
{
	int i;

	dpa_bp->percpu_count = __alloc_percpu(sizeof(*dpa_bp->percpu_count),
					__alignof__(*dpa_bp->percpu_count));

	/* Give each CPU an allotment of "page_count" buffers */
	for_each_online_cpu(i) {
		int j;

		/*
		 * Although we access another CPU's counters here
		 * we do it at boot time so it is safe
		 */
		for (j = 0; j < dpa_bp->config_count; j += 8)
			dpa_bp_add_8_pages(dpa_bp, i);
	}
}

/*
 * Cleanup function for outgoing frame descriptors that were built on Tx path,
 * either contiguous frames or scatter/gather ones.
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
	const struct qm_sg_entry *sgt;
	int i;
	struct dpa_bp *dpa_bp = priv->dpa_bp;
	dma_addr_t addr = qm_fd_addr(fd);
	struct sk_buff **skbh;
	struct sk_buff *skb = NULL;
	enum dma_data_direction dma_dir;

	dma_dir = (fd->cmd & FM_FD_CMD_FCO) ? DMA_BIDIRECTIONAL : DMA_TO_DEVICE;
	dma_unmap_single(dpa_bp->dev, addr, dpa_bp->size, dma_dir);

	/* retrieve skb back pointer */
	skbh = (struct sk_buff **)phys_to_virt(addr);
	skb = *skbh;

	if (fd->format == qm_fd_sg) {
		/*
		 * All storage items used are pages, but only the sgt and
		 * the first page are guaranteed to reside in lowmem.
		 */
		sgt = phys_to_virt(addr + dpa_fd_offset(fd));

		/* page 0 is from lowmem, was dma_map_single()-ed */
		dma_unmap_single(dpa_bp->dev, sgt[0].addr,
				 dpa_bp->size, dma_dir);

		/* remaining pages were mapped with dma_map_page() */
		for (i = 1; i < skb_shinfo(skb)->nr_frags; i++) {
			BUG_ON(sgt[i].extension);

			dma_unmap_page(dpa_bp->dev, sgt[i].addr,
					dpa_bp->size, dma_dir);
		}

		/*
		 * TODO: dpa_bp_add_page() ?
		 * We could put these in the pool, since we allocated them
		 * and we know they're not used by anyone else
		 */

		/* Free separately the pages that we allocated on Tx */
		free_page((unsigned long)phys_to_virt(addr));
		free_page((unsigned long)phys_to_virt(sgt[0].addr));
	}

	return skb;
}

/*
 * Move the first DPA_COPIED_HEADERS_SIZE bytes to the skb linear buffer to
 * provide the networking stack the headers it requires in the linear buffer.
 *
 * If the entire frame fits in the skb linear buffer, the page holding the
 * received data is recycled as it is no longer required.
 *
 * Return 0 if the ingress skb was properly constructed, non-zero if an error
 * was encountered and the frame should be dropped.
 */
static int __hot contig_fd_to_skb(const struct dpa_priv_s *priv,
	const struct qm_fd *fd, struct sk_buff *skb, int *use_gro)
{
	unsigned int copy_size = DPA_COPIED_HEADERS_SIZE;
	dma_addr_t addr = qm_fd_addr(fd);
	void *vaddr;
	struct page *page;
	int frag_offset, page_offset;
	struct dpa_bp *dpa_bp = priv->dpa_bp;
	unsigned char *tailptr;
	const t_FmPrsResult *parse_results;
	int ret;

	vaddr = phys_to_virt(addr);

#ifdef CONFIG_FSL_DPA_1588
	if (priv->tsu && priv->tsu->valid && priv->tsu->hwts_rx_en_ioctl)
		dpa_ptp_store_rxstamp(priv->net_dev, skb, fd);
#endif

	/* Peek at the parse results for frame validation. */
	parse_results = (const t_FmPrsResult *)(vaddr + DPA_RX_PRIV_DATA_SIZE);
	ret = _dpa_process_parse_results(parse_results, fd, skb, use_gro,
		&copy_size);
	if (unlikely(ret))
		/* This is definitely a bad frame, don't go further. */
		return ret;

	tailptr = skb_put(skb, copy_size);

	/* Copy (at least) the headers in the linear portion */
	memcpy(tailptr, vaddr + dpa_fd_offset(fd), copy_size);

	/*
	 * If frame is longer than the amount we copy in the linear
	 * buffer, add the page as fragment,
	 * otherwise recycle the page
	 */
	page = pfn_to_page(addr >> PAGE_SHIFT);

	if (copy_size < dpa_fd_length(fd)) {
		/* add the page as a fragment in the skb */
		page_offset = (unsigned long)vaddr & (PAGE_SIZE - 1);
		frag_offset = page_offset + dpa_fd_offset(fd) + copy_size;
		skb_add_rx_frag(skb, 0, page, frag_offset,
		                dpa_fd_length(fd) - copy_size,
		                /* TODO kernel 3.8 fixup; we might want
		                 * to better account for the truesize */
				dpa_fd_length(fd) - copy_size);
	} else {
		/* recycle the page */
		dpa_bp_add_page(dpa_bp, (unsigned long)vaddr);
	}

	return 0;
}


/*
 * Move the first bytes of the frame to the skb linear buffer to
 * provide the networking stack the headers it requires in the linear buffer,
 * and add the rest of the frame as skb fragments.
 *
 * The page holding the S/G Table is recycled here.
 */
static int __hot sg_fd_to_skb(const struct dpa_priv_s *priv,
			       const struct qm_fd *fd, struct sk_buff *skb,
			       int *use_gro)
{
	const struct qm_sg_entry *sgt;
	dma_addr_t addr = qm_fd_addr(fd);
	dma_addr_t sg_addr;
	void *vaddr, *sg_vaddr;
	struct dpa_bp *dpa_bp;
	struct page *page;
	int frag_offset, frag_len;
	int page_offset;
	int i, ret;
	unsigned int copy_size = DPA_COPIED_HEADERS_SIZE;
	const t_FmPrsResult *parse_results;

	vaddr = phys_to_virt(addr);
	/*
	 * In the case of a SG frame, FMan stores the Internal Context
	 * in the buffer containing the sgt.
	 */
	parse_results = (const t_FmPrsResult *)(vaddr + DPA_RX_PRIV_DATA_SIZE);
	/* Validate the frame before anything else. */
	ret = _dpa_process_parse_results(parse_results, fd, skb, use_gro,
		&copy_size);
	if (unlikely(ret))
		/* Bad frame, stop processing now. */
		return ret;

	/*
	 * Iterate through the SGT entries and add the data buffers as
	 * skb fragments
	 */
	sgt = vaddr + dpa_fd_offset(fd);
	for (i = 0; i < DPA_SGT_MAX_ENTRIES; i++) {
		/* Extension bit is not supported */
		BUG_ON(sgt[i].extension);

		dpa_bp = dpa_bpid2pool(sgt[i].bpid);
		BUG_ON(IS_ERR(dpa_bp));

		sg_addr = qm_sg_addr(&sgt[i]);
		sg_vaddr = phys_to_virt(sg_addr);

		dpa_bp_removed_one_page(dpa_bp, sg_addr);
		page = pfn_to_page(sg_addr >> PAGE_SHIFT);

		/*
		 * Padding at the beginning of the page
		 * (offset in page from where BMan buffer begins)
		 */
		page_offset = (unsigned long)sg_vaddr & (PAGE_SIZE - 1);

		if (i == 0) {
			/* This is the first fragment */
			/* Move the network headers in the skb linear portion */
			memcpy(skb_put(skb, copy_size),
				sg_vaddr + sgt[i].offset,
				copy_size);

			/* Adjust offset/length for the remaining data */
			frag_offset = sgt[i].offset + page_offset + copy_size;
			frag_len = sgt[i].length - copy_size;
		} else {
			/*
			 * Not the first fragment; all data from buferr will
			 * be added in an skb fragment
			 */
			frag_offset = sgt[i].offset + page_offset;
			frag_len = sgt[i].length;
		}
		/*
		 * Add data buffer to the skb
		 *
		 * TODO kernel 3.8 fixup; we might want to account for
		 * the true-truesize.
		 */
		skb_add_rx_frag(skb, i, page, frag_offset, frag_len, frag_len);

		if (sgt[i].final)
			break;
	}

#ifdef CONFIG_FSL_DPA_1588
	if (priv->tsu && priv->tsu->valid && priv->tsu->hwts_rx_en_ioctl)
		dpa_ptp_store_rxstamp(priv->net_dev, skb, fd);
#endif

	/* recycle the SGT page */
	dpa_bp = dpa_bpid2pool(fd->bpid);
	BUG_ON(IS_ERR(dpa_bp));
	dpa_bp_add_page(dpa_bp, (unsigned long)vaddr);

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
	dma_addr_t addr = qm_fd_addr(fd);
	u32 fd_status = fd->status;
	unsigned int skb_len;
	int use_gro = net_dev->features & NETIF_F_GRO;

	if (unlikely(fd_status & FM_FD_STAT_ERRORS) != 0) {
		if (netif_msg_hw(priv) && net_ratelimit())
			cpu_netdev_warn(net_dev, "FD status = 0x%08x\n",
					fd_status & FM_FD_STAT_ERRORS);

		percpu_priv->stats.rx_errors++;
		goto _release_frame;
	}

	dpa_bp = dpa_bpid2pool(fd->bpid);
	skb = dpa_list_get_skb(percpu_priv);

	if (unlikely(skb == NULL)) {
		/* List is empty, so allocate a new skb */
		skb = dev_alloc_skb(DPA_BP_HEAD + dpa_get_rx_extra_headroom() +
			DPA_COPIED_HEADERS_SIZE);
		if (unlikely(skb == NULL)) {
			if (netif_msg_rx_err(priv) && net_ratelimit())
				cpu_netdev_err(net_dev,
						"Could not alloc skb\n");
			percpu_priv->stats.rx_dropped++;
			goto _release_frame;
		}
	}

	/* TODO We might want to do some prefetches here (skb, shinfo, data) */

	/*
	 * Make sure forwarded skbs will have enough space on Tx,
	 * if extra headers are added.
	 */
	skb_reserve(skb, DPA_BP_HEAD + dpa_get_rx_extra_headroom());

	dpa_bp_removed_one_page(dpa_bp, addr);

	/* prefetch the first 64 bytes of the frame or the SGT start */
	prefetch(phys_to_virt(addr) + dpa_fd_offset(fd));

	if (likely(fd->format == qm_fd_contig)) {
		if (unlikely(contig_fd_to_skb(priv, fd, skb, &use_gro))) {
			/*
			 * There was a L4 HXS error - e.g. the L4 csum was
			 * invalid - so drop the frame early instead of passing
			 * it on to the stack. We'll increment our private
			 * counters to track this event.
			 */
			percpu_priv->l4_hxs_errors++;
			percpu_priv->stats.rx_dropped++;
			goto drop_bad_frame;
		}
	} else if (fd->format == qm_fd_sg) {
		if (unlikely(sg_fd_to_skb(priv, fd, skb, &use_gro))) {
			percpu_priv->l4_hxs_errors++;
			percpu_priv->stats.rx_dropped++;
			goto drop_bad_frame;
		}
	} else
		/* The only FD types that we may receive are contig and S/G */
		BUG();

	skb->protocol = eth_type_trans(skb, net_dev);

	if (unlikely(dpa_check_rx_mtu(skb, net_dev->mtu))) {
		percpu_priv->stats.rx_dropped++;
		goto drop_bad_frame;
	}

	skb_len = skb->len;

	if (use_gro) {
		gro_result_t gro_result;

		gro_result = napi_gro_receive(&percpu_priv->napi, skb);
		if (unlikely(gro_result == GRO_DROP)) {
			percpu_priv->stats.rx_dropped++;
			goto packet_dropped;
		}
	} else if (unlikely(netif_receive_skb(skb) == NET_RX_DROP)) {
		percpu_priv->stats.rx_dropped++;
		goto packet_dropped;
	}

	percpu_priv->stats.rx_packets++;
	percpu_priv->stats.rx_bytes += skb_len;

packet_dropped:
	net_dev->last_rx = jiffies;
	return;

drop_bad_frame:
	dev_kfree_skb(skb);
	return;

_release_frame:
	dpa_fd_release(net_dev, fd);
}

static int __hot skb_to_contig_fd(struct dpa_priv_s *priv,
				  struct sk_buff *skb, struct qm_fd *fd)
{
	struct sk_buff **skbh;
	dma_addr_t addr;
	struct dpa_bp *dpa_bp;
	struct net_device *net_dev = priv->net_dev;
	int err;

	/* We are guaranteed that we have at least DPA_BP_HEAD of headroom. */
	skbh = (struct sk_buff **)(skb->data - DPA_BP_HEAD);

	*skbh = skb;

	dpa_bp = priv->dpa_bp;

	/*
	 * Enable L3/L4 hardware checksum computation.
	 *
	 * We must do this before dma_map_single(DMA_TO_DEVICE), because we may
	 * need to write into the skb.
	 */
	err = dpa_enable_tx_csum(priv, skb, fd,
				 ((char *)skbh) + DPA_TX_PRIV_DATA_SIZE);
	if (unlikely(err < 0)) {
		if (netif_msg_tx_err(priv) && net_ratelimit())
			cpu_netdev_err(net_dev, "HW csum error: %d\n", err);
		return err;
	}

	/* Fill in the FD */
	fd->format = qm_fd_contig;
	fd->length20 = skb->len;
	fd->offset = DPA_BP_HEAD; /* This is now guaranteed */

	addr = dma_map_single(dpa_bp->dev, skbh, dpa_bp->size, DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(dpa_bp->dev, addr))) {
		if (netif_msg_tx_err(priv) && net_ratelimit())
			cpu_netdev_err(net_dev, "dma_map_single() failed\n");
		return -EINVAL;
	}
	fd->addr_hi = upper_32_bits(addr);
	fd->addr_lo = lower_32_bits(addr);

	return 0;
}

static int __hot skb_to_sg_fd(struct dpa_priv_s *priv,
			      struct dpa_percpu_priv_s *percpu_priv,
			      struct sk_buff *skb, struct qm_fd *fd)
{
	struct dpa_bp *dpa_bp = priv->dpa_bp;
	dma_addr_t addr;
	struct sk_buff **skbh;
	struct net_device *net_dev = priv->net_dev;
	int err;

	struct qm_sg_entry *sgt;
	unsigned long sgt_page, sg0_page;
	void *buffer_start;
	skb_frag_t *frag;
	int i, j, nr_frags;
	enum dma_data_direction dma_dir;
	bool can_recycle = false;

	fd->format = qm_fd_sg;

	/* get a new page to store the SGTable */
	sgt_page = __get_free_page(GFP_ATOMIC);
	if (unlikely(!sgt_page)) {
		dpaa_eth_err(dpa_bp->dev, "__get_free_page() failed\n");
		return -ENOMEM;
	}

	/*
	 * Enable L3/L4 hardware checksum computation.
	 *
	 * We must do this before dma_map_single(DMA_TO_DEVICE), because we may
	 * need to write into the skb.
	 */
	err = dpa_enable_tx_csum(priv, skb, fd,
				 (void *)sgt_page + DPA_TX_PRIV_DATA_SIZE);
	if (unlikely(err < 0)) {
		if (netif_msg_tx_err(priv) && net_ratelimit())
			cpu_netdev_err(net_dev, "HW csum error: %d\n", err);
		goto csum_failed;
	}

	sgt = (struct qm_sg_entry *)(sgt_page + DPA_BP_HEAD);
	/*
	 * TODO: do we need to memset all entries or just the number of entries
	 * we really use? Might improve perf...
	 */
	memset(sgt, 0, DPA_SGT_MAX_ENTRIES * sizeof(*sgt));

	/*
	 * Decide whether the skb is recycleable. We will need this information
	 * upfront to decide what DMA mapping direction we want to use.
	 */
	nr_frags =  skb_shinfo(skb)->nr_frags;
	if (!skb_cloned(skb) && !skb_shared(skb) &&
	   (*percpu_priv->dpa_bp_count + nr_frags + 2 < dpa_bp->target_count)) {
		can_recycle = true;
		/*
		 * We want each fragment to have at least dpa_bp->size bytes.
		 * If even one fragment is smaller, the entire FD becomes
		 * unrecycleable.
		 * Same holds if the fragments are allocated from highmem.
		 */
		for (i = 0; i < nr_frags; i++) {
			skb_frag_t *crt_frag = &skb_shinfo(skb)->frags[i];
			if ((crt_frag->size < dpa_bp->size) ||
			    PageHighMem(crt_frag->page.p)) {
				can_recycle = false;
				break;
			}
		}
	}
	dma_dir = can_recycle ? DMA_BIDIRECTIONAL : DMA_TO_DEVICE;

	/*
	 * Populate the first SGT entry
	 * get a new page to store the skb linear buffer content
	 * in the first SGT entry
	 *
	 * TODO: See if we can use the original page that contains
	 * the linear buffer
	 */
	sg0_page = __get_free_page(GFP_ATOMIC);
	if (unlikely(!sg0_page)) {
		dpaa_eth_err(dpa_bp->dev, "__get_free_page() failed\n");
		err = -ENOMEM;
		goto sg0_page_alloc_failed;
	}

	sgt[0].bpid = dpa_bp->bpid;
	sgt[0].offset = 0;
	sgt[0].length = skb_headlen(skb);

	/*
	 * FIXME need more than one page if the linear part of the skb
	 * is longer than PAGE_SIZE
	 */
	if (unlikely(sgt[0].offset + skb_headlen(skb) > dpa_bp->size)) {
		pr_warn_once("tx headlen %d larger than available buffs %d\n",
			skb_headlen(skb), dpa_bp->size);
		err = -EINVAL;
		goto skb_linear_too_large;
	}

	buffer_start = (void *)sg0_page;
	memcpy(buffer_start + sgt[0].offset, skb->data, skb_headlen(skb));
	addr = dma_map_single(dpa_bp->dev, buffer_start, dpa_bp->size, dma_dir);
	if (unlikely(dma_mapping_error(dpa_bp->dev, addr))) {
		dpaa_eth_err(dpa_bp->dev, "DMA mapping failed");
		err = -EINVAL;
		goto sg0_map_failed;

	}
	sgt[0].addr_hi = upper_32_bits(addr);
	sgt[0].addr_lo = lower_32_bits(addr);

	/* populate the rest of SGT entries */
	for (i = 1; i <= skb_shinfo(skb)->nr_frags; i++) {
		frag = &skb_shinfo(skb)->frags[i - 1];
		sgt[i].bpid = dpa_bp->bpid;
		sgt[i].offset = 0;
		sgt[i].length = frag->size;

		/* This shouldn't happen */
		BUG_ON(!frag->page.p);

		addr = dma_map_page(dpa_bp->dev, skb_frag_page(frag),
			frag->page_offset, dpa_bp->size, dma_dir);

		if (unlikely(dma_mapping_error(dpa_bp->dev, addr))) {
			dpaa_eth_err(dpa_bp->dev, "DMA mapping failed");
			err = -EINVAL;
			goto sg_map_failed;
		}

		/* keep the offset in the address */
		sgt[i].addr_hi = upper_32_bits(addr);
		sgt[i].addr_lo = lower_32_bits(addr);
	}
	sgt[i - 1].final = 1;

	fd->length20 = skb->len;
	fd->offset = DPA_BP_HEAD;

	/* DMA map the SGT page */
	buffer_start = (void *)sgt - dpa_fd_offset(fd);
	skbh = (struct sk_buff **)buffer_start;
	*skbh = skb;

	addr = dma_map_single(dpa_bp->dev, buffer_start, dpa_bp->size, dma_dir);
	if (unlikely(dma_mapping_error(dpa_bp->dev, addr))) {
		dpaa_eth_err(dpa_bp->dev, "DMA mapping failed");
		err = -EINVAL;
		goto sgt_map_failed;
	}
	fd->addr_hi = upper_32_bits(addr);
	fd->addr_lo = lower_32_bits(addr);

	if (can_recycle) {
		/* all pages are going to be recycled */
		fd->cmd |= FM_FD_CMD_FCO;
		fd->bpid = dpa_bp->bpid;
	}

	return 0;

sgt_map_failed:
sg_map_failed:
	for (j = 0; j < i; j++)
		dma_unmap_page(dpa_bp->dev, qm_sg_addr(&sgt[j]),
			dpa_bp->size, dma_dir);
sg0_map_failed:
	free_page(sg0_page);
skb_linear_too_large:
sg0_page_alloc_failed:
csum_failed:
	free_page(sgt_page);

	return err;
}

int __hot dpa_tx(struct sk_buff *skb, struct net_device *net_dev)
{
	struct dpa_priv_s	*priv;
	struct qm_fd		 fd;
	struct dpa_percpu_priv_s *percpu_priv;
	int queue_mapping;
	int err;

	priv = netdev_priv(net_dev);
	percpu_priv = per_cpu_ptr(priv->percpu_priv, smp_processor_id());

	clear_fd(&fd);

	queue_mapping = skb_get_queue_mapping(skb);


#ifdef CONFIG_FSL_DPA_1588
	if (priv->tsu && priv->tsu->valid && priv->tsu->hwts_tx_en_ioctl)
		fd.cmd |= FM_FD_CMD_UPD;
#endif

	if (skb_is_nonlinear(skb)) {
		/* Just create a S/G fd based on the skb */
		err = skb_to_sg_fd(priv, percpu_priv, skb, &fd);
		percpu_priv->tx_frag_skbuffs++;
	} else {
		/*
		 * Make sure we have enough headroom to accomodate private
		 * data, parse results, etc
		 */
		if (skb_headroom(skb) < DPA_BP_HEAD) {
			struct sk_buff *skb_new;

			skb_new = skb_realloc_headroom(skb, DPA_BP_HEAD);
			if (unlikely(!skb_new)) {
				dev_kfree_skb(skb);
				percpu_priv->stats.tx_errors++;
				return NETDEV_TX_OK;
			}
			dev_kfree_skb(skb);
			skb = skb_new;
		}

		/*
		 * We're going to store the skb backpointer at the beginning
		 * of the data buffer, so we need a privately owned skb
		 */
		skb = skb_unshare(skb, GFP_ATOMIC);
		if (unlikely(!skb)) {
			percpu_priv->stats.tx_errors++;
			return NETDEV_TX_OK;
		}

		/* Finally, create a contig FD from this skb */
		err = skb_to_contig_fd(priv, skb, &fd);
	}
	if (unlikely(err < 0)) {
		percpu_priv->stats.tx_errors++;
		dev_kfree_skb(skb);
		return NETDEV_TX_OK;
	}

#if (DPAA_VERSION >= 11)
	fd.cmd &= ~FM_FD_CMD_FCO;
#endif

	if (fd.cmd & FM_FD_CMD_FCO) {
		/*
		 * Need to free the skb, but without releasing
		 * the page fragments, so increment the pages usage count
		 */
		int i;

		for (i = 0; i < skb_shinfo(skb)->nr_frags; i++)
			get_page(skb_shinfo(skb)->frags[i].page.p);

		/*
		 * We release back to the pool a number of pages equal to
		 * the number of skb fragments + one page for the linear
		 * portion of the skb + one page for the S/G table
		 */
		*percpu_priv->dpa_bp_count += skb_shinfo(skb)->nr_frags + 2;
		percpu_priv->tx_returned++;
		dev_kfree_skb(skb);
		skb = NULL;
	}

	if (unlikely(dpa_xmit(priv, percpu_priv, queue_mapping, &fd) < 0))
		goto xmit_failed;

	net_dev->trans_start = jiffies;

	return NETDEV_TX_OK;

xmit_failed:
	if (fd.cmd & FM_FD_CMD_FCO) {
		*percpu_priv->dpa_bp_count -= skb_shinfo(skb)->nr_frags + 2;
		percpu_priv->tx_returned--;

		dpa_fd_release(net_dev, &fd);
		return NETDEV_TX_OK;
	}
	_dpa_cleanup_tx_fd(priv, &fd);
	dev_kfree_skb(skb);

	return NETDEV_TX_OK;
}

#endif /* CONFIG_DPAA_ETH_SG_SUPPORT */
