/* Copyright 2008-2012 Freescale Semiconductor, Inc.
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
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/of_mdio.h>
#include <linux/phy.h>
#include <linux/netdevice.h>

#include "dpaa_eth-common.h"
#include "dpaa_eth.h"
#include "mac.h"

#include "error_ext.h"	/* GET_ERROR_TYPE, E_OK */
#include "fm_mac_ext.h"
#include "fm_rtc_ext.h"

#define MAC_DESCRIPTION "FSL FMan MAC API based driver"

MODULE_LICENSE("Dual BSD/GPL");

MODULE_AUTHOR("Emil Medve <Emilian.Medve@Freescale.com>");

MODULE_DESCRIPTION(MAC_DESCRIPTION);

struct mac_priv_s {
	struct fm_mac_dev *fm_mac;
};

const char	*mac_driver_description __initconst = MAC_DESCRIPTION;
const size_t	 mac_sizeof_priv[] = {
	[DTSEC] = sizeof(struct mac_priv_s),
	[XGMAC] = sizeof(struct mac_priv_s),
	[MEMAC] = sizeof(struct mac_priv_s)
};

static const e_EnetMode _100[] =
{
	[PHY_INTERFACE_MODE_MII]	= e_ENET_MODE_MII_100,
	[PHY_INTERFACE_MODE_RMII]	= e_ENET_MODE_RMII_100
};

static const e_EnetMode _1000[] =
{
	[PHY_INTERFACE_MODE_GMII]	= e_ENET_MODE_GMII_1000,
	[PHY_INTERFACE_MODE_SGMII]	= e_ENET_MODE_SGMII_1000,
	[PHY_INTERFACE_MODE_TBI]	= e_ENET_MODE_TBI_1000,
	[PHY_INTERFACE_MODE_RGMII]	= e_ENET_MODE_RGMII_1000,
	[PHY_INTERFACE_MODE_RGMII_ID]	= e_ENET_MODE_RGMII_1000,
	[PHY_INTERFACE_MODE_RGMII_RXID]	= e_ENET_MODE_RGMII_1000,
	[PHY_INTERFACE_MODE_RGMII_TXID]	= e_ENET_MODE_RGMII_1000,
	[PHY_INTERFACE_MODE_RTBI]	= e_ENET_MODE_RTBI_1000
};

static e_EnetMode __cold __attribute__((nonnull))
macdev2enetinterface(const struct mac_device *mac_dev)
{
	switch (mac_dev->max_speed) {
	case SPEED_100:
		return _100[mac_dev->phy_if];
	case SPEED_1000:
		return _1000[mac_dev->phy_if];
	case SPEED_10000:
		return e_ENET_MODE_XGMII_10000;
	default:
		return e_ENET_MODE_MII_100;
	}
}

static int fm_mac_set_exception(struct fm_mac_dev *fm_mac_dev,
		e_FmMacExceptions exception, bool enable)
{
	int err;
	int _errno;

	err = FM_MAC_SetException(fm_mac_dev, exception, enable);

	_errno = -GET_ERROR_TYPE(err);
	if (unlikely(_errno < 0))
		pr_err("FM_MAC_SetException() = 0x%08x\n", err);

	return _errno;
}

static void mac_exception(t_Handle _mac_dev, e_FmMacExceptions exception)
{
	struct mac_device	*mac_dev;

	mac_dev = (struct mac_device *)_mac_dev;

	if (e_FM_MAC_EX_10G_RX_FIFO_OVFL == exception) {
		/* don't flag RX FIFO after the first */
		fm_mac_set_exception(mac_dev->get_mac_handle(mac_dev),
		    e_FM_MAC_EX_10G_RX_FIFO_OVFL, false);
		printk(KERN_ERR "10G MAC got RX FIFO Error = %x\n", exception);
	}

	dev_dbg(mac_dev->dev, "%s:%s() -> %d\n", KBUILD_BASENAME".c", __func__,
		exception);
}

static int fm_mac_free(struct fm_mac_dev *fm_mac_dev)
{
	int err;
	int _error;

	err = FM_MAC_Free(fm_mac_dev);
	_error = -GET_ERROR_TYPE(err);

	if (unlikely(_error < 0))
		pr_err("FM_MAC_Free() = 0x%08x\n", err);

	return _error;
}

