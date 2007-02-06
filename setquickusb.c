/*
 * setquickusb - get/set Linux quickusb information
 *
 * Copyright 2006 Dan Lynch <dlynch@fensystems.co.uk>
 * Modifications 2006, Richard Neill <rn214@mrao.cam.ac.uk>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 */

/*
 * TODO: This doesn't yet implement the serial ports. Also, bad things will
 * happen when we change direction of the high-speed ports, or their corresponding
 * general-purpose ports
 */

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <errno.h>
#include <getopt.h>
#include <string.h>
#include <sys/ioctl.h>

#include "kernel/quickusb.h"

#define eprintf(...) fprintf ( stderr, __VA_ARGS__ )

#define DO_NOTHING -1UL
#define SHOW_SETTING -2UL

/*
 * Options
 *
 * --outputs=value  where value is any valid positive integer 0-255
 *                  presented in either decimal or hexadecimal form
 */

struct options {
	unsigned long outputs;
	unsigned long default_outputs;
	unsigned long default_levels;
};

void gppio_ioctl ( int fd, const char *name, unsigned long option,
		   int get_ioctl, int set_ioctl );
int parseopts ( const int, char **argv, struct options * );
unsigned long parseint ( const char * );
void printhelp ();

int main ( int argc, char* argv[] ) {
	struct options opts = {
		.outputs		= DO_NOTHING,
		.default_outputs	= DO_NOTHING,
		.default_levels		= DO_NOTHING,
	};
	int last_index = parseopts(argc,argv,&opts), fd;

	if ( last_index == ( argc - 1 ) ) {
		fd = open( argv[last_index], O_RDWR );
	} else {
		eprintf("No device specified!\n");
		exit(EXIT_FAILURE);
	}

	if ( fd < 0 ) {
		eprintf( "Error: Could not open device %s: %s\n",
			 argv[last_index], strerror(errno) );
    		exit(EXIT_FAILURE);
	}

	gppio_ioctl ( fd, "outputs", opts.outputs,
		      QUICKUSB_IOC_GPPIO_GET_OUTPUTS,
		      QUICKUSB_IOC_GPPIO_SET_OUTPUTS );
	gppio_ioctl ( fd, "default-outputs", opts.default_outputs,
		      QUICKUSB_IOC_GPPIO_GET_DEFAULT_OUTPUTS,
		      QUICKUSB_IOC_GPPIO_SET_DEFAULT_OUTPUTS );
	gppio_ioctl ( fd, "default-levels", opts.default_levels,
		      QUICKUSB_IOC_GPPIO_GET_DEFAULT_LEVELS,
		      QUICKUSB_IOC_GPPIO_SET_DEFAULT_LEVELS );

	close(fd);
	return 0;
}

void gppio_ioctl ( int fd, const char *name, unsigned long option,
		   int get_ioctl, int set_ioctl ) {
	quickusb_gppio_ioctl_data_t data;

	switch ( option ) {
	case DO_NOTHING:
		break;
	case SHOW_SETTING:
		if ( ioctl ( fd, get_ioctl, &data ) != 0 ) {
			eprintf ( "Could not get %s: %s\n", name,
				  strerror ( errno ) );
			exit ( EXIT_FAILURE );
		}
		printf ( "%s = %#02x\n", name, data );
		break;
	default:
		data = option;
		if ( ioctl ( fd, set_ioctl, &data ) != 0 ) {
			eprintf ( "Could not set %s: %s\n", name,
				  strerror ( errno ) );
			exit ( EXIT_FAILURE );
		}
		break;
	}
}

/*
 * Parse command-line options and return index of last element in
 * argument list that is not an option
 */
int parseopts ( const int argc, char **argv, struct options *opts ) {
	int c;

	while (1) {
		int option_index = 0;
		static struct option long_options[] = {
			{ "outputs", optional_argument, NULL, 'o' },
			{ "default-outputs", optional_argument, NULL, 'd' },
			{ "default-levels", optional_argument, NULL, 'l' },
			{ "help", 0, NULL, 'h' },
			{ 0, 0, 0, 0 }
		};

		if ( ( c = getopt_long ( argc, argv, "o::d::l::h",
					 long_options,
					 &option_index ) ) == -1 ) {
			break;
		}

		switch(c) {
			case 'o':
				opts->outputs = parseint(optarg);
				break;
			case 'd':
				opts->default_outputs = parseint(optarg);
				break;
			case 'l':
				opts->default_levels = parseint(optarg);
				break;
			case 'h':
				printhelp();
				break;
			case '?':
			default:
				eprintf("Warning: unrecognised character code "
					"%o\n", c);
				if (optarg) {
					eprintf("         with arg %s\n",
						optarg);
				}
				eprintf("Use -h to print help.\n");
				exit(EXIT_FAILURE);
		}
	}
	return optind;
}

unsigned long parseint ( const char *nPtr ) {
	unsigned long tmp;
	char *endPtr;

	if ( ! nPtr )
		return SHOW_SETTING;

	if ( *nPtr == '\0' ) {
		eprintf("Error: No value specified\n");
		exit(EXIT_FAILURE);
	}

	tmp = strtoul ( nPtr, &endPtr, 0 );

	if ( *endPtr != '\0' ) {
		eprintf("Error: Invalid value \"%s\"\n", nPtr);
		exit(EXIT_FAILURE);
	}

	return tmp;
}

/*
 * Print helpful message
 */
void printhelp () {
 	eprintf("\n"
	"Usage: setquickusb [ --outputs=MASK ] DEVICE\n"
	"setquickusb reads or sets the I/O port directions for a QuickUSB module.\n"
	"If there are no options, it prints the output mask (in hexadecimal):\n"
	"\n"
	"	Example:\n"
	"		setquickusb /dev/qu0ga\n"
	"	Prints:\n"
	"		Outputs: 0xf0\n"
	"\n");
	eprintf("If --outputs=MASK is specified, then the I/O port concerned will\n"
	"be set to have that output mask. MASK is an integer from 0-255,\n"
	"specified in decimal, or hexadecimal.\n"
	"\n"
	"	Example:\n"
	"		setquickusb --outputs=0xf0 /dev/quoga\n"
	"	Result:\n"
	"		Port A has bits 7-4 set as outputs, and 3-0 as inputs.\n"
	"\n");
	eprintf("DEVICE is the relevant QuickUSB device and port. For example:\n"
	"	/dev/qu0ga      First QUSB device, General Purpose Port A\n"
	"	/dev/qu0gb      First QUSB device, General Purpose Port B\n"
	"	/dev/qu0gc      First QUSB device, General Purpose Port C\n"
	"	/dev/qu0gd      First QUSB device, General Purpose Port D\n"
	"	/dev/qu0ge      First QUSB device, General Purpose Port E\n"
	"\n"
	"	/dev/qu0hc      First QUSB device, High Speed Port, Control\n"
	"	/dev/qu0hd      First QUSB device, High Speed Port, Data\n"
	"\n");
	eprintf("Note 1: the high-speed port uses the same pins as G.P. ports B,D.\n"
	"Note 2: the 16-bit HSP (/dev/qu0hd) is little-endian. Byte B is read first.\n"
	"Note 3: the RS232 serial ports are not implemented in this driver.\n"
	"\n"
	"WARNING:\n"
	"	Setting the output mask on a port configured for high-speed\n"
	"	(either hc, or the corresponding gb,gd) will MESS IT UP.\n"
	"	Don't do it!\n"
	"\n");

  	exit(EXIT_SUCCESS);
}
