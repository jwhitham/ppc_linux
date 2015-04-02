/*
 * drivers/net/phy/realtek.c
 *
 * Driver for Realtek PHYs
 *
 * Author: Johnson Leung <r58129@freescale.com>
 *
 * Copyright (c) 2004 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */
#include <linux/phy.h>
#include <linux/module.h>
#include <linux/delay.h>

#define RTL821x_PHYSR		0x11
#define RTL821x_PHYSR_DUPLEX	0x2000
#define RTL821x_PHYSR_SPEED	0xc000
#define RTL821x_INER		0x12
#define RTL821x_INER_INIT	0x6400
#define RTL821x_INSR		0x13

#define RTL8211E_INER_LINK_STATUS 0x0400
#define RTL8211F_INER_LINK_STATUS 0x0010
#define RTL8211F_INSR		  0x1d
#define RTL8211F_PAGE_SELECT	  0x1f
#define RTL8211F_PHY_STATUS	  0x1a
#define RTL8211F_PHYSTAT_SPEED	  0x0030
#define RTL8211F_PHYSTAT_GBIT	  0x0020
#define RTL8211F_PHYSTAT_100	  0x0010
#define RTL8211F_PHYSTAT_DUPLEX	  0x0008
#define RTL8211F_PHYSTAT_SPDDONE  0x0800
#define RTL8211F_PHYSTAT_LINK     0x0004
#define PHY_AUTONEGOTIATE_TIMEOUT 3000

MODULE_DESCRIPTION("Realtek PHY driver");
MODULE_AUTHOR("Johnson Leung");
MODULE_LICENSE("GPL");

static int rtl821x_ack_interrupt(struct phy_device *phydev)
{
	int err;

	err = phy_read(phydev, RTL821x_INSR);

	return (err < 0) ? err : 0;
}

static int rtl8211f_ack_interrupt(struct phy_device *phydev)
{
	int err;

	phy_write(phydev, RTL8211F_PAGE_SELECT, 0xa43);
	err = phy_read(phydev, RTL8211F_INSR);
	/* restore to default page 0 */
	phy_write(phydev, RTL8211F_PAGE_SELECT, 0x0);

	return (err < 0) ? err : 0;
}

static int rtl8211b_config_intr(struct phy_device *phydev)
{
	int err;

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED)
		err = phy_write(phydev, RTL821x_INER,
				RTL821x_INER_INIT);
	else
		err = phy_write(phydev, RTL821x_INER, 0);

	return err;
}

static int rtl8211e_config_intr(struct phy_device *phydev)
{
	int err;

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED)
		err = phy_write(phydev, RTL821x_INER,
				RTL8211E_INER_LINK_STATUS);
	else
		err = phy_write(phydev, RTL821x_INER, 0);

	return err;
}

static int rtl8211f_config_intr(struct phy_device *phydev)
{
	int err;

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED)
		err = phy_write(phydev, RTL821x_INER,
				RTL8211F_INER_LINK_STATUS);
	else
		err = phy_write(phydev, RTL821x_INER, 0);

	return err;
}

static int rtl8211f_config_init(struct phy_device *phydev)
{
	unsigned int speed, mii_reg;
	int i = 0;

	phy_write(phydev, RTL8211F_PAGE_SELECT, 0xa43);
	mii_reg = phy_read(phydev, RTL8211F_PHY_STATUS);
	phydev->link = 1;
	while (!(mii_reg & RTL8211F_PHYSTAT_LINK)) {
		if (i++ > PHY_AUTONEGOTIATE_TIMEOUT) {
			pr_warn("RTL8211F LINK TIMEOUT\n");
			phydev->link = 0;
			break;
		}
		udelay(1000);
		mii_reg = phy_read(phydev, RTL8211F_PHY_STATUS);
	}

	if (mii_reg & RTL8211F_PHYSTAT_DUPLEX)
		phydev->duplex = DUPLEX_FULL;
	else
		phydev->duplex = DUPLEX_HALF;

	speed = (mii_reg & RTL8211F_PHYSTAT_SPEED);

	switch (speed) {
	case RTL8211F_PHYSTAT_GBIT:
		phydev->speed = SPEED_1000;
		break;
	case RTL8211F_PHYSTAT_100:
		phydev->speed = SPEED_100;
		break;
	default:
		phydev->speed = SPEED_10;
	}

	if (phydev->interface == PHY_INTERFACE_MODE_RGMII) {
		udelay(2000);
		/* enable TXDLY */
		phy_write(phydev, RTL8211F_PAGE_SELECT, 0xd08);
		udelay(2000);
		phy_write(phydev, 0x11, 0x109);
		/* restore to default page 0 */
		phy_write(phydev, RTL8211F_PAGE_SELECT, 0x0);
	}

	return 0;
}

static struct phy_driver realtek_drivers[] = {
	{	/* RTL8211B */
		.phy_id	 = 0x001cc912,
		.name	   = "RTL8211B Gigabit Ethernet",
		.phy_id_mask    = 0x001fffff,
		.features       = PHY_GBIT_FEATURES,
		.flags	  = PHY_HAS_INTERRUPT,
		.config_aneg    = &genphy_config_aneg,
		.read_status    = &genphy_read_status,
		.ack_interrupt  = &rtl821x_ack_interrupt,
		.config_intr    = &rtl8211b_config_intr,
		.driver		= { .owner = THIS_MODULE },
	},
	{	/* RTL8211E */
		.phy_id	 = 0x001cc915,
		.name	   = "RTL8211E Gigabit Ethernet",
		.phy_id_mask    = 0x001fffff,
		.features       = PHY_GBIT_FEATURES,
		.flags	  = PHY_HAS_INTERRUPT,
		.config_aneg    = &genphy_config_aneg,
		.read_status    = &genphy_read_status,
		.ack_interrupt  = &rtl821x_ack_interrupt,
		.config_intr    = &rtl8211e_config_intr,
		.suspend	= genphy_suspend,
		.resume	 = genphy_resume,
		.driver		= { .owner = THIS_MODULE },
	},
	{	/* RTL8211F */
		.phy_id		= 0x001cc916,
		.name		= "RTL8211F Gigabit Ethernet",
		.phy_id_mask	= 0x001fffff,
		.features	= PHY_GBIT_FEATURES,
		.flags		= PHY_HAS_INTERRUPT,
		.config_aneg	= &genphy_config_aneg,
		.config_init	= &rtl8211f_config_init,
		.read_status	= &genphy_read_status,
		.ack_interrupt	= &rtl8211f_ack_interrupt,
		.config_intr	= &rtl8211f_config_intr,
		.suspend	= genphy_suspend,
		.resume		= genphy_resume,
		.driver		= { .owner = THIS_MODULE },
	},
};

static int __init realtek_init(void)
{
	return phy_drivers_register(realtek_drivers,
		 ARRAY_SIZE(realtek_drivers));
}

static void __exit realtek_exit(void)
{
	phy_drivers_unregister(realtek_drivers,
		ARRAY_SIZE(realtek_drivers));
}

module_init(realtek_init);
module_exit(realtek_exit);

static struct mdio_device_id __maybe_unused realtek_tbl[] = {
	{ 0x001cc912, 0x001fffff },
	{ 0x001cc915, 0x001fffff },
	{ 0x001cc916, 0x001fffff },
	{ }
};

MODULE_DEVICE_TABLE(mdio, realtek_tbl);
