#ifndef QUICKUSB_H
#define QUICKUSB_H

#define QUICKUSB_BREQUEST 0xb3
#define QUICKUSB_BREQUESTTYPE_READ 0xc0
#define QUICKUSB_BREQUESTTYPE_WRITE 0x40
#define QUICKUSB_MAX_DATA_LEN 64
#define QUICKUSB_TIMEOUT ( 1 * HZ )

/**
 * QuickUsbReadPort - read data from GPPIO port
 *
 * @usb: USB device
 * @address: Port number
 * @data: Data buffer
 * @len: Length of data to read (max QUICKUSB_MAX_DATA_LEN)
 *
 * Returns 0 for success, or negative error number
 */
static inline int quickusb_read_port ( struct usb_device *usb,
				       unsigned char address,
				       unsigned char *data,
				       size_t *len ) {
	int ret;
	
	ret =  usb_control_msg ( usb, usb_rcvctrlpipe ( usb, 0 ),
				 QUICKUSB_BREQUEST,
				 QUICKUSB_BREQUESTTYPE_READ, 0, 1, data, *len,
				 QUICKUSB_TIMEOUT );
	if ( ret > 0 ) {
		*len = ret;
		ret = 0;
	}

	return ret;
}

/**
 * QuickUsbWritePort - write data to GPPIO port
 *
 * @usb: USB device
 * @address: Port number
 * @data: Data to be written
 * @len: Length of data to write (max QUICKUSB_MAX_DATA_LEN)
 *
 * Returns 0 for success, or negative error number
 */
static inline int quickusb_write_port ( struct usb_device *usb,
					unsigned char address,
					unsigned char *data,
					size_t *len ) {
	int ret;

	ret =  usb_control_msg ( usb, usb_sndctrlpipe ( usb, 0 ),
				 QUICKUSB_BREQUEST,
				 QUICKUSB_BREQUESTTYPE_WRITE, 0, 1, data, *len,
				 QUICKUSB_TIMEOUT );

	if ( ret > 0 ) {
		*len = ret;
		ret = 0;
	}

	return ret;
}

#endif /* QUICKUSB_H */
