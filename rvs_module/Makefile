K ?= /lib/modules/$(shell uname -r)/build

obj-m += rvs.o

modules clean:
	make -C $(K) M=$(PWD) $@

