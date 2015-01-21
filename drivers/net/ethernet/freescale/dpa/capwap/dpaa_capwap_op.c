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
#include <linux/kthread.h>
#include "lnxwrp_fm.h"
#include "../offline_port.h"
#include "../dpaa_eth.h"
#include "../dpaa_eth_common.h"
#include "dpaa_capwap.h"
#include "dpaa_capwap_fq.h"
#include "dpaa_capwap_domain.h"

void get_capwap_bp(struct net_device *net_dev,
		struct dpaa_capwap_domain *capwap_domain)
{
	struct dpa_priv_s               *priv;

	priv = netdev_priv(net_dev);
	capwap_domain->bpid = priv->dpa_bp->bpid;
	capwap_domain->bp_size = priv->dpa_bp->size;
}

int op_init(struct t_Port *port, struct net_device *net_dev)
{
	struct device_node	*dpa_oh_node;
	bool is_found = false;
	struct platform_device *oh_dev;
	struct dpa_oh_config_s *oh_config;
	uint32_t def_fqid, err_fqid;
	t_LnxWrpFmPortDev   *fm_port_dev = NULL;
	t_LnxWrpFmDev *fm_dev = NULL;
	static struct of_device_id dpa_oh_node_of_match[] = {
		{ .compatible = "fsl,dpa-oh", },
		{ /* end of list */ },
		};


	for_each_matching_node(dpa_oh_node, dpa_oh_node_of_match) {
		oh_dev = of_find_device_by_node(dpa_oh_node);
		oh_config = dev_get_drvdata(&oh_dev->dev);
		if (oh_config == NULL)
			continue;

		fm_port_dev = (t_LnxWrpFmPortDev *)oh_config->oh_port;
		fm_dev = fm_port_dev->h_LnxWrpFmDev;
		if (fm_port_dev->settings.param.portId == port->port_id &&
				fm_dev->id == port->fm_id) {
			is_found = true;
			port->tx_ch = fm_port_dev->txCh;
			def_fqid = oh_config->error_fqid;
			err_fqid = oh_config->default_fqid;
			break;
		}
	}

	if (!is_found) {
		pr_err("Can't found this dpa OH port:%d,%d\n", port->fm_id,
				port->port_id);
		return -ENODEV;
	}

	return 0;
}
