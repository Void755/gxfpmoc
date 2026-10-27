# SPDX-License-Identifier: GPL-2.0-only
obj-m += gxfp.o

ccflags-y += -I$(src)/include/uapi

KDIR ?= $(or $(KERNELDIR),/lib/modules/$(shell uname -r)/build)

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean

install:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules_install

.PHONY: all clean install
