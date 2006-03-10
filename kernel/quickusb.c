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
#include <linux/list.h>
#include <linux/fs.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/usb.h>

#define QUICKUSB_VENDOR_ID 0x0fbb
#define QUICKUSB_DEVICE_ID 0x0001

#define QUICKUSB_MAX_SUBDEV 16
#define QUICKUSB_SUBDEV_MASK ( QUICKUSB_MAX_SUBDEV - 1 )
#define QUICKUSB_BOARD( dev_minor ) ( (dev_minor) / QUICKUSB_MAX_SUBDEV )
#define QUICKUSB_SUBDEV( dev_minor ) ( (dev_minor) & QUICKUSB_SUBDEV_MASK )
#define QUICKUSB_MINOR( board, subdev ) \
	( (board) * QUICKUSB_MAX_SUBDEV + (subdev) )

#define QUICKUSB_SUBDEV_GPPIO_A 0

static int debug = 0;
static int dev_major = 0;

struct quickusb_device {
	struct usb_device *usbdev;
	unsigned int board;
	unsigned long subdevs;
	struct list_head list;
};

static LIST_HEAD ( quickusb_list );
static DECLARE_MUTEX ( quickusb_lock );

static const char *quickusb_subdev_name ( unsigned int subdev ) {
	switch ( subdev ) {
	case QUICKUSB_SUBDEV_GPPIO_A:
		return "gppio_a";
	default:
		return NULL;
	}
}

static int quickusb_gppio_read ( struct file *file, char __user *buf,
				 size_t len, loff_t *ppos ) {
	return 0;
}

static ssize_t quickusb_gppio_write ( struct file *file,
				      const char __user *buf,
				      size_t len, loff_t *ppos ) {
	return 0;
}

static struct file_operations quickusb_gppio_fops = {
	.owner		= THIS_MODULE,
	.read		= quickusb_gppio_read,
	.write		= quickusb_gppio_write,
};

static int quickusb_open ( struct inode *inode, struct file *file ) {
	unsigned int board = QUICKUSB_BOARD ( iminor ( inode ) );
	unsigned int subdev = QUICKUSB_SUBDEV ( iminor ( inode ) );
	struct quickusb *quickusb;
	int found = 0;

	/* Identify board */
	down ( &quickusb_lock );
	list_for_each_entry ( quickusb, &quickusb_list, list ) {
		if ( quickusb->board == board ) {
			found = 1;
			break;
		}
	}
	if ( 
	up ( &quickusb_lock );
	if ( ! found )
		return -ENODEV;

	file->private_data = ;

	/* Select subdev-specific file operations table */
	switch ( subdev ) {
	case QUICKUSB_SUBDEV_GPPIO_A:
		file->f_op = &quickusb_gppio_fops;
		break;
	default:
		return -ENXIO;
	}

	if ( file->f_op && file->f_op->open )
		return file->f_op->open ( inode, file );

	return 0;
}

static int quickusb_release ( struct inode *inode, struct file *file ) {
	struct quickusb *quickusb = file->private_data;

	usb_put_dev ( quickusb->dev );
}

static struct file_operations quickusb_fops = {
	.owner		= THIS_MODULE,
	.open		= quickusb_open,
};

static void quickusb_destroy_devices ( struct quickusb_device *quickusb ) {
	unsigned int subdev;

	for ( subdev = 0 ; subdev < QUICKUSB_MAX_SUBDEV ; subdev++ ) {
		if ( ! ( quickusb->subdevs & ( 1 << subdev ) ) )
			continue;
		devfs_remove ( "quickusb/%d/%s", quickusb->board,
			       quickusb_subdev_name ( subdev ) );
	}
}

static int quickusb_create_devices ( struct quickusb_device *quickusb ) {
	unsigned int subdev;
	unsigned int dev_minor;
	const char *name;
	dev_t dev;
	int rc;

	for ( subdev = 0 ; subdev < QUICKUSB_MAX_SUBDEV ; subdev++ ) {
		dev_minor = QUICKUSB_MINOR ( quickusb->board, subdev );
		dev = MKDEV ( dev_major, dev_minor );
		name = quickusb_subdev_name ( subdev );
		if ( ! name )
			continue;
		if ( ( rc = devfs_mk_cdev ( dev,
					    ( S_IFCHR | S_IRUSR | S_IWUSR ),
					    "quickusb/%d/%s", quickusb->board,
					    name ) ) != 0 )
			return rc;
		quickusb->subdevs |= ( 1 << subdev );
	}

	return 0;
}

static int quickusb_probe ( struct usb_interface *interface,
			    const struct usb_device_id *id ) {
	struct usb_device *usbdev = interface_to_usbdev ( interface );
	struct quickusb_device *quickusb = NULL;
	struct quickusb_device *quickusb_in_list;
	unsigned int board = 0;
	int rc = 0;

	down ( &quickusb_lock );

	/* Create new quickusb device structure */
	quickusb = kmalloc ( sizeof ( *quickusb ), GFP_KERNEL );
	if ( ! quickusb ) {
		rc = -ENOMEM;
		goto err;
	}
	memset ( quickusb, 0, sizeof ( *quickusb ) );
	quickusb->usbdev = usb_get_dev ( usbdev );
	INIT_LIST_HEAD ( &quickusb->list );

	/* Obtain a free board index and link into list */
	list_for_each_entry ( quickusb_in_list, &quickusb_list, list ) {
		if ( quickusb_in_list->board != board )
			break;
		board++;
	}
	quickusb->board = board;
	list_add_tail ( &quickusb->list, &quickusb_in_list->list );
	
	/* Create devices */
	if ( ( rc = quickusb_create_devices ( quickusb ) ) != 0 )
		goto err;

	/* Record private data */
	usb_set_intfdata ( interface, quickusb );
	goto out;

 err:
	if ( quickusb ) {
		quickusb_destroy_devices ( quickusb );
		list_del ( &quickusb->list );
		kfree ( quickusb );
	}
 out:
	up ( &quickusb_lock );
	return rc;
}

static void quickusb_disconnect ( struct usb_interface *interface ) {
	struct quickusb_device *quickusb = usb_get_intfdata ( interface );

	down ( &quickusb_lock );
	quickusb_destroy_devices ( quickusb );
	list_del ( &quickusb->list );
	usb_set_intfdata ( interface, NULL );
	kfree ( quickusb );
	up ( &quickusb_lock );
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

	/* Register USB driver */
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
