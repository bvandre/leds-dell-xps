ifeq ($(KERNELRELEASE),)

KERNELDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

.PHONY: build clean modules_install check

build:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) C=1 modules

modules_install:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules_install

check:
	$(KERNELDIR)/scripts/checkpatch.pl -f --strict leds-dell-xps.c

clean:
	rm -rf *.o *~ core .depend .*.cmd *.ko *.mod.c 
else  

$(info Building with KERNELRELEASE = ${KERNELRELEASE}) 
obj-m :=    leds-dell-xps.o
CFLAGS_dell-xps.o := -DDEBUG

endif
