# Configuration
CONFIG_QUICKUSB := m

# Kernel version
KVER := $(shell uname -r)

# Kernel source directory
KSRCDIR = /lib/modules/$(KVER)/build

# Compiler flags
EXTRA_CFLAGS += -Wall -I${PWD} -I$(KSRCDIR)/drivers/usb/serial

obj-$(CONFIG_QUICKUSB) += quickusb.o

all :
	$(MAKE) -C $(KSRCDIR) M=$(PWD) modules

clean :
	rm -f *.o *.s *.ko *.mod.c
	$(MAKE) -C $(KSRCDIR) M=$(PWD) clean

install :
	mkdir -p /lib/modules/`uname -r`/kernel/3rdparty/quickusb
	cp quickusb.ko /lib/modules/`uname -r`/kernel/3rdparty/
	/sbin/depmod -A

uninstall :
	rm -f /lib/modules/`uname -r`/kernel/3rdparty/quickusb.ko
