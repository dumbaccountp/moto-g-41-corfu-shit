# SPDX-License-Identifier: GPL-2.0-only
#
# In-tree & out-of-tree Kbuild Makefile for Hybrid-Mount v6.2.5 LKMs:
#   - hybridmount.ko (VFS path redirection subsystem, protocol "hm1")
#   - nuke.ko        (ext4 sysfs node unregister LKM)

ifneq ($(KERNELRELEASE),)

ifeq ($(CONFIG_HYBRIDMOUNT)$(CONFIG_HYBRIDMOUNT_NUKE),)
obj-m += hybridmount.o nuke.o
else
obj-$(CONFIG_HYBRIDMOUNT) += hybridmount.o
obj-$(CONFIG_HYBRIDMOUNT_NUKE) += nuke.o
endif

ccflags-y += -std=gnu11 -Wno-declaration-after-statement

else

KDIR ?= /lib/modules/$(shell uname -r)/build
MDIR := $(realpath $(dir $(abspath $(lastword $(MAKEFILE_LIST)))))

.PHONY: all clean

all:
	$(MAKE) -C $(KDIR) M=$(MDIR) modules

clean:
	$(MAKE) -C $(KDIR) M=$(MDIR) clean

endif
