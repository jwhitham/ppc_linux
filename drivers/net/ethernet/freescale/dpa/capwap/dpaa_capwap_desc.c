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

#include <linux/version.h>
#include <linux/platform_device.h>

#include "compat.h"
#include "desc.h"
#include "error.h"
#include "jr.h"
#include "ctrl.h"

#include "dpaa_capwap_desc.h"

/* If SEC ERA is unknown default to this value */
#define SEC_DEF_ERA	2 /* like in P4080 */

/* to retrieve a 256 byte aligned buffer address from an address
 * we need to copy only the first 7 bytes
 */
#define ALIGNED_PTR_ADDRESS_SZ	(CAAM_PTR_SZ - 1)

#define JOB_DESC_HDR_LEN	CAAM_CMD_SZ
#define SEQ_OUT_PTR_SGF_MASK	0x01000000;
/* relative offset where the input pointer should be updated in the descriptor*/
#define IN_PTR_REL_OFF		4 /* words from current location */
/* dummy pointer value */
#define DUMMY_PTR_VAL		0x00000000
#define PTR_LEN			2	/* Descriptor is created only for 8 byte
					 * pointer. PTR_LEN is in words.
					 */

static const struct of_device_id sec_jr_match[] = {
	{
	 .compatible = "fsl,sec-v4.0-job-ring"
	}
};

static struct device *get_jrdev(void)
{
	struct device_node *sec_jr_node;
	struct platform_device *sec_of_jr_dev;

	sec_jr_node = of_find_matching_node(NULL, &sec_jr_match[0]);
	if (!sec_jr_node) {
		pr_err("Couln't find the device_node SEC job-ring, check the device tree\n");
		return NULL;
	}

	sec_of_jr_dev = of_find_device_by_node(sec_jr_node);
	if (!sec_of_jr_dev) {
		pr_err("SEC job-ring of_device null\n");
		return NULL;
	}

	return &sec_of_jr_dev->dev;
}

/* retrieve and store SEC information */
int get_sec_info(struct dpaa_capwap_sec_info *secinfo)
{
	struct device_node *sec_node;
	const u32 *sec_era;
	int prop_size;

	sec_node = of_find_compatible_node(NULL, NULL, "fsl,sec-v5.3");
	if (sec_node)
		secinfo->sec_ver = SEC_VER_5_3;
	else {
		secinfo->sec_ver = SEC_DEF_VER;
		sec_node = of_find_compatible_node(NULL, NULL, "fsl,sec-v4.0");
		if (!sec_node) {
			pr_err("Can't find device node for SEC! Check device tree!\n");
			return -ENODEV;
		}
	}

	sec_era = of_get_property(sec_node, "fsl,sec-era", &prop_size);
	if (sec_era && prop_size == sizeof(*sec_era) && *sec_era > 0)
		secinfo->sec_era = *sec_era;
	else
		secinfo->sec_era = SEC_DEF_ERA;

	secinfo->jrdev = get_jrdev();
	if (!secinfo->jrdev)
		return -ENODEV;

	return 0;
}

void cnstr_shdsc_dtls_encap(uint32_t *desc,
					   uint16_t *bufsize,
					   struct cipher_params *cipherdata,
					   struct auth_params *authdata,
					   uint32_t data_move_size)
{
	uint32_t *key_jump;

	init_sh_desc_pdb(desc, HDR_SAVECTX | HDR_SHARE_SERIAL,
			sizeof(struct dtls_block_encap_pdb));
	if (data_move_size) {
		append_seq_fifo_load(desc, data_move_size, FIFOLD_CLASS_BOTH |
				FIFOLD_TYPE_NOINFOFIFO);
		append_seq_fifo_store(desc, FIFOST_TYPE_META_DATA |
				FIFOST_AUX_TYPE0, data_move_size);
	}
	key_jump = append_jump(desc, JUMP_TYPE_LOCAL | CLASS_BOTH |
				JUMP_TEST_ALL | JUMP_COND_SHRD);
	/* Append split authentication key */
	append_key_as_imm(desc, authdata->split_key,
			authdata->split_key_pad_len,
			authdata->split_key_len,
			CLASS_2 | KEY_ENC | KEY_DEST_MDHA_SPLIT);
	/* Append cipher key */
	append_key_as_imm(desc, cipherdata->cipher_key,
			cipherdata->cipher_key_len, cipherdata->cipher_key_len,
		   CLASS_1 | KEY_DEST_CLASS_REG);
	set_jump_tgt_here(desc, key_jump);

	/* Protocol specific operation */
	append_operation(desc, OP_PCLID_DTLS | OP_TYPE_ENCAP_PROTOCOL |
		 cipherdata->cipher_type);

	*bufsize = desc_len(desc);
}

