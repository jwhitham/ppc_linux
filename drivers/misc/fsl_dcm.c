/*
 * Freescale Data Collection Manager (DCM) device driver
 *
 * Copyright (C) 2011 Freescale Semiconductor, Inc.
 * Author: Timur Tabi <timur@freescale.com>
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2.  This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 *
 * Inside the FPGA of some Freescale QorIQ (PowerPC) reference boards is a
 * microprocessor called the General Purpose Processor (GSMA).  Running on
 * the GSMA is the Data Collection Manager (DCM), which is used to
 * periodically read and tally voltage, current, and temperature measurements
 * from the on-board sensors.  You can use this feature to measure power
 * consumption while running tests, without having the host CPU perform those
 * measurements.
 *
 */

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/slab.h>
#include <linux/string.h>
#include "fsl_dcm.h"

/*
 * Converts a 16-bit VOUT value from the Zilker ZL6100 into a voltage value,
 * in millivolts.
 */
static unsigned int voltage_from_zl6100(u16 vout)
{
	return (1000UL * vout) / (1 << 13);
}

/* unit is mv */
static unsigned int voltage_from_ina220(u16 vout)
{
	return (vout >> 3) * 4;
}

/*
 * Converts a 16-bit IOUT from the Texas Instruments INA220 chip into a
 * current value, in milliamps.  'shunt' is a board-specific shunt.
 */
static unsigned int current_from_ina220(u16 sv, unsigned int shunt)
{
	unsigned long c;

	/*
	 * Current = ShuntVoltage * CalibrationRegister / 4096
	 * = ShuntVoltage * 40,960,000 / shunt(uOhms) / 4096
	 * = ShuntVoltage * 10000 / shunt
	 */
	c = sv * 10000;
	c /= shunt;

	return c;
}

/*
 * Converts a 16-bit TOUT value from the sensor device into a temperature
 * value, in degrees Celsius.
 */
static unsigned int temp_from_u16(u16 tout)
{
	return tout;
}

/*
 * Write a byte to an address in SRAM
 */
static void write_sram(struct fsl_dcm_data *dcm, u8 offset, u8 v)
{
	out_8(dcm->addr, offset);
	out_8(dcm->data, v);
}

/*
 * Read a byte from an address in SRAM
 */
static u8 read_sram(struct fsl_dcm_data *dcm, u8 offset)
{
	out_8(dcm->addr, offset);

	return in_8(dcm->data);
}

/*
 * True TRUE if we can read/write SRAM, FALSE otherwise.
 *
 * If the SRAM is unavailable, it's probably because the DCM is busy with it.
 */
static int is_sram_available(struct fsl_dcm_data *dcm)
{
	u8 ack, cmd;

	cmd = in_8(dcm->ocmd);
	ack = in_8(dcm->mack);

	if ((cmd & PX_OCMD_MSG) || (ack & PX_OACK_ACK)) {
		dev_dbg(dcm->dev, "dcm is not ready (cmd=%02X mack=%02X)\n",
			cmd, ack);
		return 0;
	}

	return 1;
}

/*
 * Loads and program into SRAM, then tells the DCM to run it, and then waits
 * for it to finish.
 */