struct fm_mac_dev *fm_mac_config(t_FmMacParams *params)
{
	struct fm_mac_dev *fm_mac_dev;

	fm_mac_dev = FM_MAC_Config(params);
	if (unlikely(fm_mac_dev == NULL))
		pr_err("FM_MAC_Config() failed\n");

	return fm_mac_dev;
}

int fm_mac_config_max_frame_length(struct fm_mac_dev *fm_mac_dev,
		int len)
{
	int err;
	int _errno;

	err = FM_MAC_ConfigMaxFrameLength(fm_mac_dev, len);
	_errno = -GET_ERROR_TYPE(err);
	if (unlikely(_errno < 0))
		pr_err("FM_MAC_ConfigMaxFrameLength() = 0x%08x\n", err);

	return _errno;
}

int fm_mac_config_pad_and_crc(struct fm_mac_dev *fm_mac_dev, bool enable)
{
	int err;
	int _errno;

	err = FM_MAC_ConfigPadAndCrc(fm_mac_dev, enable);
	_errno = -GET_ERROR_TYPE(err);
	if (unlikely(_errno < 0))
		pr_err("FM_MAC_ConfigPadAndCrc() = 0x%08x\n", err);

	return _errno;
}

int fm_mac_config_half_duplex(struct fm_mac_dev *fm_mac_dev, bool enable)
{
	int err;
	int _errno;

	err = FM_MAC_ConfigHalfDuplex(fm_mac_dev, enable);
	_errno = -GET_ERROR_TYPE(err);
	if (unlikely(_errno < 0))
		pr_err("FM_MAC_ConfigHalfDuplex() = 0x%08x\n", err);

	return _errno;
}

int fm_mac_config_reset_on_init(struct fm_mac_dev *fm_mac_dev, bool enable)
{
	int err;
	int _errno;

	err = FM_MAC_ConfigResetOnInit(fm_mac_dev, enable);
	_errno = -GET_ERROR_TYPE(err);
	if (unlikely(_errno < 0))
		pr_err("FM_MAC_ConfigResetOnInit() = 0x%08x\n", err);

	return _errno;
}

int fm_mac_init(struct fm_mac_dev *fm_mac_dev)
{
	int err;
	int _errno;

	err = FM_MAC_Init(fm_mac_dev);
	_errno = -GET_ERROR_TYPE(err);
	if (unlikely(_errno < 0))
		pr_err("FM_MAC_Init() = 0x%08x\n", err);

	return _errno;
}

int fm_mac_get_version(struct fm_mac_dev *fm_mac_dev, uint32_t *version)
{
	int err;
	int _errno;

	err = FM_MAC_GetVesrion(fm_mac_dev, version);
	_errno = -GET_ERROR_TYPE(err);
	if (unlikely(_errno < 0))
		pr_err("FM_MAC_GetVesrion() = 0x%08x\n", err);

	return _errno;
}

