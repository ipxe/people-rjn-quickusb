# Configuration
CONFIG_QUICKUSB := m

# Kernel version
KVER := $(shell uname -r)

# Kernel source directory
KSRCDIR = /lib/modules/$(KVER)/build

# Compiler flags
EXTRA_CFLAGS += -Wall -I${PWD}

obj-$(CONFIG_QUICKUSB) += quickusb.o

all :
	$(MAKE) -C $(KSRCDIR) M=$(PWD) modules

clean :
	rm -f *.o *.s *.ko *.mod.c
	$(MAKE) -C $(KSRCDIR) M=$(PWD) clean
