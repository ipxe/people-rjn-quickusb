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
#include <linux/usb.h>
#include <linux/tty.h>
#include <asm/uaccess.h>
#include "usb-serial.h"
#include "quickusb.h"
#include "kernel_compat.h"

#define QUICKUSB_VENDOR_ID 0x0fbb
#define QUICKUSB_DEVICE_ID 0x0001

#define QUICKUSB_MAX_SUBDEVS 16
#define QUICKUSB_SUBDEV_MASK ( QUICKUSB_MAX_SUBDEVS - 1 )
#define QUICKUSB_MINOR_BOARD( dev_minor ) \
	( (dev_minor) / QUICKUSB_MAX_SUBDEVS )
#define QUICKUSB_MINOR_SUBDEV( dev_minor ) \
	( (dev_minor) & QUICKUSB_SUBDEV_MASK )
#define QUICKUSB_MINOR( board, subdev ) \
	( (board) * QUICKUSB_MAX_SUBDEVS + (subdev) )

#define QUICKUSB_MAX_GPPIO 5

struct quickusb_gppio {
	struct quickusb_device *quickusb;
	unsigned int port;
};

struct quickusb_hspio {
	struct quickusb_device *quickusb;
};

struct quickusb_subdev {
	struct file_operations *f_op;
	void *private_data;
	dev_t dev;
	unsigned char name[32];
	struct class_device *class_dev;
};

struct quickusb_device {
	struct usb_device *usb;
	struct usb_interface *interface;
	struct kref kref;
	struct list_head list;
	unsigned int board;
	struct quickusb_gppio gppio[QUICKUSB_MAX_GPPIO];
	struct quickusb_hspio hspio;
	struct quickusb_subdev subdev[QUICKUSB_MAX_SUBDEVS];
	void *serial_intfdata;
};

static void quickusb_delete ( struct kref *kref ) {
	struct quickusb_device *quickusb;

	quickusb = container_of ( kref, struct quickusb_device, kref );
	usb_put_dev ( quickusb->usb );
	kfree ( quickusb );
}

static LIST_HEAD ( quickusb_list );

static DECLARE_MUTEX ( quickusb_lock );

static struct class *quickusb_class;

static int debug = 0;
static int dev_major = 0;

static struct usb_device_id quickusb_ids[];

/****************************************************************************
 *
 * Common operations
 *
 */

static int quickusb_set_hsppmode ( struct quickusb_device *quickusb,
				   unsigned int hsppmode ) {
	uint16_t fifoconfig;
	int rc;

	if ( ( rc = quickusb_read_setting ( quickusb->usb,
					    QUICKUSB_SETTING_FIFOCONFIG,
					    &fifoconfig ) ) != 0 )
		return rc;
	fifoconfig &= ~QUICKUSB_HSPPMODE_MASK;
	fifoconfig |= ( hsppmode & QUICKUSB_HSPPMODE_MASK );
	if ( ( rc = quickusb_write_setting ( quickusb->usb,
					     QUICKUSB_SETTING_FIFOCONFIG,
					     fifoconfig ) ) != 0 )
		return rc;

	return 0;
}

/****************************************************************************
 *
 * GPPIO char device operations
 *
 */

static ssize_t quickusb_gppio_read ( struct file *file, char __user *user_data,
				     size_t len, loff_t *ppos ) {
	struct quickusb_gppio *gppio = file->private_data;
	unsigned char data[QUICKUSB_MAX_DATA_LEN];
	int rc;

	if ( len > sizeof ( data ) )
		len = sizeof ( data );

	if ( ( rc = quickusb_read_port ( gppio->quickusb->usb, gppio->port,
					 data, len ) ) != 0 )
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
	unsigned char data[QUICKUSB_MAX_DATA_LEN];
	int rc;

	if ( len > sizeof ( data ) )
		len = sizeof ( data );

	if ( ( rc = copy_from_user ( data, user_data, len ) ) != 0 )
		return rc;

	if ( ( rc = quickusb_write_port ( gppio->quickusb->usb, gppio->port,
					  data, len ) ) != 0 )
		return rc;

	*ppos += len;
	return len;
}