void cnstr_shdsc_dtls_decap(uint32_t *desc,
					   uint16_t *bufsize,
					   struct cipher_params *cipherdata,
					   struct auth_params *authdata,
					   uint32_t data_move_size)
{
	uint32_t *key_jump;

	init_sh_desc_pdb(desc, HDR_SAVECTX | HDR_SHARE_SERIAL,
			sizeof(struct dtls_block_decap_pdb));
	if (data_move_size) {
		append_seq_fifo_load(desc, data_move_size, FIFOLD_CLASS_BOTH |
				     FIFOLD_TYPE_NOINFOFIFO);
		append_seq_fifo_store(desc, FIFOST_TYPE_META_DATA |
				FIFOST_AUX_TYPE0, data_move_size);
	}
	key_jump = append_jump(desc, JUMP_TYPE_LOCAL | CLASS_BOTH |
				JUMP_TEST_ALL | JUMP_COND_SHRD);
	/* Append split authentication key */
	append_key_as_imm(desc, authdata->split_key,
			authdata->split_key_pad_len,
			authdata->split_key_len,
			CLASS_2 | KEY_ENC | KEY_DEST_MDHA_SPLIT);
	/* Append cipher key */
	append_key_as_imm(desc, cipherdata->cipher_key,
			cipherdata->cipher_key_len, cipherdata->cipher_key_len,
		   CLASS_1 | KEY_DEST_CLASS_REG);
	set_jump_tgt_here(desc, key_jump);

	/* Protocol specific operation */
	append_operation(desc, OP_PCLID_DTLS | OP_TYPE_DECAP_PROTOCOL |
		 cipherdata->cipher_type);

	*bufsize = desc_len(desc);
}

static void split_key_done(struct device *dev, u32 *desc, u32 err,
			   void *context)
{
	atomic_t *done = context;

	if (err)
		caam_jr_strstatus(dev, err);
	atomic_set(done, 1);
}

int generate_split_key(struct auth_params *auth_param,
			struct dpaa_capwap_sec_info *secinfo)
{
	struct device *jrdev;
	dma_addr_t dma_addr_in, dma_addr_out;
	u32 *desc, timeout = 1000000;
	atomic_t done;
	int ret = 0;

	/* Sizes for MDHA pads (*not* keys): MD5, SHA1, 224, 256, 384, 512
	 * Running digest size
	 */
	const u8 mdpadlen[] = {16, 20, 32, 32, 64, 64};

	jrdev = secinfo->jrdev;

	desc = kmalloc(CAAM_CMD_SZ * 6 + CAAM_PTR_SZ * 2, GFP_KERNEL | GFP_DMA);
	if (!desc) {
		dev_err(jrdev, "Allocate memory failed for split key desc\n");
		return -ENOMEM;
	}

	auth_param->split_key_len = mdpadlen[(auth_param->auth_type &
				OP_ALG_ALGSEL_SUBMASK) >>
				OP_ALG_ALGSEL_SHIFT] * 2;
	auth_param->split_key_pad_len = ALIGN(auth_param->split_key_len, 16);

	dma_addr_in = dma_map_single(jrdev, auth_param->auth_key,
				     auth_param->auth_key_len, DMA_TO_DEVICE);
	if (dma_mapping_error(jrdev, dma_addr_in)) {
		dev_err(jrdev, "Unable to DMA map the input key address\n");
		kfree(desc);
		return -ENOMEM;
	}

	dma_addr_out = dma_map_single(jrdev, auth_param->split_key,
				      auth_param->split_key_pad_len,
				      DMA_FROM_DEVICE);
	if (dma_mapping_error(jrdev, dma_addr_out)) {
		dev_err(jrdev, "Unable to DMA map the output key address\n");
		dma_unmap_single(jrdev, dma_addr_in, auth_param->auth_key_len,
				 DMA_TO_DEVICE);
		kfree(desc);
		return -ENOMEM;
	}

	init_job_desc(desc, 0);

	append_key(desc, dma_addr_in, auth_param->auth_key_len,
		   CLASS_2 | KEY_DEST_CLASS_REG);

	/* Sets MDHA up into an HMAC-INIT */
	append_operation(desc, (OP_ALG_TYPE_CLASS2 << OP_ALG_TYPE_SHIFT) |
			 auth_param->auth_type | OP_ALG_AAI_HMAC |
			OP_ALG_DECRYPT | OP_ALG_AS_INIT);

	/* Do a FIFO_LOAD of zero, this will trigger the internal key expansion
	 * into both pads inside MDHA
	 */
	append_fifo_load_as_imm(desc, NULL, 0, LDST_CLASS_2_CCB |
				FIFOLD_TYPE_MSG | FIFOLD_TYPE_LAST2);

	/* FIFO_STORE with the explicit split-key content store
	 * (0x26 output type)
	 */
	append_fifo_store(desc, dma_addr_out, auth_param->split_key_len,
			  LDST_CLASS_2_CCB | FIFOST_TYPE_SPLIT_KEK);

	atomic_set(&done, 0);
	ret = caam_jr_enqueue(jrdev, desc, split_key_done, &done);

	while (!atomic_read(&done) && --timeout) {
		udelay(1);
		cpu_relax();
	}

	if (timeout == 0)
		dev_err(jrdev, "Timeout waiting for job ring to complete\n");

	dma_unmap_single(jrdev, dma_addr_out, auth_param->split_key_pad_len,
			 DMA_FROM_DEVICE);
	dma_unmap_single(jrdev, dma_addr_in, auth_param->auth_key_len,
			 DMA_TO_DEVICE);
	kfree(desc);
	return ret;
}