static int __cold init(struct mac_device *mac_dev)
{
	int					_errno;
	struct mac_priv_s	*priv;
	t_FmMacParams		param;
	uint32_t			version;

	priv = macdev_priv(mac_dev);

	param.baseAddr =  (typeof(param.baseAddr))(uintptr_t)devm_ioremap(
		mac_dev->dev, mac_dev->res->start, 0x2000);
	param.enetMode	= macdev2enetinterface(mac_dev);
	memcpy(&param.addr, mac_dev->addr, min(sizeof(param.addr),
		sizeof(mac_dev->addr)));
	param.macId		= mac_dev->cell_index;
	param.h_Fm		= (t_Handle)mac_dev->fm;
	param.mdioIrq		= NO_IRQ;
	param.f_Exception	= mac_exception;
	param.f_Event		= mac_exception;
	param.h_App		= mac_dev;

	priv->fm_mac = fm_mac_config(&param);
	if (unlikely(priv->fm_mac == NULL)) {
		_errno = -EINVAL;
		goto _return;
	}

	fm_mac_set_handle(mac_dev->fm_dev, priv->fm_mac,
		(macdev2enetinterface(mac_dev) != e_ENET_MODE_XGMII_10000) ?
			param.macId : param.macId + FM_MAX_NUM_OF_1G_MACS);

	_errno = fm_mac_config_max_frame_length(priv->fm_mac,
					  fm_get_max_frm());
	if (unlikely(_errno < 0))
		goto _return_fm_mac_free;

	if (macdev2enetinterface(mac_dev) != e_ENET_MODE_XGMII_10000) {
		/* 10G always works with pad and CRC */
		_errno = fm_mac_config_pad_and_crc(priv->fm_mac, true);
		if (unlikely(_errno < 0))
			goto _return_fm_mac_free;

		_errno = fm_mac_config_half_duplex(priv->fm_mac,
				mac_dev->half_duplex);
		if (unlikely(_errno < 0))
			goto _return_fm_mac_free;
	}
	else  {
		_errno = fm_mac_config_reset_on_init(priv->fm_mac, true);
		if (unlikely(_errno < 0))
			goto _return_fm_mac_free;
	}

	_errno = fm_mac_init(priv->fm_mac);
	if (unlikely(_errno < 0))
		goto _return_fm_mac_free;

#ifndef CONFIG_FMAN_MIB_CNT_OVF_IRQ_EN
	/* For 1G MAC, disable by default the MIB counters overflow interrupt */
	if (macdev2enetinterface(mac_dev) != e_ENET_MODE_XGMII_10000) {
		_errno = fm_mac_set_exception(mac_dev->get_mac_handle(mac_dev),
				e_FM_MAC_EX_1G_RX_MIB_CNT_OVFL, FALSE);
		if (unlikely(_errno < 0))
			goto _return_fm_mac_free;
	}
#endif /* !CONFIG_FMAN_MIB_CNT_OVF_IRQ_EN */

	/* For 10G MAC, disable Tx ECC exception */
	if (macdev2enetinterface(mac_dev) == e_ENET_MODE_XGMII_10000) {
		_errno = fm_mac_set_exception(mac_dev->get_mac_handle(mac_dev),
					  e_FM_MAC_EX_10G_1TX_ECC_ER, FALSE);
		if (unlikely(_errno < 0))
			goto _return_fm_mac_free;
	}

	_errno = fm_mac_get_version(priv->fm_mac, &version);
	if (unlikely(_errno < 0))
		goto _return_fm_mac_free;

	dev_info(mac_dev->dev, "FMan %s version: 0x%08x\n",
		((macdev2enetinterface(mac_dev) != e_ENET_MODE_XGMII_10000) ?
			"dTSEC" : "XGEC"), version);

	goto _return;


_return_fm_mac_free:
	fm_mac_free(mac_dev->get_mac_handle(mac_dev));

_return:
	return _errno;
}

static int __cold memac_init(struct mac_device *mac_dev)
{
	int			_errno;
	struct mac_priv_s	*priv;
	t_FmMacParams		param;

	priv = macdev_priv(mac_dev);

	param.baseAddr =  (typeof(param.baseAddr))(uintptr_t)devm_ioremap(
		mac_dev->dev, mac_dev->res->start, 0x2000);
	param.enetMode	= macdev2enetinterface(mac_dev);
	memcpy(&param.addr, mac_dev->addr, sizeof(mac_dev->addr));
	param.macId		= mac_dev->cell_index;
	param.h_Fm		= (t_Handle)mac_dev->fm;
	param.mdioIrq		= NO_IRQ;
	param.f_Exception	= mac_exception;
	param.f_Event		= mac_exception;
	param.h_App		= mac_dev;

	priv->fm_mac = fm_mac_config(&param);
	if (unlikely(priv->fm_mac == NULL)) {
		_errno = -EINVAL;
		goto _return;
	}

	_errno = fm_mac_config_max_frame_length(priv->fm_mac, fm_get_max_frm());
	if (unlikely(_errno < 0))
		goto _return_fm_mac_free;

	_errno = fm_mac_config_reset_on_init(priv->fm_mac, true);
	if (unlikely(_errno < 0))
		goto _return_fm_mac_free;

	_errno = fm_mac_init(priv->fm_mac);
	if (unlikely(_errno < 0))
		goto _return_fm_mac_free;

	dev_info(mac_dev->dev, "FMan MEMAC\n");

	goto _return;

_return_fm_mac_free:
	fm_mac_free(priv->fm_mac);

_return:
	return _errno;
}

