#ifndef IO_H
#define IO_H

#include <sys/types.h>

int io_isoverlay2supported();

int io_exists(char *);

int io_unlink(char *);

#endif
