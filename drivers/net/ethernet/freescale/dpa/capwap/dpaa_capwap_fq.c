/* Copyright 2014 Freescale Semiconductor, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Freescale Semiconductor nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
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
#include <linux/fsl_qman.h>

#include "dpaa_capwap_domain.h"
#include "dpaa_eth_common.h"
#include "dpaa_capwap.h"
#include "mac.h"

/* flows: 0--capwap dtls control tunnel
 *	  1--capwap dtls data tunnel
 *	  2--capwap non-dtls control tunnel
 *	  3--capwap non-dtls data tunnel
 */
#define CAPWAP_FLOW_COUNT	4

static struct dpaa_capwap_domain_fqs *fqs;

static qman_cb_dqrr rx_cbs[] = { capwap_control_dtls_rx_dqrr,
		capwap_data_dtls_rx_dqrr,
		capwap_control_n_dtls_rx_dqrr,
		capwap_data_n_dtls_rx_dqrr };

static int fill_fq_range(struct fqid_range *fqid_r, u32 count)
{
	u32 fqid_base;
	int ret;

	ret = qman_alloc_fqid_range(&fqid_base, count, 0, 0);
	if (ret != count) {
		pr_err("Can't alloc enough fqid for capwap\n");
		return -ENODEV;
	}

	fqid_r->fq_count = count;
	fqid_r->fqid_base = fqid_base;

	return 0;
}

static int capwap_alloc_fqs(void)
{
	int ret;

	fqs = kzalloc(sizeof(struct dpaa_capwap_domain_fqs), GFP_KERNEL);
	if (!fqs)
		return -ENOMEM;

	/* Four CAPWAP Tunnel + Non-CAPWAP */
	ret = fill_fq_range(&fqs->inbound_eth_rx_fqs, CAPWAP_FLOW_COUNT + 1);
	if (ret)
		goto alloc_failed;

	/* Two DTLS Tunnel */
	ret = fill_fq_range(&fqs->inbound_sec_to_op_fqs, CAPWAP_FLOW_COUNT / 2);
	if (ret)
		goto alloc_failed;

	/* Four CAPWAP Tunnel */
	ret = fill_fq_range(&fqs->inbound_core_rx_fqs, CAPWAP_FLOW_COUNT);
	if (ret)
		goto alloc_failed;

	/* Four CAPWAP Tunnel */
	ret = fill_fq_range(&fqs->outbound_core_tx_fqs, CAPWAP_FLOW_COUNT);
	if (ret)
		goto alloc_failed;

	/* The lower four flows are for sending back to OP (NON-DTLS Tunnel),
	 * or sending to SEC for encryption and then also sent back to OP.
	 * The upper four flows are for sending to Tx port after header
	 * manipulation
	 */
	ret = fill_fq_range(&fqs->outbound_op_tx_fqs, CAPWAP_FLOW_COUNT * 2);
	if (ret)
		goto alloc_failed;

	/* Two DTLS Tunnel */
	ret = fill_fq_range(&fqs->outbound_sec_to_op_fqs,
			CAPWAP_FLOW_COUNT / 2);
	if (ret)
		goto alloc_failed;

	ret = qman_alloc_fqid_range(&fqs->debug_fqid, 1, 0, 0);
	if (ret != 1) {
		pr_err("Can't alloc enough fqid for capwap\n");
		return -ENODEV;
	}
	return 0;

alloc_failed:
	kfree(fqs);
	return ret;
}

struct dpaa_capwap_domain_fqs *get_domain_fqs(void)
{
	int ret;

	if (!fqs) {
		ret = capwap_alloc_fqs();
		if (ret)
			return NULL;
	}
	return fqs;
}