static int __cold fm_mac_enable(struct fm_mac_dev *fm_mac_dev)
{
	int	 _errno;
	t_Error	 err;

	err = FM_MAC_Enable(fm_mac_dev, e_COMM_MODE_RX_AND_TX);
	_errno = -GET_ERROR_TYPE(err);
	if (unlikely(_errno < 0))
		pr_err("FM_MAC_Enable() = 0x%08x\n", err);

	return _errno;
}

static int __cold fm_mac_disable(struct fm_mac_dev *fm_mac_dev)
{
	int	 _errno;
	t_Error	 err;

	err = FM_MAC_Disable(fm_mac_dev, e_COMM_MODE_RX_AND_TX);
	_errno = -GET_ERROR_TYPE(err);
	if (unlikely(_errno < 0))
		pr_err("FM_MAC_Disable() = 0x%08x\n", err);

	return _errno;
}

static int __cold start(struct mac_device *mac_dev)
{
	int	 _errno;
	struct phy_device *phy_dev = mac_dev->phy_dev;

	_errno = fm_mac_enable(mac_dev->get_mac_handle(mac_dev));

	if (!_errno && phy_dev) {
		if (macdev2enetinterface(mac_dev) != e_ENET_MODE_XGMII_10000)
			phy_start(phy_dev);
		else if (phy_dev->drv->read_status)
			phy_dev->drv->read_status(phy_dev);
	}

	return _errno;
}

static int __cold stop(struct mac_device *mac_dev)
{
	if (mac_dev->phy_dev &&
		(macdev2enetinterface(mac_dev) != e_ENET_MODE_XGMII_10000))
		phy_stop(mac_dev->phy_dev);

	return fm_mac_disable(mac_dev->get_mac_handle(mac_dev));
}

static int __cold fm_mac_set_promiscuous(struct fm_mac_dev *fm_mac_dev,
		bool enable)
{
	int	_errno;
	t_Error	err;

	err = FM_MAC_SetPromiscuous(fm_mac_dev, enable);
	_errno = -GET_ERROR_TYPE(err);
	if (unlikely(_errno < 0))
		pr_err("FM_MAC_SetPromiscuous() = 0x%08x\n", err);

	return _errno;
}

static int __cold fm_mac_remove_hash_mac_addr(struct fm_mac_dev *fm_mac_dev,
		t_EnetAddr *mac_addr)
{
	int	_errno;
	t_Error	err;

	err = FM_MAC_RemoveHashMacAddr(fm_mac_dev, mac_addr);
	_errno = -GET_ERROR_TYPE(err);
	if (_errno < 0) {
		pr_err("FM_MAC_RemoveHashMacAddr() = 0x%08x\n", err);
		return _errno;
	}

	return 0;
}

static int __cold fm_mac_add_hash_mac_addr(struct fm_mac_dev *fm_mac_dev,
		t_EnetAddr *mac_addr)
{
	int	_errno;
	t_Error	err;

	err = FM_MAC_AddHashMacAddr(fm_mac_dev, mac_addr);
	_errno = -GET_ERROR_TYPE(err);
	if (_errno < 0) {
		pr_err("FM_MAC_AddHashMacAddr() = 0x%08x\n", err);
		return _errno;
	}

	return 0;
}

static int __cold set_multi(struct net_device *net_dev)
{
	struct dpa_priv_s       *priv;
	struct mac_device       *mac_dev;
	struct mac_priv_s 	*mac_priv;
	struct mac_address	*old_addr, *tmp;
	struct netdev_hw_addr	*ha;
	int 			 _errno;

	priv = netdev_priv(net_dev);
	mac_dev = priv->mac_dev;
	mac_priv = macdev_priv(mac_dev);

	/* Clear previous address list */
	list_for_each_entry_safe(old_addr, tmp, &mac_dev->mc_addr_list, list) {
		_errno = fm_mac_remove_hash_mac_addr(mac_priv->fm_mac,
				(t_EnetAddr *)old_addr->addr);
		if (_errno < 0)
			return _errno;

		list_del(&old_addr->list);
		kfree(old_addr);
	}

	/* Add all the addresses from the new list */
	netdev_for_each_mc_addr(ha, net_dev) {
		_errno = fm_mac_add_hash_mac_addr(mac_priv->fm_mac,
				(t_EnetAddr *)ha->addr);
		if (_errno < 0)
			return _errno;

		tmp = kmalloc(sizeof(struct mac_address), GFP_ATOMIC);
		if (!tmp) {
			dev_err(mac_dev->dev, "Out of memory\n");
			return -ENOMEM;
		}
		memcpy(tmp->addr, ha->addr, ETH_ALEN);
		list_add(&tmp->list, &mac_dev->mc_addr_list);
	}
	return 0;
}

