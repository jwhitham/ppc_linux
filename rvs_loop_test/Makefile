all: rvs_loop_test

MFLAGS=-mcpu=8548 -mhard-float \
   -mfloat-gprs=double -mspe -mabi=spe
CFLAGS += -I../rvs_library -I. -Wall -Werror -g -O2 -I../rvs_library/ppc_headers \
      -I../rvs_library/ppc_headers/powerpc-linux-gnu $(MFLAGS)
OBJS := rvs_loop_test.o testing.o
LDFLAGS += -L../rvs_library -lrvs $(MFLAGS)


../rvs_library/librvs.a:
	make -C ../rvs_library librvs.a

rvs_loop_test: ../rvs_library/librvs.a $(OBJS)
	$(CC) -o rvs_loop_test -static $(OBJS) $(LDFLAGS)

.PHONY: clean
clean:
	rm -f $(OBJS) rvs_loop_test
