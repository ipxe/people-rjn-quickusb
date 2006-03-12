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
#include "quickusb.h"

#define QUICKUSB_VENDOR_ID 0x0fbb
#define QUICKUSB_DEVICE_ID 0x0001

#define QUICKUSB_MAX_SUBDEVS 8
#define QUICKUSB_SUBDEV_MASK ( QUICKUSB_MAX_SUBDEVS - 1 )
#define QUICKUSB_MINOR_BOARD( dev_minor ) \
	( (dev_minor) / QUICKUSB_MAX_SUBDEVS )
#define QUICKUSB_MINOR_SUBDEV( dev_minor ) \
	( (dev_minor) & QUICKUSB_SUBDEV_MASK )
#define QUICKUSB_MINOR( board, subdev ) \
	( (board) * QUICKUSB_MAX_SUBDEVS + (subdev) )

#define QUICKUSB_MAX_GPPIO 5

static int debug = 0;
static int dev_major = 0;

struct quickusb_gppio {
	struct quickusb_device *quickusb;
	unsigned int port;
};

struct quickusb_subdev {
	struct file_operations *f_op;
	void *private_data;
	unsigned char name[32];
};

struct quickusb_device {
	struct usb_device *usb;
	struct usb_interface *interface;
	struct kref kref;
	struct list_head list;
	unsigned int board;
	struct quickusb_gppio gppio[QUICKUSB_MAX_GPPIO];
	struct quickusb_subdev subdev[QUICKUSB_MAX_SUBDEVS];
};

static void quickusb_delete ( struct kref *kref ) {
	struct quickusb_device *quickusb;

	quickusb = container_of ( kref, struct quickusb_device, kref );
	usb_put_dev ( quickusb->usb );
	kfree ( quickusb );
}

static LIST_HEAD ( quickusb_list );

static DECLARE_MUTEX ( quickusb_lock );

/****************************************************************************
 *
 * GPPIO char device operations
 *
 */

static ssize_t quickusb_gppio_read ( struct file *file, char __user *user_data,
				     size_t len, loff_t *ppos ) {
	struct quickusb_gppio *gppio = file->private_data;
	struct usb_device *usb = gppio->quickusb->usb;
	unsigned char data[QUICKUSB_MAX_DATA_LEN];
	int rc;

	if ( len > sizeof ( data ) )
		len = sizeof ( data );

	if ( ( rc = quickusb_read_port ( usb, gppio->port,
					 data, &len ) ) != 0 )
		return rc;

	if ( ( rc = copy_to_user ( user_data, data, len ) ) != 0 )
		return rc;

	*ppos += len;
	return len;
}

static ssize_t quickusb_gppio_write ( struct file *file,
				      const char __user *user_data,
				      size_t len, loff_t *ppos ) {
	struct quickusb_gppio *gppio = file->private_data;
	struct usb_device *usb = gppio->quickusb->usb;
	unsigned char data[QUICKUSB_MAX_DATA_LEN];
	int rc;

	if ( len > sizeof ( data ) )
		len = sizeof ( data );

	if ( ( rc = copy_from_user ( data, user_data, len ) ) != 0 )
		return rc;

	if ( ( rc = quickusb_write_port ( usb, gppio->port,
					  data, &len ) ) != 0 )
		return rc;

	*ppos += len;
	return len;
}

static int quickusb_gppio_release ( struct inode *inode, struct file *file ) {
	struct quickusb_gppio *gppio = file->private_data;
	
	kref_put ( &gppio->quickusb->kref, quickusb_delete );
	return 0;
}

static struct file_operations quickusb_gppio_fops = {
	.owner		= THIS_MODULE,
	.read		= quickusb_gppio_read,
	.write		= quickusb_gppio_write,
	.release	= quickusb_gppio_release,
};

/****************************************************************************
 *
 * Char device (subdev) operations
 *
 */

static int quickusb_open ( struct inode *inode, struct file *file ) {
	unsigned int board = QUICKUSB_MINOR_BOARD ( iminor ( inode ) );
	unsigned int subdev = QUICKUSB_MINOR_SUBDEV ( iminor ( inode ) );
	struct quickusb_device *quickusb;
	int found = 0;
	int rc = 0;

	/* Locate board and increase refcount */
	down ( &quickusb_lock );
	list_for_each_entry ( quickusb, &quickusb_list, list ) {
		if ( quickusb->board == board ) {
			kref_get ( &quickusb->kref );
			found = 1;
			break;
		}
	}
	up ( &quickusb_lock );
	if ( ! found ) {
		quickusb = NULL;
		rc = -ENODEV;
		goto out;
	}

	/* Set up per-subdevice file operations and private data */
	file->f_op = quickusb->subdev[subdev].f_op;
	file->private_data = quickusb->subdev[subdev].private_data;
	if ( ! file->f_op ) {
		rc = -ENODEV;
		goto out;
	}
	
	/* Perform any subdev-specific open operation */
	if ( file->f_op->open )
		rc = file->f_op->open ( inode, file );

 out:
	if ( ( rc != 0 ) && quickusb )
		kref_put ( &quickusb->kref, quickusb_delete );
	return rc;
}

