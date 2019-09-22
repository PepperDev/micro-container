#ifndef IO_H
#define IO_H

char* io_realpath(const char*);

char io_isdir(const char*);

char io_mkdir(const char*, char, unsigned int, unsigned int);

char io_addmod(const char*, unsigned int, unsigned int, unsigned int);

#endif
