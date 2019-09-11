#ifndef BUFFER_H
#define BUFFER_H

#include <stddef.h>

typedef void *buffer;

buffer buffer_new(size_t capacity);
buffer buffer_new_from(size_t length, const void *data);

void buffer_delete(buffer p);

size_t buffer_write_byte(buffer p, char byte);
size_t buffer_write_data(buffer p, size_t length, const void *data);
size_t buffer_write_buffer(buffer p, buffer source);

size_t buffer_length(buffer p);
size_t buffer_read(buffer p, void *data);

void *buffer_reuse(buffer p);

#endif
