/* Copyright 2013-2014 Freescale Semiconductor Inc.
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
#include <linux/fsl_bman.h>
#include <linux/poll.h>

#include "dpaa_eth_common.h"
#include "dpaa_capwap.h"
#include "dpaa_capwap_domain.h"

#define MAX_LIST_LENGTH		2000


struct capwap_dtls_list {
	struct qm_fd *fd;
	struct list_head list;
};

struct capwap_tunnel_ctx {
	bool fd_open; /* Set to true once the fd is opened */
	u32 last_irq_count; /* Last value returned from read */
	u32 irq_count; /* Number of irqs since last read */
	wait_queue_head_t wait_queue; /* Waiting processes */
	spinlock_t lock;
	struct list_head dtls_head;
	struct list_head dtls_tail;
	struct qman_fq *tx_fq;
};

static struct capwap_tunnel_ctx control_dtls_ctx;
static struct capwap_tunnel_ctx control_n_dtls_ctx;
static struct capwap_tunnel_ctx data_dtls_ctx;
static struct capwap_tunnel_ctx data_n_dtls_ctx;
static struct dpaa_capwap_domain *capwap_domain;

static int process_fd(struct capwap_tunnel_ctx *ctx, const struct qm_fd *fd,
		struct net_device *net_dev)
{
	struct qm_fd *fd_cp;
	u32 count;
	struct capwap_dtls_list *new_node, *first_node;
	unsigned long flags;


	spin_lock_irqsave(&ctx->lock, flags);

	if (ctx->irq_count > ctx->last_irq_count)
		count = ctx->irq_count - ctx->last_irq_count;
	else
		count = ctx->last_irq_count - ctx->irq_count;

	if (count > MAX_LIST_LENGTH) {
		first_node = container_of((&ctx->dtls_head)->next,
				struct capwap_dtls_list, list);
		dpa_fd_release(net_dev, first_node->fd);
		kfree(first_node->fd);
		kfree(first_node);
		list_del((&ctx->dtls_head)->next);
	}

	new_node = kmalloc(sizeof(struct capwap_dtls_list), GFP_KERNEL);
	if (!new_node) {
		spin_unlock_irqrestore(&ctx->lock, flags);
		return -1;
	}
	fd_cp = kmalloc(sizeof(struct qm_fd), GFP_KERNEL);
	if (!fd_cp) {
		kfree(new_node);
		spin_unlock_irqrestore(&ctx->lock, flags);
		return -1;
	}
	memcpy(fd_cp, fd, sizeof(struct qm_fd));

	new_node->fd = fd_cp;
	list_add_tail(&new_node->list, &ctx->dtls_tail);
	ctx->irq_count++;
	spin_unlock_irqrestore(&ctx->lock, flags);
	wake_up_all(&ctx->wait_queue);
	return 0;
}

enum qman_cb_dqrr_result __hot
capwap_control_dtls_rx_dqrr(struct qman_portal		*portal,
			struct qman_fq			*fq,
			const struct qm_dqrr_entry	*dq)
{
	struct net_device		*net_dev;
	const struct qm_fd *fd = &dq->fd;
	struct capwap_tunnel_ctx *ctx;

	net_dev = ((struct dpa_fq *)fq)->net_dev;

	ctx = &control_dtls_ctx;

	if (!ctx || !ctx->fd_open)
		goto out;
	if (!process_fd(ctx, fd, net_dev))
		return qman_cb_dqrr_consume;
out:
	dpa_fd_release(net_dev, fd);

	return qman_cb_dqrr_consume;
}

