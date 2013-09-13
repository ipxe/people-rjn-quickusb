INTRODUCTION
------------

The BitwiseSystems QuickUSB module uses a Cypress CY7C68013A-128AXC EZ-USB FX2LP microcontroller to implement a set of USB 2.0 I/O ports:

  1x 16-bit high-speed parallel port, with FIFO handshaking (up to 20 MB/second)   [can also be used as 2x GPIO ports]
  3x 8 bit GPIO ports
  2x RS-232 ports
  1x I2C port
  1x SPI port

There is also a 48 MHz clock output. For more details, see: http://www.bitwisesys.com/qusb2-p/qusb2.htm

The device has a vendor_id:product_id of 0fbb:0001. Our module identifies as "QuickUSB QUSB2 Module v2.11rc7 (FIFO Handshake)".


DRIVER
------

It isn't supplied with a proper open-source Linux driver, so we wrote one. It currently builds on Kernel 3.8 (though it can be built on earlier 2.6 kernels, and even on 2.4).
[Comparison: the Bitwise Systems driver is a binary blob that uses libusb.]

The driver supports the high-speed 16-bit port in either master or slave mode, and it supports the 2x GPIO ports. The 2x RS-232 ports, the I2C and SPI ports are NOT implemented
at present. The scatter-gather mechanism allows for reading/writing large amounts of data (several MB) at a time, ensuring that read() and write() will always succeed to completion
[i.e. that, the read/write is never partial].

The driver is fully hotplug-capable: it won't crash/panic even if the device is unplugged while busy.


USERSPACE
---------

See setquickusb for the ioctls and manpage.

The HSP can be used in fifo master mode (as /dev/qu0hd), in fifo slave mode (as /dev/ttyUSB0), or as 2 separate GPIO ports (/dev/qu0gb and /dev/qu0gd). 
The mode is automatically selected depending on which device is opened. It is little-endian: byte B is read first.

The other ports /dev/qu0ga, /dev/qu0gc, /dev/qu0ge are GPIO ports, and the direction of each bit may be controlled separately, by setquickusb.

Simply read and write to them as normal, using cat,echo,dd,read(),write() etc.


NOTES
-----

The QUSB tends to run extremely hot. It's advisable to glue a heatsink to it.

The RS-232 ports on the module are only RxD/TxD/Gnd; they are not full RS-232 ports with the usual 9 pins. (they are not supported by this driver anyway).

It is still required to use the Windows tool to:
  * Flash new firmware to the QUSB device.
  * Change the default (at poweron) directions of the I/O ports.
  
Bug: there is no way for the USB host to purge the the QUSB's internal FIFO to start from a known empty position.

This device (with our driver) has been used in the physics departments at Cambridge University (COAST telescope) and at DESY (the XFEL project).



AUTHORS
-------

Michael Brown (Fensystems.co.uk) with Contributions from Richard Neill, Sergey Esenov, and Dan Lynch.


CONTENTS
--------
	kernel			- The quickusb driver for the Linux kernel (both 2.4 and 2.6).

	setquickusb		- Utility for changing some parameters of the QUSB device. (ioctls)

	LICENCE.txt		- GPL (v2 or later, to be compatible with kernel!)

	README.txt		- This readme

	bitwise_systems_stuff	- Symlink to the directory with all the various things supplied by Bitwise Systems.
				  Most of that is not required, because this driver obsoletes it. It may be useful
				  for testing, firmware changes, windows support, and documentation.
