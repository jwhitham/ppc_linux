
CXX=powerpc-eabi-g++
CC=$(CXX)
LD=$(CXX)
CXXFLAGS=-g -I../rvs_library -nostdinc -fno-exceptions 
LDFLAGS=-L../rvs_library
LIBS=-lrvs -nostdlib

all: example

clean:
	rm -f $(OBJS) example

OBJS=start.o cpp_code.o

example: $(OBJS) Makefile
	$(LD) -o example $(OBJS) $(LDFLAGS) $(LIBS)

