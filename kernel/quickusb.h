#ifndef QUICKUSB_H
#define QUICKUSB_H

#ifdef __KERNEL__

#include <linux/ioctl.h>

#define QUICKUSB_BREQUEST_SETTING	0xb0
#define QUICKUSB_BREQUEST_HSPIO_COMMAND	0xb2
#define QUICKUSB_BREQUEST_GPPIO		0xb3
#define QUICKUSB_BREQUEST_HSPIO		0xb7

#define QUICKUSB_BREQUESTTYPE_READ	0xc0
#define QUICKUSB_BREQUESTTYPE_WRITE	0x40

#define QUICKUSB_BULK_OUT_EP		0x02
#define QUICKUSB_BULK_IN_EP		0x86

#define QUICKUSB_MAX_DATA_LEN		64
#define QUICKUSB_MAX_BULK_DATA_LEN	512

#define QUICKUSB_WINDEX_GPPIO_DIR	0
#define QUICKUSB_WINDEX_GPPIO_DATA	1

#define QUICKUSB_SETTING_FIFOCONFIG	3
#define QUICKUSB_SETTING_GPPIO(port)	( 9 + (port) )

#define QUICKUSB_HSPPMODE_GPIO		0x00
#define QUICKUSB_HSPPMODE_MASTER	0x02
#define QUICKUSB_HSPPMODE_SLAVE		0x03
#define QUICKUSB_HSPPMODE_MASK		0x03

#define QUICKUSB_TIMEOUT ( 1 * HZ )

/**
 * quickusb_read_setting - read device setting
 *
 * @usb: USB device
 * @address: Setting address
 * @setting: Value of the setting
 *
 * Returns 0 for success, or negative error number
 */
static inline int quickusb_read_setting ( struct usb_device *usb,
					  unsigned int address,
					  uint16_t *setting ) {
	uint16_t setting_le;
	int ret;

	ret = usb_control_msg ( usb, usb_rcvctrlpipe ( usb, 0 ),
				QUICKUSB_BREQUEST_SETTING,
				QUICKUSB_BREQUESTTYPE_READ,
				0, address,
				&setting_le, sizeof ( setting_le ),
				QUICKUSB_TIMEOUT );
	if ( ret < 0 )
		return ret;

	*setting = le16_to_cpu ( setting_le );
	return 0;
}

/**
 * quickusb_write_setting - write device setting
 *
 * @usb: USB device
 * @address: Setting address
 * @setting: Value of the setting
 *
 * Returns 0 for success, or negative error number
 */
static inline int quickusb_write_setting ( struct usb_device *usb,
					   unsigned int address,
					   uint16_t setting ) {
	uint16_t setting_le = cpu_to_le16 ( setting );
	int ret;

	ret = usb_control_msg ( usb, usb_sndctrlpipe ( usb, 0 ),
				QUICKUSB_BREQUEST_SETTING,
				QUICKUSB_BREQUESTTYPE_WRITE,
				0, address,
				&setting_le, sizeof ( setting_le ),
				QUICKUSB_TIMEOUT );
	if ( ret < 0 )
		return ret;

	return 0;
}

/**
 * quickusb_read_default - read device default setting
 *
 * @usb: USB device
 * @address: Setting address
 * @setting: Value of the setting
 *
 * Returns 0 for success, or negative error number
 */
static inline int quickusb_read_default ( struct usb_device *usb,
					  unsigned int address,
					  uint16_t *setting ) {
	return -ENOTTY;
}

/**
 * quickusb_write_default - write device default setting
 *
 * @usb: USB device
 * @address: Setting address
 * @setting: Value of the setting
 *
 * Returns 0 for success, or negative error number
 */
static inline int quickusb_write_default ( struct usb_device *usb,
					   unsigned int address,
					   uint16_t setting ) {
	return -ENOTTY;
}

/**
 * quickusb_read_command - read HSPIO port with a command cycle
 *
 * @usb: USB device
 * @address: Starting address
 * @data: Data buffer
 * @len: Length of data to read (max QUICKUSB_MAX_DATA_LEN)
 *
 * Returns 0 for success, or negative error number
 */
static inline int quickusb_read_command ( struct usb_device *usb,
					  uint16_t address,
					  void *data, size_t len ) {
	int ret;
	
	ret = usb_control_msg ( usb, usb_rcvctrlpipe ( usb, 0 ),
				QUICKUSB_BREQUEST_HSPIO_COMMAND,
				QUICKUSB_BREQUESTTYPE_READ,
				len, address,
				data, len, QUICKUSB_TIMEOUT );
	if ( ret < 0 )
		return ret;

	return 0;
}

/**
 * quickusb_write_command - write HSPIO port with a command cycle
 *
 * @usb: USB device
 * @address: Starting address
 * @data: Data to be written
 * @len: Length of data to write (max QUICKUSB_MAX_DATA_LEN)
 *
 * Returns 0 for success, or negative error number
 */
static inline int quickusb_write_command ( struct usb_device *usb,
					   uint16_t address,
					   void *data, size_t len ) {
	int ret;
	
	ret = usb_control_msg ( usb, usb_sndctrlpipe ( usb, 0 ),
				QUICKUSB_BREQUEST_HSPIO_COMMAND,
				QUICKUSB_BREQUESTTYPE_WRITE,
				len, address,
				data, len, QUICKUSB_TIMEOUT );
	if ( ret < 0 )
		return ret;

	return 0;
}

/**
 * quickusb_read_data - read HSPIO port with a data cycle
 *
 * @usb: USB device
 * @data: Data buffer
 * @len: Length of data to read (max QUICKUSB_MAX_BULK_DATA_LEN)
 *
 * Returns 0 for success, or negative error number
 */
