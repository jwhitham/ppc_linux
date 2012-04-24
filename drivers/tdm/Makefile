#
# Makefile for the TDM core.
#

obj-$(CONFIG_TDM)		+= tdm-core.o device/
ifeq ($(CONFIG_TDM_DEBUG_CORE),y)
EXTRA_CFLAGS += -DDEBUG
endif