int capwap_fq_rx_init(struct qman_fq *fq, u32 fqid,
			 u16 channel, qman_cb_dqrr cb)
{
	struct qm_mcc_initfq opts;
	int ret;

	fq->cb.dqrr = cb;
	ret = qman_create_fq(fqid, QMAN_FQ_FLAG_NO_ENQUEUE, fq);
	if (ret) {
		pr_err("qman_create_fq() failed\n");
		return ret;
	}

	memset(&opts, 0, sizeof(opts));
	opts.we_mask = QM_INITFQ_WE_DESTWQ | QM_INITFQ_WE_FQCTRL |
			QM_INITFQ_WE_CONTEXTA;
	opts.fqd.dest.channel = channel;
	opts.fqd.dest.wq = 3;
	/* FIXME: why would we want to keep an empty FQ in cache? */
	opts.fqd.fq_ctrl = QM_FQCTRL_PREFERINCACHE;
	opts.fqd.fq_ctrl |= QM_FQCTRL_CTXASTASHING | QM_FQCTRL_AVOIDBLOCK;
	opts.fqd.context_a.stashing.exclusive =
		QM_STASHING_EXCL_DATA | QM_STASHING_EXCL_CTX |
		QM_STASHING_EXCL_ANNOTATION;
	opts.fqd.context_a.stashing.data_cl = 2;
	opts.fqd.context_a.stashing.annotation_cl = 1;
	opts.fqd.context_a.stashing.context_cl =
		DIV_ROUND_UP(sizeof(struct qman_fq), 64);

	ret = qman_init_fq(fq, QMAN_INITFQ_FLAG_SCHED, &opts);
	if (ret < 0) {
		pr_err("qman_init_fq(%u) = %d\n",
				qman_fq_fqid(fq), ret);
		qman_destroy_fq(fq, 0);
		return ret;
	}

	return 0;
}

int capwap_fq_tx_init(struct qman_fq *fq, u16 channel,
			u64 context_a, u32 context_b, u8 wq)
{
	struct qm_mcc_initfq opts;
	int ret;
	uint32_t flags = QMAN_FQ_FLAG_TO_DCPORTAL;

	if (!fq->fqid)
		flags |= QMAN_FQ_FLAG_DYNAMIC_FQID;
	ret = qman_create_fq(fq->fqid, flags, fq);
	if (ret) {
		pr_err("qman_create_fq() failed\n");
		return ret;
	}

	memset(&opts, 0, sizeof(opts));
	opts.we_mask = QM_INITFQ_WE_DESTWQ | QM_INITFQ_WE_FQCTRL;
	if (context_a)
		opts.we_mask |= QM_INITFQ_WE_CONTEXTA;
	if (context_b)
		opts.we_mask |= QM_INITFQ_WE_CONTEXTB;
	opts.fqd.dest.channel = channel;
	opts.fqd.dest.wq = wq;

	opts.fqd.context_b = context_b;
	qm_fqd_context_a_set64(&opts.fqd, context_a);
	ret = qman_init_fq(fq, QMAN_INITFQ_FLAG_SCHED, &opts);
	if (ret < 0) {
		pr_err("qman_init_fq(%u) = %d\n",
				qman_fq_fqid(fq), ret);
		qman_destroy_fq(fq, 0);
		return ret;
	}

	return 0;
}

void teardown_fq(struct qman_fq *fq)
{
	u32 flags;
	int s = qman_retire_fq(fq, &flags);
	if (s == 1) {
		/* Retire is non-blocking, poll for completion */
		enum qman_fq_state state;
		do {
			qman_poll();
			qman_fq_state(fq, &state, &flags);
		} while (state != qman_fq_state_retired);
		if (flags & QMAN_FQ_STATE_NE) {
			/* FQ isn't empty, drain it */
			s = qman_volatile_dequeue(fq, 0,
				QM_VDQCR_NUMFRAMES_TILLEMPTY);
			BUG_ON(s);
			/* Poll for completion */
			do {
				qman_poll();
				qman_fq_state(fq, &state, &flags);
			} while (flags & QMAN_FQ_STATE_VDQCR);
		}
	}
	s = qman_oos_fq(fq);
	BUG_ON(s);
	qman_destroy_fq(fq, 0);
}

