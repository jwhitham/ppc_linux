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

#ifndef __DPAA_CAPWAP_DESC_H__
#define __DPAA_CAPWAP_DESC_H__

#include "pdb.h"
#include "desc_constr.h"

/* DPA CAPWAP Cipher Parameters */
struct cipher_params {
	uint16_t cipher_type;	 /* Algorithm type as defined by SEC driver   */
	uint8_t *cipher_key;	 /* Address to the encryption key	      */
	uint32_t cipher_key_len; /* Length in bytes of the normal key         */
};

/* DPA CAPWAP Authentication Parameters */
struct auth_params {
	uint32_t auth_type;	/* Algorithm type as defined by SEC driver    */
	uint8_t *auth_key;	/* Address to the normal key		      */
	uint32_t auth_key_len;	/* Length in bytes of the normal key          */
	uint8_t *split_key;	/* Address to the generated split key         */
	uint32_t split_key_len;	/* Length in bytes of the split key           */
	uint32_t split_key_pad_len;/* Length in bytes of the padded split key */
};

struct dpaa_capwap_sec_info {
	int sec_era; /* SEC ERA information */
	int sec_ver; /* SEC version information */
	struct device *jrdev; /* Job ring device */
};

/* DPA CAPWAP cipher & authentication algorithm identifiers */
struct capwap_alg_suite {
	uint32_t        auth_alg;
	uint32_t        cipher_alg;
};

#define CAPWAP_ALGS_ENTRY(auth, cipher)	{\
	.auth_alg = OP_ALG_ALGSEL_ ## auth,\
	.cipher_alg = OP_PCL_DTLS_ ## cipher\
}

#define CAPWAP_ALGS	{\
	CAPWAP_ALGS_ENTRY(SHA1, DES_CBC_SHA_2),		\
	CAPWAP_ALGS_ENTRY(MD5, DES_CBC_MD5),		\
	CAPWAP_ALGS_ENTRY(MD5, 3DES_EDE_CBC_MD5),	\
	CAPWAP_ALGS_ENTRY(SHA1, 3DES_EDE_CBC_SHA160),	\
	CAPWAP_ALGS_ENTRY(SHA384, 3DES_EDE_CBC_SHA384),	\
	CAPWAP_ALGS_ENTRY(SHA224, 3DES_EDE_CBC_SHA224),	\
	CAPWAP_ALGS_ENTRY(SHA512, 3DES_EDE_CBC_SHA512),	\
	CAPWAP_ALGS_ENTRY(SHA256, 3DES_EDE_CBC_SHA256),	\
	CAPWAP_ALGS_ENTRY(SHA1, AES_256_CBC_SHA160),	\
	CAPWAP_ALGS_ENTRY(SHA384, AES_256_CBC_SHA384),	\
	CAPWAP_ALGS_ENTRY(SHA224, AES_256_CBC_SHA224),	\
	CAPWAP_ALGS_ENTRY(SHA512, AES_256_CBC_SHA512),	\
	CAPWAP_ALGS_ENTRY(SHA256, AES_256_CBC_SHA256),	\
	CAPWAP_ALGS_ENTRY(SHA1, AES_128_CBC_SHA160),	\
	CAPWAP_ALGS_ENTRY(SHA384, AES_128_CBC_SHA384),	\
	CAPWAP_ALGS_ENTRY(SHA224, AES_128_CBC_SHA224),	\
	CAPWAP_ALGS_ENTRY(SHA512, AES_128_CBC_SHA512),	\
	CAPWAP_ALGS_ENTRY(SHA256, AES_128_CBC_SHA256),	\
	CAPWAP_ALGS_ENTRY(SHA1, AES_192_CBC_SHA160),	\
	CAPWAP_ALGS_ENTRY(SHA384, AES_192_CBC_SHA384),	\
	CAPWAP_ALGS_ENTRY(SHA224, AES_192_CBC_SHA224),	\
	CAPWAP_ALGS_ENTRY(SHA512, AES_192_CBC_SHA512),	\
	CAPWAP_ALGS_ENTRY(SHA256, AES_192_CBC_SHA256)	\
}

struct preheader_t {
	union {
		uint32_t word;
		struct {
			unsigned int rsls:1;
			unsigned int rsvd1_15:15;
			unsigned int rsvd16_24:9;
			unsigned int idlen:7;
		} field;
	} __packed hi;

	union {
		uint32_t word;
		struct {
			unsigned int rsvd32_33:2;
			unsigned int fsgt:1;
			unsigned int lng:1;
			unsigned int offset:2;
			unsigned int abs:1;
			unsigned int add_buf:1;
			uint8_t pool_id;
			uint16_t pool_buffer_size;
		} field;
	} __packed lo;
} __packed;

struct init_descriptor_header_t {
	union {
		uint32_t word;
		struct {
			unsigned int ctype:5;
			unsigned int rsvd5_6:2;
			unsigned int dnr:1;
			unsigned int one:1;
			unsigned int rsvd9:1;
			unsigned int start_idx:6;
			unsigned int zro:1;
			unsigned int rsvd17_18:2;
			unsigned int sc:1;
			unsigned int propogate_dnr:1;
			unsigned int rsvd21:1;
			unsigned int share:2;
			unsigned int rsvd24_25:2;
			unsigned int desc_len:6;
		} field;
	} __packed command;
} __packed;

struct dtls_encap_descriptor_t {
	struct preheader_t prehdr;
	struct init_descriptor_header_t deschdr;
	struct dtls_block_encap_pdb pdb;
	/* DCL library will fill following info */
	uint32_t data_move_cmd[3];	/* For Storing Data Move Cmd */
	uint32_t jump_cmd;		/* For Storing Jump Command */
	uint32_t auth_key[13];		/* Max Space for storing auth Key */
	uint32_t enc_key[7];		/* Max Space for storing enc Key */
	uint32_t operation_cmd;		/* For operation Command */
} __packed __aligned(64);

struct dtls_decap_descriptor_t {
	struct preheader_t prehdr;
	struct init_descriptor_header_t deschdr;
	struct dtls_block_decap_pdb pdb;
	/* DCL library will fill following info */
	uint32_t data_move_cmd[3];	/* For Storing Data Move Cmd */
	uint32_t jump_cmd;		/* For Storing Jump Command */
	uint32_t auth_key[13];		/* Max Space for storing auth Key */
	uint32_t dec_key[7];		/* Max Space for storing dec Key */
	uint32_t operation_cmd;		/* For operation Command */
} __packed __aligned(64);

#define SEC_DEF_VER 40 /* like in P4080 */
#define SEC_VER_5_3 53

int get_sec_info(struct dpaa_capwap_sec_info *secinfo);

int generate_split_key(struct auth_params *auth_param,
			struct dpaa_capwap_sec_info *secinfo);

void cnstr_shdsc_dtls_decap(uint32_t *desc,
					   uint16_t *bufsize,
					   struct cipher_params *cipherdata,
					   struct auth_params *authdata,
					   uint32_t data_move_size);

void cnstr_shdsc_dtls_encap(uint32_t *desc,
					   uint16_t *bufsize,
					   struct cipher_params *cipherdata,
					   struct auth_params *authdata,
					   uint32_t data_move_size);

#endif /* __DPAA_CAPWAP_DESC_H__ */
