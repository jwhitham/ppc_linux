#
# Makefile for the TDM core.
#

obj-$(CONFIG_TDM)		+= tdm-core.o device/
obj-$(CONFIG_TDM_TEST)		+= test/
ifeq ($(CONFIG_TDM_DEBUG_CORE),y)
EXTRA_CFLAGS += -DDEBUG
endif
