/* Copyright (c) 2014 Freescale Semiconductor, Inc.
 * All rights reserved.
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

#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/export.h>
#include <linux/slab.h>

#include "dpaa_capwap_ioctl.h"
#include "dpaa_capwap_domain.h"

#include <linux/fdtable.h>
#include "lnxwrp_fm.h"

#define DRV_VERSION "0.1"

static int dpa_capwap_cdev_major = -1;
static struct class  *capwap_class;
static struct device *capwap_dev;
static struct list_head tunnel_list_head;

struct tunnel_info {
	struct list_head tunnel_list;
	bool is_control;
	bool is_dtls;
	enum dpaa_capwap_domain_direction dir;
	void *tunnel_id;
};


struct t_device {
	uintptr_t   id;         /**< the device id */
	int         fd;         /**< the device file descriptor */
	t_Handle    h_UserPriv;
	uint32_t    owners;
};


int wrp_dpa_capwap_open(struct inode *inode, struct file *filp)
{
	return 0;
}


int wrp_dpa_capwap_release(struct inode *inode, struct file *filp)
{
	return 0;
}

long wrp_dpa_capwap_ioctl(struct file *filp, unsigned int cmd,
			 unsigned long args)
{
	long ret = 0;

	switch (cmd) {
	case DPA_CAPWAP_IOC_DOMAIN_GET_FQIDS: {
		struct dpaa_capwap_domain_fqs *fqs;

		fqs = get_domain_fqs();
		if (fqs == NULL)
			return -ENODEV;

		if (copy_to_user((void *)args, fqs,
				sizeof(struct dpaa_capwap_domain_fqs))) {
			pr_err("Could not copy DPA CAPWAP FQID base to user\n");
			return -EINVAL;
		}
		break;
	}
	case DPA_CAPWAP_IOC_DOMAIN_INIT: {
		struct dpaa_capwap_domain_params domain_params;
		struct dpaa_capwap_domain *capwap_domain = NULL;
		struct file *fm_pcd_file, *fm_port_file;
		t_LnxWrpFmDev *fm_wrapper_dev;
		t_LnxWrpFmPortDev *port_wrapper_dev;
		struct t_device *dev;

		/* Copy parameters from user-space */
		if (copy_from_user(&domain_params, (void *)args,
					sizeof(domain_params))) {
			pr_err("Could not copy DPA CAPWAP init parameters\n");
			return -EINVAL;
		}

		/* Translate FM_PCD file descriptor */
		fm_pcd_file = fcheck((unsigned long)domain_params.h_fm_pcd);
		if (!fm_pcd_file) {
			pr_err("Could not acquire PCD handle\n");
			return -EINVAL;
		}
		fm_wrapper_dev = ((t_LnxWrpFmDev *)fm_pcd_file->private_data);
		domain_params.h_fm_pcd = (void *)fm_wrapper_dev->h_PcdDev;

		/* Translate FM_Port file descriptor */
		fm_port_file = fcheck((unsigned long)
				domain_params.outbound_op.port_handle);
		if (!fm_port_file) {
			pr_err("Could not acquire FM Port handle\n");
			return -EINVAL;
		}
		port_wrapper_dev = ((t_LnxWrpFmPortDev *)
					fm_port_file->private_data);
		domain_params.outbound_op.port_handle = (void *)
						port_wrapper_dev->h_Dev;

		/* Translate CCNode handle */
		dev = domain_params.inbound_pre_params.h_Table;
		domain_params.inbound_pre_params.h_Table = (void *)dev->id;

		capwap_domain = (struct dpaa_capwap_domain *)
			dpaa_capwap_domain_config(&domain_params);
		if (!capwap_domain)
			return -EINVAL;
		ret = dpaa_capwap_domain_init(capwap_domain);
		if (ret < 0)
			return ret;

		domain_params.id = capwap_domain;
		if (copy_to_user((void *)args, &domain_params,
					sizeof(domain_params))) {
			pr_err("Could not copy DPA CAPWAP ID to user the ID\n");
			return -EINVAL;
		}

		break;
	}

	case DPA_CAPWAP_IOC_DOMAIN_ADD_IN_TUNNEL: {
		struct dpaa_capwap_tunnel  *in_tunnel = NULL;
		struct dpaa_capwap_domain *capwap_domain = NULL;
		struct dpaa_capwap_domain_tunnel_in_params in_tunnel_params;
		struct tunnel_info *tunnel_node, *new_tunnel;

		/* Copy parameters from user-space */
		if (copy_from_user(&in_tunnel_params, (void *)args,
					sizeof(in_tunnel_params))) {
			pr_err("Could not copy DPA CAPWAP Add-In-Tunnel parameters\n");
			return -EINVAL;
		}

		capwap_domain = (struct dpaa_capwap_domain *)
					in_tunnel_params.capwap_domain_id;
		if (!capwap_domain)
			return -EINVAL;

		list_for_each_entry(tunnel_node, &tunnel_list_head,
				tunnel_list) {
			if (tunnel_node->is_dtls == in_tunnel_params.dtls &&
					tunnel_node->is_control ==
					in_tunnel_params.is_control &&
					tunnel_node->dir ==
					e_DPAA_CAPWAP_DOMAIN_DIR_INBOUND) {
				pr_err("%s-%s inbound tunnel already exist, please remove it firstly\n",
						tunnel_node->is_dtls ? "DTLS" :
						"N-DTLS",
						tunnel_node->is_control ?
						"Control" : "Data");
				return -EINVAL;
			}
		}

		in_tunnel = dequeue_tunnel_obj(&capwap_domain->in_tunnel_list);
		if (!in_tunnel) {
			pr_err("You've reached the maximum number of inbound tunnels\n");
			return -EINVAL;
		}

		if (copy_from_user(in_tunnel->auth_data.auth_key,
				in_tunnel_params.dtls_params.auth_key,
				in_tunnel_params.dtls_params
				.auth_key_len / 8)) {
			pr_err("Could not copy auth key from user space\n");
			return -EINVAL;
		}
		if (copy_from_user(in_tunnel->cipher_data.cipher_key,
				in_tunnel_params.dtls_params.cipher_key,
				in_tunnel_params.dtls_params
				.cipher_key_len / 8)) {
			pr_err("Could not copy cipher key from user space\n");
			return -EINVAL;
		}

		ret = add_in_tunnel(capwap_domain, in_tunnel,
				&in_tunnel_params);
		if (ret < 0)
			return ret;
		in_tunnel_params.tunnel_id = in_tunnel;

		new_tunnel = kzalloc(sizeof(struct tunnel_info), GFP_KERNEL);
		new_tunnel->is_control = in_tunnel_params.is_control;
		new_tunnel->is_dtls = in_tunnel_params.dtls;
		new_tunnel->tunnel_id = in_tunnel_params.tunnel_id;
		new_tunnel->dir = e_DPAA_CAPWAP_DOMAIN_DIR_INBOUND;
		list_add_tail(&new_tunnel->tunnel_list, &tunnel_list_head);

		if (copy_to_user((void *)args, &in_tunnel_params,
					sizeof(in_tunnel_params))) {
			pr_err("Could not copy DPA CAPWAP ID to user the ID\n");
			return -EINVAL;
		}


		break;
	}

	case DPA_CAPWAP_IOC_DOMAIN_ADD_OUT_TUNNEL: {
		struct dpaa_capwap_tunnel  *out_tunnel = NULL;
		struct dpaa_capwap_domain *capwap_domain = NULL;
		struct dpaa_capwap_domain_tunnel_out_params out_tunnel_params;
		struct tunnel_info *tunnel_node, *new_tunnel = NULL;
		uint8_t *buf;

		/* Copy parameters from user-space */
		if (copy_from_user(&out_tunnel_params, (void *)args,
					sizeof(out_tunnel_params))) {
			pr_err("Could not copy DPA CAPWAP Add-Out-Tunnel parameters\n");
			return -EINVAL;
		}

		if (!out_tunnel_params.p_ether_header &&
				out_tunnel_params.eth_header_size != 0) {
			buf = kzalloc(out_tunnel_params.eth_header_size,
					GFP_KERNEL);
			if (buf == NULL) {
				pr_err("No memory for ether header in %s\n",
						__func__);
				return -ENOMEM;
			}
			if (copy_from_user(buf,
					out_tunnel_params.p_ether_header,
					out_tunnel_params.eth_header_size)) {
				pr_err("Could not copy ether header from user space:%s\n",
					__func__);
				kfree(buf);
				return -EINVAL;
			}
			out_tunnel_params.p_ether_header = buf;
		}
		if (!out_tunnel_params.p_ip_header &&
				out_tunnel_params.ip_header_size != 0) {
			buf = kzalloc(out_tunnel_params.ip_header_size,
					GFP_KERNEL);
			if (buf == NULL) {
				pr_err("No memory for IP header in %s\n",
						__func__);
				kfree(out_tunnel_params.p_ether_header);
				return -ENOMEM;
			}
			if (copy_from_user(buf, out_tunnel_params.p_ip_header,
					out_tunnel_params.ip_header_size)) {
				pr_err("Could not copy IP header from user space:%s\n",
					__func__);
				kfree(out_tunnel_params.p_ether_header);
				kfree(buf);
				return -EINVAL;
			}
			out_tunnel_params.p_ip_header = buf;
		}
		if (!out_tunnel_params.p_udp_header) {
			buf = kzalloc(UDP_HDR_SIZE, GFP_KERNEL);
			if (buf == NULL) {
				pr_err("No memory for UDP header in %s\n",
						__func__);
				kfree(out_tunnel_params.p_ether_header);
				kfree(out_tunnel_params.p_ip_header);
				return -ENOMEM;
			}
			if (copy_from_user(buf, out_tunnel_params.p_udp_header,
					UDP_HDR_SIZE)) {
				pr_err("Could not copy UDP header from user space:%s\n",
					__func__);
				kfree(out_tunnel_params.p_ether_header);
				kfree(out_tunnel_params.p_ip_header);
				kfree(buf);
				return -EINVAL;
			}
			out_tunnel_params.p_udp_header = buf;
		}
		if (!out_tunnel_params.p_capwap_header &&
				out_tunnel_params.capwap_header_size != 0) {
			buf = kzalloc(out_tunnel_params.capwap_header_size,
					GFP_KERNEL);
			if (buf == NULL) {
				pr_err("No memory for CAPWAP header in %s\n",
						__func__);
				kfree(out_tunnel_params.p_ether_header);
				kfree(out_tunnel_params.p_ip_header);
				kfree(out_tunnel_params.p_udp_header);
				return -ENOMEM;
			}
			if (copy_from_user(buf,
					out_tunnel_params.p_capwap_header,
					out_tunnel_params.capwap_header_size)) {
				pr_err("Could not copy CAPWAP header from user space:%s\n",
					__func__);
				kfree(out_tunnel_params.p_ether_header);
				kfree(out_tunnel_params.p_ip_header);
				kfree(out_tunnel_params.p_udp_header);
				kfree(buf);
				return -EINVAL;
			}
			out_tunnel_params.p_capwap_header = buf;
		}

		capwap_domain = (struct dpaa_capwap_domain *)
					out_tunnel_params.capwap_domain_id;
		if (!capwap_domain) {
			ret = -EINVAL;
			goto err_out;
		}

		list_for_each_entry(tunnel_node, &tunnel_list_head,
				tunnel_list) {
			if (tunnel_node->is_dtls == out_tunnel_params.dtls &&
					tunnel_node->is_control ==
					out_tunnel_params.is_control &&
					tunnel_node->dir ==
					e_DPAA_CAPWAP_DOMAIN_DIR_OUTBOUND) {
				pr_err("%s-%s outbound tunnel already exist, please remove it firstly\n",
						tunnel_node->is_dtls ?
						"DTLS" : "N-DTLS",
						tunnel_node->is_control ?
						"Control" : "Data");
				ret = -EINVAL;
				goto err_out;
			}
		}

		out_tunnel =
			dequeue_tunnel_obj(&capwap_domain->out_tunnel_list);
		if (!out_tunnel) {
			pr_err("You've reached the maximum number of inbound tunnels\n");
			ret = -EINVAL;
			goto err_out;
		}

		if (copy_from_user(out_tunnel->auth_data.auth_key,
				out_tunnel_params.dtls_params.auth_key,
				out_tunnel_params.dtls_params
				.auth_key_len / 8)) {
			pr_err("Could not copy auth key from user space\n");
			ret = -EINVAL;
			goto err_out;
		}
		if (copy_from_user(out_tunnel->cipher_data.cipher_key,
				out_tunnel_params.dtls_params.cipher_key,
				out_tunnel_params.dtls_params
				.cipher_key_len / 8)) {
			pr_err("Could not copy cipher key from user space\n");
			ret = -EINVAL;
			goto err_out;
		}

		ret = add_out_tunnel(capwap_domain, out_tunnel,
				&out_tunnel_params);
		if (ret < 0)
			goto err_out;
		out_tunnel_params.tunnel_id = out_tunnel;

		new_tunnel = kzalloc(sizeof(struct tunnel_info), GFP_KERNEL);
		if (new_tunnel == NULL) {
			ret = -ENOMEM;
			goto err_out;
		}
		new_tunnel->is_control = out_tunnel_params.is_control;
		new_tunnel->is_dtls = out_tunnel_params.dtls;
		new_tunnel->tunnel_id = out_tunnel_params.tunnel_id;
		new_tunnel->dir = e_DPAA_CAPWAP_DOMAIN_DIR_OUTBOUND;
		list_add_tail(&new_tunnel->tunnel_list, &tunnel_list_head);

		if (copy_to_user((void *)args, &out_tunnel_params,
					sizeof(out_tunnel_params))) {
			pr_err("Could not copy DPA CAPWAP ID to user the ID\n");
			ret = -EINVAL;
			goto err_out;
		}

		break;
err_out:
		kfree(out_tunnel_params.p_ether_header);
		kfree(out_tunnel_params.p_ip_header);
		kfree(out_tunnel_params.p_udp_header);
		kfree(out_tunnel_params.p_capwap_header);
		kfree(new_tunnel);
		return ret;

	}

	case DPA_CAPWAP_IOC_DOMAIN_REMOVE_TUNNEL: {
		struct dpaa_capwap_tunnel *tunnel = NULL;
		struct tunnel_info *tunnel_node;
		int is_found = 0;

		/* Copy parameters from user-space */
		if (copy_from_user(&tunnel, (void *)args, sizeof(void *))) {
			pr_err("Could not copy DPA CAPWAP Remove-Tunnel parameters\n");
			return -EINVAL;
		}
		list_for_each_entry(tunnel_node, &tunnel_list_head,
				tunnel_list) {
			if (tunnel_node->tunnel_id == tunnel) {
				is_found = 1;
				break;
			}
		}
		if (is_found) {
			dpaa_capwap_domain_remove_tunnel(tunnel);
			list_del(&tunnel_node->tunnel_list);
			kfree(tunnel_node);
		} else
			return -EINVAL;

		break;
	}
	case DPA_CAPWAP_IOC_DOMAIN_KERNAL_RX_CTL: {
		struct capwap_domain_kernel_rx_ctl rx_ctl;

		/* Copy parameters from user-space */
		if (copy_from_user(&rx_ctl, (void *)args,
				sizeof(struct capwap_domain_kernel_rx_ctl))) {
			pr_err("Could not copy DPA CAPWAP Remove-Tunnel parameters\n");
			return -EINVAL;
		}

		ret = capwap_kernel_rx_ctl(&rx_ctl);
		if (ret)
			return ret;

		if (copy_to_user((void *)args, &rx_ctl,
				sizeof(struct capwap_domain_kernel_rx_ctl))) {
			pr_err("Could not copy DPA CAPWAP rx fqid to user space\n");
			return -EINVAL;
		}

		break;
	}
	default:
		pr_err("Invalid DPA CAPWAP ioctl (0x%x)\n", cmd);
		ret = -EINVAL;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
long wrp_dpa_capwap_ioctl_compat(struct file *filp, unsigned int cmd,
				unsigned long args)
{
	long ret = 0;

	return ret;
}
#endif

static const struct file_operations dpa_capwap_fops = {
	.owner = THIS_MODULE,
	.open = wrp_dpa_capwap_open,
	.read = NULL,
	.write = NULL,
	.unlocked_ioctl = wrp_dpa_capwap_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = wrp_dpa_capwap_ioctl_compat,
#endif
	.release = wrp_dpa_capwap_release
};

static ssize_t domain_show_statistic(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	ssize_t bytes = 0;
	struct tunnel_info *tunnel_node;
	t_FmPcdManipStats manip_stats;
	struct dpaa_capwap_tunnel *capwap_tunnel = NULL;
	int ret;

	list_for_each_entry(tunnel_node, &tunnel_list_head, tunnel_list) {
	capwap_tunnel = (struct dpaa_capwap_tunnel *)tunnel_node->tunnel_id;
	if (capwap_tunnel->h_capwap_frag) {
		memset(&manip_stats, 0, sizeof(manip_stats));
		ret = FM_PCD_ManipGetStatistics(capwap_tunnel->h_capwap_frag,
				&manip_stats);
		if (!ret) {
			bytes += sprintf(buf + bytes, "%s-%s Tunnel:\n",
						tunnel_node->is_dtls ?
						"DTLS" : "N-DTLS",
						tunnel_node->is_control ?
						"Control" : "Data");
			bytes += sprintf(buf + bytes,
					"\tfrag-total-count: %u\n",
				manip_stats.u.frag.u.capwapFrag.totalFrames);
		}
	}
	}

	return bytes;
}

static DEVICE_ATTR(domain_statistic, S_IRUGO, domain_show_statistic, NULL);

static int __init wrp_dpa_capwap_init(void)
{
	/* Cannot initialize the wrapper twice */
	if (dpa_capwap_cdev_major >= 0)
		return -EBUSY;

	dpa_capwap_cdev_major =
	    register_chrdev(0, DPA_CAPWAP_CDEV, &dpa_capwap_fops);
	if (dpa_capwap_cdev_major < 0) {
		pr_err("Could not register DPA CAPWAP character device\n");
		return dpa_capwap_cdev_major;
	}

	capwap_class = class_create(THIS_MODULE, DPA_CAPWAP_CDEV);
	if (IS_ERR(capwap_class)) {
		pr_err("Cannot create DPA CAPWAP class device\n");
		unregister_chrdev(dpa_capwap_cdev_major, DPA_CAPWAP_CDEV);
		dpa_capwap_cdev_major = -1;
		return PTR_ERR(capwap_class);
	}

	capwap_dev = device_create(capwap_class, NULL,
				  MKDEV(dpa_capwap_cdev_major, 0), NULL,
				  DPA_CAPWAP_CDEV);
	if (IS_ERR(capwap_dev)) {
		pr_err("Cannot create DPA CAPWAP device\n");
		class_destroy(capwap_class);
		unregister_chrdev(dpa_capwap_cdev_major, DPA_CAPWAP_CDEV);
		dpa_capwap_cdev_major = -1;
		return PTR_ERR(capwap_dev);
	}
	INIT_LIST_HEAD(&tunnel_list_head);

	if (device_create_file(capwap_dev, &dev_attr_domain_statistic))
		dev_err(capwap_dev, "Error creating sysfs file\n");

	pr_info("DPAA CAPWAP Domain driver v%s", DRV_VERSION);

	return 0;
}


static void __exit wrp_dpa_capwap_exit(void)
{
	device_destroy(capwap_class, MKDEV(dpa_capwap_cdev_major, 0));
	class_destroy(capwap_class);
	unregister_chrdev(dpa_capwap_cdev_major, DPA_CAPWAP_CDEV);
	dpa_capwap_cdev_major = -1;
}

module_init(wrp_dpa_capwap_init);
module_exit(wrp_dpa_capwap_exit);

MODULE_AUTHOR("Freescale, <freescale.com>");
MODULE_DESCRIPTION("DPA CAPWAP Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);
