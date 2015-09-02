.global _start
.global start
.global main
_start:
start:
/* call main */
	bl main

/* exit = syscall 1 */
0: li 0,1
   li 3,0
   sc
   b 0b


