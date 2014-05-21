/*
 * Freescale deep sleep FSM (finite-state machine) configuration
 *
 * Copyright 2014 Freescale Semiconductor Inc.
 *
 * Author: Hongbo Zhang <hongbo.zhang@freescale.com>
 *         Chenhui Zhao <chenhui.zhao@freescale.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/io.h>
#include <linux/types.h>

#define FSL_STRIDE_4B	4
#define FSL_STRIDE_8B	8

/* End flag */
#define FSM_END_FLAG		0xFFFFFFFFUL

/* EPGCR (Event Processor Global Control Register) */
#define EPGCR		0x000

/* EPEVTCR0-9 (Event Processor EVT Pin Control Registers) */
#define EPEVTCR0	0x050
#define EPEVTCR9	0x074
#define EPEVTCR_STRIDE	FSL_STRIDE_4B

/* EPXTRIGCR (Event Processor Crosstrigger Control Register) */
#define EPXTRIGCR	0x090

/* EPIMCR0-31 (Event Processor Input Mux Control Registers) */
#define EPIMCR0		0x100
#define EPIMCR31	0x17C
#define EPIMCR_STRIDE	FSL_STRIDE_4B

/* EPSMCR0-15 (Event Processor SCU Mux Control Registers) */
#define EPSMCR0		0x200
#define EPSMCR15	0x278
#define EPSMCR_STRIDE	FSL_STRIDE_8B

/* EPECR0-15 (Event Processor Event Control Registers) */
#define EPECR0		0x300
#define EPECR15		0x33C
#define EPECR_STRIDE	FSL_STRIDE_4B

/* EPACR0-15 (Event Processor Action Control Registers) */
#define EPACR0		0x400
#define EPACR15		0x43C
#define EPACR_STRIDE	FSL_STRIDE_4B

/* EPCCRi0-15 (Event Processor Counter Control Registers) */
#define EPCCR0		0x800
#define EPCCR15		0x83C
#define EPCCR_STRIDE	FSL_STRIDE_4B

/* EPCMPR0-15 (Event Processor Counter Compare Registers) */
#define EPCMPR0		0x900
#define EPCMPR15	0x93C
#define EPCMPR_STRIDE	FSL_STRIDE_4B

/* EPCTR0-31 (Event Processor Counter Register) */
#define EPCTR0		0xA00
#define EPCTR31		0xA7C
#define EPCTR_STRIDE	FSL_STRIDE_4B

/* NPC triggered Memory-Mapped Access Registers */
#define NCR		0x000
#define MCCR1		0x0CC
#define MCSR1		0x0D0
#define MMAR1LO		0x0D4
#define MMAR1HI		0x0D8
#define MMDR1		0x0DC
#define MCSR2		0x0E0
#define MMAR2LO		0x0E4
#define MMAR2HI		0x0E8
#define MMDR2		0x0EC
#define MCSR3		0x0F0
#define MMAR3LO		0x0F4
#define MMAR3HI		0x0F8
#define MMDR3		0x0FC

/* RCPM Core State Action Control Register 0 */
#define CSTTACR0	0xB00

/* RCPM Core Group 1 Configuration Register 0 */
#define CG1CR0		0x31C

/* Block offsets */
#define RCPM_BLOCK_OFFSET	0x00022000
#define EPU_BLOCK_OFFSET	0x00000000
#define NPC_BLOCK_OFFSET	0x00001000

struct fsm_reg_vals {
	u32 offset;
	u32 value;
};

/*
 * These values are from chip's reference manual. For example,
 * the values for T1040 can be found in "8.4.3.8 Programming
 * supporting deep sleep mode" of Chapter 8 "Run Control and
 * Power Management (RCPM)".
 * The default value can be applied to T104x.
 */
