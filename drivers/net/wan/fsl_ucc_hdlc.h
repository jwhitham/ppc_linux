/* Freescale QUICC Engine HDLC Device Driver
 *
 * Copyright 2014 Freescale Semiconductor Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef CONFIG_UCC_HDLC_H
#define CONFIG_UCC_HDLC_H

#include <linux/kernel.h>
#include <linux/list.h>

#include <linux/fsl/immap_qe.h>
#include <linux/fsl/qe.h>

#include <linux/fsl/ucc.h>
#include <linux/fsl/ucc_fast.h>

/* SI RAM entries */
#define SIR_LAST	0x0001
#define SIR_BYTE	0x0002
#define SIR_CNT(x)	((x) << 2)
#define SIR_CSEL(x)	((x) << 5)
#define SIR_SGS		0x0200
#define SIR_SWTR	0x4000
#define SIR_MCC		0x8000
#define SIR_IDLE	0

/* SIxMR fields */
#define SIMR_SAD(x) ((x) << 12)
#define SIMR_SDM_NORMAL	0x0000
#define SIMR_SDM_INTERNAL_LOOPBACK	0x0800
#define SIMR_SDM_MASK	0x0c00
#define SIMR_CRT	0x0040
#define SIMR_SL		0x0020
#define SIMR_CE		0x0010
#define SIMR_FE		0x0008
#define SIMR_GM		0x0004
#define SIMR_TFSD(n)	(n)
#define SIMR_RFSD(n)	((n) << 8)

enum tdm_ts_t {
	TDM_TX_TS,
	TDM_RX_TS
};


enum tdm_framer_t {
	TDM_FRAMER_T1,
	TDM_FRAMER_E1
};

enum tdm_mode_t {
	TDM_INTERNAL_LOOPBACK,
	TDM_NORMAL
};

struct ucc_hdlc_param {
	__be16 riptr;
	__be16 tiptr;
	__be16 res0;
	__be16 mrblr;
	__be32 rstate;
	__be32 rbase;
	__be16 rbdstat;
	__be16 rbdlen;
	__be32 rdptr;
	__be32 tstate;
	__be32 tbase;
	__be16 tbdstat;
	__be16 tbdlen;
	__be32 tdptr;
	__be32 rbptr;
	__be32 tbptr;
	__be32 rcrc;
	__be32 res1;
	__be32 tcrc;
	__be32 res2;
	__be32 res3;
	__be32 c_mask;
	__be32 c_pres;
	__be16 disfc;
	__be16 crcec;
	__be16 abtsc;
	__be16 nmarc;
	__be32 max_cnt;
	__be16 mflr;
	__be16 rfthr;
	__be16 rfcnt;
	__be16 hmask;
	__be16 haddr1;
	__be16 haddr2;
	__be16 haddr3;
	__be16 haddr4;
	__be16 ts_tmp;
	__be16 tmp_mb;
} __attribute__ ((__packed__));

struct si_mode_info {
	u8 simr_rfsd;
	u8 simr_tfsd;
	u8 simr_crt;
	u8 simr_sl;
	u8 simr_ce;
	u8 simr_fe;
	u8 simr_gm;
};

struct ucc_hdlc_info {
	struct ucc_fast_info uf_info;
	struct si_mode_info si_info;
};

struct ucc_hdlc_private {
	struct ucc_hdlc_info *uh_info;
	struct ucc_fast_private *uccf;
	struct device *dev;
	struct net_device *ndev;
	struct ucc_fast __iomem *uf_regs;	/* UCC Fast registers */
	struct si1 __iomem *si_regs;
	struct ucc_hdlc_param __iomem *ucc_pram;
	u16 tsa;
	u16 tdm_port;		/* port for this tdm:TDMA,TDMB */
	u32 siram_entry_id;
	u16 __iomem *siram;
	enum tdm_mode_t tdm_mode;
	enum tdm_framer_t tdm_framer_type;
	bool hdlc_busy;
	u8 loopback;
	u8 num_of_ts;		/* the number of timeslots in this tdm frame */
	u32 tx_ts_mask;		/* tx time slot mask */
	u32 rx_ts_mask;		/* rx time slot mask */
	u8 *rx_buffer;		/* buffer used for Rx by the HDLC */
	u8 *tx_buffer;		/* buffer used for Tx by the HDLC */
	dma_addr_t dma_rx_addr;	/* dma mapped buffer for HDLC Rx */
	dma_addr_t dma_tx_addr;	/* dma mapped buffer for HDLC Tx */
	struct qe_bd *tx_bd_base;
	struct qe_bd *rx_bd_base;
	struct qe_bd *curtx_bd;
	struct qe_bd *currx_bd;
	struct qe_bd *dirty_tx;
	struct sk_buff **tx_skbuff;
	struct sk_buff **rx_skbuff;
	u16 skb_currx;
	u16 skb_curtx;
	u16 currx_bdnum;
	unsigned short skb_dirtytx;
	unsigned short tx_ring_size;
	unsigned short rx_ring_size;
	u32 ucc_pram_offset;
	dma_addr_t dma_rx_bd;
	dma_addr_t dma_tx_bd;

	unsigned short encoding;
	unsigned short parity;
	u32 clocking;
#ifdef CONFIG_PM
	struct ucc_hdlc_param *ucc_pram_bak;
	u32 gumr;
	u8 guemr;
	u32 cmxsi1cr_l, cmxsi1cr_h;
	u32 cmxsi1syr;
	u32 cmxucr[4];
#endif
};

#define TX_BD_RING_LEN	0x10
#define RX_BD_RING_LEN	0x20
#define RX_CLEAN_MAX	0x10
#define NUM_OF_BUF	4
#define MAX_RX_BUF_LENGTH	(48*0x20)
#define ALIGNMENT_OF_UCC_HDLC_PRAM	64
#define SI_BANK_SIZE	128
#define MAX_HDLC_NUM	4
#define BD_LEN_MASK	0xffff
#define HDLC_HEAD_LEN	3
#define HDLC_CRC_SIZE	2
#define TX_RING_MOD_MASK(size) (size-1)
#define RX_RING_MOD_MASK(size) (size-1)

#define HDLC_HEAD_MASK		0x000000ff
#define DEFAULT_HDLC_HEAD	0x68aa4400
#define DEFAULT_ADDR_MASK	0xffff
#define DEFAULT_HDLC_ADDR	0xaa68
#define DEFAULT_BROAD_ADDR	0xffff

#endif