static int run_program(struct fsl_dcm_data *dcm, u8 addr,
		unsigned int len, ...)
{
	u8 v, n;
	va_list args;

	if (addr + len > 0xff) {
		dev_err(dcm->dev, "address/length of %u/%u is out of bounds\n",
			addr, len);
		return -EBUSY;
	}

	/* load the program into SRAM */
	va_start(args, len);
	for (n = addr; n < addr + len; n++) {
		v = va_arg(args, int);
		write_sram(dcm, n, v);
	}
	va_end(args);

	/* start the DCM */
	out_8(dcm->omsg, addr);
	out_8(dcm->ocmd, PX_OCMD_MSG);

	/* wait for ack or error */
	v = spin_event_timeout(in_8(dcm->mack) & (PX_OACK_ERR | PX_OACK_ACK),
			       50000, 1000);
	if ((!v) || (v & PX_OACK_ERR)) {
		dev_err(dcm->dev, "timeout or error waiting for start ack\n");
		return -EBUSY;
	}

	/* 4. allow the host to read SRAM */
	out_8(dcm->ocmd, 0);

	/* 5. wait for DCM to stop (ack == 0) or error (err == 1) */
	spin_event_timeout(
		((v = in_8(dcm->mack)) & (PX_OACK_ERR | PX_OACK_ACK))
		!= PX_OACK_ACK, 50000, 1000);

	/* 6. check for error or timeout */
	if (v & (PX_OACK_ERR | PX_OACK_ACK)) {
		dev_err(dcm->dev, "timeout or error waiting for stop ack\n");
		return -EBUSY;
	}

	return 0;
}

#define TRATE0	241122		/* t-rate if prescale==0, in millihertz */
#define TRATE1	38579330	/* t-rate if prescale==1, in millihertz */
/*
 * Empirical tests show that any frequency higher than 48Hz is unreliable.
 */
static int set_dcm_frequency(struct fsl_dcm_data *dcm,
		unsigned long frequency)
{
	unsigned long timer;

	if (!is_sram_available(dcm)) {
		dev_err(dcm->dev, "dcm is busy\n");
		return -EBUSY;
	}

	/* Restrict the frequency to a supported range. */
	frequency = clamp_t(unsigned long, frequency, 1, MAX_FREQUENCY);

	/* We only support prescale == 0 */
	timer = TRATE0 / frequency;
	dcm->timer = ((timer / 1000) - 1) & 0xff;

	return run_program(dcm, 0, 6, OM_TIMER, 0, dcm->timer, 0, 0, OM_END);
}

static int copy_from_sram(struct fsl_dcm_data *dcm, unsigned int addr,
			  void *buf, unsigned int len)
{
	u8 *p = buf;
	unsigned int i;

	if (addr + len > 0xff) {
		dev_err(dcm->dev, "address/length of %u/%u is out of bounds\n",
			addr, len);
		return -EBUSY;
	}

	for (i = 0; i < len; i++)
		p[i] = read_sram(dcm, addr + i);

	return 0;
}

/*
 * Tells the DCM which channels to collect data on.
 */
static int select_dcm_channels(struct fsl_dcm_data *dcm, u16 mask)
{
	if (!is_sram_available(dcm)) {
		dev_err(dcm->dev, "dcm is busy\n");
		return -EBUSY;
	}

	return run_program(dcm, 0, 4, OM_ENABLE,
		((mask >> 8) & 0xFF), mask & 0xFF, OM_END);
}

/*
 * Tells the DCM to start data collection.  If the DCM is currently running,
 * it is restarted.  Any currently collected data is cleared.
 */
int start_data_collection(struct fsl_dcm_data *dcm)
{
	if (!is_sram_available(dcm)) {
		dev_err(dcm->dev, "dcm is busy\n");
		return -EBUSY;
	}

	if (dcm->running)
		dev_dbg(dcm->dev, "restarting\n");

	dcm->running = true;

	return run_program(dcm, 0, 4, OM_STOP, OM_SCLR, OM_START, OM_END);
}

/* Tells the DCM to stop data collection. */
int stop_data_collection(struct fsl_dcm_data *dcm)
{
	if (!dcm->running) {
		dev_dbg(dcm->dev, "dcm is already stopped\n");
		return 0;
	}

	if (!is_sram_available(dcm)) {
		dev_err(dcm->dev, "dcm is busy\n");
		return -EBUSY;
	}

	if (run_program(dcm, 0, 2, OM_STOP, OM_END)) {
		dev_err(dcm->dev, "could not stop monitoring\n");
		return -EBUSY;
	}

	dcm->running = false;

	return 0;
}

