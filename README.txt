The QuickUSB device, as provided by bitwise systems has a binary-blob userspace library, working on top of libusb. It's generally horrible; what we want, and have written, 
is a proper GPL'd Linux kernel driver. This is complete, and works on both 2.4 and 2.6, and it creates some sensible nodes in /dev; just read and write to them as normal. 
setquickusb  handles the ioctls to do GPIO port direction etc.  (Unlike the BitwiseSystems "driver", no messing around with libusb is required.)

There are still some ugly limitations on what the device can actually do, especially in that there is no way to clear the internal FIFO to start from a known empty position!
PortIO direction control is also implemented in an ugly manner.

Here is what we have in  /dev  (see also setquickusb --help):

        /dev/qu0ga      First QUSB device, General Purpose Port A
        /dev/qu0gb      First QUSB device, General Purpose Port B
        /dev/qu0gc      First QUSB device, General Purpose Port C
        /dev/qu0gd      First QUSB device, General Purpose Port D
        /dev/qu0ge      First QUSB device, General Purpose Port E

        /dev/qu0hc      First QUSB device, High Speed Port, Control
        /dev/qu0hd      First QUSB device, High Speed Port, Data

Note 1: the high-speed port uses the same pins as G.P. ports B,D.
Note 2: the 16-bit HSP (/dev/qu0hd) is little-endian. Byte B is read first.
Note 3: the RS232 serial ports are not implemented in this driver.
Note 4: this driver *is* well-behaved when being hot-plugged/unplugged: device nodes appear/disappear correctly, and no panic will result if the device is unplugged while in use.
Note 5: setting the output mask on a port configured for high-speed (either hc, or the corresponding gb,gd) will MESS IT UP. Don't do it!
Note 6: it's still necessary to have Windows, if we want to flash the firmware.



Contents:
	kernel			- The quickusb driver for the Linux kernel (both 2.4 and 2.6).
				  This is finished, and in a releasable state. (We don't really recommend
				  using this hardware if you have a choice though).

	setquickusb		- Utility for changing some parameters of the QUSB device.  (ioctls)

	LICENCE.txt		- GPL (v2 or later, to be compatible with kernel!)

	README.txt		- This readme

	bitwise_systems_stuff	- Symlink to the directory with all the various things supplied by Bitwise Systems.
				  Most of that is not required, because this driver obsoletes it. It may be useful
				  for testing, firmware changes, windows support, and documentation.

Mostly written by Michael Brown, with contributions from Richard Neill, Sergey Esenov, and Dan Lynch

