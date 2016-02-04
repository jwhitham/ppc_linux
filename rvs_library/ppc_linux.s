	.file	"ppc_linux.c"
	.section	".text"
	.align 2
	.p2align 4,,15
	.type	handle_overflow_imminent, @function
handle_overflow_imminent:
	b rvs_overflow_imminent_signal
	.size	handle_overflow_imminent,.-handle_overflow_imminent
	.align 2
	.p2align 4,,15
	.type	handle_segfault, @function
handle_segfault:
	lwz 9,48(5)
	lwz 5,12(4)
	lwz 3,128(9)
	mr 4,9
	b rvs_segfault_signal
	.size	handle_segfault,.-handle_segfault
	.align 2
	.p2align 4,,15
	.globl ppc_fatal_error
	.type	ppc_fatal_error, @function
ppc_fatal_error:
	stwu 1,-16(1)
	mr 5,3
	mflr 0
	stw 0,20(1)
	lbz 9,0(3)
	cmpwi 7,9,0
	beq 7,.L6
	mr 10,3
	li 6,0
	.p2align 4,,15
.L5:
	lbzu 9,1(10)
	addi 6,6,1
	cmpwi 7,9,0
	bne 7,.L5
.L4:
	li 4,2
	li 3,4
	crxor 6,6,6
	bl ppc_syscall
	lis 5,.LC0@ha
	li 4,2
	la 5,.LC0@l(5)
	li 6,1
	li 3,4
	crxor 6,6,6
	bl ppc_syscall
	lwz 0,20(1)
	li 3,1
	li 4,1
	li 5,0
	mtlr 0
	li 6,0
	addi 1,1,16
	crxor 6,6,6
	b ppc_syscall
.L6:
	li 6,0
	b .L4
	.size	ppc_fatal_error,.-ppc_fatal_error
	.align 2
	.p2align 4,,15
	.globl ppc_ioctl
	.type	ppc_ioctl, @function
ppc_ioctl:
	mr 10,3
	mr 9,4
	mr 6,5
	li 3,54
	mr 4,10
	mr 5,9
	crxor 6,6,6
	b ppc_syscall
	.size	ppc_ioctl,.-ppc_ioctl
	.align 2
	.p2align 4,,15
	.globl ppc_read
	.type	ppc_read, @function
ppc_read:
	mr 10,3
	mr 9,4
	mr 6,5
	li 3,3
	mr 4,10
	mr 5,9
	crxor 6,6,6
	b ppc_syscall
	.size	ppc_read,.-ppc_read
	.align 2
	.p2align 4,,15
	.globl ppc_write
	.type	ppc_write, @function
ppc_write:
	mr 10,3
	mr 9,4
	mr 6,5
	li 3,4
	mr 4,10
	mr 5,9
	crxor 6,6,6
	b ppc_syscall
	.size	ppc_write,.-ppc_write
	.align 2
	.p2align 4,,15
	.globl ppc_open_rdwr
	.type	ppc_open_rdwr, @function
ppc_open_rdwr:
	mr 4,3
	li 5,2
	li 3,5
	li 6,0
	crxor 6,6,6
	b ppc_syscall
	.size	ppc_open_rdwr,.-ppc_open_rdwr
	.align 2
	.p2align 4,,15
	.globl ppc_creat
	.type	ppc_creat, @function
ppc_creat:
	mr 4,3
	li 5,436
	li 3,8
	li 6,0
	crxor 6,6,6
	b ppc_syscall
	.size	ppc_creat,.-ppc_creat
	.align 2
	.p2align 4,,15
	.globl ppc_close
	.type	ppc_close, @function
ppc_close:
	mr 4,3
	li 5,0
	li 3,6
	li 6,0
	crxor 6,6,6
	b ppc_syscall
	.size	ppc_close,.-ppc_close
	.align 2
	.p2align 4,,15
	.globl ppc_mprotect_read
	.type	ppc_mprotect_read, @function
ppc_mprotect_read:
	mr 9,3
	mr 5,4
	li 3,125
	mr 4,9
	li 6,1
	crxor 6,6,6
	b ppc_syscall
	.size	ppc_mprotect_read,.-ppc_mprotect_read
	.align 2
	.p2align 4,,15
	.globl ppc_mprotect_rdwr
	.type	ppc_mprotect_rdwr, @function