static void dump_hex(uint8_t *data, uint32_t count)
{
	uint32_t i;

	for (i = 0; i < count; i++) {
		if (!(i%16))
			pr_info("\n%04x  ", i);
		else if (!(i%8))
			pr_info(" ");
		pr_info("%02x ", *data++);
	}
	pr_info("\n");
}

void dump_fd(const struct qm_fd *fd)
{
	u64 addr;
	struct qm_sg_entry *sg_entry;
	uint32_t len;
	uint32_t final = 0;
	uint8_t *data;

	addr = qm_fd_addr_get64(fd);
	pr_info("fd_status = 0x%08x\n", fd->status);
	pr_info("fd_opaque= 0x%08x\n", fd->opaque);
	pr_info("format is 0x%x\n", fd->format);
	pr_info("bpid = %d\n", fd->bpid);
	pr_info("addr=0x%llx, vaddr=0x%p\n", addr, phys_to_virt(fd->addr));

	if (fd->format == qm_fd_sg) {/*short sg */
		addr = qm_fd_addr(fd);
		len = fd->length20;
		pr_info("FD: addr = 0x%llx\n", addr);
		pr_info("    offset=%d\n", fd->offset);
		pr_info("    len  = %d\n", len);
		data = phys_to_virt(fd->addr);
		data += fd->offset;
		sg_entry = (struct qm_sg_entry *) data;
		do {
			addr = qm_sg_addr(sg_entry);
			len = sg_entry->length;
			final = sg_entry->final;
			pr_info("SG ENTRY: addr = 0x%llx\n", addr);
			pr_info("          len  = %d\n", len);
			pr_info("          bpid = %d\n", sg_entry->bpid);
			pr_info("          extension = %d\n",
					sg_entry->extension);
			data = phys_to_virt(addr);
			pr_info("          v-addr=%p\n", data);
			data += sg_entry->offset;
			dump_hex(data, len);
			if (final)
				break;
			sg_entry++;
		} while (1);
	} else if (fd->format == qm_fd_contig) { /* short single */
		addr = qm_fd_addr(fd);
		len = fd->length20;
		pr_info("FD: addr = 0x%llx\n", addr);
		pr_info("    offset=%d\n", fd->offset);
		pr_info("    len  = %d\n", len);
		data = phys_to_virt(addr);
		pr_info("    v-addr=%p\n", data);
		dump_hex(data, len + fd->offset);
	}

}

static enum qman_cb_dqrr_result
rx_def_dqrr(struct qman_portal                *portal,
		     struct qman_fq                    *fq,
		     const struct qm_dqrr_entry        *dq)
{
	struct net_device               *net_dev;
	struct dpa_priv_s               *priv;
	struct dpa_bp			*dpa_bp;
	const struct qm_fd		*fd = &dq->fd;

	pr_info("rx default dqrr\n");
	net_dev = ((struct dpa_fq *)fq)->net_dev;
	priv = netdev_priv(net_dev);

	pr_info("op_rx_def_dqrr:fqid=0x%x, bpid = %d\n", fq->fqid, fd->bpid);
	dpa_bp = dpa_bpid2pool(fd->bpid);
	BUG_ON(!dpa_bp);


	if (netif_msg_hw(priv) && net_ratelimit())
		netdev_warn(net_dev, "FD status = 0x%08x\n",
				fd->status & FM_FD_STAT_TX_ERRORS);

	dump_fd(fd);

	dpa_fd_release(net_dev, fd);

	return qman_cb_dqrr_consume;
}

