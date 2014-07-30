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


#define DPA_CAPWAP_CDEV "fsl-capwap"

#define DPA_CAPWAP_IOC_MAGIC	0xee

#define DPA_CAPWAP_IOC_DOMAIN_INIT \
		_IOWR(DPA_CAPWAP_IOC_MAGIC, 0, struct dpaa_capwap_domain_params)

#define DPA_CAPWAP_IOC_DOMAIN_ADD_IN_TUNNEL \
		_IOWR(DPA_CAPWAP_IOC_MAGIC, 1, \
				struct dpaa_capwap_domain_tunnel_in_params)

#define DPA_CAPWAP_IOC_DOMAIN_ADD_OUT_TUNNEL \
		_IOWR(DPA_CAPWAP_IOC_MAGIC, 2, \
				struct dpaa_capwap_domain_tunnel_out_params)

#define DPA_CAPWAP_IOC_DOMAIN_GET_FQIDS \
		_IOWR(DPA_CAPWAP_IOC_MAGIC, 3, struct dpaa_capwap_domain_fqs)

#define DPA_CAPWAP_IOC_DOMAIN_REMOVE_TUNNEL \
		_IOWR(DPA_CAPWAP_IOC_MAGIC, 4, void *)

#define DPA_CAPWAP_IOC_DOMAIN_KERNAL_RX_CTL \
		_IOWR(DPA_CAPWAP_IOC_MAGIC, 5, \
				struct capwap_domain_kernel_rx_ctl)