static int quickusb_gppio_ioctl ( struct inode *inode, struct file *file,
				  unsigned int cmd, unsigned long arg ) {
	struct quickusb_gppio *gppio = file->private_data;
	struct quickusb_device *quickusb = gppio->quickusb;
	void __user *user_data = ( void __user * ) arg;
	size_t ioctl_size = _IOC_SIZE(cmd);
	union {
		quickusb_gppio_ioctl_data_t gppio;
		struct quickusb_setting_ioctl_data setting;
		char bytes[ioctl_size];
	} u;
	unsigned char outputs;
	uint16_t default_address = QUICKUSB_SETTING_GPPIO ( gppio->port );
	uint16_t default_value;
	int rc;

	if ( ( rc = copy_from_user ( u.bytes, user_data, ioctl_size ) ) != 0 )
		return rc;

	switch ( cmd ) {
	case QUICKUSB_IOC_GPPIO_GET_OUTPUTS:
		if ( ( rc = quickusb_read_port_dir ( quickusb->usb,
						     gppio->port,
						     &outputs ) ) != 0 )
			return rc;
		u.gppio = outputs;
		break;
	case QUICKUSB_IOC_GPPIO_SET_OUTPUTS:
		outputs = u.gppio;
		if ( ( rc = quickusb_write_port_dir ( quickusb->usb,
						      gppio->port,
						      outputs ) ) != 0 )
			return rc;
		break;
	case QUICKUSB_IOC_GPPIO_GET_DEFAULT_OUTPUTS:
		if ( ( rc = quickusb_read_default ( quickusb->usb,
						    default_address,
						    &default_value ) ) != 0 )
			return rc;
		u.gppio = ( default_value >> 8 );
		break;
	case QUICKUSB_IOC_GPPIO_SET_DEFAULT_OUTPUTS:
		if ( ( rc = quickusb_read_default ( quickusb->usb,
						    default_address,
						    &default_value ) ) != 0 )
			return rc;
		default_value &= 0x00ff;
		default_value |= ( u.gppio << 8 );
		if ( ( rc = quickusb_write_default ( quickusb->usb,
						     default_address,
						     default_value ) ) != 0 )
			return rc;
		break;
	case QUICKUSB_IOC_GPPIO_GET_DEFAULT_LEVELS:
		if ( ( rc = quickusb_read_default ( quickusb->usb,
						    default_address,
						    &default_value ) ) != 0 )
			return rc;
		u.gppio = ( default_value & 0x00ff );
		break;
	case QUICKUSB_IOC_GPPIO_SET_DEFAULT_LEVELS:
		if ( ( rc = quickusb_read_default ( quickusb->usb,
						    default_address,
						    &default_value ) ) != 0 )
			return rc;
		default_value &= 0x00ff;
		default_value |= ( u.gppio & 0x00ff );
		if ( ( rc = quickusb_write_default ( quickusb->usb,
						     default_address,
						     default_value ) ) != 0 )
			return rc;
		break;
	case QUICKUSB_IOC_GET_SETTING:
		if ( ( rc = quickusb_read_setting ( quickusb->usb,
						    u.setting.address,
						    &u.setting.value ) ) != 0 )
			return rc;
		break;
	case QUICKUSB_IOC_SET_SETTING:
		if ( ( rc = quickusb_write_setting ( quickusb->usb,
						     u.setting.address,
						     u.setting.value ) ) != 0 )
			return rc;
		break;
	default:
		return -ENOTTY;
	}

	if ( ( rc = copy_to_user ( user_data, u.bytes, ioctl_size ) ) != 0 )
		return rc;

	return 0;
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
	.ioctl		= quickusb_gppio_ioctl,
	.release	= quickusb_gppio_release,
};

/****************************************************************************
 *
 * HSPIO char device operations (master mode)
 *
 */

static int quickusb_hspio_open ( struct inode *inode, struct file *file ) {
	struct quickusb_hspio *hspio = file->private_data;
	int rc;

	if ( ( rc = quickusb_set_hsppmode ( hspio->quickusb,
					    QUICKUSB_HSPPMODE_MASTER ) ) != 0 )
		return rc;

	return 0;
}

static ssize_t quickusb_hspio_read_command ( struct file *file,
					     char __user *user_data,
					     size_t len, loff_t *ppos ) {
	struct quickusb_hspio *hspio = file->private_data;
	unsigned char data[QUICKUSB_MAX_DATA_LEN];
	int rc;

	if ( len > sizeof ( data ) )
		len = sizeof ( data );

	if ( ( rc = quickusb_read_command ( hspio->quickusb->usb, *ppos,
					    data, len ) ) != 0 )
		return rc;

	if ( ( rc = copy_to_user ( user_data, data, len ) ) != 0 )
		return rc;

	*ppos += len;
	return len;
}

