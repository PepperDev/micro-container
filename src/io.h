#ifndef IO_H
#define IO_H

#include <sys/types.h>

int io_isoverlay2supported();

int io_exists(char *);

int io_unlink(char *);

int io_mkdir(char *, size_t);

int io_chown(char *, uid_t, gid_t);

int io_touch(char *);

int io_truncate(char *, off_t);

int io_blankfirststsector(char *);

int io_samefs(char *, char *);

int io_loop(char *, char *);

#endif
