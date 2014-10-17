/* drivers/net/ethernet/freescale/asf_gianfar.c
 *
 * Gianfar ASF function
 *
 * Description : This file is designed to add ASF functionality to gianfar.c
 * This functionalities includes tx_hook for asf and some
 * optimization for packet xmit.
 *
 * Author : Alok Makhariya
 *
 * Copyright 2014 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#include <linux/if_vlan.h>
#include "gianfar.h"
#include "asf_gianfar.h"

#define RT_PKT_ID 0xff
#define KER_PKT_ID 0xfe

#define GFAR_RXB_REC_SZ (DEFAULT_RX_BUFFER_SIZE + RXBUF_ALIGNMENT)

devfp_hook_t   devfp_rx_hook;
EXPORT_SYMBOL(devfp_rx_hook);

devfp_hook_t   devfp_tx_hook;
EXPORT_SYMBOL(devfp_tx_hook);

static inline void gfar_asf_reclaim_skb(struct sk_buff *skb)
{
	/* Just reset the fields used in software DPA */
	skb->next = skb->prev = NULL;
	skb->dev = NULL;
	skb->len = 0;
	skb->ip_summed = 0;
	skb->transport_header = 0;
	skb->mac_header = 0;
	skb->network_header = 0;
	skb->pkt_type = 0;
	skb->mac_len = 0;
	skb->protocol = 0;
	skb->vlan_tci = 0;
	skb->data = 0;
	/* reset data and tail pointers */
	skb->data = skb->head + NET_SKB_PAD;
	skb_reset_tail_pointer(skb);
}

static inline void gfar_recycle_skb(struct sk_buff *skb)
{
	struct sk_buff_head *h = &__get_cpu_var(skb_recycle_list);
	int skb_size = SKB_DATA_ALIGN(GFAR_RXB_REC_SZ + NET_SKB_PAD
							 + EXTRA_HEADROOM);

	if (skb_queue_len(h) < DEFAULT_RX_RING_SIZE &&
		!skb_cloned(skb) && !skb_is_nonlinear(skb) &&
		skb->fclone == SKB_FCLONE_UNAVAILABLE && !skb_shared(skb) &&
		skb_end_offset(skb) == skb_size) {

		if (skb->pkt_type == PACKET_FASTROUTE)
			gfar_asf_reclaim_skb(skb);
		else
			skb_recycle(skb);

		gfar_align_skb(skb);

		__skb_queue_head(h, skb);

		return;
	}

	dev_kfree_skb_any(skb);
}

/* gfar_process_frame() -- handle one incoming packet if skb isn't NULL. */
static void gfar_process_frame(struct net_device *dev, struct sk_buff *skb,
			       int amount_pull, struct napi_struct *napi)
{
	struct gfar_private *priv = netdev_priv(dev);
	struct rxfcb *fcb = NULL;

	/* fcb is at the beginning if exists */
	fcb = (struct rxfcb *)skb->data;

	/* Remove the FCB from the skb
	 * Remove the padded bytes, if there are any
	 */
	if (amount_pull) {
		skb_record_rx_queue(skb, fcb->rq);
		skb_pull(skb, amount_pull);
	}

	/* Get receive timestamp from the skb */
	if (priv->hwts_rx_en) {
		struct skb_shared_hwtstamps *shhwtstamps = skb_hwtstamps(skb);
		u64 *ns = (u64 *) skb->data;

		memset(shhwtstamps, 0, sizeof(*shhwtstamps));
		shhwtstamps->hwtstamp = ns_to_ktime(*ns);
	}

	if (priv->padding)
		skb_pull(skb, priv->padding);

	if (dev->features & NETIF_F_RXCSUM)
		gfar_rx_checksum(skb, fcb);

	if (devfp_rx_hook) {
		/* Drop the packet silently if IP Checksum is not correct */
		if ((fcb->flags & RXFCB_CIP) && (fcb->flags & RXFCB_EIP)) {
			dev_kfree_skb_any(skb);
			return;
		}
	if (dev->features & NETIF_F_HW_VLAN_CTAG_RX &&
		fcb->flags & RXFCB_VLN)
		__vlan_hwaccel_put_tag(skb, htons(ETH_P_8021Q), fcb->vlctl);

	skb->dev = dev;
	if (devfp_rx_hook(skb, dev) == AS_FP_STOLEN)
		return;
	}

	/* Tell the skb what kind of packet this is */
	skb->protocol = eth_type_trans(skb, dev);

	/* There's need to check for NETIF_F_HW_VLAN_CTAG_RX here.
	 * Even if vlan rx accel is disabled, on some chips
	 * RXFCB_VLN is pseudo randomly set.
	 */
	if (dev->features & NETIF_F_HW_VLAN_CTAG_RX &&
	    fcb->flags & RXFCB_VLN)
		__vlan_hwaccel_put_tag(skb, htons(ETH_P_8021Q), fcb->vlctl);

	/* Send the packet up the stack */
	napi_gro_receive(napi, skb);

}

