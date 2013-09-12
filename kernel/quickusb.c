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
#include <linux/usb.h>
#include <linux/tty.h>
#include <linux/kernel.h>
#include <linux/scatterlist.h>
#include <linux/usb/serial.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include "quickusb.h"

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

#define INTERRUPT_RATE 1 /* msec/transfer */

#define ERROR(fmt, args...) printk(KERN_ERR fmt , ## args)
#define INFO(fmt, args...) printk(KERN_INFO fmt , ## args)
#define DBG(fmt, args...) printk(KERN_DEBUG fmt , ## args)

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
	struct device *devp;
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
};

static void quickusb_delete ( struct kref *kref ) {
	struct quickusb_device *quickusb;

	quickusb = container_of ( kref, struct quickusb_device, kref );
	usb_put_dev ( quickusb->usb );
	kfree ( quickusb );
}

static LIST_HEAD ( quickusb_list );

static DEFINE_SEMAPHORE ( quickusb_lock );

static struct class *quickusb_class;

static bool debug = 0;
static int dev_major = 0;

static struct usb_device_id quickusb_ids[];

/****************************************************************************
 *
 * Auxiliary scatter-gather functions
 *
 ****************************************************************************/

struct buffer_size_pair
{
	char *buffer;
	int   size;
};


static void free_ba( struct buffer_size_pair *ba, int n )
{
	unsigned	i;
	if (!ba)
		return;
	for (i = 0; i < n; i++) {
		if (ba[i].buffer == NULL)
			continue;
		kfree( ba[i].buffer );
	}
	kfree(ba);
}


static void free_sglist(struct scatterlist *sg, int nents)
{
	unsigned i;
	
	if (!sg)
		return;
	for (i = 0; i < nents; i++) {
		if (!sg_page(&sg[i]))
			continue;
		kfree (sg_virt(&sg[i]));
	}
	kfree (sg);
}


static struct scatterlist *alloc_sglist(int bytes, unsigned int *nents)
{
	struct scatterlist	*sg;
	unsigned		i, entries;
	unsigned		size = 128 * 1024; // 128K
	struct buffer_size_pair *ba;
	int                     ba_size = (bytes + PAGE_SIZE - 1)/PAGE_SIZE;
	
        *nents = 0;
	ba = kzalloc ( ba_size * sizeof *ba, GFP_KERNEL );
	if (!ba)
		return NULL;
        
	entries = 0;
	for (i = 0; i < ba_size; i++) {
again:
		ba[i].buffer = kzalloc( size, GFP_KERNEL );
		if (!ba[i].buffer) {
			if (size == PAGE_SIZE) {
				free_ba( ba, i );
				return NULL;
			}
			size /= 2;
			goto again;
		}
		ba[i].size = size;
		entries++;
		bytes -= size;
		if (bytes == 0)
			break;
		size = bytes < size ? bytes : size;
	}
	// Now we know number of entries
	sg = kmalloc (entries * sizeof *sg, GFP_KERNEL);
	if (!sg) {
		free_ba( ba, entries );
		return NULL;
	}
	sg_init_table(sg, entries);
	
	for (i = 0; i < entries; i++) {
		sg_set_buf(&sg[i], ba[i].buffer, ba[i].size);
	}
	// remove temporary array
	kfree (ba);
        *nents = entries;
	return sg;
}

static int perform_sglist ( struct usb_device *usb, int pipe,
			    struct usb_sg_request *req,
			    struct scatterlist *sg,
			    int nents, size_t length)
{
	int ret = usb_sg_init (req, usb, pipe, 0, sg, nents, length, GFP_KERNEL);
	if (!ret) {
		usb_sg_wait (req);
		ret = req->status;
	}

	if (ret)
		printk(KERN_ERR "perform_sglist failed, rc %d\n", ret);
	return ret;
}

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

static long quickusb_gppio_ioctl ( struct file *file,
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
	.unlocked_ioctl	= quickusb_gppio_ioctl,
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
	int rc, nents, i;
	uint32_t len_le = cpu_to_le32 ( len );
	struct scatterlist *sg, *s;
	struct usb_sg_request req;
	struct quickusb_hspio *hspio = file->private_data;
	struct usb_device *usb = hspio->quickusb->usb;
	int pipe = usb_rcvbulkpipe ( usb, QUICKUSB_BULK_IN_EP );
	
	rc = usb_control_msg ( usb, usb_sndctrlpipe ( usb, 0 ),
				QUICKUSB_BREQUEST_HSPIO,
				QUICKUSB_BREQUESTTYPE_WRITE,
				0, 0,
				&len_le, sizeof ( len_le ),
				QUICKUSB_TIMEOUT );
	if ( rc < 0 )
		return rc;

	/*
	 * Allocate the largest possible chunks from kernel space with
	 * total length 'len'
	 * The output is scatterlist pointer and number of entries
	 */
	sg = alloc_sglist ( len, &nents );
	if (!sg)
		return -ENOMEM;
	
	/*
	 * Perform actual IO operation using scatterlist 
	 */
	rc = perform_sglist ( hspio->quickusb->usb, pipe, &req, sg, nents, len );
	if (rc < 0) {
		free_sglist(sg, nents);
		return rc;
	}
	
	/*
	 * Pass over all the buffers in scatterlist and copy
	 * their contents to userspace buffer.
	 */
	for_each_sg(sg, s, nents, i) {
		unsigned char *data = sg_virt(s);
		unsigned length = s->length;
		rc = copy_to_user ( user_data, data, length );
		if (rc < 0) {
			free_sglist(sg, nents);
			return rc;
		}
		user_data += length;
		*ppos += length;
	}
	
	free_sglist(sg, nents);
	return len;
}

