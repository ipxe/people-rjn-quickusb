#ifndef QUICKUSB_KERNEL_COMPAT_H
#define QUICKUSB_KERNEL_COMPAT_H

#include <linux/version.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)

#include <asm/atomic.h>
#include <linux/slab.h>
#include <linux/usb.h>

#define __user

struct device;

struct kref {
	atomic_t refcount;
};

static inline void kref_init ( struct kref *kref ) {
	atomic_set ( &kref->refcount, 1 );
}

static inline void kref_get ( struct kref *kref ) {
	atomic_inc ( &kref->refcount );
}

static inline void kref_put ( struct kref *kref,
			      void (*release) ( struct kref *kref ) ) {
	if ( atomic_dec_and_test ( &kref->refcount ) )
		release ( kref );
}

#define container_of(ptr, type, member) ( { 			\
	const typeof( ((type *)0)->member ) *__mptr = (ptr);    \
	(type *)( (char *)__mptr - offsetof(type,member) ); } )

static inline struct usb_device *usb_get_dev ( struct usb_device *usb ) {
	usb_inc_dev_use ( usb );
	return usb;
}

static inline void usb_put_dev ( struct usb_device *usb ) {
	usb_free_dev ( usb );
}

static inline unsigned iminor ( struct inode *inode ) {
	return MINOR ( inode->i_rdev );
}

static inline struct class_simple *class_simple_create ( struct module *owner,
							 const char *name ) {
	return NULL;
}

static inline void class_simple_destroy ( struct class_simple *class ) {
	/* Do nothing */
}

#define class_simple_device_add(...) NULL

static inline void class_simple_device_remove ( dev_t dev ) {
	/* Do nothing */
}

static struct file_operations quickusb_fops;

static inline int devfs_mk_cdev ( dev_t dev, umode_t mode,
				  const char *name ) {
	devfs_handle_t devfs_handle;

	devfs_handle = devfs_register ( NULL, name, DEVFS_FL_DEFAULT,
					MAJOR ( dev ), MINOR ( dev ),
					mode, &quickusb_fops, NULL );
	return ( devfs_handle == NULL ) ? -EIO : 0;
}

static inline void devfs_remove ( const char *name ) {
	devfs_handle_t devfs_handle;

	devfs_handle = devfs_get_handle ( NULL, name, 0, 0,
					  DEVFS_SPECIAL_CHR, 0 );
	if ( ! devfs_handle ) {
		printk ( "Aargh: can't find %s to unregister it\n",
			 name );
		return;
	}

	devfs_unregister ( devfs_handle );
}

#define module_param( name, type, perm ) MODULE_PARM ( name, "i" )

#define usb_set_intfdata(...) do { } while ( 0 )

#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0) */

#endif /* QUICKUSB_KERNEL_COMPAT_H */

/*
 * Local variables:
 *  c-basic-offset: 8
 *  c-indent-level: 8
 *  tab-width: 8
 * End:
 */