int gfar_asf_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct gfar_private *priv = netdev_priv(dev);
	struct gfar_priv_tx_q *tx_queue = NULL;
	struct netdev_queue *txq;
	struct gfar __iomem *regs = NULL;
	struct txfcb *fcb = NULL;
	struct txbd8 *txbdp, *txbdp_start, *base, *txbdp_tstamp = NULL;
	u32 lstatus;
	int i, rq = 0;
	int do_tstamp, do_csum, do_vlan;
	u32 bufaddr;
	int skb_curtx = 0;
	unsigned int nr_frags, nr_txbds, bytes_sent, fcb_len = 0;

	if (devfp_tx_hook && (skb->pkt_type != PACKET_FASTROUTE))
		if (devfp_tx_hook(skb, dev) == AS_FP_STOLEN)
			return 0;

	rq = smp_processor_id();

	tx_queue = priv->tx_queue[rq];
	txq = netdev_get_tx_queue(dev, rq);
	base = tx_queue->tx_bd_base;
	regs = tx_queue->grp->regs;
	do_csum = (CHECKSUM_PARTIAL == skb->ip_summed);
	do_vlan = vlan_tx_tag_present(skb);
	do_tstamp = (skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP) &&
			priv->hwts_tx_en;
	if (do_csum || do_vlan)
		fcb_len = GMAC_FCB_LEN;
	/* check if time stamp should be generated */
		if (unlikely(do_tstamp))
			fcb_len = GMAC_FCB_LEN + GMAC_TXPAL_LEN;

	/* make space for additional header when fcb is needed */
	if (fcb_len && unlikely(skb_headroom(skb) < fcb_len)) {
		struct sk_buff *skb_new;
		skb_new = skb_realloc_headroom(skb, fcb_len);
		if (!skb_new) {
			dev->stats.tx_errors++;
			kfree_skb(skb);
			return NETDEV_TX_OK;
		}

		if (skb->sk)
			skb_set_owner_w(skb_new, skb->sk);
		consume_skb(skb);
		skb = skb_new;
	}

	/* total number of fragments in the SKB */
	nr_frags = skb_shinfo(skb)->nr_frags;

	/* calculate the required number of TxBDs for this skb */
	if (unlikely(do_tstamp))
		nr_txbds = nr_frags + 2;
	else
		nr_txbds = nr_frags + 1;

	txbdp = tx_queue->cur_tx;
	skb_curtx = tx_queue->skb_curtx;
	do {
		lstatus = txbdp->lstatus;
		if ((lstatus & BD_LFLAG(TXBD_READY))) {
			/* BD not free for tx */
			dev->stats.tx_fifo_errors++;
			return NETDEV_TX_BUSY;
		}

		/* BD is free to be used by s/w */
		/* Free skb for this BD if not recycled */

		txbdp->lstatus &= BD_LFLAG(TXBD_WRAP);
		skb_curtx = (skb_curtx + 1)
				& TX_RING_MOD_MASK(tx_queue->tx_ring_size);
		nr_txbds--;

		if (!nr_txbds)
			break;

		txbdp = next_txbd(txbdp, base, tx_queue->tx_ring_size);
	} while (1);

	/* Update transmit stats */
	bytes_sent = skb->len;
	tx_queue->stats.tx_bytes += bytes_sent;
	/* keep Tx bytes on wire for BQL accounting */
	GFAR_CB(skb)->bytes_sent = bytes_sent;
	tx_queue->stats.tx_packets++;

	txbdp = txbdp_start = tx_queue->cur_tx;
	lstatus = txbdp->lstatus;

	/* Time stamp insertion requires one additional TxBD */
	if (unlikely(do_tstamp))
		txbdp_tstamp = txbdp = next_txbd(txbdp, base,
					tx_queue->tx_ring_size);

	if (nr_frags == 0) {
		if (unlikely(do_tstamp))
			txbdp_tstamp->lstatus |= BD_LFLAG(TXBD_LAST |
							TXBD_INTERRUPT);
		else
			lstatus |= BD_LFLAG(TXBD_LAST | TXBD_INTERRUPT);
	} else {
		/* Place the fragment addresses and lengths into the TxBDs */
		for (i = 0; i < nr_frags; i++) {
			unsigned int frag_len;
			/* Point at the next BD, wrapping as needed */
			txbdp = next_txbd(txbdp, base, tx_queue->tx_ring_size);
			frag_len = skb_shinfo(skb)->frags[i].size;
			lstatus = txbdp->lstatus | frag_len |
						BD_LFLAG(TXBD_READY);

			/* Handle the last BD specially */
			if (i == nr_frags - 1)
				lstatus |= BD_LFLAG(TXBD_LAST | TXBD_INTERRUPT);

			bufaddr = skb_frag_dma_map(priv->dev,
						&skb_shinfo(skb)->frags[i],
						0,
						frag_len,
						DMA_TO_DEVICE);

			/* set the TxBD length and buffer pointer */
			txbdp->bufPtr = bufaddr;
			txbdp->lstatus = lstatus;
		}

		lstatus = txbdp_start->lstatus;
	}

	/* Add TxPAL between FCB and frame if required */
	if (unlikely(do_tstamp)) {
		skb_push(skb, GMAC_TXPAL_LEN);
		memset(skb->data, 0, GMAC_TXPAL_LEN);
	}

	/* Add TxFCB if required */
	if (fcb_len) {
		fcb = gfar_add_fcb(skb);
		lstatus |= BD_LFLAG(TXBD_TOE);
	}

	/* Set up checksumming */
	if (do_csum) {
		gfar_tx_checksum(skb, fcb, fcb_len);

		if (unlikely(gfar_csum_errata_12(priv, (unsigned long)fcb)) ||
			unlikely(gfar_csum_errata_76(priv, skb->len))) {
			__skb_pull(skb, GMAC_FCB_LEN);
			skb_checksum_help(skb);
			if (do_vlan || do_tstamp) {
				/* put back a new fcb for vlan/tstamp TOE */
				fcb = gfar_add_fcb(skb);
			} else {
				/* Tx TOE not used */
				lstatus &= ~(BD_LFLAG(TXBD_TOE));
				fcb = NULL;
			}
		}
	}

	if (do_vlan)
		gfar_tx_vlan(skb, fcb);

	/* Setup tx hardware time stamping if requested */
	if (unlikely(do_tstamp)) {
		skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;
		fcb->ptp = 1;
	}

	txbdp_start->bufPtr = dma_map_single(priv->dev, skb->data,
					skb_headlen(skb), DMA_TO_DEVICE);

	/* If time stamping is requested one additional TxBD must be set up. The
	* first TxBD points to the FCB and must have a data length of
	* GMAC_FCB_LEN. The second TxBD points to the actual frame data with
	* the full frame length.
	*/
	if (unlikely(do_tstamp)) {
		txbdp_tstamp->bufPtr = txbdp_start->bufPtr + fcb_len;
		txbdp_tstamp->lstatus |= BD_LFLAG(TXBD_READY) |
					(skb_headlen(skb) - fcb_len);
		lstatus |= BD_LFLAG(TXBD_CRC | TXBD_READY) | GMAC_FCB_LEN;
	} else {
		lstatus |= BD_LFLAG(TXBD_CRC | TXBD_READY) | skb_headlen(skb);
	}

	/* We can work in parallel with gfar_clean_tx_ring(), except
	* when modifying num_txbdfree. Note that we didn't grab the lock
	* when we were reading the num_txbdfree and checking for available
	* space, that's because outside of this function it can only grow,
	* and once we've got needed space, it cannot suddenly disappear.
	*
	* The lock also protects us from gfar_error(), which can modify
	* regs->tstat and thus retrigger the transfers, which is why we
	* also must grab the lock before setting ready bit for the first
	* to be transmitted BD.
	*/

	/* The powerpc-specific eieio() is used, as wmb() has too strong
	* semantics (it requires synchronization between cacheable and
	* uncacheable mappings, which eieio doesn't provide and which we
	* don't need), thus requiring a more expensive sync instruction.  At
	* some point, the set of architecture-independent barrier functions
	* should be expanded to include weaker barriers.
	*/
	eieio();

	txbdp_start->lstatus = lstatus;

	eieio(); /* force lstatus write before tx_skbuff */

	skb_curtx = tx_queue->skb_curtx;

	/* Update the current skb pointer to the next entry we will use
	* (wrapping if necessary)
	*/
	tx_queue->skb_curtx = (tx_queue->skb_curtx + 1) &
				TX_RING_MOD_MASK(tx_queue->tx_ring_size);

	tx_queue->cur_tx = next_txbd(txbdp, base, tx_queue->tx_ring_size);


	/* Tell the DMA to go go go */
	gfar_write(&regs->tstat, TSTAT_CLEAR_THALT >> tx_queue->qindex);

	if ((skb->owner != RT_PKT_ID) ||
		(!skb_is_recycleable(skb, DEFAULT_RX_BUFFER_SIZE +
						RXBUF_ALIGNMENT))) {
		skb->new_skb = NULL;
		gfar_recycle_skb(skb);
	} else {
		if (skb->pkt_type == PACKET_FASTROUTE)
			gfar_asf_reclaim_skb(skb);
		else
			skb_recycle(skb);
		gfar_align_skb(skb);
		skb->new_skb = skb;
	}
	txq->trans_start = jiffies;

	return NETDEV_TX_OK;
}

