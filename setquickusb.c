/*
 * setquickusb - get/set Linux quickusb information
 *
 * Copyright 2006 Dan Lynch <dlynch@fensystems.co.uk>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
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

#define UNDEFINED_UL -1UL

/*
 * Options
 *
 * --outputs=value  where value is any valid positive integer 0-255
 *                  presented in either decimal or hexadecimal form
 */
struct options
{
  unsigned long outputs;
};

int parseopts ( const int, char **argv, struct options * );
unsigned long parseint ( const char * );
void printhelp ();

/*
 * Program Usage
 * 
 * setquickusb [options] device
 *
 */
int main ( int argc, char* argv[] ) {
  struct options opts = { UNDEFINED_UL }; /* initialise options structure */
  int last_index = parseopts(argc,argv,&opts), fd;
  quickusb_gppio_ioctl_data_t outputs;

  if ( last_index == ( argc - 1 ) )
    fd = open( argv[last_index], O_RDWR );
  else {
    printf("No device specified!\n");
    exit(EXIT_FAILURE);
  }
  if ( fd < 0 ) {
    printf( "Error: Could not open device %s: %s\n", argv[last_index], strerror(errno) );
    exit(EXIT_FAILURE);
  }

  if ( opts.outputs != UNDEFINED_UL ) {
    outputs = opts.outputs;
    if ( ioctl(fd,QUICKUSB_IOC_GPPIO_SET_OUTPUTS, &(outputs) ) == 0 ) {}
    else {
      printf("Error: %s\n", strerror(errno));
      exit(EXIT_FAILURE);
    }
  }
  else {
    if ( ioctl(fd,QUICKUSB_IOC_GPPIO_GET_OUTPUTS, &(outputs) ) == 0 ) {
      printf("Outputs: %#x\n",outputs);
    }
    else {
      printf("Error: %s\n", strerror(errno));
      exit(EXIT_FAILURE);
    }
  }

  close(fd);

  return 0;
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
      { "outputs", 1, NULL, 'o' },
      { "help", 0, NULL, 'h' },
      { 0, 0, 0, 0 }
    };

    if ( ( c = getopt_long ( argc, argv, "o:h", long_options, &option_index ) ) == -1 )
      break;

    switch(c) {
    case 'o':
      opts->outputs = parseint(optarg);
      break;
    case 'h':
    case '?':
      printhelp();
      break;
    default:
      printf("Warning: unrecognised character code %o\n", c);
      if (optarg)
	printf("         with arg %s\n", optarg);
    }
  }

  return optind;
}

unsigned long parseint ( const char *nPtr ) {
  unsigned long tmp;
  char *endPtr;

  if ( *nPtr == '\0' ) {
    printf("Error: No value specified\n");
    exit(EXIT_FAILURE);
  }

  tmp = strtoul ( nPtr, &endPtr, 0 );

  if ( *endPtr != '\0' ) {
    printf("Error: Invalid value \"%s\"\n", nPtr);
    exit(EXIT_FAILURE);
  }

  return tmp;
}

/*
 * Print helpful message
 */
void printhelp () {
  printf("Usage: setquickusb [options] device\n");
  exit(EXIT_SUCCESS);
}
