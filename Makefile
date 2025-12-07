obj-m += ahci_lld.o

ahci_lld-objs := ahci_lld_main.o ahci_lld_hba.o ahci_lld_port.o ahci_lld_util.o

KDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

install:
	$(MAKE) -C $(KDIR) M=$(PWD) modules_install

load:
	sudo insmod ahci_lld.ko

unload:
	sudo rmmod ahci_lld

reload: unload load

.PHONY: all clean install load unload reload
