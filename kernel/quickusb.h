#ifndef QUICKUSB_H
#define QUICKUSB_H

#define QUICKUSB_BREQUEST 0xb3
#define QUICKUSB_BREQUESTTYPE_READ 0xc0
#define QUICKUSB_BREQUESTTYPE_WRITE 0x40
#define QUICKUSB_MAX_DATA_LEN 64
#define QUICKUSB_TIMEOUT ( 1 * HZ )

/**
 * quickusb_write_port_dir
 *
 * @usb: USB device
 * @address: Port number
 * @outputs: Output bit mask
 *
 * Returns 0 for success, or negative error number
 */
static inline int quickusb_write_port_dir ( struct usb_device *usb,
					    unsigned int address,
					    unsigned char outputs ) {
	int ret;

	ret =  usb_control_msg ( usb, usb_sndctrlpipe ( usb, 0 ),
				 QUICKUSB_BREQUEST,
				 QUICKUSB_BREQUESTTYPE_WRITE, 0, 0,
				 &outputs, sizeof ( outputs ),
				 QUICKUSB_TIMEOUT );

	if ( ret > 0 ) {
		ret = 0;
	}

	return ret;
}

/**
 * quickusb_read_port - read data from GPPIO port
 *
 * @usb: USB device
 * @address: Port number
 * @data: Data buffer
 * @len: Length of data to read (max QUICKUSB_MAX_DATA_LEN)
 *
 * Returns 0 for success, or negative error number
 */
static inline int quickusb_read_port ( struct usb_device *usb,
				       unsigned int address,
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
 * quickusb_write_port - write data to GPPIO port
 *
 * @usb: USB device
 * @address: Port number
 * @data: Data to be written
 * @len: Length of data to write (max QUICKUSB_MAX_DATA_LEN)
 *
 * Returns 0 for success, or negative error number
 */
static inline int quickusb_write_port ( struct usb_device *usb,
					unsigned int address,
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
