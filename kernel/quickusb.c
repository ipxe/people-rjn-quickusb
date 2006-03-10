/*
 * QuickUSB driver
 *
 * Copyright 2006 Michael Brown <mbrown@fensystems.co.uk>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 */

#include <linux/module.h>
#include <linux/usb.h>

#define QUICKUSB_VENDOR_ID 0x0fbb
#define QUICKUSB_DEVICE_ID 0x0001

static int debug;

static int quickusb_probe ( struct usb_interface *interface,
			    const struct usb_device_id *id ) {
	return 0;
}

static void quickusb_disconnect ( struct usb_interface *interface ) {
}

static struct usb_device_id quickusb_ids[] = {
	{ USB_DEVICE ( QUICKUSB_VENDOR_ID, QUICKUSB_DEVICE_ID ) },
	{ },
};

static struct usb_driver quickusb_driver = {
	.owner		= THIS_MODULE,
	.name		= "quickusb",
	.probe		= quickusb_probe,
	.disconnect	= quickusb_disconnect,
	.id_table	= quickusb_ids,
};

static int quickusb_init ( void ) {
	int rc;

	if ( ( rc = usb_register ( &quickusb_driver ) ) != 0 )
		return rc;
	return 0;
}

static void quickusb_exit ( void ) {
	usb_deregister ( &quickusb_driver );
}

module_init ( quickusb_init );
module_exit ( quickusb_exit );

/* Module information */
MODULE_AUTHOR ( "Michael Brown <mbrown@fensystems.co.uk>" );
MODULE_DESCRIPTION ( "QuickUSB serial driver" );
MODULE_LICENSE ( "GPL" );
MODULE_DEVICE_TABLE ( usb, quickusb_ids );

module_param ( debug, bool, S_IRUGO | S_IWUSR );
MODULE_PARM_DESC ( debug, "Enable debugging" );