static ssize_t dcm_get_info(struct device *dev)
{
	struct fsl_dcm_data *dcm = dev_get_drvdata(dev);

	if (!is_sram_available(dcm)) {
		dev_err(dev, "dcm is busy\n");
		return -EBUSY;
	}

	if (run_program(dcm, 0, 3, OM_INFO, DATA_ADDR, OM_END)) {
		dev_err(dev, "could not run 'info' program\n");
		return -EBUSY;
	}

	if (copy_from_sram(dcm, DATA_ADDR, &dcm->board.info,
			   sizeof(struct om_info))) {
		dev_err(dev, "could not copy 'info' data\n");
		return -EBUSY;
	}

	return 0;
}

ssize_t fsl_dcm_sysfs_control_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct fsl_dcm_data *dcm = dev_get_drvdata(dev);

	return sprintf(buf, "%s\n", dcm->running ? "running" : "stoppped");
}

ssize_t fsl_dcm_sysfs_control_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct fsl_dcm_data *dcm = dev_get_drvdata(dev);
	unsigned long pm_cmd;
	int ret;

	ret = kstrtoul(buf, 10, &pm_cmd);
	if (ret)
		return ret;

	switch (pm_cmd) {
	case SYSFS_DCM_CMD_START:
		ret = start_data_collection(dcm);
		if (ret)
			dev_err(dev, "failed to start power monitoring.\n");
		break;
	case SYSFS_DCM_CMD_STOP:
		ret = stop_data_collection(dcm);
		if (ret)
			dev_err(dev, "failed to stop power monitoring\n");
		break;
	default:
		return -EIO;
	}

	return count;
}

static int get_crecords(struct fsl_dcm_data *dcm)
{
	int len;
	u8 addr1, addr2;
	struct crecord *crec = dcm->board.crec;
	struct om_info *info = &dcm->board.info;

	/* get CRECORDs from ocm sram */
	if (!is_sram_available(dcm)) {
		dev_err(dcm->dev, "dcm is busy\n");
		return -EBUSY;
	}

	len = sizeof(struct crecord) * info->count;
	addr1 = (info->addr >> 8) & 0xff;
	addr2 = info->addr & 0xff;

	if (run_program(dcm, 0, 6, OM_GETMEMX, addr1,
			addr2, len, DATA_ADDR, OM_END)) {
		dev_err(dcm->dev, "could not stop monitoring\n");
		return -EBUSY;
	}

	if (copy_from_sram(dcm, DATA_ADDR, crec, len)) {
		dev_err(dcm->dev, "could not copy sensor data\n");
		return -EBUSY;
	}

	return 0;
}

/* Calculate the average, even if 'count' is zero */
#define AVG(sum, count)	((sum) / ((count) ?: 1))
#define MAX_RECORD 9
static ssize_t fsl_dcm_sysfs_result(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct fsl_dcm_data *dcm = dev_get_drvdata(dev);
	struct record *rec = dcm->board.rec;
	struct crecord *crec = dcm->board.crec;
	ssize_t len;
	int i, avg, num, val;

	if (get_crecords(dcm))
		return sprintf(buf, "Get record error\n");

	len = sprintf(buf,
		"Name                         Average\n"
		"====================         ================\n");

	for (i = 0; i < dcm->board.info.count; i++) {
		num = (crec->num1 << 8) | (crec->num2);
		val = AVG(crec->accum, num);

		if (crec->ctl & CRCTL_GET_V) {
			avg = voltage_from_zl6100(val);
		} else if (crec->ctl & CRCTL_GET_T) {
			avg = temp_from_u16(val);
		} else if (crec->ctl & CRCTL_GET_V2) {
			avg = voltage_from_ina220(val);
		} else if (crec->ctl & CRCTL_GET_I2) {
			avg = current_from_ina220(val, dcm->board.shunt);
		} else {
			dev_err(dev, "Unknown record\n");
			return -EBUSY;
		}

		len += sprintf(buf + len,
				"%-8s                     %-6d %s\n",
				rec->name, avg, rec->unit);
		crec++;
		rec++;
	}

	return len;
}