static int __cold fm_mac_modify_mac_addr(struct fm_mac_dev *fm_mac_dev,
					 uint8_t *addr)
{
	int	_errno;
	t_Error err;

	err = FM_MAC_ModifyMacAddr(fm_mac_dev, (t_EnetAddr *)addr);
	_errno = -GET_ERROR_TYPE(err);
	if (_errno < 0)
		pr_err("FM_MAC_ModifyMacAddr() = 0x%08x\n", err);

	return _errno;
}

static int fm_mac_adjust_link(struct fm_mac_dev *fm_mac_dev,
		bool link, int speed, bool duplex)
{
	int	 _errno;
	t_Error	 err;

	if (!link) {
#if (DPAA_VERSION < 11)
		FM_MAC_RestartAutoneg(fm_mac_dev);
#endif
		return 0;
	}

	err = FM_MAC_AdjustLink(fm_mac_dev, speed, duplex);
	_errno = -GET_ERROR_TYPE(err);
	if (unlikely(_errno < 0))
		pr_err("FM_MAC_AdjustLink() = 0x%08x\n", err);

	return _errno;
}

static void adjust_link(struct net_device *net_dev)
{
	struct dpa_priv_s *priv = netdev_priv(net_dev);
	struct mac_device *mac_dev = priv->mac_dev;
	struct phy_device *phy_dev = mac_dev->phy_dev;

	fm_mac_adjust_link(mac_dev->get_mac_handle(mac_dev),
			phy_dev->link, phy_dev->speed, phy_dev->duplex);
}

/* Initializes driver's PHY state, and attaches to the PHY.
 * Returns 0 on success.
 */
static int dtsec_init_phy(struct net_device *net_dev)
{
	struct dpa_priv_s	*priv;
	struct mac_device	*mac_dev;
	struct phy_device	*phy_dev;

	priv = netdev_priv(net_dev);
	mac_dev = priv->mac_dev;

	if (!mac_dev->phy_node)
		phy_dev = phy_connect(net_dev, mac_dev->fixed_bus_id,
				&adjust_link, 0, mac_dev->phy_if);
	else
		phy_dev = of_phy_connect(net_dev, mac_dev->phy_node,
				&adjust_link, 0, mac_dev->phy_if);
	if (unlikely(phy_dev == NULL) || IS_ERR(phy_dev)) {
		netdev_err(net_dev, "Could not connect to PHY %s\n",
				mac_dev->phy_node ?
					mac_dev->phy_node->full_name :
					mac_dev->fixed_bus_id);
		return phy_dev == NULL ? -ENODEV : PTR_ERR(phy_dev);
	}

	/* Remove any features not supported by the controller */
	phy_dev->supported &= priv->mac_dev->if_support;
	phy_dev->advertising = phy_dev->supported;

	priv->mac_dev->phy_dev = phy_dev;

	return 0;
}

static int xgmac_init_phy(struct net_device *net_dev)
{
	struct dpa_priv_s *priv = netdev_priv(net_dev);
	struct mac_device *mac_dev = priv->mac_dev;
	struct phy_device *phy_dev;

	if (!mac_dev->phy_node)
		phy_dev = phy_attach(net_dev, mac_dev->fixed_bus_id, 0,
				mac_dev->phy_if);
	else
		phy_dev = of_phy_attach(net_dev, mac_dev->phy_node, 0,
				mac_dev->phy_if);
	if (unlikely(phy_dev == NULL) || IS_ERR(phy_dev)) {
		netdev_err(net_dev, "Could not attach to PHY %s\n",
				mac_dev->phy_node ?
					mac_dev->phy_node->full_name :
					mac_dev->fixed_bus_id);
		return phy_dev == NULL ? -ENODEV : PTR_ERR(phy_dev);
	}

	phy_dev->supported &= priv->mac_dev->if_support;
	phy_dev->advertising = phy_dev->supported;

	mac_dev->phy_dev = phy_dev;

	return 0;
}

