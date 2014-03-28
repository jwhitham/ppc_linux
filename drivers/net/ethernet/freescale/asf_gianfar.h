/* drivers/net/ethernet/freescale/asf_gianfar.h
 *
 * ASF Specific Gianfar Ethernet Driver
 *
 * Copyright 2014 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */
#ifndef __ASF_GIANFAR_H
#define __ASF_GIANFAR_H

#define TSTAT_TXF_MASK_ALL	0x0000FF00
#define TSTAT_TXF0_MASK		0x00008000


#ifdef IMASK_DEFAULT
#undef IMASK_DEFAULT
#endif

#define IMASK_DEFAULT  (IMASK_TXEEN | \
			IMASK_RXFEN0 | IMASK_BSY | IMASK_EBERR | IMASK_BABR | \
			IMASK_XFUN | IMASK_RXC | IMASK_BABT | IMASK_DPE \
			| IMASK_PERR)

extern DEFINE_PER_CPU(struct sk_buff_head, skb_recycle_list);
extern irqreturn_t gfar_enable_tx_queue(int irq, void *dev_id);
extern void gfar_tx_vlan(struct sk_buff *skb, struct txfcb *fcb);
extern int gfar_asf_start_xmit(struct sk_buff *skb, struct net_device *dev);
extern struct sk_buff *gfar_new_skb(struct net_device *dev);
int gfar_asf_clean_rx_ring(struct gfar_priv_rx_q *rx_queue, int rx_work_limit);
extern void gfar_asf_process_frame(struct net_device *dev, struct sk_buff *skb,
				int amount_pull, struct napi_struct *napi);

#define AS_FP_PROCEED  1
#define AS_FP_STOLEN   2
typedef        int (*devfp_hook_t)(struct sk_buff *skb, struct net_device *dev);
extern devfp_hook_t    devfp_rx_hook;
extern devfp_hook_t    devfp_tx_hook;

/* Overwrite the Rx/Tx Hooks pointers
 * if already configured.
 */
static inline int devfp_register_rx_hook(devfp_hook_t hook)
{
	devfp_rx_hook = hook;
	return 0;
}

static inline int devfp_register_tx_hook(devfp_hook_t hook)
{
	devfp_tx_hook = hook;
	return 0;
}
#endif /* __ASF_GIANFAR_H */
