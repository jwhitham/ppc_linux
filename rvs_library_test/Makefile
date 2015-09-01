all: rvs_library_test

CFLAGS += -I../rvs_library -I. -Wall -Werror -g -O2
OBJS := rvs_library_test.o
LDFLAGS += -L../rvs_library -lrvs


../rvs_library/librvs.a:
	make -C ../rvs_library librvs.a

rvs_library_test: ../rvs_library/librvs.a $(OBJS)
	gcc -o rvs_library_test $(OBJS) $(LDFLAGS)

.PHONY: clean
clean:
	rm -f $(OBJS) rvs_library_test
