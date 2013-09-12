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
 * TODO: This doesn't yet implement the RS-232 serial ports. Also, bad things will
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

#include "../kernel/quickusb.h"

#define eprintf(...) fprintf ( stderr, __VA_ARGS__ )

enum action_type {
	DO_NOTHING = 0,
	SHOW = 1,
	SET = 2
};

struct action {
	unsigned int type;
	unsigned int value;
};

struct options {
	struct action outputs;
	struct action default_outputs;
	struct action default_levels;
	struct action settings[16];
};

void gppio_ioctl ( int fd, const char *name, struct action *action, int get_ioctl, int set_ioctl );
void setting_ioctl ( int fd, unsigned int setting, struct action *action );
int parseopts ( const int, char **argv, struct options * );
void parsegppio ( struct action *action, const char *arg );
void parsesetting ( struct options *options, const char *arg );
void printhelp ();

int main ( int argc, char* argv[] ) {
	struct options opts;
	int last_index;
	int fd;
	unsigned int i;

	memset ( &opts, 0, sizeof ( opts ) );

	last_index = parseopts ( argc, argv, &opts );
	if ( last_index == ( argc - 1 ) ) {
		fd = open( argv[last_index], O_RDWR );
	} else {
		eprintf("No device specified!\n");
		exit(EXIT_FAILURE);
	}

	if ( fd < 0 ) {
		eprintf( "Error: Could not open device %s: %s\n", argv[last_index], strerror(errno) );
    		exit(EXIT_FAILURE);
	}

	gppio_ioctl ( fd, "outputs", &opts.outputs,
		      QUICKUSB_IOC_GPPIO_GET_OUTPUTS,
		      QUICKUSB_IOC_GPPIO_SET_OUTPUTS );
	gppio_ioctl ( fd, "default-outputs", &opts.default_outputs,
		      QUICKUSB_IOC_GPPIO_GET_DEFAULT_OUTPUTS,
		      QUICKUSB_IOC_GPPIO_SET_DEFAULT_OUTPUTS );
	gppio_ioctl ( fd, "default-levels", &opts.default_levels,
		      QUICKUSB_IOC_GPPIO_GET_DEFAULT_LEVELS,
		      QUICKUSB_IOC_GPPIO_SET_DEFAULT_LEVELS );

	for ( i = 0 ; ( i < ( sizeof ( opts.settings ) /  sizeof ( opts.settings[0] ) ) ) ; i++ ) {
		setting_ioctl ( fd, i, &opts.settings[i] );
	}

	close(fd);
	return 0;
}

void gppio_ioctl ( int fd, const char *name, struct action *action, int get_ioctl, int set_ioctl ) {
	quickusb_gppio_ioctl_data_t data;

	switch ( action->type ) {
	case DO_NOTHING:
		break;
	case SHOW:
		if ( ioctl ( fd, get_ioctl, &data ) != 0 ) {
			eprintf ( "Could not get %s: %s\n", name, strerror ( errno ) );
			exit ( EXIT_FAILURE );
		}
		printf ( "%s = 0x%02x\n", name, data );
		break;
	case SET:
		data = action->value;
		if ( ioctl ( fd, set_ioctl, &data ) != 0 ) {
			eprintf ( "Could not set %s: %s\n", name, strerror ( errno ) );
			exit ( EXIT_FAILURE );
		}
		break;
	}
}

void setting_ioctl ( int fd, unsigned int setting, struct action *action ) {
	struct quickusb_setting_ioctl_data data;

	data.address = setting;
	data.value = action->value;

	switch ( action->type ) {
	case DO_NOTHING:
		break;
	case SHOW:
		if ( ioctl ( fd, QUICKUSB_IOC_GET_SETTING, &data ) != 0 ) {
			eprintf ( "Could not get setting %d: %s\n", setting, strerror ( errno ) );
			exit ( EXIT_FAILURE );
		}
		printf ( "setting[%d] = 0x%04x\n", setting, data.value );
		break;
	case SET:
		if ( ioctl ( fd, QUICKUSB_IOC_SET_SETTING, &data ) != 0 ) {
			eprintf ( "Could not set setting %d: %s\n", setting, strerror ( errno ) );
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
			{ "setting", required_argument, NULL, 's' },
			{ "help", 0, NULL, 'h' },
			{ 0, 0, 0, 0 }
		};

		if ( ( c = getopt_long ( argc, argv, "o::d::l::s:h",  long_options,  &option_index ) ) == -1 ) {
			break;
		}

		switch ( c ) {
		case 'o':
			parsegppio ( &opts->outputs, optarg );
			break;
		case 'd':
			parsegppio ( &opts->default_outputs, optarg );
			break;
		case 'l':
			parsegppio ( &opts->default_levels, optarg );
			break;
		case 's':
			parsesetting ( opts, optarg );
			break;
		case 'h':
			printhelp();
			break;
		case '?':
		default:
			eprintf ( "Warning: unrecognised character code %o\n", c );
			if ( optarg ) {
				eprintf ( "         with arg %s\n", optarg );
			}
			eprintf ( "Use -h to print help.\n" );
			exit ( EXIT_FAILURE );
		}
	}
	return optind;
}