static ssize_t quickusb_hspio_write_command ( struct file *file,
					      const char __user *user_data,
					      size_t len, loff_t *ppos ) {
	struct quickusb_hspio *hspio = file->private_data;
	unsigned char data[QUICKUSB_MAX_DATA_LEN];
	int rc;

	if ( len > sizeof ( data ) )
		len = sizeof ( data );

	if ( ( rc = copy_from_user ( data, user_data, len ) ) != 0 )
		return rc;

	if ( ( rc = quickusb_write_command ( hspio->quickusb->usb, *ppos,
					     data, len ) ) != 0 )
		return rc;

	*ppos += len;
	return len;
}

static ssize_t quickusb_hspio_read_data ( struct file *file,
					  char __user *user_data,
					  size_t len, loff_t *ppos ) {
	struct quickusb_hspio *hspio = file->private_data;
	unsigned char data[QUICKUSB_MAX_BULK_DATA_LEN];
	int rc;

	if ( len > sizeof ( data ) )
		len = sizeof ( data );

	if ( ( rc = quickusb_read_data ( hspio->quickusb->usb,
					 data, len ) ) != 0 )
		return rc;

	if ( ( rc = copy_to_user ( user_data, data, len ) ) != 0 )
		return rc;

	*ppos += len;
	return len;
}

static ssize_t quickusb_hspio_write_data ( struct file *file,
					   const char __user *user_data,
					   size_t len, loff_t *ppos ) {
	struct quickusb_hspio *hspio = file->private_data;
	unsigned char data[QUICKUSB_MAX_BULK_DATA_LEN];
	int rc;

	if ( len > sizeof ( data ) )
		len = sizeof ( data );

	if ( ( rc = copy_from_user ( data, user_data, len ) ) != 0 )
		return rc;

	if ( ( rc = quickusb_write_data ( hspio->quickusb->usb,
					  data, len ) ) != 0 )
		return rc;

	*ppos += len;
	return len;
}

static int quickusb_hspio_release ( struct inode *inode, struct file *file ) {
	struct quickusb_hspio *hspio = file->private_data;
	
	kref_put ( &hspio->quickusb->kref, quickusb_delete );
	return 0;
}

static struct file_operations quickusb_hspio_command_fops = {
	.owner		= THIS_MODULE,
	.open		= quickusb_hspio_open,
	.read		= quickusb_hspio_read_command,
	.write		= quickusb_hspio_write_command,
	.release	= quickusb_hspio_release,
};

static struct file_operations quickusb_hspio_data_fops = {
	.owner		= THIS_MODULE,
	.open		= quickusb_hspio_open,
	.read		= quickusb_hspio_read_data,
	.write		= quickusb_hspio_write_data,
	.release	= quickusb_hspio_release,
};

/****************************************************************************
 *
 * HSPIO ttyUSB device operations (slave mode)
 *
 */

static int quickusb_ttyusb_open ( struct usb_serial_port *port,
				  struct file *file ) {
	struct quickusb_device *quickusb
		= usb_get_intfdata ( port->serial->interface );
	int rc;

	if ( ( rc = quickusb_set_hsppmode ( quickusb,
					    QUICKUSB_HSPPMODE_SLAVE ) ) != 0 )
		return rc;

	if ( ( rc = usb_serial_generic_open ( port, file ) ) != 0 )
		return rc;

	return 0;
}

static struct usb_serial_driver quickusb_serial = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "quickusb",
	},
	.open		= quickusb_ttyusb_open,
	.id_table	= quickusb_ids,
	.num_interrupt_in = NUM_DONT_CARE,
	.num_bulk_in	= NUM_DONT_CARE,
	.num_bulk_out	= NUM_DONT_CARE,
	.num_ports	= 1,
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
	struct usb_interface *interface = quickusb->interface;
	unsigned int dev_minor;
	va_list ap;
	int rc;

	/* Construct device number */
	dev_minor = QUICKUSB_MINOR ( quickusb->board, subdev_idx );
	subdev->dev = MKDEV ( dev_major, dev_minor );

	/* Fill subdev structure */
	subdev->f_op = f_op;
	subdev->private_data = private_data;

	/* Construct device name */
	va_start ( ap, subdev_fmt );
	vsnprintf ( subdev->name, sizeof ( subdev->name ), subdev_fmt, ap );
	va_end ( ap );

	/* Create devfs device */
	if ( ( rc = devfs_mk_cdev ( subdev->dev,
				    ( S_IFCHR | S_IRUSR | S_IWUSR |
				      S_IRGRP | S_IWGRP ),
				    subdev->name ) ) != 0 )
		goto err_devfs;

	/* Create class device */
	subdev->class_dev = class_device_create ( quickusb_class, NULL,
						  subdev->dev, &interface->dev,
						  subdev->name );
	if ( IS_ERR ( subdev->class_dev ) ) {
		rc = PTR_ERR ( subdev->class_dev );
		goto err_class;
	}

	return 0;

 err_class:
	devfs_remove ( subdev->name );
 err_devfs:
	memset ( subdev, 0, sizeof ( *subdev ) );
	return rc;
}
				      
