#
# Makefile for the Freescale Ethernet controllers
#
ccflags-y += -DVERSION=\"\"
#
# Include netcomm SW specific definitions
include $(srctree)/drivers/net/ethernet/freescale/fman/ncsw_config.mk

ccflags-y += -I$(NET_DPA)

obj-$(CONFIG_FSL_DPAA_1588) += dpaa_1588.o
# dpaa_debugfs needs to be initialized before dpaa_eth
obj-$(CONFIG_FSL_DPAA_ETH_DEBUGFS) += dpaa_debugfs.o dpaa_generic_debugfs.o
obj-$(CONFIG_FSL_DPAA_ETH) += fsl-mac.o fsl-dpa.o fsl-dpa-common.o \
 fsl-dpa-generic.o
obj-$(CONFIG_PTP_1588_CLOCK_DPAA) += dpaa_ptp.o

fsl-dpa-objs	:= dpaa_ethtool.o dpaa_eth_sysfs.o \
  dpaa_eth.o dpaa_eth_sg.o
fsl-dpa-common-objs	:= dpaa_eth_common.o
# Advanced drivers
ifeq ($(CONFIG_FSL_DPAA_ADVANCED_DRIVERS),y)
obj-$(CONFIG_FSL_DPAA_ETH) += fsl_advanced.o

fsl_advanced-objs += dpaa_eth_base.o dpaa_eth_proxy.o dpaa_eth_shared.o dpaa_eth_macless.o

ifeq ($(CONFIG_FSL_DPAA_OFFLINE_PORTS),y)
obj-$(CONFIG_FSL_DPAA_ETH) += fsl_oh.o

fsl_oh-objs += offline_port.o
endif
endif

fsl-dpa-generic-objs	:= dpaa_eth_generic.o dpaa_eth_generic_sysfs.o \
 dpaa_generic_ethtool.o
fsl-mac-objs	:= mac.o mac-api.o
obj-$(CONFIG_FSL_CAPWAP) += capwap/

# Needed by the tracing framework
CFLAGS_dpaa_eth.o := -I$(src)