static struct file_operations quickusb_fops = {
	.owner		= THIS_MODULE,
	.open		= quickusb_open,
};

/****************************************************************************
 *
 * Char device (subdev) registration/deregistration
 *
 */

static int quickusb_register_subdev ( struct quickusb_device *quickusb,
				      unsigned int subdev_idx,
				      struct file_operations *f_op,
				      void *private_data,
				      const char *subdev_fmt, ... ) {
	struct quickusb_subdev *subdev = &quickusb->subdev[subdev_idx];
	unsigned int dev_minor;
	dev_t dev;
	va_list ap;
	int rc;

	/* Construct device number */
	dev_minor = QUICKUSB_MINOR ( quickusb->board, subdev_idx );
	dev = MKDEV ( dev_major, dev_minor );

	/* Construct device name */
	va_start ( ap, subdev_fmt );
	vsnprintf ( subdev->name, sizeof ( subdev->name ), subdev_fmt, ap );
	va_end ( ap );

	/* Create devfs device */
	if ( ( rc = devfs_mk_cdev ( dev,
				    ( S_IFCHR | S_IRUSR | S_IWUSR |
				      S_IRGRP | S_IWGRP ),
				    subdev->name ) ) != 0 )
		return rc;

	/* Fill subdev structure */
	subdev->f_op = f_op;
	subdev->private_data = private_data;

	return 0;
}
				      
static void quickusb_deregister_subdev ( struct quickusb_device *quickusb,
					 unsigned int subdev_idx ) {
	struct quickusb_subdev *subdev = &quickusb->subdev[subdev_idx];

	if ( ! subdev->f_op )
		return;

	/* Clear subdev structure */
	memset ( subdev, 0, sizeof ( *subdev ) );
	
	/* Remove devfs device */
	devfs_remove ( subdev->name );
}

/****************************************************************************
 *
 * Device creation / destruction
 *
 */

static int quickusb_register_devices ( struct quickusb_device *quickusb ) {
	unsigned int subdev_idx = 0;
	struct quickusb_gppio *gppio;
	unsigned char gppio_char;
	int i;
	int rc;

	/* Register GPPIO ports as subdevs */
	for ( i = 0 ; i < QUICKUSB_MAX_GPPIO ; i++ ) {
		gppio = &quickusb->gppio[i];
		gppio_char = ( 'a' + gppio->port );
		if ( ( rc = quickusb_register_subdev ( quickusb, subdev_idx++,
						       &quickusb_gppio_fops,
						       gppio,
						       "quickusb%d/gppio_%c",
						       quickusb->board,
						       gppio_char ) ) != 0 )
			return rc;
	}
	
	return 0;
}

static void quickusb_deregister_devices ( struct quickusb_device *quickusb ) {
	int i;

	/* Deregister all subdevs */
	for ( i = 0 ; i < QUICKUSB_MAX_SUBDEVS ; i++ ) {
		quickusb_deregister_subdev ( quickusb, i );
	}
}

/****************************************************************************
 *
 * USB hotplug add/remove
 *
 */

static int quickusb_probe ( struct usb_interface *interface,
			    const struct usb_device_id *id ) {
	struct quickusb_device *quickusb = NULL;
	struct quickusb_device *pre_existing_quickusb;
	unsigned int board = 0;
	int i;
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
	INIT_LIST_HEAD ( &quickusb_list );
	quickusb->usb = usb_get_dev ( interface_to_usbdev ( interface ) );
	quickusb->interface = interface;
	for ( i = 0 ; i < QUICKUSB_MAX_GPPIO ; i++ ) {
		quickusb->gppio[i].quickusb = quickusb;
		quickusb->gppio[i].port = i;
	}
	
	/* Obtain a free board board and link into list */
	list_for_each_entry ( pre_existing_quickusb, &quickusb_list, list ) {
		if ( pre_existing_quickusb->board != board )
			break;
		board++;
	}
	quickusb->board = board;
	list_add_tail ( &quickusb->list, &pre_existing_quickusb->list );

	/* Record driver private data */
	usb_set_intfdata ( interface, quickusb );

	/* Register devices */
	if ( ( rc = quickusb_register_devices ( quickusb ) ) != 0 ) {
		printk ( KERN_ERR "quickusb unable to register devices\n" );
		goto err;
	}

	printk ( KERN_INFO "quickusb%d connected\n", quickusb->board ); 
	goto out;

 err:
	usb_set_intfdata ( interface, NULL );
	if ( quickusb ) {
		quickusb_deregister_devices ( quickusb );
		list_del ( &quickusb->list );
		kref_put ( &quickusb->kref, quickusb_delete );
	}
 out:
	up ( &quickusb_lock );
	return rc;
}

static void quickusb_disconnect ( struct usb_interface *interface ) {
	struct quickusb_device *quickusb = usb_get_intfdata ( interface );

	printk ( KERN_INFO "quickusb%d disconnected\n", quickusb->board );

	down ( &quickusb_lock );
	usb_set_intfdata ( interface, NULL );
	quickusb_deregister_devices ( quickusb );
	list_del ( &quickusb->list );
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

/****************************************************************************
 *
 * Kernel module interface
 *
 */

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
