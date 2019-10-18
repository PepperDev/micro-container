#ifndef IO_H
#define IO_H

#include <unistd.h>
#include <sys/stat.h>

char* io_realpath(const char*);

char io_exists(const char*);

char io_isdir(const char*);

char io_isrunnable(const char*);

char io_mkdir(const char*, char, uid_t, gid_t);

char io_addmod(const char*, mode_t, uid_t, gid_t);

size_t io_readfile(const char*, char**);

#endif
