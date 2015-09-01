#!/bin/bash -e

rm -f example.p example.c.i example *.rvd example.xsc

echo 'preprocessing example.c'
powerpc-eabi-gcc -E example.c > example.p

echo 'instrumenting example.c'
cins --exf tmp.exf example.p  -c rvs_ipoint.h

echo 'compile instrumented code'
#powerpc-eabi-gcc -E main.c -I../rvs_library >main.p
powerpc-eabi-gcc -c -x c example.p.i -g
powerpc-eabi-gcc -o example example.p.o \
   -L../rvs_library -lrvs  -nostdlib

echo 'create RVD'
xstutils -r task1 -o example.rvd example.xsc