/* Initialize some fqs have no relation with detailed tunnel params */
int capwap_fq_pre_init(struct dpaa_capwap_domain *capwap_domain)
{
	int ret;
	struct dpa_fq *d_fq;
	struct qman_fq *q_fq;
	uint32_t fqid;
	int i, j;
	struct dpa_priv_s *net_priv;
	u64 context_a;
	u32 context_b;
	uint16_t channel;
	u32 debug_fqid;

	net_priv = netdev_priv(capwap_domain->net_dev);

/* Debug FQ initilization */
	debug_fqid = capwap_domain->fqs->debug_fqid;
	d_fq = kzalloc(sizeof(struct dpa_fq), GFP_KERNEL);
	if (d_fq == NULL)
		return -ENOMEM;
	d_fq->net_dev = capwap_domain->net_dev;
	ret = capwap_fq_rx_init(&d_fq->fq_base, debug_fqid, net_priv->channel,
			rx_def_dqrr);
	if (ret) {
		pr_err("init debug fq failed\n");
		kfree(d_fq);
		return ret;
	}


/* Inbound fqs pre-initilization */
	/* Four FQs to Core */
	d_fq = kzalloc(sizeof(struct dpa_fq) * CAPWAP_FLOW_COUNT, GFP_KERNEL);
	if (d_fq == NULL)
		return -ENOMEM;
	capwap_domain->fqs->inbound_core_rx_fqs.fq_base = d_fq;
	for (i = 0; i < CAPWAP_FLOW_COUNT; i++) {
		d_fq[i].net_dev = capwap_domain->net_dev;

		fqid = capwap_domain->fqs->inbound_core_rx_fqs.fqid_base + i;
		ret = capwap_fq_rx_init(&d_fq[i].fq_base, fqid,
				net_priv->channel, rx_cbs[i]);
		if (ret) {
			for (j = 0; j < i; j++)
				qman_destroy_fq(&d_fq[j].fq_base, 0);
			kfree(d_fq);
			return ret;
		}
	}

	/* Two Fqs from SEC to OP */
	q_fq = kzalloc(sizeof(struct qman_fq) * CAPWAP_FLOW_COUNT / 2,
			GFP_KERNEL);
	if (!q_fq)
		return -ENOMEM;
	capwap_domain->fqs->inbound_sec_to_op_fqs.fq_base = q_fq;
	for (i = 0; i < CAPWAP_FLOW_COUNT / 2; i++) {
		q_fq[i].fqid =
			capwap_domain->fqs->inbound_sec_to_op_fqs.fqid_base + i;
		channel = capwap_domain->post_dec_op_port.tx_ch;
		context_a = (u64)1 << 63;
		/* a1v */
		context_a |= (u64)1 << 61;
		/* flowid for a1 */
		context_a |= (u64)i << (32 + 4);
		/* SpOperCode for DTLS Decap */
		context_a |= (u64)9 << 32;
		context_b =
			capwap_domain->fqs->inbound_core_rx_fqs.fqid_base + i;
		ret = capwap_fq_tx_init(&q_fq[i], channel, context_a,
				context_b, 3);
		if (ret) {
			for (j = 0; j < i; j++)
				qman_destroy_fq(&q_fq[j], 0);
			kfree(q_fq);
			return ret;
		}
	}

	/* Four Fqs from Rx port */
	q_fq = kzalloc(sizeof(struct qman_fq) * CAPWAP_FLOW_COUNT, GFP_KERNEL);
	if (!q_fq)
		return -ENOMEM;
	capwap_domain->fqs->inbound_eth_rx_fqs.fq_base = q_fq;
	/* Just initizlize two fqs for NON-DTLS flows from rx port to OP,
	 * the other fqs for DTLS flows from rx port to SEC is dynamicly
	 * initialized
	 */
	for (i = CAPWAP_FLOW_COUNT / 2; i < CAPWAP_FLOW_COUNT; i++) {
		q_fq[i].fqid =
			capwap_domain->fqs->inbound_eth_rx_fqs.fqid_base + i;
		channel = capwap_domain->post_dec_op_port.tx_ch;
		context_a = (u64)1 << 63;
		/* a1v */
		context_a |= (u64)1 << 61;
		/* flowid for a1 */
		context_a |= (u64)i << (32 + 4);
		context_b =
			capwap_domain->fqs->inbound_core_rx_fqs.fqid_base + i;
		ret = capwap_fq_tx_init(&q_fq[i], channel, context_a,
				context_b, 3);
		if (ret) {
			for (j = 0; j < i; j++)
				qman_destroy_fq(&q_fq[j], 0);
			kfree(q_fq);
			return ret;
		}
	}

/* Outbound fqs pre-initilization */
	/* Eight Fqs from OP tx */
	q_fq = kzalloc(sizeof(struct qman_fq) * CAPWAP_FLOW_COUNT * 2,
			GFP_KERNEL);
	if (!q_fq)
		return -ENOMEM;
	capwap_domain->fqs->outbound_op_tx_fqs.fq_base = q_fq;

	/* The upper four flows are for sending to Tx port after header
	 * manipulation
	 */
	for (i = CAPWAP_FLOW_COUNT; i < CAPWAP_FLOW_COUNT * 2; i++) {
		q_fq[i].fqid =
			capwap_domain->fqs->outbound_op_tx_fqs.fqid_base + i;
		channel = (uint16_t)
			fm_get_tx_port_channel(net_priv->mac_dev->port_dev[TX]);
		context_a = (u64)1 << 63;
#if (defined CONFIG_FMAN_V3H) || (defined CONFIG_FMAN_V3L)
		/* Configure the Tx queues for recycled frames, such that the
		 * buffers are released by FMan and no confirmation is sent
		 */
#define FMAN_V3_CONTEXTA_EN_A2V	0x10000000
#define FMAN_V3_CONTEXTA_EN_OVOM 0x02000000
#define FMAN_V3_CONTEXTA_EN_EBD	0x80000000
		context_a |= (((uint64_t) FMAN_V3_CONTEXTA_EN_A2V |
					FMAN_V3_CONTEXTA_EN_OVOM) << 32) |
					FMAN_V3_CONTEXTA_EN_EBD;
#endif
		context_b = 0;
		ret = capwap_fq_tx_init(&q_fq[i], channel, context_a,
				context_b, 3);
		if (ret) {
			for (j = CAPWAP_FLOW_COUNT; j < i; j++)
				qman_destroy_fq(&q_fq[j], 0);
			kfree(q_fq);
			return ret;
		}
	}
	/* The lower four flows:
	 * 1 & 2 are for DTLS Tunnel, and are dynamically initilized when
	 * insert tunnel;
	 * 3 & 4 are for NON-DTLS Tunnel and sent back to OP, and are
	 * initilized here
	 */
	for (i = CAPWAP_FLOW_COUNT / 2; i < CAPWAP_FLOW_COUNT; i++) {
		q_fq[i].fqid =
			capwap_domain->fqs->outbound_op_tx_fqs.fqid_base + i;
		channel = capwap_domain->out_op_port.tx_ch;
		context_a = (u64)1 << 63;
		/* a1v */
		context_a |= (u64)1 << 61;
		/* flowid for a1, Upper flow for OP */
		context_a |= (u64)(i + CAPWAP_FLOW_COUNT) << (32 + 4);
		context_b = capwap_domain->fqs->outbound_op_tx_fqs.fqid_base +
			i + CAPWAP_FLOW_COUNT;
		ret = capwap_fq_tx_init(&q_fq[i], channel, context_a,
				context_b, 1);
		if (ret) {
			for (j = CAPWAP_FLOW_COUNT / 2; j < i; j++)
				qman_destroy_fq(&q_fq[j], 0);
			for (j = CAPWAP_FLOW_COUNT;
					j < CAPWAP_FLOW_COUNT * 2; j++)
				qman_destroy_fq(&q_fq[j], 0);
			kfree(q_fq);
			return ret;
		}
	}

	/* Two Fqs from SEC to OP */
	q_fq = kzalloc(sizeof(struct qman_fq) * CAPWAP_FLOW_COUNT / 2,
			GFP_KERNEL);
	if (!q_fq)
		return -ENOMEM;
	capwap_domain->fqs->outbound_sec_to_op_fqs.fq_base = q_fq;
	for (i = 0; i < CAPWAP_FLOW_COUNT / 2; i++) {
		q_fq[i].fqid = capwap_domain->fqs->
			outbound_sec_to_op_fqs.fqid_base + i;
		channel = capwap_domain->out_op_port.tx_ch;
		context_a = (u64)1 << 63;
		/* a1v */
		context_a |= (u64)1 << 61;
		/* flowid for a1, Upper flow for OP */
		context_a |= (u64)(i + CAPWAP_FLOW_COUNT) << (32 + 4);
		/* SpOperCode for DTLS Encap */
		context_a |= (u64) 10 << 32;
		context_b = capwap_domain->fqs->outbound_op_tx_fqs.fqid_base +
			i + CAPWAP_FLOW_COUNT;
		ret = capwap_fq_tx_init(&q_fq[i], channel, context_a,
				context_b, 1);
		if (ret) {
			for (j = 0; j < i; j++)
				qman_destroy_fq(&q_fq[j], 0);
			kfree(q_fq);
			return ret;
		}
	}

	/* Four Fqs from Core to OP port */
	q_fq = kzalloc(sizeof(struct qman_fq) * CAPWAP_FLOW_COUNT, GFP_KERNEL);
	if (!q_fq)
		return -ENOMEM;
	capwap_domain->fqs->outbound_core_tx_fqs.fq_base = q_fq;
	for (i = 0; i < CAPWAP_FLOW_COUNT; i++) {
		q_fq[i].fqid = capwap_domain->fqs->
			outbound_core_tx_fqs.fqid_base + i;
		channel = capwap_domain->out_op_port.tx_ch;
		context_a = (u64)1 << 63;
		/* a1v */
		context_a |= (u64)1 << 61;
		/* flowid for a1, Lower flow for OP*/
		context_a |= (u64)i << (32 + 4);
		context_b = 0;
		ret = capwap_fq_tx_init(&q_fq[i], channel, context_a,
				context_b, 3);
		if (ret) {
			for (j = 0; j < i; j++)
				qman_destroy_fq(&q_fq[j], 0);
			kfree(q_fq);
			return ret;
		}
	}

	return 0;
}

