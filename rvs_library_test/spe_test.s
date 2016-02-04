	.file	"spe_test.c"
	.gnu_attribute 4, 2
	.gnu_attribute 8, 3
	.section	".text"
	.align 2
	.globl spe_one
	.type	spe_one, @function
spe_one:
	mr. 0,3
	mtctr 0
	beqlr- 0
	lis 11,v_one@ha
	lis 9,.LC0@ha
	lwz 9,.LC0@l(9)
	lwz 0,v_one@l(11)
.L3:
	efsadd 0,0,9
	bdnz .L3
	stw 0,v_one@l(11)
	blr
	.size	spe_one, .-spe_one
	.align 2
	.globl spe_two
	.type	spe_two, @function
spe_two:
	mr. 0,3
	mtctr 0
	beqlr- 0
	lis 11,v_two@ha
	lis 9,.LC1@ha
	la 11,v_two@l(11)
	la 9,.LC1@l(9)
	evldd 9,0(9)
	evldd 0,0(11)
.L9:
	efdadd 0,0,9
	bdnz .L9
	evstdd 0,0(11)
	blr
	.size	spe_two, .-spe_two
	.globl v_one
	.globl v_two
	.section	.rodata.cst4,"aM",@progbits,4
	.align 2
.LC0:
	.4byte	1067320907
	.section	.rodata.cst8,"aM",@progbits,8
	.align 3
.LC1:
	.4byte	1072939209
	.4byte	1402701959
	.section	.sdata,"aw",@progbits
	.align 3
	.type	v_one, @object
	.size	v_one, 4
v_one:
	.long	1065353216
	.zero	4
	.type	v_two, @object
	.size	v_two, 8
v_two:
	.long	1072693248
	.long	0
	.ident	"GCC: (GNU) 4.3.2"