static int memac_init_phy(struct net_device *net_dev)
{
	struct dpa_priv_s       *priv;
	struct mac_device       *mac_dev;
	struct phy_device       *phy_dev;

	priv = netdev_priv(net_dev);
	mac_dev = priv->mac_dev;

	if (macdev2enetinterface(mac_dev) == e_ENET_MODE_XGMII_10000) {
		if (!mac_dev->phy_node) {
			mac_dev->phy_dev = NULL;
			return 0;
		} else
			phy_dev = of_phy_attach(net_dev, mac_dev->phy_node, 0,
				mac_dev->phy_if);
	} else {
		if (!mac_dev->phy_node)
			phy_dev = phy_connect(net_dev, mac_dev->fixed_bus_id,
				&adjust_link, 0, mac_dev->phy_if);
		else
			phy_dev = of_phy_connect(net_dev, mac_dev->phy_node,
				&adjust_link, 0, mac_dev->phy_if);
	}

	if (unlikely(phy_dev == NULL) || IS_ERR(phy_dev)) {
		netdev_err(net_dev, "Could not connect to PHY %s\n",
			mac_dev->phy_node ?
				mac_dev->phy_node->full_name :
				mac_dev->fixed_bus_id);
		return phy_dev == NULL ? -ENODEV : PTR_ERR(phy_dev);
	}

	/* Remove any features not supported by the controller */
	phy_dev->supported &= priv->mac_dev->if_support;
	phy_dev->advertising = phy_dev->supported;

	mac_dev->phy_dev = phy_dev;

	return 0;
}

static int __cold uninit(struct fm_mac_dev *fm_mac_dev)
{
	int			 _errno, __errno;

	_errno = fm_mac_disable(fm_mac_dev);
	__errno = fm_mac_free(fm_mac_dev);

	if (unlikely(__errno < 0)) {
		_errno = __errno;
	}

	return _errno;
}

static struct fm_mac_dev *get_mac_handle(struct mac_device *mac_dev)
{
	const struct mac_priv_s	*priv;
	priv = macdev_priv(mac_dev);
	return priv->fm_mac;
}

static int __cold fm_mac_enable_1588_time_stamp(struct fm_mac_dev *fm_mac_dev)
{
	int			 _errno;
	t_Error			 err;

	err = FM_MAC_Enable1588TimeStamp(fm_mac_dev);
	_errno = -GET_ERROR_TYPE(err);
	if (unlikely(_errno < 0))
		pr_err("FM_MAC_Enable1588TimeStamp() = 0x%08x\n", err);
	return _errno;
}

static int __cold fm_mac_disable_1588_time_stamp(struct fm_mac_dev *fm_mac_dev)
{
	int			 _errno;
	t_Error			 err;

	err = FM_MAC_Disable1588TimeStamp(fm_mac_dev);
	_errno = -GET_ERROR_TYPE(err);
	if (unlikely(_errno < 0))
		pr_err("FM_MAC_Disable1588TimeStamp() = 0x%08x\n", err);
	return _errno;
}

static int __cold fm_mac_set_rx_ignore_pause_frames(
		struct fm_mac_dev *fm_mac_dev, bool en)
{
	int	_errno;
	t_Error err;

	/* if rx pause is enabled, do NOT ignore pause frames */
	err = FM_MAC_SetRxIgnorePauseFrames(fm_mac_dev, !en);

	_errno = -GET_ERROR_TYPE(err);
	if (_errno < 0)
		pr_err("FM_MAC_SetRxIgnorePauseFrames() = 0x%08x\n", err);

	return _errno;
}

