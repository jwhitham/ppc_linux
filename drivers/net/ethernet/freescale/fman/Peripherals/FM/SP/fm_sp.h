/*
 * Copyright 2008-2012 Freescale Semiconductor Inc.
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


/******************************************************************************
 @File          fm_sp.h

 @Description   FM SP  ...
*//***************************************************************************/
#ifndef __FM_SP_H
#define __FM_SP_H

#include "std_ext.h"
#include "error_ext.h"
#include "list_ext.h"

#include "fm_sp_common.h"
#include "fm_common.h"


#define __ERR_MODULE__  MODULE_FM_SP



/***********************************************************************/
/*          Memory map                                                 */
/***********************************************************************/
#if defined(__MWERKS__) && !defined(__GNUC__)
#pragma pack(push,1)
#endif /* defined(__MWERKS__) && ... */

typedef _Packed struct {
    volatile uint32_t   fm_sp_ebmpi[FM_PORT_MAX_NUM_OF_EXT_POOLS];
                                         /*offset 0 - 0xc*/
                                         /**< Buffer Manager pool Information-*/

    volatile uint32_t   res[8-FM_PORT_MAX_NUM_OF_EXT_POOLS];
                                         /*offset 0x10 - 0xc*/
    volatile uint32_t   fm_sp_acnt;      /*offset 0x20*/
    volatile uint32_t   fm_sp_ebm;       /*offset 0x24*/
    volatile uint32_t   fm_sp_da;        /*offset 0x28*/
    volatile uint32_t   fm_sp_icp;       /*offset 0x2c*/
    volatile uint32_t   fm_sp_mpd;       /*offset 0x30*/
    volatile uint32_t   res1[2];         /*offset 0x34 - 0x38*/
    volatile uint32_t   fm_sp_spliodn;   /*offset 0x3c*/
} _PackedType fm_pcd_storage_profile_regs;

#if defined(__MWERKS__) && !defined(__GNUC__)
#pragma pack(pop)
#endif /* defined(__MWERKS__) && ... */


typedef struct fm_storage_profile_params {
    t_FmExtPools                *fm_ext_pools;
    t_FmBackupBmPools           *backup_pools;
    t_FmSpIntContextDataCopy    *int_context;
    t_FmSpBufMargins            *buf_margins;

    e_FmDmaSwapOption           dma_swap_data;
    e_FmDmaCacheOption          int_context_cache_attr;
    e_FmDmaCacheOption          header_cache_attr;
    e_FmDmaCacheOption          scatter_gather_cache_attr;
    bool                        dma_write_optimize;
    uint16_t                    liodn_offset;
    bool                        no_scather_gather;
    t_FmBufPoolDepletion        *buf_pool_depletion;
} fm_storage_profile_params;

typedef struct {
    t_FmBufferPrefixContent             bufferPrefixContent;
    e_FmDmaSwapOption                   dmaSwapData;
    e_FmDmaCacheOption                  dmaIntContextCacheAttr;
    e_FmDmaCacheOption                  dmaHeaderCacheAttr;
    e_FmDmaCacheOption                  dmaScatterGatherCacheAttr;
    bool                                dmaWriteOptimize;
    uint16_t                            liodnOffset;
    bool                                noScatherGather;
    t_FmBufPoolDepletion                *p_BufPoolDepletion;
    t_FmBackupBmPools                   *p_BackupBmPools;
    t_FmExtPools                        extBufPools;
} t_FmVspEntryDriverParams;

typedef struct {
    bool                        valid;
    volatile bool               lock;
    uint8_t                     pointedOwners;
    uint16_t                    absoluteSpId;
    uint8_t                     internalBufferOffset;
    t_FmSpBufMargins            bufMargins;
    t_FmSpIntContextDataCopy    intContext;
    t_FmSpBufferOffsets         bufferOffsets;
    t_Handle                    h_Fm;
    e_FmPortType                portType;           /**< Port type */
    uint8_t                     portId;             /**< Port Id - relative to type */
    uint8_t                     relativeProfileId;
    fm_pcd_storage_profile_regs *p_FmSpRegsBase;
    t_FmExtPools                extBufPools;
    t_FmVspEntryDriverParams    *p_FmVspEntryDriverParams;
} t_FmVspEntry;


#endif /* __FM_SP_H */