struct fsm_reg_vals fsm_default_val[] = {
	/* EPGCR (Event Processor Global Control Register) */
	{EPU_BLOCK_OFFSET + EPGCR, 0},
	/* EPECR (Event Processor Event Control Registers) */
	{EPU_BLOCK_OFFSET + EPECR0 + EPECR_STRIDE * 0, 0},
	{EPU_BLOCK_OFFSET + EPECR0 + EPECR_STRIDE * 1, 0},
	{EPU_BLOCK_OFFSET + EPECR0 + EPECR_STRIDE * 2, 0xF0004004},
	{EPU_BLOCK_OFFSET + EPECR0 + EPECR_STRIDE * 3, 0x80000084},
	{EPU_BLOCK_OFFSET + EPECR0 + EPECR_STRIDE * 4, 0x20000084},
	{EPU_BLOCK_OFFSET + EPECR0 + EPECR_STRIDE * 5, 0x08000004},
	{EPU_BLOCK_OFFSET + EPECR0 + EPECR_STRIDE * 6, 0x80000084},
	{EPU_BLOCK_OFFSET + EPECR0 + EPECR_STRIDE * 7, 0x80000084},
	{EPU_BLOCK_OFFSET + EPECR0 + EPECR_STRIDE * 8, 0x60000084},
	{EPU_BLOCK_OFFSET + EPECR0 + EPECR_STRIDE * 9, 0x08000084},
	{EPU_BLOCK_OFFSET + EPECR0 + EPECR_STRIDE * 10, 0x42000084},
	{EPU_BLOCK_OFFSET + EPECR0 + EPECR_STRIDE * 11, 0x90000084},
	{EPU_BLOCK_OFFSET + EPECR0 + EPECR_STRIDE * 12, 0x80000084},
	{EPU_BLOCK_OFFSET + EPECR0 + EPECR_STRIDE * 13, 0x08000084},
	{EPU_BLOCK_OFFSET + EPECR0 + EPECR_STRIDE * 14, 0x02000084},
	{EPU_BLOCK_OFFSET + EPECR0 + EPECR_STRIDE * 15, 0x00000004},
	/*
	 * EPEVTCR (Event Processor EVT Pin Control Registers)
	 * SCU8 triger EVT2, and SCU11 triger EVT9
	 */
	{EPU_BLOCK_OFFSET + EPEVTCR0 + EPEVTCR_STRIDE * 0, 0},
	{EPU_BLOCK_OFFSET + EPEVTCR0 + EPEVTCR_STRIDE * 1, 0},
	{EPU_BLOCK_OFFSET + EPEVTCR0 + EPEVTCR_STRIDE * 2, 0x80000001},
	{EPU_BLOCK_OFFSET + EPEVTCR0 + EPEVTCR_STRIDE * 3, 0},
	{EPU_BLOCK_OFFSET + EPEVTCR0 + EPEVTCR_STRIDE * 4, 0},
	{EPU_BLOCK_OFFSET + EPEVTCR0 + EPEVTCR_STRIDE * 5, 0},
	{EPU_BLOCK_OFFSET + EPEVTCR0 + EPEVTCR_STRIDE * 6, 0},
	{EPU_BLOCK_OFFSET + EPEVTCR0 + EPEVTCR_STRIDE * 7, 0},
	{EPU_BLOCK_OFFSET + EPEVTCR0 + EPEVTCR_STRIDE * 8, 0},
	{EPU_BLOCK_OFFSET + EPEVTCR0 + EPEVTCR_STRIDE * 9, 0xB0000001},
	/* EPCMPR (Event Processor Counter Compare Registers) */
	{EPU_BLOCK_OFFSET + EPCMPR0 + EPCMPR_STRIDE * 0, 0},
	{EPU_BLOCK_OFFSET + EPCMPR0 + EPCMPR_STRIDE * 1, 0},
	{EPU_BLOCK_OFFSET + EPCMPR0 + EPCMPR_STRIDE * 2, 0x000000FF},
	{EPU_BLOCK_OFFSET + EPCMPR0 + EPCMPR_STRIDE * 3, 0},
	{EPU_BLOCK_OFFSET + EPCMPR0 + EPCMPR_STRIDE * 4, 0x000000FF},
	{EPU_BLOCK_OFFSET + EPCMPR0 + EPCMPR_STRIDE * 5, 0x00000020},
	{EPU_BLOCK_OFFSET + EPCMPR0 + EPCMPR_STRIDE * 6, 0},
	{EPU_BLOCK_OFFSET + EPCMPR0 + EPCMPR_STRIDE * 7, 0},
	{EPU_BLOCK_OFFSET + EPCMPR0 + EPCMPR_STRIDE * 8, 0x000000FF},
	{EPU_BLOCK_OFFSET + EPCMPR0 + EPCMPR_STRIDE * 9, 0x000000FF},
	{EPU_BLOCK_OFFSET + EPCMPR0 + EPCMPR_STRIDE * 10, 0x000000FF},
	{EPU_BLOCK_OFFSET + EPCMPR0 + EPCMPR_STRIDE * 11, 0x000000FF},
	{EPU_BLOCK_OFFSET + EPCMPR0 + EPCMPR_STRIDE * 12, 0x000000FF},
	{EPU_BLOCK_OFFSET + EPCMPR0 + EPCMPR_STRIDE * 13, 0},
	{EPU_BLOCK_OFFSET + EPCMPR0 + EPCMPR_STRIDE * 14, 0x000000FF},
	{EPU_BLOCK_OFFSET + EPCMPR0 + EPCMPR_STRIDE * 15, 0x000000FF},
	/* EPCCR (Event Processor Counter Control Registers) */
	{EPU_BLOCK_OFFSET + EPCCR0 + EPCCR_STRIDE * 0, 0},
	{EPU_BLOCK_OFFSET + EPCCR0 + EPCCR_STRIDE * 1, 0},
	{EPU_BLOCK_OFFSET + EPCCR0 + EPCCR_STRIDE * 2, 0x92840000},
	{EPU_BLOCK_OFFSET + EPCCR0 + EPCCR_STRIDE * 3, 0},
	{EPU_BLOCK_OFFSET + EPCCR0 + EPCCR_STRIDE * 4, 0x92840000},
	{EPU_BLOCK_OFFSET + EPCCR0 + EPCCR_STRIDE * 5, 0x92840000},
	{EPU_BLOCK_OFFSET + EPCCR0 + EPCCR_STRIDE * 6, 0},
	{EPU_BLOCK_OFFSET + EPCCR0 + EPCCR_STRIDE * 7, 0},
	{EPU_BLOCK_OFFSET + EPCCR0 + EPCCR_STRIDE * 8, 0x92840000},
	{EPU_BLOCK_OFFSET + EPCCR0 + EPCCR_STRIDE * 9, 0x92840000},
	{EPU_BLOCK_OFFSET + EPCCR0 + EPCCR_STRIDE * 10, 0x92840000},
	{EPU_BLOCK_OFFSET + EPCCR0 + EPCCR_STRIDE * 11, 0x92840000},
	{EPU_BLOCK_OFFSET + EPCCR0 + EPCCR_STRIDE * 12, 0x92840000},
	{EPU_BLOCK_OFFSET + EPCCR0 + EPCCR_STRIDE * 13, 0},
	{EPU_BLOCK_OFFSET + EPCCR0 + EPCCR_STRIDE * 14, 0x92840000},
	{EPU_BLOCK_OFFSET + EPCCR0 + EPCCR_STRIDE * 15, 0x92840000},
	/* EPSMCR (Event Processor SCU Mux Control Registers) */
	{EPU_BLOCK_OFFSET + EPSMCR0 + EPSMCR_STRIDE * 0, 0},
	{EPU_BLOCK_OFFSET + EPSMCR0 + EPSMCR_STRIDE * 1, 0},
	{EPU_BLOCK_OFFSET + EPSMCR0 + EPSMCR_STRIDE * 2, 0x6C700000},
	{EPU_BLOCK_OFFSET + EPSMCR0 + EPSMCR_STRIDE * 3, 0x2F000000},
	{EPU_BLOCK_OFFSET + EPSMCR0 + EPSMCR_STRIDE * 4, 0x002F0000},
	{EPU_BLOCK_OFFSET + EPSMCR0 + EPSMCR_STRIDE * 5, 0x00002E00},
	{EPU_BLOCK_OFFSET + EPSMCR0 + EPSMCR_STRIDE * 6, 0x7C000000},
	{EPU_BLOCK_OFFSET + EPSMCR0 + EPSMCR_STRIDE * 7, 0x30000000},
	{EPU_BLOCK_OFFSET + EPSMCR0 + EPSMCR_STRIDE * 8, 0x64300000},
	{EPU_BLOCK_OFFSET + EPSMCR0 + EPSMCR_STRIDE * 9, 0x00003000},
	{EPU_BLOCK_OFFSET + EPSMCR0 + EPSMCR_STRIDE * 10, 0x65000030},
	{EPU_BLOCK_OFFSET + EPSMCR0 + EPSMCR_STRIDE * 11, 0x31740000},
	{EPU_BLOCK_OFFSET + EPSMCR0 + EPSMCR_STRIDE * 12, 0x7F000000},
	{EPU_BLOCK_OFFSET + EPSMCR0 + EPSMCR_STRIDE * 13, 0x00003100},
	{EPU_BLOCK_OFFSET + EPSMCR0 + EPSMCR_STRIDE * 14, 0x00000031},
	{EPU_BLOCK_OFFSET + EPSMCR0 + EPSMCR_STRIDE * 15, 0x76000000},
	/* EPACR (Event Processor Action Control Registers) */
	{EPU_BLOCK_OFFSET + EPACR0 + EPACR_STRIDE * 0, 0},
	{EPU_BLOCK_OFFSET + EPACR0 + EPACR_STRIDE * 1, 0},
	{EPU_BLOCK_OFFSET + EPACR0 + EPACR_STRIDE * 2, 0},
	{EPU_BLOCK_OFFSET + EPACR0 + EPACR_STRIDE * 3, 0x00000080},
	{EPU_BLOCK_OFFSET + EPACR0 + EPACR_STRIDE * 4, 0},
	{EPU_BLOCK_OFFSET + EPACR0 + EPACR_STRIDE * 5, 0x00000040},
	{EPU_BLOCK_OFFSET + EPACR0 + EPACR_STRIDE * 6, 0},
	{EPU_BLOCK_OFFSET + EPACR0 + EPACR_STRIDE * 7, 0},
	{EPU_BLOCK_OFFSET + EPACR0 + EPACR_STRIDE * 8, 0},
	{EPU_BLOCK_OFFSET + EPACR0 + EPACR_STRIDE * 9, 0x0000001C},
	{EPU_BLOCK_OFFSET + EPACR0 + EPACR_STRIDE * 10, 0x00000020},
	{EPU_BLOCK_OFFSET + EPACR0 + EPACR_STRIDE * 11, 0},
	{EPU_BLOCK_OFFSET + EPACR0 + EPACR_STRIDE * 12, 0x00000003},
	{EPU_BLOCK_OFFSET + EPACR0 + EPACR_STRIDE * 13, 0x06000000},
	{EPU_BLOCK_OFFSET + EPACR0 + EPACR_STRIDE * 14, 0x04000000},
	{EPU_BLOCK_OFFSET + EPACR0 + EPACR_STRIDE * 15, 0x02000000},
	/* EPIMCR (Event Processor Input Mux Control Registers) */
	{EPU_BLOCK_OFFSET + EPIMCR0 + EPIMCR_STRIDE * 0, 0},
	{EPU_BLOCK_OFFSET + EPIMCR0 + EPIMCR_STRIDE * 1, 0},
	{EPU_BLOCK_OFFSET + EPIMCR0 + EPIMCR_STRIDE * 2, 0},
	{EPU_BLOCK_OFFSET + EPIMCR0 + EPIMCR_STRIDE * 3, 0},
	{EPU_BLOCK_OFFSET + EPIMCR0 + EPIMCR_STRIDE * 4, 0x44000000},
	{EPU_BLOCK_OFFSET + EPIMCR0 + EPIMCR_STRIDE * 5, 0x40000000},
	{EPU_BLOCK_OFFSET + EPIMCR0 + EPIMCR_STRIDE * 6, 0},
	{EPU_BLOCK_OFFSET + EPIMCR0 + EPIMCR_STRIDE * 7, 0},
	{EPU_BLOCK_OFFSET + EPIMCR0 + EPIMCR_STRIDE * 8, 0},
	{EPU_BLOCK_OFFSET + EPIMCR0 + EPIMCR_STRIDE * 9, 0},
	{EPU_BLOCK_OFFSET + EPIMCR0 + EPIMCR_STRIDE * 10, 0},
	{EPU_BLOCK_OFFSET + EPIMCR0 + EPIMCR_STRIDE * 11, 0},
	{EPU_BLOCK_OFFSET + EPIMCR0 + EPIMCR_STRIDE * 12, 0x44000000},
	{EPU_BLOCK_OFFSET + EPIMCR0 + EPIMCR_STRIDE * 13, 0},
	{EPU_BLOCK_OFFSET + EPIMCR0 + EPIMCR_STRIDE * 14, 0},
	{EPU_BLOCK_OFFSET + EPIMCR0 + EPIMCR_STRIDE * 15, 0},
	{EPU_BLOCK_OFFSET + EPIMCR0 + EPIMCR_STRIDE * 16, 0x6A000000},
	{EPU_BLOCK_OFFSET + EPIMCR0 + EPIMCR_STRIDE * 17, 0},
	{EPU_BLOCK_OFFSET + EPIMCR0 + EPIMCR_STRIDE * 18, 0},
	{EPU_BLOCK_OFFSET + EPIMCR0 + EPIMCR_STRIDE * 19, 0},
	{EPU_BLOCK_OFFSET + EPIMCR0 + EPIMCR_STRIDE * 20, 0x48000000},
	{EPU_BLOCK_OFFSET + EPIMCR0 + EPIMCR_STRIDE * 21, 0},
	{EPU_BLOCK_OFFSET + EPIMCR0 + EPIMCR_STRIDE * 22, 0x6C000000},
	{EPU_BLOCK_OFFSET + EPIMCR0 + EPIMCR_STRIDE * 23, 0},
	{EPU_BLOCK_OFFSET + EPIMCR0 + EPIMCR_STRIDE * 24, 0},
	{EPU_BLOCK_OFFSET + EPIMCR0 + EPIMCR_STRIDE * 25, 0},
	{EPU_BLOCK_OFFSET + EPIMCR0 + EPIMCR_STRIDE * 26, 0},
	{EPU_BLOCK_OFFSET + EPIMCR0 + EPIMCR_STRIDE * 27, 0},
	{EPU_BLOCK_OFFSET + EPIMCR0 + EPIMCR_STRIDE * 28, 0x76000000},
	{EPU_BLOCK_OFFSET + EPIMCR0 + EPIMCR_STRIDE * 29, 0},
	{EPU_BLOCK_OFFSET + EPIMCR0 + EPIMCR_STRIDE * 30, 0},
	{EPU_BLOCK_OFFSET + EPIMCR0 + EPIMCR_STRIDE * 31, 0x76000000},
	/* EPXTRIGCR (Event Processor Crosstrigger Control Register) */
	{EPU_BLOCK_OFFSET + EPXTRIGCR, 0x0000FFDF},
	/* NPC triggered Memory-Mapped Access Registers */
	{NPC_BLOCK_OFFSET + NCR, 0x80000000},
	{NPC_BLOCK_OFFSET + MCCR1, 0},
	{NPC_BLOCK_OFFSET + MCSR1, 0},
	{NPC_BLOCK_OFFSET + MMAR1LO, 0},
	{NPC_BLOCK_OFFSET + MMAR1HI, 0},
	{NPC_BLOCK_OFFSET + MMDR1, 0},
	{NPC_BLOCK_OFFSET + MCSR2, 0},
	{NPC_BLOCK_OFFSET + MMAR2LO, 0},
	{NPC_BLOCK_OFFSET + MMAR2HI, 0},
	{NPC_BLOCK_OFFSET + MMDR2, 0},
	{NPC_BLOCK_OFFSET + MCSR3, 0x80000000},
	{NPC_BLOCK_OFFSET + MMAR3LO, 0x000E2130},
	{NPC_BLOCK_OFFSET + MMAR3HI, 0x00030000},
	{NPC_BLOCK_OFFSET + MMDR3, 0x00020000},
	/* Configure RCPM for detecting Core0’s PH15 state */
	{RCPM_BLOCK_OFFSET + CSTTACR0, 0x00001001},
	{RCPM_BLOCK_OFFSET + CG1CR0, 0x00000001},
	/* end */
	{FSM_END_FLAG, 0},
};

