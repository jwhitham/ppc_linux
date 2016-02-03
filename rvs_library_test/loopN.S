.text
.globl loopN
loopN:
	mtctr %r3
	.p2align 4,,15
0:
	bdnz 0b
	blr


.globl loopN2
loopN2:
	subi	%r1, %r1, 0x80
	mflr	%r0
	mfcr	%r4
	mtctr %r3
	stw	%r0,  0x00(%r1) /* lr */
	stw	%r2,  0x04(%r1)
	stw	%r13, 0x08(%r1)
	stw	%r14, 0x0c(%r1)
	stw	%r15, 0x10(%r1)
	stw	%r16, 0x14(%r1)
	stw	%r17, 0x18(%r1)
	stw	%r18, 0x1c(%r1)
	stw	%r19, 0x20(%r1)
	stw	%r20, 0x24(%r1)
	stw	%r21, 0x28(%r1)
	stw	%r22, 0x2c(%r1)
	stw	%r23, 0x30(%r1)
	stw	%r24, 0x34(%r1)
	stw	%r25, 0x38(%r1)
	stw	%r26, 0x3c(%r1)
	stw	%r27, 0x40(%r1)
	stw	%r28, 0x44(%r1)
	stw	%r29, 0x48(%r1)
	stw	%r30, 0x4c(%r1)
	stw	%r31, 0x50(%r1)
	stw	%r4,  0x54(%r1) /* cr */

/* load test pattern into all registers */
#define X 0x1e00

	li		%r0,  0x0032 | X
	mtlr	%r0
	li		%r0,  0x0033 | X
	mtcr	%r0

	li		%r0,  0x0000 | X
	li		%r2,  0x0002 | X
	li		%r3,  0x0003 | X
	li		%r4,  0x0004 | X
	li		%r5,  0x0005 | X
	li		%r6,  0x0006 | X
	li		%r7,  0x0007 | X
	li		%r8,  0x0008 | X
	li		%r9,  0x0009 | X
	li		%r10, 0x0010 | X
	li		%r11, 0x0011 | X
	li		%r12, 0x0012 | X
	li		%r13, 0x0013 | X
	li		%r14, 0x0014 | X
	li		%r15, 0x0015 | X
	li		%r16, 0x0016 | X
	li		%r17, 0x0017 | X
	li		%r18, 0x0018 | X
	li		%r19, 0x0019 | X
	li		%r20, 0x0020 | X
	li		%r21, 0x0021 | X
	li		%r22, 0x0022 | X
	li		%r23, 0x0023 | X
	li		%r24, 0x0024 | X
	li		%r25, 0x0025 | X
	li		%r26, 0x0026 | X
	li		%r27, 0x0027 | X
	li		%r28, 0x0028 | X
	li		%r29, 0x0029 | X
	li		%r30, 0x0030 | X
	li		%r31, 0x0031 | X

	.p2align 4,,15
0:
	bdnz 0b

/* check test pattern in registers */

	xori		%r0, %r0,  0x0000 | X
	xori		%r2, %r2,  0x0002 | X
	xori		%r3, %r3,  0x0003 | X
	xori		%r4, %r4,  0x0004 | X
	xori		%r5, %r5,  0x0005 | X
	xori		%r6, %r6,  0x0006 | X
	xori		%r7, %r7,  0x0007 | X
	xori		%r8, %r8,  0x0008 | X
	xori		%r9, %r9,  0x0009 | X
	xori		%r10, %r10, 0x0010 | X
	xori		%r11, %r11, 0x0011 | X
	xori		%r12, %r12, 0x0012 | X
	xori		%r13, %r13, 0x0013 | X
	xori		%r14, %r14, 0x0014 | X
	xori		%r15, %r15, 0x0015 | X
	xori		%r16, %r16, 0x0016 | X
	xori		%r17, %r17, 0x0017 | X
	xori		%r18, %r18, 0x0018 | X
	xori		%r19, %r19, 0x0019 | X
	xori		%r20, %r20, 0x0020 | X
	xori		%r21, %r21, 0x0021 | X
	xori		%r22, %r22, 0x0022 | X
	xori		%r23, %r23, 0x0023 | X
	xori		%r24, %r24, 0x0024 | X
	xori		%r25, %r25, 0x0025 | X
	xori		%r26, %r26, 0x0026 | X
	xori		%r27, %r27, 0x0027 | X
	xori		%r28, %r28, 0x0028 | X
	xori		%r29, %r29, 0x0029 | X
	xori		%r30, %r30, 0x0030 | X
	xori		%r31, %r31, 0x0031 | X

	/* all registers should contain 0 */
	or			%r2, %r2, %r3
	or			%r4, %r4, %r5
	or			%r6, %r6, %r7
	or			%r8, %r8, %r9
	or			%r10, %r10, %r11
	or			%r12, %r12, %r13
	or			%r14, %r14, %r15
	or			%r16, %r16, %r17
	or			%r18, %r18, %r19
	or			%r20, %r20, %r21
	or			%r22, %r22, %r23
	or			%r24, %r24, %r25
	or			%r26, %r26, %r27
	or			%r28, %r28, %r29
	or			%r30, %r30, %r31

	or			%r0, %r0, %r2
	or			%r4, %r4, %r6
	or			%r8, %r8, %r10
	or			%r12, %r12, %r14
	or			%r16, %r16, %r18
	or			%r20, %r20, %r22
	or			%r24, %r24, %r26
	or			%r28, %r28, %r30

	or			%r0, %r0, %r4
	or			%r8, %r8, %r12
	or			%r16, %r16, %r20
	or			%r24, %r24, %r28

	or			%r0, %r0, %r8
	or			%r16, %r16, %r24

	/* r3 should be 0 */
	or			%r3, %r0, %r16

	mflr	%r4
	xori	%r4, %r4, 0x0032 | X
	mfcr	%r5
	xori	%r5, %r5, 0x0033 | X

	or			%r9, %r5, %r4
	or.		%r9, %r3, %r9
	beq		is_zero


	lwz	%r2,  0x04(%r1)
	bl			explosion

is_zero:
	mr		%r3, %r9
	lwz	%r0,  0x00(%r1) /* lr */
	lwz	%r2,  0x04(%r1)
	lwz	%r13, 0x08(%r1)
	lwz	%r14, 0x0c(%r1)
	lwz	%r15, 0x10(%r1)
	lwz	%r16, 0x14(%r1)
	lwz	%r17, 0x18(%r1)
	lwz	%r18, 0x1c(%r1)
	lwz	%r19, 0x20(%r1)
	lwz	%r20, 0x24(%r1)
	lwz	%r21, 0x28(%r1)
	lwz	%r22, 0x2c(%r1)
	lwz	%r23, 0x30(%r1)
	lwz	%r24, 0x34(%r1)
	lwz	%r25, 0x38(%r1)
	lwz	%r26, 0x3c(%r1)
	lwz	%r27, 0x40(%r1)
	lwz	%r28, 0x44(%r1)
	lwz	%r29, 0x48(%r1)
	lwz	%r30, 0x4c(%r1)
	lwz	%r31, 0x50(%r1)
	lwz	%r4,  0x54(%r1) /* cr */
	mtlr	%r0
	mtcr	%r4
	addi	%r1, %r1, 0x80
	blr

