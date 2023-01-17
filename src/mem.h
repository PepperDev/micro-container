#ifndef MEM_H
#define MEM_H

#include <sys/types.h>

typedef void *buffer_t;

void *mem_allocate(size_t);
void *mem_reallocate(void *, size_t);
char *mem_append(char *, size_t, char *, size_t, char *, size_t);
char *mem_path(char *, size_t, char *, size_t, size_t *);

buffer_t buffer_new(size_t);
void buffer_delete(buffer_t);
size_t buffer_write_data(buffer_t, size_t, const void *);
size_t buffer_write_byte(buffer_t, char);
void *buffer_use(buffer_t);

#endif
