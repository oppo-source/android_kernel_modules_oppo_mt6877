# SPDX-License-Identifier: GPL-2.0-only

#only for MTK platform
ifeq ($(CHIPSET_COMPANY), MTK)
KBUILD_OPTIONS += CONFIG_OPLUS_WIFI_SWTP=m
endif

KERNEL_SRC ?= /lib/modules/$(shell uname -r)/build

M ?= $(shell pwd)
modules modules_install clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) $(KBUILD_OPTIONS) $(@)