static void quickusb_deregister_subdev ( struct quickusb_device *quickusb,
					 unsigned int subdev_idx ) {
	struct quickusb_subdev *subdev = &quickusb->subdev[subdev_idx];

	if ( ! subdev->f_op )
		return;

	/* Remove class device */
	class_device_destroy ( quickusb_class, subdev->dev );

	/* Remove devfs device */
	devfs_remove ( subdev->name );

	/* Clear subdev structure */
	memset ( subdev, 0, sizeof ( *subdev ) );
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
						       "qu%dg%c",
						       quickusb->board,
						       gppio_char ) ) != 0 )
			return rc;
	}

	/* Register HSPIO port in all its variants */
	if ( ( rc = quickusb_register_subdev ( quickusb, subdev_idx++,
					       &quickusb_hspio_command_fops,
					       &quickusb->hspio,
					       "qu%dhc",
					       quickusb->board ) ) != 0 )
		return rc;
	if ( ( rc = quickusb_register_subdev ( quickusb, subdev_idx++,
					       &quickusb_hspio_data_fops,
					       &quickusb->hspio,
					       "qu%dhd",
					       quickusb->board ) ) != 0 )
		return rc;
	
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
	quickusb->hspio.quickusb = quickusb;
	
	/* Obtain a free board board and link into list */
	list_for_each_entry ( pre_existing_quickusb, &quickusb_list, list ) {
		if ( pre_existing_quickusb->board != board )
			break;
		board++;
	}
	quickusb->board = board;
	list_add_tail ( &quickusb->list, &pre_existing_quickusb->list );


	/* Register ttyUSB device */
	if ( ( rc = usb_serial_probe ( interface, id ) ) != 0 ) {
		printk ( KERN_ERR "quickusb unable to register ttyUSB\n" );
		goto err;
	}
	quickusb->serial_intfdata = usb_get_intfdata ( interface );

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
		if ( quickusb->serial_intfdata ) {
			usb_set_intfdata ( interface,
					   quickusb->serial_intfdata );
			usb_serial_disconnect ( interface );
		}
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
	if ( quickusb->serial_intfdata ) {
		usb_set_intfdata ( interface,
				   quickusb->serial_intfdata );
		usb_serial_disconnect ( interface );
	}
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
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "quickusb",
	},
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
				      &quickusb_fops ) ) < 0 ) {
		printk ( KERN_ERR "quickusb could not register char device: "
			 "error %d\n", rc );
		goto err_chrdev;
	}
	if ( ! dev_major ) {
		dev_major = rc;
		printk ( KERN_INFO "quickusb using major device %d\n",
			 dev_major );
	}
	
	/* Create device class */
	quickusb_class = class_create ( THIS_MODULE, "quickusb" );
	if ( IS_ERR ( quickusb_class ) ) {
		rc = PTR_ERR ( quickusb_class );
		printk ( KERN_ERR "quickusb could not create device class: "
			 "error %d\n", rc );
		goto err_class;
	}

	if ( ( rc = usb_serial_register ( &quickusb_serial ) ) != 0 )
		goto err_usbserial;

	if ( ( rc = usb_register ( &quickusb_driver ) ) != 0 )
		goto err_usb;

	return 0;

 err_usb:
	usb_serial_deregister ( &quickusb_serial );
 err_usbserial:
	class_destroy ( quickusb_class );
 err_class:
	unregister_chrdev ( dev_major, "quickusb" );
 err_chrdev:
	return rc;
}

static void quickusb_exit ( void ) {
	usb_deregister ( &quickusb_driver );
	usb_serial_deregister ( &quickusb_serial );
	class_destroy ( quickusb_class );
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
