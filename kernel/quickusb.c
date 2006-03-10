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
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/usb.h>
#include "usb-serial.h"

#define QUICKUSB_VENDOR_ID 0x0fbb
#define QUICKUSB_DEVICE_ID 0x0001

static int debug;

static struct usb_device_id quickusb_ids[] = {
	{ USB_DEVICE ( QUICKUSB_VENDOR_ID, QUICKUSB_DEVICE_ID ) },
	{ },
};

MODULE_DEVICE_TABLE ( usb, quickusb_ids );

static struct usb_driver quickusb_driver = {
	.owner		= THIS_MODULE,
	.name		= "quickusb",
	.probe		= usb_serial_probe,
	.disconnect	= usb_serial_disconnect,
	.id_table	= quickusb_ids,
};

static void quickusb_read_bulk_callback ( struct urb *urb,
					  struct pt_regs *regs ) {
	struct usb_serial_port *port = urb->context;
	unsigned char *data = urb->transfer_buffer;
	struct tty_struct *tty;
	int i;
	int result;

	dbg("%s - port %d", __FUNCTION__, port->number);

	if (urb->status) {
		dbg("%s - nonzero read bulk status received: %d", __FUNCTION__, urb->status);
		return;
	}

	usb_serial_debug_data(debug, &port->dev, __FUNCTION__, urb->actual_length, data);

	tty = port->tty;
	if (tty && urb->actual_length) {
		for (i = 0; i < urb->actual_length ; ++i) {
			/* if we insert more than TTY_FLIPBUF_SIZE characters, we drop them. */
			if(tty->flip.count >= TTY_FLIPBUF_SIZE) {
				tty_flip_buffer_push(tty);
			}
			/* this doesn't actually push the data through unless tty->low_latency is set */
			tty_insert_flip_char(tty, data[i], 0);
		}
		tty_flip_buffer_push(tty);
	}

	/* Continue trying to always read  */
	usb_fill_bulk_urb (port->read_urb, port->serial->dev,
			   usb_rcvbulkpipe(port->serial->dev,
					   port->bulk_in_endpointAddress),
			   port->read_urb->transfer_buffer,
			   port->read_urb->transfer_buffer_length,
			   quickusb_read_bulk_callback, port);
	result = usb_submit_urb(port->read_urb, GFP_ATOMIC);
	if (result)
		dev_err(&port->dev, "%s - failed resubmitting read urb, error %d\n", __FUNCTION__, result);
	return;
}

