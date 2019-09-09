#include "buffer.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define BUFFER_MIN_GROW 64

typedef struct
{
	size_t capacity;
	size_t length;
	char   *data;
} local_buffer;

buffer buffer_new(size_t capacity)
{
	local_buffer *p = malloc(sizeof(local_buffer));
	p->capacity = capacity;
	p->length = 0;
	p->data = malloc(capacity);
	assert(p->data != NULL);
	return p;
}

buffer buffer_new_from(size_t length, const char *data)
{
	local_buffer *p = (local_buffer*)buffer_new(length);
	memcpy(p->data, data, length);
	p->length = length;
	return p;
}


void buffer_delete(buffer b)
{
	local_buffer *p = (local_buffer*)b;
	free(p->data);
	p->data = NULL;
	p->capacity = 0;
	p->length = 0;
	free(p);
}


static void buffer_grow(local_buffer *p, size_t required)
{
	if (p->length + required < p->capacity)
	{
		return;
	}

	p->capacity = p->length + required;
	p->capacity += p->capacity % BUFFER_MIN_GROW;
	p->data = realloc(p->data, p->capacity);
	assert(p->data != NULL);
}

size_t buffer_write_byte(buffer b, char byte)
{
	local_buffer *p = (local_buffer*)b;
	buffer_grow(p, 1);
	p->data[p->length++] = byte;
	return 1;
}

size_t buffer_write_bytes(buffer b, size_t length, const char *data)
{
	local_buffer *p = (local_buffer*)b;
	buffer_grow(p, length);
	memcpy(p->data + p->length, data, length);
	p->length += length;
	return length;
}

size_t buffer_write_buffer(buffer p, buffer source)
{
	local_buffer *s = (local_buffer*)source;
	return buffer_write_bytes(p, s->length, s->data);
}


size_t buffer_length(buffer p)
{
	return ((local_buffer*)p)->length;
}

size_t buffer_read(buffer b, char *data)
{
	local_buffer *p = (local_buffer*)b;
	memcpy(data, p->data, p->length);
	return p->length;
}