enum qman_cb_dqrr_result __hot
capwap_control_n_dtls_rx_dqrr(struct qman_portal		*portal,
			struct qman_fq			*fq,
			const struct qm_dqrr_entry	*dq)
{
	struct net_device		*net_dev;
	const struct qm_fd *fd = &dq->fd;
	struct capwap_tunnel_ctx *ctx;

	net_dev = ((struct dpa_fq *)fq)->net_dev;

	ctx = &control_n_dtls_ctx;

	if (!ctx || !ctx->fd_open)
		goto out;
	if (!process_fd(ctx, fd, net_dev))
		return qman_cb_dqrr_consume;
out:
	dpa_fd_release(net_dev, fd);

	return qman_cb_dqrr_consume;
}

int upload_data_packets(u32 fqid, const struct qm_fd *fd,
		struct net_device *net_dev)
{
	u32 fqid_base;
	struct capwap_tunnel_ctx *ctx;

	fqid_base = capwap_domain->fqs->inbound_core_rx_fqs.fqid_base;
	if (fqid == (fqid_base + 1))
		ctx = &data_dtls_ctx;
	else
		ctx = &data_n_dtls_ctx;

	if (!ctx || !ctx->fd_open)
		goto out;
	if (!process_fd(ctx, fd, net_dev))
		return 0;
out:
	return -1;
}

static void init_tunnel_ctx(struct capwap_tunnel_ctx *ctx, int tunnel_id,
		struct file *filp)
{
	struct qman_fq *fq;

	ctx->irq_count = 0;
	ctx->last_irq_count = 0;
	INIT_LIST_HEAD(&ctx->dtls_head);
	list_add(&ctx->dtls_tail, &ctx->dtls_head);
	init_waitqueue_head(&ctx->wait_queue);
	spin_lock_init(&ctx->lock);
	fq = (struct qman_fq *)capwap_domain->fqs->outbound_core_tx_fqs.fq_base;
	ctx->tx_fq = &fq[tunnel_id];
	filp->private_data = ctx;
	ctx->fd_open = true;
}

static int capwap_control_dtls_open(struct inode *inode, struct file *filp)
{
	init_tunnel_ctx(&control_dtls_ctx, 0, filp);
	return 0;
}

static int capwap_control_n_dtls_open(struct inode *inode, struct file *filp)
{
	init_tunnel_ctx(&control_n_dtls_ctx, 2, filp);
	return 0;
}

static int capwap_data_dtls_open(struct inode *inode, struct file *filp)
{
	init_tunnel_ctx(&data_dtls_ctx, 1, filp);
	return 0;
}

static int capwap_data_n_dtls_open(struct inode *inode, struct file *filp)
{
	init_tunnel_ctx(&data_n_dtls_ctx, 3, filp);
	return 0;
}

static int capwap_tunnel_release(struct inode *inode, struct file *filp)
{
	struct capwap_tunnel_ctx *ctx = filp->private_data;
	struct capwap_dtls_list *capwap_node;
	unsigned long flags;

	ctx->fd_open = false;

	spin_lock_irqsave(&ctx->lock, flags);
	list_del(&ctx->dtls_tail);
	list_for_each_entry(capwap_node, &ctx->dtls_head, list) {
		dpa_fd_release(capwap_domain->net_dev, capwap_node->fd);
		kfree(capwap_node->fd);
		kfree(capwap_node);
	}
	INIT_LIST_HEAD(&ctx->dtls_head);
	list_add(&ctx->dtls_tail, &ctx->dtls_head);
	spin_unlock_irqrestore(&ctx->lock, flags);
	return 0;
}

