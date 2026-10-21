#ifndef CAGE_MEM_H
#define CAGE_MEM_H

// cppcheck-suppress missingIncludeSystem
#include <stddef.h>

void *mem_alloc(size_t size);
void *mem_realloc(void *ptr, size_t size);
void mem_free(void *ptr);
char *mem_strdup(const char *s);
char *mem_cat(const void *s1, size_t n1, ...);
char *mem_path(const char *base, const char *append);
int mem_path_escapes(const char *rel);

#define MEM_CAT_END ((const void *)0)

typedef struct {
  size_t cap;
  size_t len;
  char *data;
} buf_t;

#define BUF_INIT { 0, 0, NULL }

int buf_grow(buf_t * b, size_t need);
int buf_append(buf_t * b, const void *data, size_t len);
int buf_append_c(buf_t * b, char c);
char *buf_done(buf_t * b);
void buf_free(buf_t * b);

#endif
