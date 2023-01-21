#include "mem.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static const char MEM_ERROR[] = "Unable to allocate memory.\n";

typedef struct {
    size_t capacity;
    size_t length;
    void *data;
} buffer_local;

static void buffer_grow(buffer_local *, size_t);

void *mem_allocate(size_t size)
{
    void *mem = malloc(size);
    if (!mem) {
        fprintf(stderr, MEM_ERROR);
    }
    return mem;
}

void *mem_reallocate(void *mem, size_t size)
{
    mem = realloc(mem, size);
    if (!mem) {
        fprintf(stderr, MEM_ERROR);
    }
    return mem;
}

char *mem_append(char *prefix, size_t prefix_size, char *base, size_t base_size, char *suffix, size_t suffix_size)
{
    char *mem = mem_allocate(prefix_size + base_size + suffix_size);
    if (!mem) {
        return NULL;
    }
    memcpy(mem, prefix, prefix_size);
    char *aux = mem + prefix_size;
    memcpy(aux, base, base_size);
    aux += base_size;
    if (suffix_size) {
        memcpy(aux, suffix, suffix_size);
    }
    return mem;
}

char *mem_path(char *base, size_t base_size, char *append, size_t append_size, size_t *computed)
{
    while (base_size && base[base_size - 1] == '/') {
        base_size--;
    }
    while (append_size && append[0] == '/') {
        append++;
        append_size--;
    }
    *computed = base_size + append_size + 1;
    char *mem = mem_allocate(*computed + 1);
    if (!mem) {
        return NULL;
    }
    memcpy(mem, base, base_size);
    char *aux = mem + base_size;
    *aux = '/';
    aux++;
    memcpy(aux, append, append_size);
    aux += append_size;
    *aux = 0;
    return mem;
}

buffer_t buffer_new(size_t capacity)
{
    buffer_local *buf = mem_allocate(sizeof(buffer_local));
    if (!buf) {
        return NULL;
    }
    buf->data = mem_allocate(capacity);
    if (!buf->data) {
        return NULL;
    }
    buf->capacity = capacity + 64 - capacity % 64;
    buf->length = 0;
    return buf;
}

size_t buffer_write_data(buffer_t buf, size_t length, const void *data)
{
    buffer_local *p = (buffer_local *) buf;
    buffer_grow(p, length);
    if (p->data) {
        memcpy(((char *)p->data) + p->length, data, length);
    }
    p->length += length;
    return length;
}

size_t buffer_write_byte(buffer_t buf, char byte)
{
    buffer_local *p = (buffer_local *) buf;
    buffer_grow(p, 1);
    if (p->data) {
        ((char *)p->data)[p->length] = byte;
    }
    p->length++;
    return 1;
}

void *buffer_use(buffer_t buf)
{
    void *data = ((buffer_local *) buf)->data;
    memset(buf, 0, sizeof(buffer_local));
    free(buf);
    return data;
}

static void buffer_grow(buffer_local * buf, size_t required)
{
    if (buf->length + required <= buf->capacity) {
        return;
    }
    buf->capacity = (size_t)((buf->length + required + 63) / 64) * 64;
    if (!buf->data) {
        return;
    }
    void *data = mem_reallocate(buf->data, buf->capacity);
    if (!data) {
        free(buf->data);
    }
    buf->data = data;
}
