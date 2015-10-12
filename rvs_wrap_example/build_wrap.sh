#!/bin/bash -xe
make clean
rm -rf ./rvs/out

export RVS_WRAPPER_RVS=./rvs
export RVS_WRAPPER_RVSCCPATH=/home/jwhitham/linux-tools/bin/powerpc-eabi-g++
export RVS_WRAPPER_RVSTEMPLATES=./templates 
export RVS_WRAPPER_RVSOUT=./rvs/out/ 
export RVS_WRAPPER_RVSTMP=./rvs/out/tmp 
export RVS_WRAPPER_RVSV=1
export WRAPPER=/home/jwhitham/rvs_4/bin/wrappers/gcc/gcc_wrap


make CC=$WRAPPER CXX=$WRAPPER LD=$WRAPPER

