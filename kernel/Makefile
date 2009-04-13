# Configuration
CONFIG_QUICKUSB := m

# Kernel version
KVER := $(shell uname -r)

# Kernel source directory
KSRCDIR = /lib/modules/$(KVER)/build

# Kernel major version (2_4 or 2_6)
KVERPARTS = $(subst ., ,$(KVER))
KVERMAJOR = $(word 1,$(KVERPARTS))_$(word 2,$(KVERPARTS))

# Compiler flags
EXTRA_CFLAGS += -Wall -I${PWD}

# For 2.6 kernel modules
ifeq ($(KVERMAJOR),2_6)

obj-$(CONFIG_QUICKUSB) += quickusb.o

all :
	$(MAKE) -C $(KSRCDIR) M=$(PWD) modules

clean :
	rm -f *.o *.s *.ko *.mod.c
	$(MAKE) -C $(KSRCDIR) M=$(PWD) clean

endif

# For 2.4 kernel modules
ifeq ($(KVERMAJOR),2_4)

CFLAGS_2_4 = -D__KERNEL__ -DMODULE -DEXPORT_SYMTAB -I$(KSRCDIR)/include \
		-Wstrict-prototypes -Wno-trigraphs \
		-O2 -fno-strict-aliasing -fno-common -fomit-frame-pointer \
		-DMODVERSIONS -include $(KSRCDIR)/include/linux/modversions.h \
		-nostdinc -iwithprefix include -DKBUILD_BASENAME=QUICKUSB

all : quickusb.o

%.o : %.c *.h
	@echo [CC] $@
	@$(CC) $(CFLAGS) $(CFLAGS_2_4) $(EXTRA_CFLAGS) -c -o $@ $<

%.s : %.c *.h
	$(CC) $(CFLAGS) $(CFLAGS_2_4) $(EXTRA_CFLAGS) -S -c -o $@ $<

clean :
	rm -f *.o *.s

endif
