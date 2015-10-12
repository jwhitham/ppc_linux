all: librvs.a

CPPFLAGS += -I../rvs_module -I. -Ippc_headers
CFLAGS += -Wall -Werror -DLIBRVS -g -O2 -DCONFIG_PPC
OBJS := librvs.o ppc_syscall.o

librvs.a: $(OBJS)
	$(AR) rcs $@ $(OBJS)

.PHONY: clean
clean:
	rm -f $(OBJS) librvs.a
