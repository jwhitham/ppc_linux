.globl loopN
loopN:
	cmpwi 0,3,0
	beqlr 0
	mtctr 3
	.p2align 4,,15
.L3:
	bdnz .L3
	blr