irqreturn_t gfar_enable_tx_queue(int irq, void *grp_id)
{
	struct gfar_priv_grp *grp = (struct gfar_priv_grp *)grp_id;
	struct gfar_private *priv = priv = grp->priv;
	struct gfar_priv_tx_q *tx_queue = NULL;
	u32 tstat, mask;
	int i;
	unsigned long flags;

	struct net_device *dev = NULL;
	tstat = gfar_read(&grp->regs->tstat);
	tstat = tstat & TSTAT_TXF_MASK_ALL;

	/* Clear IEVENT */
	gfar_write(&grp->regs->ievent, IEVENT_TX_MASK);

	for_each_set_bit(i, &grp->tx_bit_map, priv->num_tx_queues) {
		mask = TSTAT_TXF0_MASK >> i;
		if (tstat & mask) {
			tx_queue = priv->tx_queue[i];
			dev = tx_queue->dev;
			if (__netif_subqueue_stopped(dev, tx_queue->qindex))
				netif_wake_subqueue(dev, tx_queue->qindex);
		}
	}

	spin_lock_irqsave(&grp->grplock, flags);
	mask = gfar_read(&grp->regs->imask);
	mask = mask & IMASK_TX_DISABLED;
	gfar_write(&grp->regs->imask, mask);
	spin_unlock_irqrestore(&grp->grplock, flags);

	return IRQ_HANDLED;
}

