
CXX=powerpc-eabi-g++
CC=powerpc-eabi-gcc
LD=powerpc-eabi-g++
CFLAGS=-g -I../rvs_library
CXXFLAGS=-g -I../rvs_library

all: example

clean:
	rm -f $(OBJS) example

OBJS=start.o cpp_code.o c_code.o

example: $(OBJS) Makefile
	$(LD) -o example $(OBJS)