ssize_t fsl_dcm_sysfs_info(struct device *dev, struct device_attribute *attr,
	char *buf)
{
	struct fsl_dcm_data *dcm = dev_get_drvdata(dev);
	struct om_info *info = &dcm->board.info;
	ssize_t len;

	len = sprintf(buf, "DCM Version: 0x%02x\n", info->ver);
	len += sprintf(buf + len, "Prescale: %u\n", info->prescale);
	len += sprintf(buf + len, "Timer: %u\n", info->timer);
	len += sprintf(buf + len, "Number of CRECORDs: %u\n", info->count);
	len += sprintf(buf + len, "CRECORD Address: 0x%04x\n", info->addr);

	return len;
}

ssize_t fsl_dcm_sysfs_frequency_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct fsl_dcm_data *dcm = dev_get_drvdata(dev);
	unsigned long frequency;

	frequency = TRATE0 / (dcm->timer + 1);

	return sprintf(buf, "%lu Hz\n", frequency / 1000);
}

ssize_t fsl_dcm_sysfs_frequency_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct fsl_dcm_data *dcm = dev_get_drvdata(dev);
	unsigned long frequency;
	int ret;

	ret = kstrtoul(buf, 10, &frequency);
	if (ret)
		return ret;

	set_dcm_frequency(dcm, frequency);

	return count;
}

static DEVICE_ATTR(control, 0666, fsl_dcm_sysfs_control_show,
		   fsl_dcm_sysfs_control_store);
static DEVICE_ATTR(result, 0444, fsl_dcm_sysfs_result, NULL);
static DEVICE_ATTR(info, 0444, fsl_dcm_sysfs_info, NULL);
static DEVICE_ATTR(frequency, 0666, fsl_dcm_sysfs_frequency_show,
		   fsl_dcm_sysfs_frequency_store);

static const struct attribute_group fsl_dcm_attr_group = {
	.attrs = (struct attribute * []) {
		&dev_attr_control.attr,
		&dev_attr_result.attr,
		&dev_attr_info.attr,
		&dev_attr_frequency.attr,
		NULL,
	},
};

static int board_data_init(struct fsl_dcm_data *dcm)
{
	int i, len;
	u8 addr1, addr2;
	struct record *rec = dcm->board.rec;
	struct crecord *crec = dcm->board.crec;
	struct om_info *info = &dcm->board.info;
	struct om_xinfo xinfo;
	char name[MAX_NAME_LEN + 1];

	/* 1. get CRECORD array from ocm */
	if (get_crecords(dcm))
		return -EBUSY;

	/* 2. get xinfo located in CRECORD.xinfo_addr */
	for (i = 0; i < info->count; i++) {
		if (!is_sram_available(dcm)) {
			dev_err(dcm->dev, "dcm is busy\n");
			return -EBUSY;
		}

		addr1 = (crec->xinfo_addr >> 8) & 0xff;
		addr2 = (crec->xinfo_addr) & 0xff;
		len = sizeof(struct om_xinfo);

		if (run_program(dcm, 0, 6, OM_GETMEMX, addr1,
				addr2, len, DATA_ADDR, OM_END)) {
			dev_err(dcm->dev, "get xinfo error\n");
			return -EBUSY;
		}

		if (copy_from_sram(dcm, DATA_ADDR, &xinfo, len)) {
			dev_err(dcm->dev, "could not copy xinfo data\n");
			return -EBUSY;
		}

		/* 3. get record name in struct xinfo */
		if (!is_sram_available(dcm)) {
			dev_err(dcm->dev, "dcm is busy\n");
			return -EBUSY;
		}

		addr1 = (xinfo.name_addr >> 8) & 0xff;
		addr2 = (xinfo.name_addr) & 0xff;
		len = MAX_NAME_LEN;

		if (run_program(dcm, 0, 6, OM_GETMEMX, addr1,
				addr2, len, DATA_ADDR, OM_END)) {
			dev_err(dcm->dev, "\n");
			return -EBUSY;
		}

		if (copy_from_sram(dcm, DATA_ADDR, name, len)) {
			dev_err(dcm->dev, "could not copy record name data\n");
			return -EBUSY;
		}

		name[MAX_NAME_LEN] = 0;
		rec->name = kstrdup(name, GFP_KERNEL);

		/* assign the unit according record type */
		if ((crec->ctl & CRCTL_GET_V) || (crec->ctl & CRCTL_GET_V2)) {
			rec->unit = "mV";
		} else if (crec->ctl & CRCTL_GET_T) {
			rec->unit = "C ";
		} else if (crec->ctl & CRCTL_GET_I2) {
			rec->unit = "mA";
		} else {
			dev_err(dcm->dev, "Unknown record\n");
			return -EBUSY;
		}

		/* deal with next record */
		rec++;
		crec++;
	}

	return 0;
}