static ssize_t capwap_tunnel_read(struct file *filp, char __user *buff,
			       size_t count, loff_t *offp)
{
	struct capwap_tunnel_ctx *ctx = filp->private_data;
	struct qm_fd *fd;
	dma_addr_t addr;
	struct qm_sg_entry *sg_entry;
	uint32_t len;
	uint32_t final = 0;
	void *data;
	struct capwap_dtls_list *first_node;
	unsigned long flags;
	struct dpa_bp			*dpa_bp;
	struct dpa_priv_s		*priv;

	priv = netdev_priv(capwap_domain->net_dev);
	dpa_bp = priv->dpa_bp;

	spin_lock_irqsave(&ctx->lock, flags);

	if (ctx->dtls_head.next == &ctx->dtls_tail) {
		spin_unlock_irqrestore(&ctx->lock, flags);
		return 0;
	}

	first_node = container_of((&ctx->dtls_head)->next,
			struct capwap_dtls_list, list);

	fd = first_node->fd;

	if (fd->format == qm_fd_sg) {/*short sg */
		addr = qm_fd_addr(fd);
		len = fd->length20;
		data = phys_to_virt(addr);
		data += fd->offset;
		sg_entry = (struct qm_sg_entry *) data;
		do {
			addr = qm_sg_addr(sg_entry);
			len = sg_entry->length;
			final = sg_entry->final;
			data = phys_to_virt(addr);
			data += sg_entry->offset;
			if (copy_to_user(buff, data, len)) {
				spin_unlock_irqrestore(&ctx->lock, flags);
				return -EFAULT;
			}
			if (final)
				break;
			buff += len;
			sg_entry++;
		} while (1);
	} else if (fd->format == qm_fd_contig) { /* short single */
		addr = qm_fd_addr(fd);
		len = fd->length20;
		data = phys_to_virt(addr);
		data += fd->offset;
		if (copy_to_user(buff, data, len)) {
			spin_unlock_irqrestore(&ctx->lock, flags);
			return -EFAULT;
		}
	}

	len = fd->length20;
	ctx->last_irq_count = ctx->irq_count;

	dpa_fd_release(capwap_domain->net_dev, fd);

	list_del((&ctx->dtls_head)->next);
	kfree(first_node->fd);
	kfree(first_node);
	spin_unlock_irqrestore(&ctx->lock, flags);

	return len;

}

static ssize_t capwap_tunnel_write(struct file *filp,
		const char __user *buf, size_t count, loff_t *off)
{
	struct capwap_tunnel_ctx *ctx = filp->private_data;
	struct dpa_percpu_priv_s *percpu_priv;
	struct dpa_priv_s *priv;
	struct dpa_bp *dpa_bp;
	struct qm_fd fd;
	dma_addr_t addr;
	void *data;
	int err;
	struct bm_buffer bmb;
	int i;

	priv = netdev_priv(capwap_domain->net_dev);
	percpu_priv = __this_cpu_ptr(priv->percpu_priv);

	dpa_bp = priv->dpa_bp;

	err = bman_acquire(dpa_bp->pool, &bmb, 1, 0);
	if (unlikely(err <= 0)) {
		percpu_priv->stats.tx_errors++;
		if (err == 0)
			err = -ENOMEM;
		return -EFAULT;
	}

	memset(&fd, 0, sizeof(fd));
	fd.bpid = dpa_bp->bpid;

	fd.length20 = count;
	fd.addr_hi = bmb.hi;
	fd.addr_lo = bmb.lo;
	fd.offset = priv->tx_headroom + 64;
	fd.format = qm_fd_contig;

	addr = qm_fd_addr(&fd);
	data = phys_to_virt(addr + dpa_fd_offset(&fd));
	if (count > dpa_bp->size)
		return -EINVAL;
	if (copy_from_user(data, buf, count))
		return -EFAULT;

	for (i = 0; i < 100000; i++) {
		err = qman_enqueue(ctx->tx_fq, &fd, 0);
		if (err != -EBUSY)
			break;
	}
	if (unlikely(err < 0)) {
		pr_warn("capwap_tunnel:transmit to dpaa error\n");
		return -EFAULT;
	}

	return 0;
}

static unsigned int capwap_tunnel_poll(struct file *filp, poll_table *wait)
{
	struct capwap_tunnel_ctx *ctx = filp->private_data;
	unsigned int ret = 0;

	poll_wait(filp, &ctx->wait_queue, wait);

	if (ctx->irq_count != ctx->last_irq_count)
		ret |= POLLIN | POLLRDNORM;
	return ret;
}