int capwap_kernel_rx_ctl(struct capwap_domain_kernel_rx_ctl *rx_ctl)
{
	u32 fqid;
	uint16_t flow_index;
	struct dpa_fq *fq_base, *d_fq;
	struct dpa_priv_s *net_priv;
	struct dpaa_capwap_domain *capwap_domain =
	       (struct dpaa_capwap_domain *)rx_ctl->capwap_domain_id;
	int ret = 0;

	if (capwap_domain == NULL)
		return -EINVAL;

	net_priv = netdev_priv(capwap_domain->net_dev);
	flow_index = get_flow_index(rx_ctl->is_dtls, rx_ctl->is_control);

	fqid = capwap_domain->fqs->inbound_core_rx_fqs.fqid_base + flow_index;

	fq_base = (struct dpa_fq *)capwap_domain->fqs->
		inbound_core_rx_fqs.fq_base;
	d_fq = &fq_base[flow_index];

	if (rx_ctl->on) {
		if (d_fq->fq_base.fqid == fqid) {
			pr_err("CAPWAP %s-%s tunnel kernel Rx is already on\n",
					rx_ctl->is_control ? "control" : "data",
					rx_ctl->is_dtls ? "dtls" : "non-dtls");
			return -EINVAL;
		}

		d_fq->net_dev = capwap_domain->net_dev;
		ret = capwap_fq_rx_init(&d_fq->fq_base, fqid, net_priv->channel,
				rx_cbs[flow_index]);
		if (ret) {
			memset(d_fq, 0, sizeof(struct dpa_fq));
			return ret;
		}
	} else {
		if (!d_fq->fq_base.fqid) {
			pr_err("CAPWAP %s-%s tunnel kernel Rx is already off\n",
					rx_ctl->is_control ? "control" : "data",
					rx_ctl->is_dtls ? "dtls" : "non-dtls");
			return -EINVAL;
		}
		teardown_fq(&d_fq->fq_base);
		memset(d_fq, 0, sizeof(struct dpa_fq));
	}
	rx_ctl->fqid = fqid;

	return 0;
}
