# SPDX-License-Identifier: GPL-2.0-only
# Convenience wrapper for building the driver out of tree against a kernel source tree.
# Buildroot does not use this file (it calls Kbuild directly); static_analysis.sh does.
# Usage: make KDIR=<kernel build dir> ARCH=arm64 CROSS_COMPILE=<prefix> [C=2]

KDIR ?= /lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean

.PHONY: all clean
