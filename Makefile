all :: setquickusb

setquickusb : setquickusb.c kernel/quickusb.h
	$(CC) -Wall -O2 $(CFLAGS) $(EXTRA_CFLAGS) -o $@ $<

clean ::
	rm -f setquickusb
