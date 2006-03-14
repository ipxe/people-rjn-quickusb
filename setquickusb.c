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
#include <fcntl.h>
#include <errno.h>
#include <getopt.h>

/*
  #include "kernel/quickusb.h"
*/

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
int check_valid_strtoint ( const char* );
void printhelp ();

/*
 * Program Usage
 * 
 * setquickusb [options] device
 *
 */
int main ( int argc, char* argv[] ) {
  int last_index;
  struct options opts = { -1 }; /* initialise options structure */
  last_index = parseopts(argc,argv,&opts);

  int fd;
  if ( argv[last_index] != NULL ) 
    fd = open( argv[last_index], O_RDWR );
  if ( fd == -1 ) {
    printf( "Could not open device %s: %s\n", argv[last_index], strerror(errno) );
    exit;
  }

  if ( opts.outputs >= 0) {
  }
  else {
    int rc;
    /*    quickusb_gppio_ioctl_data_t outputs;
	  rc = ioctl ();*/
  }

  close(fd);

  return 0;
}

/*
 * Parse command-line options and return index of last element in
 * argument list that is not an option
 */
int parseopts ( const int argc, char **argv, struct options *opt ) {
  int c;

  while (1) {
    int option_index = 0;
    static struct option long_options[] = {
      { "outputs", 1, 0, 0 },
      { "help", 0, 0, 0 },
      { 0, 0, 0, 0 }
    };

    if ( ( c = getopt_long ( argc, argv, "o:h", long_options, &option_index ) ) == -1 )
      break;

    switch(c) {
    case 0:
      printf("warning: unrecognised option %s\n", long_options[option_index].name);
      if (optarg)
	printf("         with arg %s\n", optarg);
      break;
    case 'o':
      break;
    case 'h':
    case '?':
      printhelp();
      break;
    default:
      printf("?? getopt returned character code 0x%o\n",c);
    }
  }

  return optind;
}

/*
 * Print helpful message
 */
void printhelp () {
  printf("Usage: setquickusb [options] device\n");
  exit(EXIT_SUCCESS);
}

int check_valid_strtoint ( const char *nPtr ) {
  unsigned long tmp;
  char **endPtr;
  tmp = strtoul(nPtr,endPtr,0);
  if ( endPtr != NULL ) {
    printf("Value contains invalid characters [%s]\n",endPtr);
    exit(EXIT_FAILURE);
  }
}
