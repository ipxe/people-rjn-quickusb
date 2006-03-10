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
#include <linux/devfs_fs_kernel.h>
#include <linux/kref.h>
#include <linux/usb.h>
#include <asm/uaccess.h>

#define QUICKUSB_VENDOR_ID 0x0fbb
#define QUICKUSB_DEVICE_ID 0x0001

#define QUICKUSB_BREQUEST 0xb3
#define QUICKUSB_BREQUESTTYPE_READ 0xc0
#define QUICKUSB_BREQUESTTYPE_WRITE 0x40
#define QUICKUSB_MAX_DATA_LEN 64
#define QUICKUSB_TIMEOUT ( 1 * HZ )

#define QUICKUSB_MAX_BOARDS 256
#define QUICKUSB_MAX_SUBDEV 16
#define QUICKUSB_SUBDEV_MASK ( QUICKUSB_MAX_SUBDEV - 1 )
#define QUICKUSB_BOARD( dev_minor ) ( (dev_minor) / QUICKUSB_MAX_SUBDEV )
#define QUICKUSB_SUBDEV( dev_minor ) ( (dev_minor) & QUICKUSB_SUBDEV_MASK )
#define QUICKUSB_MINOR( board, subdev ) \
	( (board) * QUICKUSB_MAX_SUBDEV + (subdev) )

#define QUICKUSB_SUBDEV_GPPIO_A 0


static const char *quickusb_subdev_name ( unsigned int subdev ) {
	switch ( subdev ) {
	case QUICKUSB_SUBDEV_GPPIO_A:
		return "gppio_a";
	default:
		return NULL;
	}
}

static int debug = 0;
static int dev_major = 0;

static DECLARE_MUTEX ( quickusb_lock );

struct quickusb_board {
	struct usb_device *usbdev;
	struct usb_interface *interface;
	struct kref kref;
	int board;
	unsigned long subdevs;
};

static struct quickusb_board *quickusb_boards[QUICKUSB_MAX_BOARDS];

static void quickusb_delete ( struct kref *kref ) {
	struct quickusb_board *quickusb;

	quickusb = container_of ( kref, struct quickusb_board, kref );
	usb_put_dev ( quickusb->usbdev );
	kfree ( quickusb );
}

static ssize_t quickusb_gppio_read ( struct file *file, char __user *user_data,
				     size_t len, loff_t *ppos ) {
	struct quickusb_board *quickusb = file->private_data;
	struct usb_device *usbdev = quickusb->usbdev;
	unsigned char data[QUICKUSB_MAX_DATA_LEN];
	int rc;

	if ( len > sizeof ( data ) )
		len = sizeof ( data );

	if ( ( rc = usb_control_msg ( usbdev, usb_rcvctrlpipe ( usbdev, 0 ),
				      QUICKUSB_BREQUEST,
				      QUICKUSB_BREQUESTTYPE_READ, 0, 1, data,
				      len, QUICKUSB_TIMEOUT ) ) < 0 )
		return rc;

	if ( ( rc = copy_to_user ( user_data, data, len ) ) != 0 )
		return rc;

	*ppos += len;
	return len;
}

static ssize_t quickusb_gppio_write ( struct file *file,
				      const char __user *user_data,
				      size_t len, loff_t *ppos ) {
	struct quickusb_board *quickusb = file->private_data;
	struct usb_device *usbdev = quickusb->usbdev;
	unsigned char data[QUICKUSB_MAX_DATA_LEN];
	int rc;

	if ( len > sizeof ( data ) )
		len = sizeof ( data );

	if ( ( rc = copy_from_user ( data, user_data, len ) ) != 0 )
		return rc;

	if ( ( rc = usb_control_msg ( usbdev, usb_sndctrlpipe ( usbdev, 0 ),
				      QUICKUSB_BREQUEST,
				      QUICKUSB_BREQUESTTYPE_WRITE, 0, 1, data,
				      len, QUICKUSB_TIMEOUT ) ) < 0 )
		return rc;

	*ppos += len;
	return len;
}

static int quickusb_open ( struct inode *inode, struct file *file ) {
	unsigned int board = QUICKUSB_BOARD ( iminor ( inode ) );
	unsigned int subdev = QUICKUSB_SUBDEV ( iminor ( inode ) );
	struct quickusb_board *quickusb;
	int rc = 0;

	down ( &quickusb_lock );

	/* Increase refcount on the board */
	quickusb = quickusb_boards[board];
	if ( ! quickusb ) {
		rc = -ENODEV;
		goto out;
	}
	kref_get ( &quickusb->kref );

	/* Set private data pointer */
	file->private_data = quickusb;

 out:
	up ( &quickusb_lock );
	return 0;
}