/* .data is shunt value(in uOhms) of ina220 */
static struct of_device_id fsl_dcm_ids[] = {
	{ .compatible = "fsl,p1022ds-fpga", .data = 0},
	{ .compatible = "fsl,p5020ds-fpga", .data = (void *)2106},
	{ .compatible = "fsl,tetra-fpga", .data = (void *)1000},
	{}
};

static int fsl_dcm_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct fsl_dcm_data *dcm;
	const struct of_device_id *match;
	int ret;
	u8 ver;

	dcm = kzalloc(sizeof(*dcm), GFP_KERNEL);
	if (!dcm)
		return -ENOMEM;

	dcm->base = of_iomap(np, 0);
	if (!dcm->base) {
		dev_err(&pdev->dev, "could not map fpga node\n");
		ret = -ENOMEM;
		goto err_kzalloc;
	}

	/*
	 * Get GMSA version
	 *
	 * write 0x1F to GDC register then read GDD register
	 * 0x00: v1  -> pixis
	 * 0x01: v2  -> qixis
	 */
	out_8(dcm->base + 0x16, 0x1F);
	ver = in_8(dcm->base + 0x17);
	if (ver == 0x0) {
		dcm->addr = dcm->base + 0x0a;
		dcm->data = dcm->base + 0x0d;
	} else if (ver == 0x01) {
		dcm->addr = dcm->base + 0x12;
		dcm->data = dcm->base + 0x13;
	}
	dcm->ocmd = dcm->base + 0x14;
	dcm->omsg = dcm->base + 0x15;
	dcm->mack = dcm->base + 0x18;

	/* Check to make sure the DCM is enable and working */
	if (!is_sram_available(dcm)) {
		dev_err(&pdev->dev, "dcm is not responding\n");
		ret = -EBUSY;
		goto err_iomap;
	}

	dcm->dev = &pdev->dev;
	dev_set_drvdata(&pdev->dev, dcm);

	/* get the struct om_info */
	if (dcm_get_info(&pdev->dev)) {
		dev_err(&pdev->dev, "could not get struct om_info\n");
		ret = -EBUSY;
		goto err_iomap;
	}

	/* only support v41 or later */
	if (dcm->board.info.ver < 41 || dcm->board.info.ver == 0xff) {
		dev_err(&pdev->dev, "dcm is invalid or needs to update\n");
		ret = -ENODEV;
		goto err_iomap;
	}

	dcm->board.rec = kzalloc(sizeof(*dcm->board.rec) *
			dcm->board.info.count, GFP_KERNEL);
	if (!dcm->board.rec) {
		dev_err(&pdev->dev, "no memory\n");
		ret = -ENOMEM;
		goto err_iomap;
	}

	dcm->board.crec = kzalloc(sizeof(*dcm->board.crec) *
			dcm->board.info.count, GFP_KERNEL);
	if (!dcm->board.crec) {
		dev_err(&pdev->dev, "no memory\n");
		ret = -ENOMEM;
		goto err_kzall;
	}

	/* get the shunt value */
	match = of_match_node(fsl_dcm_ids, np);
	dcm->board.shunt = (long)match->data;

	/* enable all the channel */
	dcm->board.mask = 0x1FF;

	/* init all the board specific data */
	ret = board_data_init(dcm);
	if (ret) {
		dev_err(&pdev->dev, "could not create sysfs group\n");
		ret = -ENODEV;
		goto err_kzall2;
	}

	/* enable all the channel */
	if (select_dcm_channels(dcm, dcm->board.mask)) {
		dev_err(&pdev->dev, "could not set crecord mask\n");
		ret = -ENODEV;
		goto err_kzall2;
	}

	/* Set the timer to the 1 Hz */
	if (set_dcm_frequency(dcm, 1)) {
		dev_err(&pdev->dev, "could not set frequency\n");
		ret = -ENODEV;
		goto err_kzall2;
	}

	/* create sysfs interface */
	ret = sysfs_create_group(&pdev->dev.kobj, &fsl_dcm_attr_group);
	if (ret) {
		dev_err(&pdev->dev, "could not create sysfs group\n");
		goto err_kzall2;
	}

	return 0;

