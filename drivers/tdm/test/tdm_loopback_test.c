/*
 *
 * Copyright 2011-2012 Freescale Semiconductor, Inc.
 *
 * TDM Loopback Test Module.
 * This TDM test module is a small test module which registers with the
 * TDM framework and transfer and receive data in loopback mode and also
 * compares if the data sent is received correctly.
 *
 * Author: Sandeep Kumar Singh <sandeep@freescale.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the  GNU General Public License along
 * with this program; if not, write  to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <linux/module.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/wait.h>
#include <linux/param.h>
#include <linux/tdm.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/tdm.h>


#define DRV_DESC "Test Module for Freescale Platforms with TDM support"
#define DRV_NAME "tdm_test"


#define POLL_COUNT		15
#define TDM_FRAME_LENGTH	NUM_SAMPLES_PER_FRAME
#define TDM_E_OK		0
#define BUF_COUNT		5

#define DEBUG			0

static int in_loopback = 1;
module_param(in_loopback, int, 0);
MODULE_PARM_DESC(in_loopback, "Loopback mode, in_loopback=1(default) internal , in_loopback=0 external(SLIC)");

static int poll_count = POLL_COUNT;
module_param(poll_count, int, 0);

static struct task_struct *tdm_thread_task;
static struct tdm_driver test_tdmdev_driver;
static int tdm_thread_state;

static int tdm_check_data(unsigned short *tx_data, unsigned short *rx_data)
{
	int i, error = 0;
	int offset;

	if (in_loopback)
		offset = 0;
	else
		offset = 2;

	for (i = 1; i + offset < TDM_FRAME_LENGTH; i++) {
		if (tx_data[i] != rx_data[i + offset])
			error++;
		else
			continue;
	}
#if DEBUG
	static int call_count;
	pr_info("Iteration: %d\n", ++call_count);
	pr_info("TX DATA:\n");
	for (i = 0; i < TDM_FRAME_LENGTH; i++) {
		pr_info("%5x ", tx_data[i]);
		if (i%20 == 19)
			pr_info("\n");
	}
	pr_info("RX DATA:\n");
	for (i = 0; i < TDM_FRAME_LENGTH-offset; i++) {
		pr_info("%5x ", rx_data[i+offset]);
		if (i%20 == 19)
			pr_info("\n");
	}
	if (error)
		pr_info("TX and RX buffer do NOT match. Err_count:"
				"%d\n", error);
	else
		pr_info("TX and RX buffer MATCH\n");
#endif

	return error;
}


static int tdm_thread(void *ptr)
{
	void *h_port;
	void *h_channel1;
	int ret = TDM_E_OK;
	int poll = 0;
	int i = 0, j = 0;
	int index = 0;
	int error_count = 0;
	int buffer_size;
	uint16_t size = TDM_FRAME_LENGTH;
	u16 ch1_id = 0;
	unsigned short *tx_data[BUF_COUNT];
	unsigned short *rx_data[BUF_COUNT];

	tdm_thread_state = 1;

	/* Open port */
	ret = tdm_port_open(&test_tdmdev_driver, &h_port);
	pr_debug("%s tdm_port_open ret = %d\n", __func__, ret);
	if ((ret != TDM_E_OK) || (h_port == NULL)) {
		pr_err("Error in tdm_port_open- ret %x\n", ret);
		goto port1_failed;
	}
	/* Open Channel 1*/
	ret = tdm_channel_open(ch1_id, 1, h_port, &h_channel1);
	if ((ret != TDM_E_OK) || (h_channel1 == NULL)) {
		pr_err("Error in tdm_channel_open(%d)- ret %x\n", ch1_id, ret);
		goto ch1_failed;
	}

	buffer_size = sizeof(unsigned short)*BUF_COUNT*TDM_FRAME_LENGTH;
	tx_data[0] = kmalloc(buffer_size, GFP_KERNEL);
	rx_data[0] = kmalloc(buffer_size, GFP_KERNEL);

	if ((tx_data[0] == NULL) || (rx_data[0] == NULL)) {
		pr_err("Failed to get memory for buffer");
		return 0;
	}
	memset(tx_data[0], 0, buffer_size);
	memset(rx_data[0], 0, buffer_size);
	for (i = 0; i < BUF_COUNT-1; i++) {
		tx_data[i+1] = tx_data[i] + TDM_FRAME_LENGTH;
		rx_data[i+1] = rx_data[i] + TDM_FRAME_LENGTH;
	}
	while ((poll < poll_count) && !kthread_should_stop()) {

		poll++;
		ret = tdm_ch_poll(h_channel1, 10);
		if (ret != TDM_E_OK)
			continue;
		for (i = 0; i < TDM_FRAME_LENGTH; i++)
			tx_data[index][i] = j*TDM_FRAME_LENGTH + i;

		ret = tdm_channel_write(h_port, h_channel1, tx_data[index],
					size);
		if (ret != TDM_E_OK)
			pr_info("Error in tdm_channel_write\n");
		ret = tdm_channel_read(h_port, h_channel1, rx_data[index],
					&size);
		if (ret != TDM_E_OK)
			pr_info("Error in tdm_channel_read\n");
		/*
		 * There is a delay of 6 frame  between transmitted data and
		 * received data. Hence we compare tx_data[0] with rx data[6]
		 * and so on
		 */
		if (++j >= BUF_COUNT)
			error_count += tdm_check_data(tx_data[(index + 1)
					%BUF_COUNT], rx_data[index]);
		index++;
		index = index%BUF_COUNT;

	}
	pr_info("TDM Loppback test completed.\n");
	if ((error_count == 0) && (j != 0))
		pr_info("%s TDM loopback test PASSED!\n", in_loopback ?
				"Internal" : "External");
	else
		pr_info("%s TDM loopback test FAILED!\n", in_loopback ?
				"Internal" : "External");

	ret = tdm_channel_close(ch1_id, 1, h_port, h_channel1);
	if (ret != TDM_E_OK) {
		pr_err("Error in tdm_channel_close(%d)- ret %x\n", ch1_id, ret);
		ret = -ENXIO;
	}

	ret = tdm_port_close(h_port);
	pr_debug("%s tdm_port_close ret = %d\n", __func__, ret);
	if (ret != TDM_E_OK) {
		pr_err("Error in tdm_port_close- ret %x\n", ret);
		ret = -ENXIO;
	}

	tdm_thread_state = 0;

	return ret;

