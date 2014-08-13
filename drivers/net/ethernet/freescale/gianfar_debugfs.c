/* Copyright 2013 Freescale Semiconductor Inc.
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

#include <linux/debugfs.h>

#include "gianfar.h"

static struct dentry *gfar_dbg_root;
LIST_HEAD(gfar_dbg_ndevs);

static ssize_t gfar_dbg_ndev_loopbk_read(struct file *f,
					 char __user *buf,
					 size_t count, loff_t *ppos)
{
	struct gfar_private *priv = f->private_data;
	const char *name = NULL;
	const char *b;
	int len;

	if (*ppos != 0)
		return 0;

	if (priv->dbg_ndev_loopbk_tgt)
		name = priv->dbg_ndev_loopbk_tgt->name;

	b = kasprintf(GFP_KERNEL, "%s\n", name ? name : "off");

	if (!b)
		return -ENOMEM;

	if (count < strlen(b)) {
		kfree(b);
		return -ENOSPC;
	}

	len = simple_read_from_buffer(buf, count, ppos, b, strlen(b));

	kfree(b);

	return len;
}

static ssize_t gfar_dbg_ndev_loopbk_write(struct file *f,
					  const char __user *buf,
					  size_t count, loff_t *ppos)
{
	struct gfar_private *priv = f->private_data;
	struct gfar_private *gfar_dbg_priv = NULL;
	int found = 0;

	if (*ppos != 0)
		return 0;

	if (!strncmp("off", buf, count - 1) || !strncmp("0", buf, count - 1))
		goto update;

	list_for_each_entry(gfar_dbg_priv, &gfar_dbg_ndevs, dbg_ndev_node) {
		const char *name = gfar_dbg_priv->ndev->name;
		if ((strlen(name) == count - 1) &&
		    !strncmp(buf, name, count - 1)) {
			found = 1;
			break;
		}
	}

	if (!found) {
		pr_err("Invalid loopback target device name!\n");
		return count;
	}

update:
	priv->dbg_ndev_loopbk_tgt = NULL;
	if (gfar_dbg_priv)
		priv->dbg_ndev_loopbk_tgt = gfar_dbg_priv->ndev;

	return count;
}

static const struct file_operations gfar_dbg_ndev_loopbk_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = gfar_dbg_ndev_loopbk_read,
	.write = gfar_dbg_ndev_loopbk_write
};

void gfar_dbg_ndev_init(struct gfar_private *priv)
{
	const char *name = priv->ndev->name;
	struct dentry *f;

	priv->dbg_ndev_dir = debugfs_create_dir(name, gfar_dbg_root);
	if (!priv->dbg_ndev_dir) {
		pr_err("debugfs init failed for %s\n", name);
		return;
	}

	f = debugfs_create_file("lo", 0600, priv->dbg_ndev_dir,
				priv, &gfar_dbg_ndev_loopbk_fops);
	if (!f) {
		pr_err("debugfs failed to init loopback for %s\n", name);
		return;
	}

	list_add(&priv->dbg_ndev_node, &gfar_dbg_ndevs);
}

void gfar_dbg_ndev_exit(struct gfar_private *priv)
{
	if (!priv->dbg_ndev_dir)
		return;

	list_del(&priv->dbg_ndev_node);

	debugfs_remove_recursive(priv->dbg_ndev_dir);
	priv->dbg_ndev_dir = NULL;
	priv->dbg_ndev_loopbk_tgt = NULL;
}

void gfar_dbg_init(void)
{
	gfar_dbg_root = debugfs_create_dir(KBUILD_MODNAME, NULL);
	if (!gfar_dbg_root)
		pr_err("debugfs init failed\n");
}

void gfar_dbg_exit(void)
{
	debugfs_remove_recursive(gfar_dbg_root);
}
