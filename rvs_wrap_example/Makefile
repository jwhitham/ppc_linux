
CXX=powerpc-eabi-g++
CC=powerpc-eabi-gcc
LD=powerpc-eabi-g++
CFLAGS=-g
CXXFLAGS=-g

all: example

clean:
	rm -f $(OBJS) example

OBJS=start.o cpp_code.o c_code.o

example: $(OBJS) Makefile
	$(LD) -o example $(OBJS)