static int __cold fm_mac_set_tx_pause_frames(struct fm_mac_dev *fm_mac_dev,
					     bool en)
{
	int	_errno;
	t_Error err;

	if (en)
		err = FM_MAC_SetTxPauseFrames(fm_mac_dev,
				TX_PAUSE_PRIO_DEFAULT,
				TX_PAUSE_TIME_ENABLE,
				TX_PAUSE_THRESH_DEFAULT);
	else
		err = FM_MAC_SetTxPauseFrames(fm_mac_dev,
				TX_PAUSE_PRIO_DEFAULT,
				TX_PAUSE_TIME_DISABLE,
				TX_PAUSE_THRESH_DEFAULT);

	_errno = -GET_ERROR_TYPE(err);
	if (_errno < 0)
		pr_err("FM_MAC_SetTxPauseFrames() = 0x%08x\n", err);

	return _errno;
}

static int __cold fm_rtc_enable(struct fm *fm_dev)
{
	int			 _errno;
	t_Error			 err;

	err = FM_RTC_Enable(fm_get_rtc_handle(fm_dev), 0);
	_errno = -GET_ERROR_TYPE(err);
	if (unlikely(_errno < 0))
		pr_err("FM_RTC_Enable = 0x%08x\n", err);

	return _errno;
}

static int __cold fm_rtc_disable(struct fm *fm_dev)
{
	int			 _errno;
	t_Error			 err;

	err = FM_RTC_Disable(fm_get_rtc_handle(fm_dev));
	_errno = -GET_ERROR_TYPE(err);
	if (unlikely(_errno < 0))
		pr_err("FM_RTC_Disable = 0x%08x\n", err);

	return _errno;
}

static int __cold fm_rtc_get_cnt(struct fm *fm_dev, uint64_t *ts)
{
	int _errno;
	t_Error	err;

	err = FM_RTC_GetCurrentTime(fm_get_rtc_handle(fm_dev), ts);
	_errno = -GET_ERROR_TYPE(err);
	if (unlikely(_errno < 0))
		pr_err("FM_RTC_GetCurrentTime = 0x%08x\n", err);

	return _errno;
}

static int __cold fm_rtc_set_cnt(struct fm *fm_dev, uint64_t ts)
{
	int _errno;
	t_Error	err;

	err = FM_RTC_SetCurrentTime(fm_get_rtc_handle(fm_dev), ts);
	_errno = -GET_ERROR_TYPE(err);
	if (unlikely(_errno < 0))
		pr_err("FM_RTC_SetCurrentTime = 0x%08x\n", err);

	return _errno;
}

static int __cold fm_rtc_get_drift(struct fm *fm_dev, uint32_t *drift)
{
	int _errno;
	t_Error	err;

	err = FM_RTC_GetFreqCompensation(fm_get_rtc_handle(fm_dev),
			drift);
	_errno = -GET_ERROR_TYPE(err);
	if (unlikely(_errno < 0))
		pr_err("FM_RTC_GetFreqCompensation = 0x%08x\n", err);

	return _errno;
}

static int __cold fm_rtc_set_drift(struct fm *fm_dev, uint32_t drift)
{
	int _errno;
	t_Error	err;

	err = FM_RTC_SetFreqCompensation(fm_get_rtc_handle(fm_dev),
			drift);
	_errno = -GET_ERROR_TYPE(err);
	if (unlikely(_errno < 0))
		pr_err("FM_RTC_SetFreqCompensation = 0x%08x\n", err);

	return _errno;
}

static int __cold fm_rtc_set_alarm(struct fm *fm_dev, uint32_t id,
		uint64_t time)
{
	t_FmRtcAlarmParams alarm;
	int _errno;
	t_Error	err;

	alarm.alarmId = id;
	alarm.alarmTime = time;
	alarm.f_AlarmCallback = NULL;
	err = FM_RTC_SetAlarm(fm_get_rtc_handle(fm_dev),
			&alarm);
	_errno = -GET_ERROR_TYPE(err);
	if (unlikely(_errno < 0))
		pr_err("FM_RTC_SetAlarm = 0x%08x\n", err);

	return _errno;
}

static int __cold fm_rtc_set_fiper(struct fm *fm_dev, uint32_t id,
		uint64_t fiper)
{
	t_FmRtcPeriodicPulseParams pp;
	int _errno;
	t_Error	err;

	pp.periodicPulseId = id;
	pp.periodicPulsePeriod = fiper;
	pp.f_PeriodicPulseCallback = NULL;
	err = FM_RTC_SetPeriodicPulse(fm_get_rtc_handle(fm_dev), &pp);
	_errno = -GET_ERROR_TYPE(err);
	if (unlikely(_errno < 0))
		pr_err("FM_RTC_SetPeriodicPulse = 0x%08x\n", err);

	return _errno;
}

