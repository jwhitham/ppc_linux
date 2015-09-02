
CXX=powerpc-eabi-g++
CC=$(CXX)
LD=$(CXX)
CXXFLAGS=-g -I../rvs_library
LDFLAGS=-L../rvs_library
LIBS=-lrvs

all: example

clean:
	rm -f $(OBJS) example

OBJS=start.o cpp_code.o

example: $(OBJS) Makefile
	$(LD) -o example $(OBJS) $(LDFLAGS) $(LIBS)