/* gfar_asf_clean_rx_ring() -- Processes each frame in the rx ring
 * until the budget/quota has been reached. Returns the number
 * of frames handled
 */
int gfar_asf_clean_rx_ring(struct gfar_priv_rx_q *rx_queue, int rx_work_limit)
{
	struct net_device *dev = rx_queue->dev;
	struct rxbd8 *bdp, *base;
	struct sk_buff *skb;
	int pkt_len;
	int amount_pull;
	int howmany = 0;
	struct gfar_private *priv = netdev_priv(dev);

	/* Get the first full descriptor */
	bdp = rx_queue->cur_rx;
	base = rx_queue->rx_bd_base;

	amount_pull = priv->uses_rxfcb ? GMAC_FCB_LEN : 0;

	while (!((bdp->status & RXBD_EMPTY) || (--rx_work_limit < 0))) {
		struct sk_buff *newskb = NULL;

		/* Adding memory barrier */
		rmb();

		skb = rx_queue->rx_skbuff[rx_queue->skb_currx];

		dma_unmap_single(priv->dev, bdp->bufPtr,
				 priv->rx_buffer_size, DMA_FROM_DEVICE);

		if (unlikely(!(bdp->status & RXBD_ERR) &&
			     bdp->length > priv->rx_buffer_size))
			bdp->status = RXBD_LARGE;

		if (unlikely(!(bdp->status & RXBD_LAST) ||
					bdp->status & RXBD_ERR)) {
			count_errors(bdp->status, dev);
			newskb = skb;
		} else {
			/* Increment the number of packets */
			rx_queue->stats.rx_packets++;
			howmany++;

			if (likely(skb)) {
				pkt_len = bdp->length - ETH_FCS_LEN;
				/* Remove the FCS from the packet length */
				skb_put(skb, pkt_len);
				rx_queue->stats.rx_bytes += pkt_len;
				skb_record_rx_queue(skb, rx_queue->qindex);
				skb->owner = RT_PKT_ID;
				gfar_process_frame(dev, skb, amount_pull,
						   &rx_queue->grp->napi_rx);
				newskb = skb->new_skb;
				skb->owner = 0;
				skb->new_skb = NULL;
			} else {
				netif_warn(priv, rx_err, dev, "Missing skb!\n");
				rx_queue->stats.rx_dropped++;
				atomic64_inc(&priv->extra_stats.rx_skbmissing);
			}

		}

		if (!newskb) {
			/* Allocate new skb for Rx ring */
			newskb = gfar_new_skb(dev);
		}
		if (!newskb)
			/* All memory Exhausted,a BUG */
			BUG();
		rx_queue->rx_skbuff[rx_queue->skb_currx] = newskb;

		/* Setup the new bdp */
		gfar_new_rxbdp(rx_queue, bdp, newskb);

		/* Update to the next pointer */
		bdp = next_bd(bdp, base, rx_queue->rx_ring_size);

		/* update to point at the next skb */
		rx_queue->skb_currx = (rx_queue->skb_currx + 1) &
				      RX_RING_MOD_MASK(rx_queue->rx_ring_size);
	}

	/* Update the current rxbd pointer to be the next one */
	rx_queue->cur_rx = bdp;

	return howmany;
}

