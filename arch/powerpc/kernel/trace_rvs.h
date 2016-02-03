/*
 *  RVS Tracing API
 *
 *  Author: Jack Whitham (jwhitham@rapitasystems.com)
 *
 *  Trace point assembly code, as a macro. Provide any three different
 *  GPRs, with the restriction that RtempA and RtempB must not be r0,
 *  and one condition register.
 *
 *  Copyright (C) 2016 Rapita Systems Ltd.
 *
 */

#define TRACE_RVS_ASM(Rparam, RtempA, RtempB, crA) 						\
	mfspr	RtempA, 286; /* get PIR */							\
	;											\
	lis	RtempB,trace_rvs_pir@ha;							\
	lwz	RtempB,trace_rvs_pir@l(RtempB);							\
	cmpw	crA, RtempA, RtempB;								\
	bne	crA, 11f;		/* skip if trace requested for a different CPU */ 	\
	lis	RtempB,trace_rvs_end_p@ha; 							\
	lwz	RtempB,trace_rvs_end_p@l(RtempB); 						\
	lis	RtempA,trace_rvs_write_p@ha; 							\
	lwz	RtempA,trace_rvs_write_p@l(RtempA); 						\
	cmplw	crA, RtempA, RtempB;								\
	bge	crA, 11f;	/* skip if buffer full */ 					\
	;											\
	stw	Rparam,0(RtempA);	/* write id */						\
	mfspr	RtempB, 526;									\
	stw	RtempB,4(RtempA);	/* write timestamp */					\
	addi	RtempB,RtempA,8;	/* increment write pointer */				\
	lis	RtempA,trace_rvs_write_p@ha;							\
	stw	RtempB,trace_rvs_write_p@l(RtempA);						\
11:;