err_kzall2:
	kfree(dcm->board.crec);

err_kzall:
	kfree(dcm->board.rec);

err_iomap:
	iounmap(dcm->base);

err_kzalloc:
	kfree(dcm);

	return ret;
}

static int fsl_dcm_remove(struct platform_device *pdev)
{
	struct fsl_dcm_data *dcm = dev_get_drvdata(&pdev->dev);

	stop_data_collection(dcm);
	sysfs_remove_group(&pdev->dev.kobj, &fsl_dcm_attr_group);
	iounmap(dcm->base);
	kfree(dcm->board.rec);
	kfree(dcm->board.crec);
	kfree(dcm);

	return 0;
}

static struct platform_driver fsl_dcm_driver = {
	.driver = {
		.name = "fsl-dcm-driver",
		.owner = THIS_MODULE,
	},
	.probe = fsl_dcm_probe,
	.remove = fsl_dcm_remove,
};

static int __init fsl_dcm_init(void)
{
	struct device_node *np;
	struct platform_device *pdev;
	int ret;

	np = of_find_matching_node(NULL, fsl_dcm_ids);
	if (!np)
		return -ENODEV;

	/* We found a supported platform, so register a platform driver */
	ret = platform_driver_register(&fsl_dcm_driver);
	if (ret) {
		pr_err("fsl-dcm: could not register platform driver\n");
		goto err_np;
	}

	/* We need to create a device and add the data for this platform */
	pdev = platform_device_alloc(fsl_dcm_driver.driver.name, 0);
	if (!pdev) {
		ret = -ENOMEM;
		goto err_drv;
	}

	/* Pass the device_node pointer to the probe function */
	pdev->dev.of_node = np;

	/* This will call the probe function */
	ret = platform_device_add(pdev);
	if (ret) {
		pr_err("fsl-dcm: could not register platform driver\n");
		goto err_dev;
	}

	pr_info("Freescale Data Collection Module is installed.\n");

	return 0;

err_dev:
	platform_device_unregister(pdev);

err_drv:
	platform_driver_unregister(&fsl_dcm_driver);

err_np:
	of_node_put(np);

	return ret;
}

static void __exit fsl_dcm_exit(void)
{
	platform_driver_unregister(&fsl_dcm_driver);
}

MODULE_AUTHOR("Timur Tabi <timur@freescale.com>");
MODULE_DESCRIPTION("Freescale Data Collection Manager driver");
MODULE_LICENSE("GPL v2");

module_init(fsl_dcm_init);
module_exit(fsl_dcm_exit);
