#include "mem.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static const char MEM_ERROR[] = "Unable to allocate memory.\n";

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