static inline int quickusb_read_data ( struct usb_device *usb,
				       void *data, size_t len ) {
	uint32_t len_le = cpu_to_le32 ( len );
	int actual_length;
	int ret;
	
	ret = usb_control_msg ( usb, usb_sndctrlpipe ( usb, 0 ),
				QUICKUSB_BREQUEST_HSPIO,
				QUICKUSB_BREQUESTTYPE_WRITE,
				0, 0,
				&len_le, sizeof ( len_le ),
				QUICKUSB_TIMEOUT );
	if ( ret < 0 )
		return ret;

	ret = usb_bulk_msg ( usb,
			     usb_rcvbulkpipe ( usb, QUICKUSB_BULK_IN_EP ),
			     data, len, &actual_length, QUICKUSB_TIMEOUT );
	if ( ret < 0 )
		return ret;

	return 0;
}

/**
 * quickusb_write_data - write HSPIO port with a data cycle
 *
 * @usb: USB device
 * @data: Data to be written
 * @len: Length of data to write (max QUICKUSB_MAX_BULK_DATA_LEN)
 *
 * Returns 0 for success, or negative error number
 */
static inline int quickusb_write_data ( struct usb_device *usb,
					void *data, size_t len ) {
	int actual_length;
	int ret;
	
	ret = usb_bulk_msg ( usb,
			     usb_sndbulkpipe ( usb, QUICKUSB_BULK_OUT_EP ),
			     data, len, &actual_length, QUICKUSB_TIMEOUT );
	if ( ret < 0 )
		return ret;

	return 0;
}

/**
 * quickusb_read_port_dir - read GPPIO port output enables
 *
 * @usb: USB device
 * @address: Port number
 * @outputs: Output bit mask
 *
 * Returns 0 for success, or negative error number
 */
static inline int quickusb_read_port_dir ( struct usb_device *usb,
					   unsigned int address,
					   uint8_t *outputs ) {
	int ret;
	
	ret = usb_control_msg ( usb, usb_rcvctrlpipe ( usb, 0 ),
				QUICKUSB_BREQUEST_GPPIO,
				QUICKUSB_BREQUESTTYPE_READ,
				address, QUICKUSB_WINDEX_GPPIO_DIR,
				outputs, sizeof ( *outputs ),
				QUICKUSB_TIMEOUT );
	if ( ret < 0 )
		return ret;

	return 0;
}

/**
 * quickusb_write_port_dir - set GPPIO port output enables
 *
 * @usb: USB device
 * @address: Port number
 * @outputs: Output bit mask
 *
 * Returns 0 for success, or negative error number
 */
static inline int quickusb_write_port_dir ( struct usb_device *usb,
					    unsigned int address,
					    uint8_t outputs ) {
	int ret;

	ret = usb_control_msg ( usb, usb_sndctrlpipe ( usb, 0 ),
				QUICKUSB_BREQUEST_GPPIO,
				QUICKUSB_BREQUESTTYPE_WRITE,
				address, QUICKUSB_WINDEX_GPPIO_DIR,
				&outputs, sizeof ( outputs ),
				QUICKUSB_TIMEOUT );
	if ( ret < 0 )
		return ret;

	return 0;
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
				       void *data, size_t len ) {
	int ret;
	
	ret = usb_control_msg ( usb, usb_rcvctrlpipe ( usb, 0 ),
				QUICKUSB_BREQUEST_GPPIO,
				QUICKUSB_BREQUESTTYPE_READ,
				address, QUICKUSB_WINDEX_GPPIO_DATA,
				data, len, QUICKUSB_TIMEOUT );
	if ( ret < 0 )
		return ret;

	return 0;
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
					void *data, size_t len ) {
	int ret;

	ret = usb_control_msg ( usb, usb_sndctrlpipe ( usb, 0 ),
				QUICKUSB_BREQUEST_GPPIO,
				QUICKUSB_BREQUESTTYPE_WRITE,
				address, QUICKUSB_WINDEX_GPPIO_DATA,
				data, len, QUICKUSB_TIMEOUT );
	if ( ret < 0 )
		return ret;

	return 0;
}

#endif /* __KERNEL__ */

/****************************************************************************
 *
 * ioctls
 *
 */

typedef uint32_t quickusb_gppio_ioctl_data_t;

#define QUICKUSB_IOC_GPPIO_GET_OUTPUTS \
	_IOR ( 'Q', 0x00, quickusb_gppio_ioctl_data_t )
#define QUICKUSB_IOC_GPPIO_SET_OUTPUTS \
	_IOW ( 'Q', 0x01, quickusb_gppio_ioctl_data_t )
#define QUICKUSB_IOC_GPPIO_GET_DEFAULT_OUTPUTS \
	_IOR ( 'Q', 0x02, quickusb_gppio_ioctl_data_t )
#define QUICKUSB_IOC_GPPIO_SET_DEFAULT_OUTPUTS \
	_IOW ( 'Q', 0x03, quickusb_gppio_ioctl_data_t )
#define QUICKUSB_IOC_GPPIO_GET_DEFAULT_LEVELS \
	_IOR ( 'Q', 0x04, quickusb_gppio_ioctl_data_t )
#define QUICKUSB_IOC_GPPIO_SET_DEFAULT_LEVELS \
	_IOW ( 'Q', 0x05, quickusb_gppio_ioctl_data_t )

typedef struct quickusb_setting_ioctl_data {
	uint16_t address;
	uint16_t value;
} quickusb_setting_ioctl_data_t;

#define QUICKUSB_IOC_GET_SETTING \
	_IOWR ( 'Q', 0x06, struct quickusb_setting_ioctl_data )
#define QUICKUSB_IOC_SET_SETTING \
	_IOW ( 'Q', 0x07, struct quickusb_setting_ioctl_data )

#endif /* QUICKUSB_H */
