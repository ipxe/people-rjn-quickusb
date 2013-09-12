all :: setquickusb

setquickusb : setquickusb.c ../kernel/quickusb.h
	$(CC) -Wall -O2 $(CFLAGS) $(EXTRA_CFLAGS) -o $@ $<
	strip setquickusb
	bash man/setquickusb.1.sh

install ::
	@[ `whoami` = root ] || (echo "Error, please be root"; exit 1)
	cp setquickusb /usr/local/bin
	cp setquickusb.bashcompletion /etc/bash_completion.d/setquickusb
	cp man/setquickusb.1.bz2      /usr/local/share/man/man1/

uninstall ::
	@[ `whoami` = root ] || (echo "Error, please be root"; exit 1)
	rm -f /usr/local/bin/setquickusb
	rm -f /etc/bash_completion.d/setquickusb
	rm -f /usr/local/share/man/man1/setquickusb.1.bz2

clean ::
	rm -f setquickusb
	rm -f man/*.bz2 man/*.html