/* This is function is called directly by ASF when ASF runs in Minimal mode
* transmission.
*/

int gfar_fast_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct gfar_private *priv = netdev_priv(dev);
	struct gfar_priv_tx_q *tx_queue = NULL;
	struct netdev_queue *txq;
	struct gfar __iomem *regs = NULL;
	struct txbd8 *txbdp, *txbdp_start, *base;
	u32 lstatus;
	int rq = 0;
	int skb_curtx = 0;
	unsigned int fcb_length = GMAC_FCB_LEN;

	rq = smp_processor_id();
	tx_queue = priv->tx_queue[rq];
	txq = netdev_get_tx_queue(dev, rq);
	base = tx_queue->tx_bd_base;
	regs = tx_queue->grp->regs;

	txbdp = tx_queue->cur_tx;
	skb_curtx = tx_queue->skb_curtx;

	lstatus = txbdp->lstatus;
	if ((lstatus & BD_LFLAG(TXBD_READY))) {
		u32 imask;
		/* BD not free for tx */
		netif_tx_stop_queue(txq);
		dev->stats.tx_fifo_errors++;
		spin_lock_irq(&tx_queue->grp->grplock);
		imask = gfar_read(&regs->imask);
		imask |= IMASK_TX_DEFAULT;
		gfar_write(&regs->imask, imask);
		spin_unlock_irq(&tx_queue->grp->grplock);
		return NETDEV_TX_BUSY;
	}

	/* BD is free to be used by s/w */
	/* Free skb for this BD if not recycled */
	txbdp->lstatus &= BD_LFLAG(TXBD_WRAP);
	/* Update transmit stats */
	tx_queue->stats.tx_bytes += skb->len;
	tx_queue->stats.tx_packets++;

	txbdp = txbdp_start = tx_queue->cur_tx;
	lstatus = txbdp->lstatus | BD_LFLAG(TXBD_LAST | TXBD_INTERRUPT);

	/* Set up checksumming */

	if (CHECKSUM_PARTIAL == skb->ip_summed) {
		struct txfcb *fcb = NULL;
		fcb = gfar_add_fcb(skb);
		lstatus |= BD_LFLAG(TXBD_TOE);
		gfar_tx_checksum(skb, fcb, fcb_length);
	}

	txbdp_start->bufPtr = dma_map_single(priv->dev, skb->data,
					skb_headlen(skb), DMA_TO_DEVICE);

	lstatus |= BD_LFLAG(TXBD_CRC | TXBD_READY) | skb_headlen(skb);
	/* The powerpc-specific eieio() is used, as wmb() has too strong
	* semantics (it requires synchronization between cacheable and
	* uncacheable mappings, which eieio doesn't provide and which we
	* don't need), thus requiring a more expensive sync instruction.  At
	* some point, the set of architecture-independent barrier functions
	* should be expanded to include weaker barriers.
	*/
	eieio();

	txbdp_start->lstatus = lstatus;

	eieio(); /* force lstatus write before tx_skbuff */

	skb_curtx = tx_queue->skb_curtx;

	/* Update the current skb pointer to the next entry we will use
	* (wrapping if necessary)
	*/
	tx_queue->skb_curtx = (tx_queue->skb_curtx + 1) &
			TX_RING_MOD_MASK(tx_queue->tx_ring_size);

	tx_queue->cur_tx = next_txbd(txbdp, base, tx_queue->tx_ring_size);

	/* Tell the DMA to go go go */
	gfar_write(&regs->tstat, TSTAT_CLEAR_THALT >> tx_queue->qindex);

	if ((skb->owner != RT_PKT_ID) ||
		(!skb_is_recycleable(skb, DEFAULT_RX_BUFFER_SIZE +
						RXBUF_ALIGNMENT))) {
		skb->new_skb = NULL;
		gfar_recycle_skb(skb);
	} else {
		gfar_asf_reclaim_skb(skb);
		gfar_align_skb(skb);
		skb->new_skb = skb;
	}
	txq->trans_start = jiffies;
	return NETDEV_TX_OK;
}
EXPORT_SYMBOL(gfar_fast_xmit);