static const struct file_operations capwap_control_dtls_fops = {
	.open		   = capwap_control_dtls_open,
	.release	   = capwap_tunnel_release,
	.read              = capwap_tunnel_read,
	.write             = capwap_tunnel_write,
	.poll              = capwap_tunnel_poll
};

static struct miscdevice capwap_ctrl_dtls_miscdev = {
	.name = "fsl-capwap-ctrl-dtls",
	.fops = &capwap_control_dtls_fops,
	.minor = MISC_DYNAMIC_MINOR,
};

static const struct file_operations capwap_control_n_dtls_fops = {
	.open		   = capwap_control_n_dtls_open,
	.release	   = capwap_tunnel_release,
	.read              = capwap_tunnel_read,
	.write             = capwap_tunnel_write,
	.poll              = capwap_tunnel_poll
};

static struct miscdevice capwap_ctrl_n_dtls_miscdev = {
	.name = "fsl-capwap-ctrl-n-dtls",
	.fops = &capwap_control_n_dtls_fops,
	.minor = MISC_DYNAMIC_MINOR,
};

static const struct file_operations capwap_data_dtls_fops = {
	.open		   = capwap_data_dtls_open,
	.release	   = capwap_tunnel_release,
	.read              = capwap_tunnel_read,
	.write             = capwap_tunnel_write,
	.poll              = capwap_tunnel_poll
};

static struct miscdevice capwap_data_dtls_miscdev = {
	.name = "fsl-capwap-data-dtls",
	.fops = &capwap_data_dtls_fops,
	.minor = MISC_DYNAMIC_MINOR,
};

static const struct file_operations capwap_data_n_dtls_fops = {
	.open		   = capwap_data_n_dtls_open,
	.release	   = capwap_tunnel_release,
	.read              = capwap_tunnel_read,
	.write             = capwap_tunnel_write,
	.poll              = capwap_tunnel_poll
};

static struct miscdevice capwap_data_n_dtls_miscdev = {
	.name = "fsl-capwap-data-n-dtls",
	.fops = &capwap_data_n_dtls_fops,
	.minor = MISC_DYNAMIC_MINOR,
};

int capwap_tunnel_drv_init(struct dpaa_capwap_domain *domain)
{
	int ret;

	memset(&control_dtls_ctx, 0, sizeof(struct capwap_tunnel_ctx));
	memset(&control_n_dtls_ctx, 0, sizeof(struct capwap_tunnel_ctx));
	memset(&data_dtls_ctx, 0, sizeof(struct capwap_tunnel_ctx));
	memset(&data_n_dtls_ctx, 0, sizeof(struct capwap_tunnel_ctx));
	capwap_domain = domain;

	pr_info("Freescale CAPWAP Control Packet Tunnel Interface driver\n");
	ret = misc_register(&capwap_ctrl_dtls_miscdev);
	if (ret)
		pr_err("fsl-capwap-control: failed to register misc device\n");
	ret = misc_register(&capwap_ctrl_n_dtls_miscdev);
	if (ret)
		pr_err("fsl-capwap-n-control: failed to register misc device\n");
	ret = misc_register(&capwap_data_dtls_miscdev);
	if (ret)
		pr_err("fsl-capwap-control: failed to register misc device\n");
	ret = misc_register(&capwap_data_n_dtls_miscdev);
	if (ret)
		pr_err("fsl-capwap-n-control: failed to register misc device\n");
	return ret;
}

void capwap_tunnel_drv_exit(void)
{
	capwap_domain = NULL;
	misc_deregister(&capwap_ctrl_dtls_miscdev);
	misc_deregister(&capwap_ctrl_n_dtls_miscdev);
	misc_deregister(&capwap_data_dtls_miscdev);
	misc_deregister(&capwap_data_n_dtls_miscdev);
}
