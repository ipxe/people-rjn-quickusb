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
#include <linux/fs.h>
#include <linux/kref.h>
#include <linux/usb.h>
#include <asm/uaccess.h>

#define QUICKUSB_VENDOR_ID 0x0fbb
#define QUICKUSB_DEVICE_ID 0x0001

#define QUICKUSB_MINOR_BASE 0

#define QUICKUSB_BREQUEST 0xb3
#define QUICKUSB_BREQUESTTYPE_READ 0xc0
#define QUICKUSB_BREQUESTTYPE_WRITE 0x40
#define QUICKUSB_MAX_DATA_LEN 64
#define QUICKUSB_TIMEOUT ( 1 * HZ )

static int debug = 0;

static DECLARE_MUTEX ( quickusb_lock );

static struct usb_driver quickusb_driver;

struct quickusb_device {
	struct usb_device *usbdev;
	struct usb_interface *interface;
	struct kref kref;
};

static void quickusb_delete ( struct kref *kref ) {
	struct quickusb_device *quickusb;

	quickusb = container_of ( kref, struct quickusb_device, kref );
	usb_put_dev ( quickusb->usbdev );
	kfree ( quickusb );
}

static int quickusb_gppio_read ( struct file *file, char __user *user_data,
				 size_t len, loff_t *ppos ) {
	struct quickusb_device *quickusb = file->private_data;
	struct usb_device *usbdev = quickusb->usbdev;
	unsigned char data[QUICKUSB_MAX_DATA_LEN];
	unsigned int frag_len;
	int rc = 0;

	while ( len ) {
		frag_len = ( len < sizeof ( data ) ) ? len : sizeof ( data );
		if ( ( rc = usb_control_msg ( usbdev,
					      usb_sndctrlpipe ( usbdev, 0 ),
					      QUICKUSB_BREQUEST,
					      QUICKUSB_BREQUESTTYPE_WRITE,
					      0, 1, data, frag_len,
					      QUICKUSB_TIMEOUT ) ) != 0 )
			goto out;
		if ( ( rc = copy_to_user ( user_data, data, frag_len ) ) != 0 )
			goto out;
		len -= frag_len;
		user_data += frag_len;
	}

 out:
	return rc;
}

static ssize_t quickusb_gppio_write ( struct file *file,
				      const char __user *user_data,
				      size_t len, loff_t *ppos ) {
	struct quickusb_device *quickusb = file->private_data;
	struct usb_device *usbdev = quickusb->usbdev;
	unsigned char data[QUICKUSB_MAX_DATA_LEN];
	unsigned int frag_len;
	int rc = 0;

	while ( len ) {
		frag_len = ( len < sizeof ( data ) ) ? len : sizeof ( data );
		if ( ( rc = copy_from_user ( data, user_data,
					     frag_len ) ) != 0 )
			goto out;
		if ( ( rc = usb_control_msg ( usbdev,
					      usb_sndctrlpipe ( usbdev, 0 ),
					      QUICKUSB_BREQUEST,
					      QUICKUSB_BREQUESTTYPE_WRITE,
					      0, 1, data, frag_len,
					      QUICKUSB_TIMEOUT ) ) != 0 )
			goto out;
		len -= frag_len;
		user_data += frag_len;
	}

 out:
	return rc;
}

static int quickusb_open ( struct inode *inode, struct file *file ) {
	struct usb_interface *interface;
	struct quickusb_device *quickusb;
	
	interface = usb_find_interface ( &quickusb_driver, iminor ( inode ) );
	if ( ! interface )
		return -ENODEV;

	quickusb = usb_get_intfdata ( interface );
	if ( ! quickusb )
		return -ENODEV;

	kref_get ( &quickusb->kref );
	file->private_data = quickusb;

	return 0;
}

static int quickusb_release ( struct inode *inode, struct file *file ) {
	struct quickusb_device *quickusb = file->private_data;

	kref_put ( &quickusb->kref, quickusb_delete );
	return 0;
}

static struct file_operations quickusb_fops = {
	.owner		= THIS_MODULE,
	.open		= quickusb_open,
	.read		= quickusb_gppio_read,
	.write		= quickusb_gppio_write,
	.release	= quickusb_release,
};

static struct usb_class_driver quickusb_class = {
	.name		= "usb/quickusb%d",
	.fops		= &quickusb_fops,
	.mode		= ( S_IFCHR | S_IRUSR | S_IWUSR |
			    S_IRGRP | S_IWGRP | S_IROTH ),
	.minor_base	= QUICKUSB_MINOR_BASE,
};

static int quickusb_probe ( struct usb_interface *interface,
			    const struct usb_device_id *id ) {
	struct quickusb_device *quickusb = NULL;
	int rc = 0;

	down ( &quickusb_lock );

	/* Create new quickusb device structure */
	quickusb = kmalloc ( sizeof ( *quickusb ), GFP_KERNEL );
	if ( ! quickusb ) {
		rc = -ENOMEM;
		goto err;
	}
	memset ( quickusb, 0, sizeof ( *quickusb ) );
	kref_init ( &quickusb->kref );
	quickusb->usbdev = usb_get_dev ( interface_to_usbdev ( interface ) );
	quickusb->interface = interface;

	/* Record driver private data */
	usb_set_intfdata ( interface, quickusb );

	/* Register device */
	if ( ( rc = usb_register_dev ( interface, &quickusb_class ) ) != 0 ) {
		printk ( KERN_ERR "quickusb unable to register device\n" );
		goto err;
	}

	printk ( KERN_INFO "quickusb %d connected\n", interface->minor ); 
	goto out;

 err:
	usb_set_intfdata ( interface, NULL );
	if ( quickusb )
		kref_put ( &quickusb->kref, quickusb_delete );
 out:
	up ( &quickusb_lock );
	return rc;
}

static void quickusb_disconnect ( struct usb_interface *interface ) {
	struct quickusb_device *quickusb = usb_get_intfdata ( interface );
	unsigned int minor = interface->minor;

	down ( &quickusb_lock );
	usb_set_intfdata ( interface, NULL );
	usb_deregister_dev ( interface, &quickusb_class );
	up ( &quickusb_lock );

	kref_put ( &quickusb->kref, quickusb_delete );

	printk ( KERN_INFO "quickusb %d disconnected\n", minor );
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

MODULE_AUTHOR ( "Michael Brown <mbrown@fensystems.co.uk>" );
MODULE_DESCRIPTION ( "QuickUSB serial driver" );
MODULE_LICENSE ( "GPL" );
MODULE_DEVICE_TABLE ( usb, quickusb_ids );

module_param ( debug, bool, S_IRUGO | S_IWUSR );
MODULE_PARM_DESC ( debug, "Enable debugging" );