ppc_mprotect_rdwr:
	mr 9,3
	mr 5,4
	li 3,125
	mr 4,9
	li 6,3
	crxor 6,6,6
	b ppc_syscall
	.size	ppc_mprotect_rdwr,.-ppc_mprotect_rdwr
	.align 2
	.p2align 4,,15
	.globl ppc_exit
	.type	ppc_exit, @function
ppc_exit:
	mr 4,3
	li 5,0
	li 3,1
	li 6,0
	crxor 6,6,6
	b ppc_syscall
	.size	ppc_exit,.-ppc_exit
	.align 2
	.p2align 4,,15
	.globl ppc_restore_signal_handler
	.type	ppc_restore_signal_handler, @function
ppc_restore_signal_handler:
	stwu 1,-16(1)
	lis 5,.LANCHOR0@ha
	mflr 0
	li 3,173
	li 4,11
	la 5,.LANCHOR0@l(5)
	li 6,0
	li 7,8
	stw 0,20(1)
	crxor 6,6,6
	bl ppc_syscall
	cmpwi 7,3,0
	bne 7,.L22
	lwz 0,20(1)
	addi 1,1,16
	mtlr 0
	blr
	.p2align 4,,15
.L22:
	lwz 0,20(1)
	lis 3,.LC1@ha
	la 3,.LC1@l(3)
	addi 1,1,16
	mtlr 0
	b ppc_fatal_error
	.size	ppc_restore_signal_handler,.-ppc_restore_signal_handler
	.align 2
	.p2align 4,,15
	.globl ppc_install_signal_handler
	.type	ppc_install_signal_handler, @function
ppc_install_signal_handler:
	stwu 1,-48(1)
	li 8,16
	mflr 0
	mtctr 8
	li 10,0
	stw 0,52(1)
	stw 31,44(1)
	addi 9,1,23
	mr 31,3
	.p2align 4,,15
.L24:
	stbu 10,1(9)
	bdnz .L24
	lis 9,handle_segfault@ha
	lis 6,.LANCHOR0@ha
	la 9,handle_segfault@l(9)
	li 3,173
	stw 9,24(1)
	li 4,11
	li 9,4
	addi 5,1,24
	la 6,.LANCHOR0@l(6)
	li 7,8
	stw 9,28(1)
	crxor 6,6,6
	bl ppc_syscall
	cmpwi 7,3,0
	bne 7,.L35
.L25:
	lis 9,handle_overflow_imminent@ha
	li 3,173
	la 9,handle_overflow_imminent@l(9)
	mr 4,31
	stw 9,24(1)
	addi 5,1,24
	li 9,4
	addi 6,1,8
	li 7,8
	stw 9,28(1)
	crxor 6,6,6
	bl ppc_syscall
	cmpwi 7,3,0
	beq 7,.L23
	lis 3,.LC3@ha
	la 3,.LC3@l(3)
	bl ppc_fatal_error
.L23:
	lwz 0,52(1)
	lwz 31,44(1)
	addi 1,1,48
	mtlr 0
	blr
	.p2align 4,,15
.L35:
	lis 3,.LC2@ha
	la 3,.LC2@l(3)
	bl ppc_fatal_error
	b .L25
	.size	ppc_install_signal_handler,.-ppc_install_signal_handler
	.section	.rodata.str1.4,"aMS",@progbits,1
	.align 2
.LC0:
	.string	"\n"
	.zero	2
.LC1:
	.string	"sigaction (SIGSEGV) reinstall of old handler failed"
.LC2:
	.string	"RVS_Init: sigaction (SIGSEGV) install failed"
	.zero	3
.LC3:
	.string	"RVS_Init: sigaction (SIG_RVS_IMMINENT_OVERFLOW) install failed"
	.section	".bss"
	.align 2
	.set	.LANCHOR0,. + 0
	.type	old_segv_signal_handler, @object
	.size	old_segv_signal_handler, 16
old_segv_signal_handler:
	.zero	16
	.ident	"GCC: (Debian 4.9.2-10) 4.9.2"
	.section	.note.GNU-stack,"",@progbits
