#ifndef IO_H
#define IO_H

char* io_realpath(const char*);

char io_isdir(const char*);

char io_mkdir(const char*, char);

char io_addmod(const char*, unsigned int);

#endif