static int quickusb_release ( struct inode *inode, struct file *file ) {
	struct quickusb_board *quickusb = file->private_data;

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

static int quickusb_register_dev ( struct quickusb_board *quickusb ) {
	unsigned int board;
	unsigned int subdev;
	unsigned int dev_minor;
	const char *name;
	dev_t dev;
	int rc;

	/* Allocate a board number */
	for ( board = 0 ; board < QUICKUSB_MAX_BOARDS ; board++ ) {
		if ( ! quickusb_boards[board] ) {
			quickusb_boards[board] = quickusb;
			quickusb->board = board;
			break;
		}
	}
	if ( quickusb->board < 0 )
		return -ENOMEM;
	
	/* Create device nodes */
	for ( subdev = 0 ; subdev < QUICKUSB_MAX_SUBDEV ; subdev++ ) {
		dev_minor = QUICKUSB_MINOR ( quickusb->board, subdev );
		dev = MKDEV ( dev_major, dev_minor );
		name = quickusb_subdev_name ( subdev );
		if ( ! name )
			continue;
		if ( ( rc = devfs_mk_cdev ( dev,
					    ( S_IFCHR | S_IRUSR | S_IWUSR |
					      S_IRGRP | S_IWGRP ),
					    "quickusb/%d/%s", quickusb->board,
					    name ) ) != 0 )
			return rc;
		quickusb->subdevs |= ( 1 << subdev );
	}
	return 0;
}

static void quickusb_deregister_dev ( struct quickusb_board *quickusb ) {
	unsigned int subdev;
	
	if ( quickusb->board < 0 )
		return;

	/* Remove device nodes */
	for ( subdev = 0 ; subdev < QUICKUSB_MAX_SUBDEV ; subdev++ ) {
		if ( ! ( quickusb->subdevs & ( 1 << subdev ) ) )
			continue;
		devfs_remove ( "quickusb/%d/%s", quickusb->board,
			       quickusb_subdev_name ( subdev ) );
	}

	/* Free up board number */
	quickusb_boards[quickusb->board] = NULL;
	quickusb->board = -1;
}

static int quickusb_probe ( struct usb_interface *interface,
			    const struct usb_device_id *id ) {
	struct quickusb_board *quickusb = NULL;
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
	quickusb->board = -1;
	quickusb->usbdev = usb_get_dev ( interface_to_usbdev ( interface ) );
	quickusb->interface = interface;

	/* Record driver private data */
	usb_set_intfdata ( interface, quickusb );

	/* Register devices */
	if ( ( rc = quickusb_register_dev ( quickusb ) ) != 0 ) {
		printk ( KERN_ERR "quickusb unable to register devices\n" );
		goto err;
	}

	printk ( KERN_INFO "quickusb%d connected\n", quickusb->board ); 
	goto out;

 err:
	usb_set_intfdata ( interface, NULL );
	if ( quickusb ) {
		quickusb_deregister_dev ( quickusb );
		kref_put ( &quickusb->kref, quickusb_delete );
	}
 out:
	up ( &quickusb_lock );
	return rc;
}

static void quickusb_disconnect ( struct usb_interface *interface ) {
	struct quickusb_board *quickusb = usb_get_intfdata ( interface );

	printk ( KERN_INFO "quickusb%d disconnected\n", quickusb->board );

	down ( &quickusb_lock );
	quickusb_deregister_dev ( quickusb );
	usb_set_intfdata ( interface, NULL );
	up ( &quickusb_lock );

	kref_put ( &quickusb->kref, quickusb_delete );
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

	/* Register major char device */
	if ( ( rc = register_chrdev ( dev_major, "quickusb",
				      &quickusb_fops ) ) < 0 )
		goto err_chrdev;
	if ( ! dev_major ) {
		dev_major = rc;
		printk ( KERN_INFO "quickusb using major device %d\n",
			 dev_major );
	}
	if ( ( rc = usb_register ( &quickusb_driver ) ) != 0 )
		goto err_usb;

	return 0;

 err_usb:
	unregister_chrdev ( dev_major, "quickusb" );
 err_chrdev:
	return rc;
}

static void quickusb_exit ( void ) {
	usb_deregister ( &quickusb_driver );
	unregister_chrdev ( dev_major, "quickusb" );
}

module_init ( quickusb_init );
module_exit ( quickusb_exit );

MODULE_AUTHOR ( "Michael Brown <mbrown@fensystems.co.uk>" );
MODULE_DESCRIPTION ( "QuickUSB serial driver" );
MODULE_LICENSE ( "GPL" );
MODULE_DEVICE_TABLE ( usb, quickusb_ids );

module_param ( debug, bool, S_IRUGO | S_IWUSR );
MODULE_PARM_DESC ( debug, "Enable debugging" );

module_param ( dev_major, uint, S_IRUGO | S_IWUSR );
MODULE_PARM_DESC ( dev_major, "Major device number" );