/**
 * fsl_dp_fsm_clean - Clear EPU's FSM
 * @dcsr_base: the base address of DCSR registers
 * @val: Pointer to values for FSM registers. If NULL,
 *       will use the default value.
 */
void fsl_dp_fsm_clean(void __iomem *dcsr_base, struct fsm_reg_vals *val)
{
	void *epu_base = dcsr_base + EPU_BLOCK_OFFSET;
	u32 offset;
	struct fsm_reg_vals *data;

	if (val) {
		data = val;
		while (data->offset != FSM_END_FLAG) {
			out_be32(dcsr_base + data->offset, 0);
			data++;
		}
		return;
	}

	/* follow the exact sequence to clear the registers */
	/* Clear EPACRn */
	for (offset = EPACR0; offset <= EPACR15; offset += EPACR_STRIDE)
		out_be32(epu_base + offset, 0);

	/* Clear EPEVTCRn */
	for (offset = EPEVTCR0; offset <= EPEVTCR9; offset += EPEVTCR_STRIDE)
		out_be32(epu_base + offset, 0);

	/* Clear EPGCR */
	out_be32(epu_base + EPGCR, 0);

	/* Clear EPSMCRn */
	for (offset = EPSMCR0; offset <= EPSMCR15; offset += EPSMCR_STRIDE)
		out_be32(epu_base + offset, 0);

	/* Clear EPCCRn */
	for (offset = EPCCR0; offset <= EPCCR15; offset += EPCCR_STRIDE)
		out_be32(epu_base + offset, 0);

	/* Clear EPCMPRn */
	for (offset = EPCMPR0; offset <= EPCMPR15; offset += EPCMPR_STRIDE)
		out_be32(epu_base + offset, 0);

	/* Clear EPCTRn */
	for (offset = EPCTR0; offset <= EPCTR31; offset += EPCTR_STRIDE)
		out_be32(epu_base + offset, 0);

	/* Clear EPIMCRn */
	for (offset = EPIMCR0; offset <= EPIMCR31; offset += EPIMCR_STRIDE)
		out_be32(epu_base + offset, 0);

	/* Clear EPXTRIGCRn */
	out_be32(epu_base + EPXTRIGCR, 0);

	/* Clear EPECRn */
	for (offset = EPECR0; offset <= EPECR15; offset += EPECR_STRIDE)
		out_be32(epu_base + offset, 0);
}

/**
 * fsl_dp_fsm_setup - Configure EPU's FSM
 * @dcsr_base: the base address of DCSR registers
 * @val: Pointer to values for FSM registers. If NULL,
 *       will use the default value.
 */
void fsl_dp_fsm_setup(void __iomem *dcsr_base, struct fsm_reg_vals *val)
{
	struct fsm_reg_vals *data;

	/* if NULL, use the default values */
	if (val)
		data = val;
	else
		data = fsm_default_val;

	/* clear all registers */
	fsl_dp_fsm_clean(dcsr_base, NULL);

	while (data->offset != FSM_END_FLAG) {
		if (data->value)
			out_be32(dcsr_base + data->offset, data->value);
		data++;
	}
}