void parsegppio ( struct action *action, const char *arg ) {
	unsigned long tmp;
	char *end;

	if ( arg ) {
		if ( *arg == '\0' ) {
			eprintf ( "Error: No value specified\n" );
			exit ( EXIT_FAILURE );
		}
		
		tmp = strtoul ( arg, &end, 0 );
		if ( *end != '\0' ) {
			eprintf ( "Error: Invalid value \"%s\"\n", arg );
			exit ( EXIT_FAILURE );
		}
		
		action->type = SET;
		action->value = tmp;
	} else {
		action->type = SHOW;
	}
}

void parsesetting ( struct options *options, const char *arg ) {
	unsigned long setting;
	unsigned long value = 0;
	int type = SHOW;
	char *end;
	
	if ( *arg == '\0' ) {
		eprintf ( "Error: No setting specified\n" );
		exit ( EXIT_FAILURE );
	}
	setting = strtoul ( arg, &end, 0 );
	if ( *end == '=' ) {
		end++;
		if ( *end == '\0' ) {
			eprintf ( "Error: No setting value specified\n" );
			exit ( EXIT_FAILURE );
		}
		type = SET;
		value = strtoul ( end, &end, 0 );
	}
	if ( *end != '\0' ) {
		eprintf ( "Error: Invalid setting string \"%s\"\n", arg );
		exit ( EXIT_FAILURE );
	}

	if ( setting >= ( sizeof ( options->settings ) /
			  sizeof ( options->settings[0] ) ) ) {
		eprintf ( "Error: Setting %ld out of range\n", setting );
		exit ( EXIT_FAILURE );
	}

	options->settings[setting].type = type;
	options->settings[setting].value = value;
}

/*
 * Print helpful message
 */
void printhelp () {
 	eprintf("\n"
	"USAGE: setquickusb --OPTION [ ARG ] DEVICE\n"
	"\n"
	"setquickusb reads or sets the port parameters for a QuickUSB module.\n"
	"The value to be set is any integer from 0-255, specified in either decimal or:\n"
	"hexadecimal form; when reading, setquickusb returns values in hexadecimal.\n"
	"\n"
	"OPTIONS:\n"
	"        --outputs  DEVICE\n"
	"              gets the current output-mask for DEVICE, and prints it.\n"
	"        --outputs=0x12  DEVICE\n"
	"              sets the output-mask to 0x12\n"
	"\n" 
	"        --setting 0x03  DEVICE\n"
	"              gets the current value of option register 0x03, and prints it.\n"
	"        --setting 0x03=0x02  DEVICE\n"
	"              sets the option register at address 0x03 to a value of 0x02.\n"
	"\n"
	"        --default-outputs  DEVICE\n"
	"        --default-outputs=0x34  DEVICE\n"
	"        --default-levels DEVICE\n"
        "        --default-levels=0x56 DEVICE\n"
	"              these should set the default output mask and values for the device\n"
	"              at power-on. Not yet implemented in the kernel driver.\n"
	"\n"
	"EXAMPLE:\n"
	"         setquickusb --outputs=0xf0 /dev/qu0ga\n"
	"            Result:  Port A has bits 7-4 set as outputs, and 3-0 as inputs.\n"
	"\n"
	"DEVICE NAMES:\n"
	"DEVICE is the relevant QuickUSB device and port. For example:\n"
	"	/dev/qu0ga      First QUSB device, General Purpose Port A\n"
	"	/dev/qu0gb      First QUSB device, General Purpose Port B\n"
	"	/dev/qu0gc      First QUSB device, General Purpose Port C\n"
	"	/dev/qu0gd      First QUSB device, General Purpose Port D\n"
	"	/dev/qu0ge      First QUSB device, General Purpose Port E\n"
	"\n"
	"	/dev/qu0hc      First QUSB device, High Speed Port, Control\n"
	"	/dev/qu0hd      First QUSB device, High Speed Port, Data\n"
	"\n"
	"Note 1: the high-speed port uses the same pins as G.P. ports B,D.\n"
	"Note 2: the 16-bit HSP (/dev/qu0hd) is little-endian. Byte B is read first.\n"
	"Note 3: the RS232 serial ports and I2C are not implemented in this driver.\n"
	"\n"
	"WARNING:\n"
	"	Setting the output mask on a port configured for high-speed\n"
	"	(either hc, or the corresponding gb,gd) will MESS IT UP.\n"
	"	Don't do it!\n"
	"\n");

  	exit(EXIT_SUCCESS);
}

/* ToDo: explain
     - how to switch the HSP port from HSP to GPIO and back.
     - how to change the direction of the HSP port.
     - the difference between master and slave mode; and timeouts.
     - what we send to the HD port.
*/