ch1_failed:
	tdm_channel_close(ch1_id, 1, h_port, h_channel1);
port1_failed:
	tdm_port_close(h_port);
	return -ENXIO;
}

static int test_attach_adapter(struct tdm_adapter *adap)
{
	tdm_thread_state = 0;
	tdm_thread_task = kthread_run(tdm_thread, NULL, "tdm_thread");

	return 0;
}

static int test_detach_adapter(struct tdm_adapter *adap)
{
	if (tdm_thread_state)
		kthread_stop(tdm_thread_task);

	return 0;
}

static const struct tdm_device_id tdm_loopback_test_id[] = {
	{ "fsl_tdm", 0 },
	{ }
};

static struct tdm_driver test_tdmdev_driver = {
	.attach_adapter	= test_attach_adapter,
	.detach_adapter	= test_detach_adapter,
	.id_table	= tdm_loopback_test_id,
};

static int __init tdm_loopback_test_init(void)
{
	int ret;
	pr_info("TDM LOOPBACK TEST: \n");
	test_tdmdev_driver.id = 1;

	/* create a binding with TDM driver */
	ret = tdm_add_driver(&test_tdmdev_driver);
	if (ret == 0)
		pr_info("TDM LOOPBACK TEST module installed\n");
	else
		pr_err("%s tdm_port_init failed\n", __func__);
	return ret;
}

static void __exit tdm_loopback_test_exit(void)
{
	tdm_unregister_driver(&test_tdmdev_driver);
	pr_info("TDM LOOPBACK TEST module un-installed\n");
}

module_init(tdm_loopback_test_init);
module_exit(tdm_loopback_test_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sandeep Kumar Singh <sandeep@freescale.com>");
MODULE_DESCRIPTION("Test Loopback Test Module for Freescale Platforms"
" with TDM support");