void fm_mac_dump_regs(struct fm_mac_dev *fm_mac_dev)
{
	FM_MAC_DumpRegs(fm_mac_dev);
}

static void __cold setup_dtsec(struct mac_device *mac_dev)
{
	mac_dev->init_phy	= dtsec_init_phy;
	mac_dev->init		= init;
	mac_dev->start		= start;
	mac_dev->stop		= stop;
	mac_dev->set_promisc	= fm_mac_set_promiscuous;
	mac_dev->change_addr    = fm_mac_modify_mac_addr;
	mac_dev->set_multi      = set_multi;
	mac_dev->uninit		= uninit;
	mac_dev->ptp_enable		= fm_mac_enable_1588_time_stamp;
	mac_dev->ptp_disable		= fm_mac_disable_1588_time_stamp;
	mac_dev->get_mac_handle		= get_mac_handle;
	mac_dev->set_tx_pause		= fm_mac_set_tx_pause_frames;
	mac_dev->set_rx_pause		= fm_mac_set_rx_ignore_pause_frames;
	mac_dev->fm_rtc_enable		= fm_rtc_enable;
	mac_dev->fm_rtc_disable		= fm_rtc_disable;
	mac_dev->fm_rtc_get_cnt		= fm_rtc_get_cnt;
	mac_dev->fm_rtc_set_cnt		= fm_rtc_set_cnt;
	mac_dev->fm_rtc_get_drift	= fm_rtc_get_drift;
	mac_dev->fm_rtc_set_drift	= fm_rtc_set_drift;
	mac_dev->fm_rtc_set_alarm	= fm_rtc_set_alarm;
	mac_dev->fm_rtc_set_fiper	= fm_rtc_set_fiper;
}

static void __cold setup_xgmac(struct mac_device *mac_dev)
{
	mac_dev->init_phy	= xgmac_init_phy;
	mac_dev->init		= init;
	mac_dev->start		= start;
	mac_dev->stop		= stop;
	mac_dev->set_promisc	= fm_mac_set_promiscuous;
	mac_dev->change_addr    = fm_mac_modify_mac_addr;
	mac_dev->set_multi      = set_multi;
	mac_dev->uninit		= uninit;
	mac_dev->get_mac_handle	= get_mac_handle;
	mac_dev->set_tx_pause	= fm_mac_set_tx_pause_frames;
	mac_dev->set_rx_pause	= fm_mac_set_rx_ignore_pause_frames;
}

static void __cold setup_memac(struct mac_device *mac_dev)
{
	mac_dev->init_phy	= memac_init_phy;
	mac_dev->init		= memac_init;
	mac_dev->start		= start;
	mac_dev->stop		= stop;
	mac_dev->set_promisc	= fm_mac_set_promiscuous;
	mac_dev->change_addr    = fm_mac_modify_mac_addr;
	mac_dev->set_multi      = set_multi;
	mac_dev->uninit		= uninit;
	mac_dev->get_mac_handle		= get_mac_handle;
	mac_dev->set_tx_pause		= fm_mac_set_tx_pause_frames;
	mac_dev->set_rx_pause		= fm_mac_set_rx_ignore_pause_frames;
	mac_dev->fm_rtc_enable		= fm_rtc_enable;
	mac_dev->fm_rtc_disable		= fm_rtc_disable;
	mac_dev->fm_rtc_get_cnt		= fm_rtc_get_cnt;
	mac_dev->fm_rtc_set_cnt		= fm_rtc_set_cnt;
	mac_dev->fm_rtc_get_drift	= fm_rtc_get_drift;
	mac_dev->fm_rtc_set_drift	= fm_rtc_set_drift;
	mac_dev->fm_rtc_set_alarm	= fm_rtc_set_alarm;
	mac_dev->fm_rtc_set_fiper	= fm_rtc_set_fiper;
}

void (*const mac_setup[])(struct mac_device *mac_dev) = {
	[DTSEC] = setup_dtsec,
	[XGMAC] = setup_xgmac,
	[MEMAC] = setup_memac
};