static int quickusb_open ( struct usb_serial_port *port, struct file *filp ) {
	struct usb_device *dev = port->serial->dev;
	u8 buf_flow_static[16] = IPW_BYTES_FLOWINIT;
	u8 *buf_flow_init;
	int result;

	dbg("%s", __FUNCTION__);

	buf_flow_init = kmalloc(16, GFP_KERNEL);
	if (!buf_flow_init)
		return -ENOMEM;
	memcpy(buf_flow_init, buf_flow_static, 16);

	if (port->tty)
		port->tty->low_latency = 1;

	/* --1: Tell the modem to initialize (we think) From sniffs this is always the
	 * first thing that gets sent to the modem during opening of the device */
	dbg("%s: Sending SIO_INIT (we guess)",__FUNCTION__);
	result = usb_control_msg(dev, usb_sndctrlpipe(dev,0),
				 IPW_SIO_INIT,
				 USB_TYPE_VENDOR | USB_RECIP_INTERFACE | USB_DIR_OUT,
				 0,
				 0, /* index */
				 NULL,
				 0,
				 100000);
	if (result < 0)
		dev_err(&port->dev, "Init of modem failed (error = %d)", result);

	/* reset the bulk pipes */
	usb_clear_halt(dev, usb_rcvbulkpipe(dev, port->bulk_in_endpointAddress));
	usb_clear_halt(dev, usb_sndbulkpipe(dev, port->bulk_out_endpointAddress));

	/*--2: Start reading from the device */	
	dbg("%s: setting up bulk read callback",__FUNCTION__);
	usb_fill_bulk_urb(port->read_urb, dev,
			  usb_rcvbulkpipe(dev, port->bulk_in_endpointAddress),
			  port->bulk_in_buffer,
			  port->bulk_in_size,
			  quickusb_read_bulk_callback, port);
	result = usb_submit_urb(port->read_urb, GFP_KERNEL);
	if (result < 0)
		dbg("%s - usb_submit_urb(read bulk) failed with status %d", __FUNCTION__, result);

	/*--3: Tell the modem to open the floodgates on the rx bulk channel */
	dbg("%s:asking modem for RxRead (RXBULK_ON)",__FUNCTION__);
	result = usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
				 IPW_SIO_RXCTL,
				 USB_TYPE_VENDOR | USB_RECIP_INTERFACE | USB_DIR_OUT,
				 IPW_RXBULK_ON,
				 0, /* index */
				 NULL,
				 0,
				 100000);
	if (result < 0) 
		dev_err(&port->dev, "Enabling bulk RxRead failed (error = %d)", result);

	/*--4: setup the initial flowcontrol */
	dbg("%s:setting init flowcontrol (%s)",__FUNCTION__,buf_flow_init);
	result = usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
				 IPW_SIO_HANDFLOW,
				 USB_TYPE_VENDOR | USB_RECIP_INTERFACE | USB_DIR_OUT,
				 0,
				 0,
				 buf_flow_init,
				 0x10,
				 200000);
	if (result < 0)
		dev_err(&port->dev, "initial flowcontrol failed (error = %d)", result);


	/*--5: raise the dtr */
	dbg("%s:raising dtr",__FUNCTION__);
	result = usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
				 IPW_SIO_SET_PIN,
				 USB_TYPE_VENDOR | USB_RECIP_INTERFACE | USB_DIR_OUT,
				 IPW_PIN_SETDTR,
				 0,
				 NULL,
				 0,
				 200000);
	if (result < 0)
		dev_err(&port->dev, "setting dtr failed (error = %d)", result);

	/*--6: raise the rts */
	dbg("%s:raising rts",__FUNCTION__);
	result = usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
				 IPW_SIO_SET_PIN,
				 USB_TYPE_VENDOR | USB_RECIP_INTERFACE | USB_DIR_OUT,
				 IPW_PIN_SETRTS,
				 0,
				 NULL,
				 0,
				 200000);
	if (result < 0)
		dev_err(&port->dev, "setting dtr failed (error = %d)", result);
	
	kfree(buf_flow_init);
	return 0;
}

static void quickusb_close ( struct usb_serial_port *port,
			     struct file * filp ) {
	struct usb_device *dev = port->serial->dev;
	int result;

	if (tty_hung_up_p(filp)) {
		dbg("%s: tty_hung_up_p ...", __FUNCTION__);
		return;
	}

	/*--1: drop the dtr */
	dbg("%s:dropping dtr",__FUNCTION__);
	result = usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
				 IPW_SIO_SET_PIN,
				 USB_TYPE_VENDOR | USB_RECIP_INTERFACE | USB_DIR_OUT,
				 IPW_PIN_CLRDTR,
				 0,
				 NULL,
				 0,
				 200000);
	if (result < 0)
		dev_err(&port->dev, "dropping dtr failed (error = %d)", result);

	/*--2: drop the rts */
	dbg("%s:dropping rts",__FUNCTION__);
	result = usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
				 IPW_SIO_SET_PIN, USB_TYPE_VENDOR | USB_RECIP_INTERFACE | USB_DIR_OUT,
				 IPW_PIN_CLRRTS,
				 0,
				 NULL,
				 0,
				 200000);
	if (result < 0)
		dev_err(&port->dev, "dropping rts failed (error = %d)", result);


	/*--3: purge */
	dbg("%s:sending purge",__FUNCTION__);
	result = usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
				 IPW_SIO_PURGE, USB_TYPE_VENDOR | USB_RECIP_INTERFACE | USB_DIR_OUT,
				 0x03,
				 0,
				 NULL,
				 0,
				 200000);
	if (result < 0)
		dev_err(&port->dev, "purge failed (error = %d)", result);


	/* send RXBULK_off (tell modem to stop transmitting bulk data on rx chan) */
	result = usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
				 IPW_SIO_RXCTL,
				 USB_TYPE_VENDOR | USB_RECIP_INTERFACE | USB_DIR_OUT,
				 IPW_RXBULK_OFF,
				 0, /* index */
				 NULL,
				 0,
				 100000);

	if (result < 0)
		dev_err(&port->dev, "Disabling bulk RxRead failed (error = %d)", result);

	/* shutdown any in-flight urbs that we know about */
	usb_kill_urb(port->read_urb);
	usb_kill_urb(port->write_urb);
}

