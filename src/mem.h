#ifndef MEM_H
#define MEM_H

#include <sys/types.h>

void *mem_allocate(size_t);
void *mem_reallocate(void *, size_t);
char *mem_append(char *, size_t, char *, size_t, char *, size_t);
char *mem_path(char *, size_t, char *, size_t, size_t *);

#endif
