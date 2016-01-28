# No cross compiler: this can only be built on the target board
# as it statically links against libc.

RVS_LIB_DIR=/opt/rvs
CFLAGS += -I$(RVS_LIB_DIR) -I. -Wall -Werror -g -O2
OBJS := rvs_library_test.o loopN.o tip.o
LDFLAGS += -L$(RVS_LIB_DIR) -lrvs

all: rvs_library_test

rvs_library_test: $(OBJS)
	gcc -o rvs_library_test -static $(OBJS) $(LDFLAGS)

.PHONY: clean
clean:
	rm -f $(OBJS) rvs_library_test