static void quickusb_write_bulk_callback ( struct urb *urb,
					   struct pt_regs *regs ) {
	struct usb_serial_port *port = urb->context;

	dbg("%s", __FUNCTION__);

	if (urb->status)
		dbg("%s - nonzero write bulk status received: %d", __FUNCTION__, urb->status);

	schedule_work(&port->work);
}

static int quickusb_write ( struct usb_serial_port *port,
			    const unsigned char *buf, int count ) {
	struct usb_device *dev = port->serial->dev;
	int ret;

	dbg("%s: TOP: count=%d, in_interrupt=%ld", __FUNCTION__,
		count, in_interrupt() );

	if (count == 0) {
		dbg("%s - write request of 0 bytes", __FUNCTION__);
		return 0;
	}
	
	/* Racy and broken, FIXME properly! */
	if (port->write_urb->status == -EINPROGRESS)
		return 0;

	count = min(count, port->bulk_out_size);
	memcpy(port->bulk_out_buffer, buf, count);

	dbg("%s count now:%d", __FUNCTION__, count);
	
	usb_fill_bulk_urb(port->write_urb, dev,
			  usb_sndbulkpipe(dev, port->bulk_out_endpointAddress),
			  port->write_urb->transfer_buffer,
			  count,
			  quickusb_write_bulk_callback,
			  port);

	ret = usb_submit_urb(port->write_urb, GFP_ATOMIC);
	if (ret != 0) {
		dbg("%s - usb_submit_urb(write bulk) failed with error = %d", __FUNCTION__, ret);
		return ret;
	}

	dbg("%s returning %d", __FUNCTION__, count);
	return count;
} 

static int quickusb_probe ( struct usb_serial_port *port ) {
	return 0;
}

static int quickusb_disconnect ( struct usb_serial_port *port ) {
	usb_set_serial_port_data ( port, NULL );
	return 0;
}

static struct usb_serial_device_type quickusb_serial_driver = {
	.owner			= THIS_MODULE,
	.name			= "QuickUSB",
	.short_name		= "quickusb",
	.id_table		= quickusb_ids,
	.num_interrupt_in	= NUM_DONT_CARE,
	.num_bulk_in		= 1,
	.num_bulk_out		= 1,
	.num_ports		= 1,
	.open			= quickusb_open,
	.close			= quickusb_close,
	.port_probe 		= quickusb_probe,
	.port_remove		= quickusb_disconnect,
	.write			= quickusb_write,
	.write_bulk_callback	= quickusb_write_bulk_callback,
	.read_bulk_callback	= quickusb_read_bulk_callback,
};

static int quickusb_init ( void ) {
	int rc;

	if ( ( rc = usb_serial_register ( &quickusb_serial_driver ) ) != 0 )
		goto err_usb_serial;
	if ( ( rc = usb_register ( &quickusb_driver ) ) != 0 )
		goto err_usb;
	return 0;

 err_usb:
	usb_serial_deregister ( &quickusb_serial_driver );
 err_usb_serial:
	return rc;
}

static void quickusb_exit ( void ) {
	usb_deregister ( &quickusb_driver );
	usb_serial_deregister ( &quickusb_serial_driver );
}

module_init ( quickusb_init );
module_exit ( quickusb_exit );

/* Module information */
MODULE_AUTHOR ( "Michael Brown <mbrown@fensystems.co.uk>" );
MODULE_DESCRIPTION ( "QuickUSB serial driver" );
MODULE_LICENSE ( "GPL" );

module_param ( debug, bool, S_IRUGO | S_IWUSR );
MODULE_PARM_DESC ( debug, "Enable debugging" );