static ssize_t quickusb_hspio_write_data ( struct file *file,
					   const char __user *user_data,
					   size_t len, loff_t *ppos ) {
	int rc, nents, i;
	struct scatterlist *sg, *s;
	struct usb_sg_request req;
	struct quickusb_hspio *hspio = file->private_data;
	struct usb_device *usb = hspio->quickusb->usb;
	int pipe = usb_sndbulkpipe ( usb, QUICKUSB_BULK_OUT_EP );

	
	/*
	 * Allocate the scatterlist according the requested 'len'
	 */
	sg = alloc_sglist( len, &nents );
	if (!sg)
		return -ENOMEM;
	
	/*
	 * Go through all the buffers and copy the data from userspace...
	 */
	for_each_sg(sg, s, nents, i) {
		unsigned char *data = sg_virt(s);
		unsigned length = s->length;
		rc = copy_from_user ( data, user_data, length );
		if (rc != 0) {
			free_sglist(sg, nents);
			return rc;
		}
		user_data += length;
	}
	
	/*
	 * Perform the actual IO operation
	 */
	rc = perform_sglist ( usb, pipe, &req, sg, nents, len );
	if (rc < 0) {
		free_sglist(sg, nents);
		return rc;
	}
	
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

static int quickusb_ttyusb_open ( struct tty_struct *tty,
                                  struct usb_serial_port *port ) {
	struct quickusb_device *quickusb
		= usb_get_intfdata ( port->serial->interface );
	int rc;

	if ( ( rc = quickusb_set_hsppmode ( quickusb,
					    QUICKUSB_HSPPMODE_SLAVE ) ) != 0 )
		return rc;

	if ( ( rc = usb_serial_generic_open ( tty, port ) ) != 0 )
		return rc;

	return 0;
}

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

        /* Create a device */
        subdev->devp = device_create( quickusb_class, NULL, subdev->dev,
                                      NULL, subdev->name );
        if ( IS_ERR ( subdev->devp ) ) {
                rc = PTR_ERR ( subdev->devp );
                goto err_class;
        }

	return 0;

 err_class:
	memset ( subdev, 0, sizeof ( *subdev ) );
	return rc;
}
				      
static void quickusb_deregister_subdev ( struct quickusb_device *quickusb,
					 unsigned int subdev_idx ) {
	struct quickusb_subdev *subdev = &quickusb->subdev[subdev_idx];

	if ( ! subdev->f_op )
		return;

	/* Remove device */
        device_destroy ( quickusb_class, subdev->dev );

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

static int quickusb_probe ( struct usb_serial *serial,
			    const struct usb_device_id *id ) {
	struct usb_interface *interface = serial->interface;
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

	/* Record driver private data */
	usb_set_serial_data ( serial, quickusb );

	/* Register devices */
	if ( ( rc = quickusb_register_devices ( quickusb ) ) != 0 ) {
		printk ( KERN_ERR "quickusb unable to register devices\n" );
		goto err;
	}

	printk ( KERN_INFO "quickusb%d connected\n", quickusb->board ); 
	goto out;

 err:
	usb_set_serial_data ( serial, NULL );
	if ( quickusb ) {
		quickusb_deregister_devices ( quickusb );
		list_del ( &quickusb->list );
		kref_put ( &quickusb->kref, quickusb_delete );
	}
 out:
	up ( &quickusb_lock );
	return rc;
}

static void quickusb_disconnect ( struct usb_serial *serial ) {
	struct quickusb_device *quickusb = usb_get_serial_data ( serial );

	printk ( KERN_INFO "quickusb%d disconnected\n", quickusb->board );

	down ( &quickusb_lock );
	usb_set_serial_data ( serial, NULL );
	quickusb_deregister_devices ( quickusb );
	list_del ( &quickusb->list );
	up ( &quickusb_lock );

	kref_put ( &quickusb->kref, quickusb_delete );
}

static struct usb_device_id quickusb_ids[] = {
	{ USB_DEVICE ( QUICKUSB_VENDOR_ID, QUICKUSB_DEVICE_ID ) },
	{ },
};

static struct usb_serial_driver quickusb_serial = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "quickusb",
	},
	.probe		= quickusb_probe,
	.disconnect	= quickusb_disconnect,
	.open		= quickusb_ttyusb_open,
	.id_table	= quickusb_ids,
	.num_ports	= 1,
};

static struct usb_serial_driver * const quickusb_serial_drivers[] = {
	&quickusb_serial, NULL
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

	/* Register driver */
	if ( ( rc = usb_serial_register_drivers ( quickusb_serial_drivers,
						  "quickusb",
						  quickusb_ids ) ) != 0 )
		goto err_usbserial;

	return 0;

	usb_serial_deregister_drivers ( quickusb_serial_drivers );
 err_usbserial:
	class_destroy ( quickusb_class );
 err_class:
	unregister_chrdev ( dev_major, "quickusb" );
 err_chrdev:
	return rc;
}

static void quickusb_exit ( void ) {
	usb_serial_deregister_drivers ( quickusb_serial_drivers );
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
